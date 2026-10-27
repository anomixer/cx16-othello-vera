// =============================================================================
// ZSM -> Apple II / VERA "VZM" stream converter (VERA PSG + YM2151 FM)
// =============================================================================
//
// The Commander X16 ZSM format interleaves VERA PSG writes with Yamaha YM2151
// (FM) register batches.  The Apple II VERA card is driven from a 6502 stream
// player that runs from the VERA VSYNC tick, so this module rewrites a ZSM song
// into a compact, self-contained byte stream that the player can decode with a
// handful of compares.  Ported from `C:\dev\veratest\src\zsm2vera.mjs`; the
// format and the decoder contract are identical (see vera/AGENTS.md §3.11).
//
// Stream format (all records are kept inside one 512-byte ProDOS block so the
// block-at-a-time reader never has to split a record across two reads):
//
//   $00-$7F  delay       : consume the current 60 Hz tick, then wait N more
//                          ticks (N = byte value).  A ZSM delay of D ticks is
//                          encoded as one byte D-1, matching the original
//                          1 + (D-1) = D tick timing of the X16 player.
//   $80-$BF  PSG write   : register = byte & $3F ($1F9C0 + reg), next byte value
//   $C0-$DF  FM batch    : count = (byte & $1F) + 1, then count x (reg, value)
//                          pairs written to YM_REG/YM_DATA ($C0A0/$C0A1 (slot 2), $C0C0/$C0C1 (slot 4))
//   $E0-$FE  no-op       : padding / reserved, skipped without consuming a tick
//   $FF      end of song : the player seeks to the loop offset (or restarts)
//
// Two further optimisations keep the stream small:
//
//   * Register write de-duplication.  Re-writing a register with the value it
//     already holds is a hardware no-op, so it is dropped.  Both tables start
//     at zero because the player explicitly initialises the PSG (all sixteen
//     voices) and the YM2151 ($20..$FF) to zero before the first stream byte.
//   * All FM pairs of one 60 Hz tick are merged into a single batch.
//
// FM note: the X16 mix is PSG + FM; on the Apple II the FM daughterboard is
// optional, so a player may skip $C0-$DF records when no YM2151 is present.
// This port keeps the PSG-only MUSIC.PSG for those machines and plays this
// stream only when the FM chip was detected.
// =============================================================================

import fs from "fs"

export const STREAM_NOP = 0xe0
export const STREAM_END = 0xff
const BLOCK_SIZE = 512
const MAX_FM_BATCH = 32

// Convert a ZSM buffer into a VERA PSG + YM2151 FM byte stream.
//   options.balance : optional (reg, value) => value volume curve
//   options.sourceName : used in diagnostics only
export const convertZsmToVeraStream = (zsm, options = {}) => {
  const { balance = null, sourceName = "ZSM" } = options
  if (zsm.length < 16 || zsm[0] !== 0x7a || zsm[1] !== 0x6d) {
    throw new Error(`${sourceName}: not a ZSM file (missing 'zm' magic)`)
  }
  const tickRate = zsm[12] | (zsm[13] << 8)
  if (tickRate !== 60) {
    throw new Error(`${sourceName}: unsupported ZSM tick rate ${tickRate} (expected 60)`)
  }

  // YM2151 key on/off ($08) must reach the chip verbatim.
  //
  // The OPM key register is "operator mask in bits 6..3, channel in bits 2..0":
  // a non-zero mask keys those operators on, a zero mask releases them.  ymfm
  // (and therefore the VERA daughterboard) ignores bit 7 and derives the key
  // state from the mask alone, so $78 keys channel 0 on and $00 releases it.
  // Rewriting the value would turn every release into a key-on: the envelope
  // then decays once and the FM part never re-triggers, which is heard as "the
  // music stops after a moment".  Pass it through untouched, and always forward
  // it: two consecutive key-ons with the same mask are a legitimate retrigger.

  const out = []
  const fmLast = new Uint8Array(256) // YM2151 registers start cleared by the player
  const psgLast = new Uint8Array(64) // PSG registers start silenced by the player
  let tickPsg = []
  let tickFm = []
  let ptr = 16
  const stats = { psgWrites: 0, psgKept: 0, fmWrites: 0, fmKept: 0, ticks: 0, extCommands: 0 }

  const padRecord = (length) => {
    while ((out.length % BLOCK_SIZE) + length > BLOCK_SIZE) out.push(STREAM_NOP)
  }

  // Publish one 60 Hz tick: PSG writes first (cheap, keeps the shadow in sync),
  // then the ordered YM2151 batch, then the delay that ends the tick.
  const flushTick = () => {
    for (const [reg, value] of tickPsg) {
      padRecord(2)
      out.push(0x80 | reg, value)
    }
    for (let i = 0; i < tickFm.length; i += MAX_FM_BATCH) {
      const batch = tickFm.slice(i, i + MAX_FM_BATCH)
      padRecord(1 + 2 * batch.length)
      out.push(0xc0 | (batch.length - 1))
      for (const [reg, value] of batch) out.push(reg, value)
    }
    tickPsg = []
    tickFm = []
  }

  while (ptr < zsm.length) {
    const command = zsm[ptr++]

    if (command < 0x40) {
      // PSG write: [reg, value]
      const value = zsm[ptr++]
      let adjusted = value
      if (balance) adjusted = balance(command, value)
      stats.psgWrites++
      if (adjusted !== psgLast[command]) {
        psgLast[command] = adjusted
        tickPsg.push([command, adjusted])
        stats.psgKept++
      }
      continue
    }

    if (command === 0x40) {
      // Extension command: channel/count in the low six bits, payload follows.
      const ext = zsm[ptr++]
      ptr += ext & 0x3f
      stats.extCommands++
      if (ptr > zsm.length) throw new Error(`${sourceName}: truncated extension command`)
      continue
    }

    if (command < 0x80) {
      // YM2151 write batch: low six bits are the number of (reg, value) pairs.
      const count = command & 0x3f
      if (ptr + count * 2 > zsm.length) throw new Error(`${sourceName}: truncated FM batch`)
      for (let i = 0; i < count; i++) {
        const reg = zsm[ptr++]
        const value = zsm[ptr++]
        stats.fmWrites++
        if (reg === 0x08) {
          // Key register: always forwarded, a repeat write retriggers the note.
          tickFm.push([reg, value])
          stats.fmKept++
        } else if (value !== fmLast[reg]) {
          fmLast[reg] = value
          tickFm.push([reg, value])
          stats.fmKept++
        }
      }
      continue
    }

    if (command === 0x80) {
      // End of song marker.
      break
    }

    // Delay: 1..127 ticks, split into the player's 0..127 byte encoding.
    flushTick()
    let delay = command & 0x7f
    stats.ticks += delay
    while (delay > 0) {
      const chunk = Math.min(delay, 127)
      padRecord(1)
      out.push(chunk - 1)
      delay -= chunk
    }
  }

  flushTick()
  padRecord(1)
  out.push(STREAM_END)
  while (out.length % BLOCK_SIZE !== 0) out.push(STREAM_NOP)

  return { data: Buffer.from(out), stats, tickRate }
}

export const convertZsmFile = (zsmPath, outPath, options = {}) => {
  const zsm = fs.readFileSync(zsmPath)
  const result = convertZsmToVeraStream(zsm, { ...options, sourceName: options.sourceName || zsmPath })
  if (outPath) fs.writeFileSync(outPath, result.data)
  return result
}
