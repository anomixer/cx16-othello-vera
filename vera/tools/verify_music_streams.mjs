#!/usr/bin/env node
// Music stream verifier for the Apple II / VERA port.
//
//   node tools/verify_music_streams.mjs
//
// Two checks:
//   1. `generated/music.vzm` is byte-identical to a fresh conversion of
//      `assets/othello.zsm` (i.e. the packaged stream is really the built one).
//   2. The conversion is register-equivalent to the source ZSM: for every 60 Hz
//      tick the PSG and YM2151 register image implied by the raw ZSM equals the
//      image implied by the VZM stream, and the YM2151 `$08` key-on/key-off
//      write sequence is identical in order and value.
//
// Check 2 is what guarantees the FM daughterboard hears the arrangement the
// tracker wrote: a batch decoded one pair short, or a rewritten `$08` register,
// would silently change the music instead of failing loudly.
import fs from "node:fs"
import path from "node:path"
import { convertZsmToVeraStream } from "./zsm2vera.mjs"

const here = path.dirname(new URL(import.meta.url).pathname.replace(/^\/(\w:)/, "$1"))
const root = path.resolve(here, "..")
const sourcePath = path.join(root, "assets", "othello.zsm")
const streamPath = path.join(root, "generated", "music.vzm")

const PSG = 0
const FM = 1

// Per 60 Hz tick, every write the song issues, in stream order.
const zsmTicks = (buffer) => {
  const ticks = []
  let current = []
  let ptr = 16
  while (ptr < buffer.length) {
    const command = buffer[ptr++]
    if (command < 0x40) {
      current.push([PSG, command, buffer[ptr++]])
      continue
    }
    if (command === 0x40) {
      const ext = buffer[ptr++]
      ptr += ext & 0x3f
      continue
    }
    if (command < 0x80) {
      const count = command & 0x3f
      for (let i = 0; i < count; i++) current.push([FM, buffer[ptr++], buffer[ptr++]])
      continue
    }
    if (command === 0x80) {
      ticks.push(current)
      break
    }
    // Delay: the writes above happen on this tick, then (command & $7F) - 1 silent ticks.
    ticks.push(current)
    current = []
    for (let i = 1; i < (command & 0x7f); i++) ticks.push([])
  }
  return ticks
}

const vzmTicks = (buffer) => {
  const ticks = []
  let current = []
  let ptr = 0
  while (ptr < buffer.length) {
    const command = buffer[ptr++]
    if (command === 0xff) {
      ticks.push(current)
      break
    }
    if (command <= 0x7f) {
      // Delay byte N: this tick plus N silent ticks.
      ticks.push(current)
      current = []
      for (let i = 0; i < command; i++) ticks.push([])
      continue
    }
    if (command <= 0xbf) {
      current.push([PSG, command & 0x3f, buffer[ptr++]])
      continue
    }
    if (command <= 0xdf) {
      const count = (command & 0x1f) + 1
      for (let i = 0; i < count; i++) current.push([FM, buffer[ptr++], buffer[ptr++]])
      continue
    }
    // $E0..$FE: alignment padding between 512-byte blocks.
  }
  return ticks
}

const sameImage = (left, right) => {
  for (let i = 0; i < left.length; i++) if (left[i] !== right[i]) return false
  return true
}

const zsm = fs.readFileSync(sourcePath)
const packaged = fs.readFileSync(streamPath)
const { data, stats } = convertZsmToVeraStream(zsm, { sourceName: "othello.zsm" })
if (Buffer.compare(data, packaged) !== 0) {
  throw new Error(`generated/music.vzm differs from a fresh conversion ` +
    `(${packaged.length} vs ${data.length} bytes) - rerun the build`)
}

const reference = zsmTicks(zsm)
const actual = vzmTicks(packaged)
if (reference.length !== actual.length) {
  throw new Error(`${reference.length} ticks in the ZSM but ${actual.length} in the VZM`)
}

const psgRef = new Uint8Array(64)
const psgAct = new Uint8Array(64)
const fmRef = new Uint8Array(256)
const fmAct = new Uint8Array(256)
let mismatched = 0
let firstMismatch = -1
let keyWrites = 0
for (let tick = 0; tick < reference.length; tick++) {
  const keyRef = []
  const keyAct = []
  for (const [kind, reg, value] of reference[tick]) {
    if (kind === PSG) psgRef[reg] = value
    else {
      fmRef[reg] = value
      if (reg === 0x08) keyRef.push(value)
    }
  }
  for (const [kind, reg, value] of actual[tick]) {
    if (kind === PSG) psgAct[reg] = value
    else {
      fmAct[reg] = value
      if (reg === 0x08) keyAct.push(value)
    }
  }
  keyWrites += keyRef.length
  const same = sameImage(psgRef, psgAct) && sameImage(fmRef, fmAct) &&
    keyRef.length === keyAct.length && keyRef.every((v, i) => v === keyAct[i])
  if (!same) {
    mismatched++
    if (firstMismatch < 0) firstMismatch = tick
  }
}

const blocks = packaged.length / 512
console.log(`${mismatched === 0 ? "OK  " : "FAIL"} othello  ${reference.length} ticks, ` +
  `${blocks} blocks (${packaged.length} B), ` +
  `${stats.psgWrites} PSG / ${stats.fmWrites} FM writes, ${keyWrites} key writes, ` +
  `${mismatched} mismatched ticks` + (firstMismatch >= 0 ? ` (first at tick ${firstMismatch})` : ""))
if (mismatched !== 0) {
  console.error("Music stream verification FAILED")
  process.exit(1)
}
console.log("Stream matches the source ZSM tick by tick")
