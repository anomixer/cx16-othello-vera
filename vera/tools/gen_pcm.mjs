#!/usr/bin/env node
/* ZSM -> WAV -> 8 kHz PCM for the VERA PCM FIFO (OTHELLO.PCM).
 *
 * The 16-voice PSG re-voicing cannot reproduce this soundtrack's low end: the
 * reference mix has ~68 % of its energy below 500 Hz while MUSIC.PSG produces
 * ~10 %, which is why the PSG-only build sounds thin.  Instead of synthesising
 * the music online, the whole song is rendered once at build time:
 *
 *   1. melodius.exe (c:\dev\veramusic) renders assets/othello.zsm through
 *      ymfm (YM2151) + the VERA PSG + PCM to a 48 kHz stereo WAV.
 *   2. This script mixes to mono, lowpasses at 3.6 kHz (32-tap Blackman-windowed
 *      sinc) and resamples to the card's exact 8,010.86 Hz PCM clock.
 *   3. Peak-normalise to -0.3 dBFS, TPDF-dither to 8-bit signed mono.
 *
 * Output: generated/othello.pcm (raw signed bytes, -128..127) plus
 * generated/pcm_table.h (loop offset for src/audio.c).
 *
 * The renderer is an external tool; when it is not installed the script keeps
 * an existing generated/othello.pcm (or warns and builds without PCM, in which
 * case the game falls back to MUSIC.PSG).  Point MELODIUS_EXE at the exe to
 * override the search path.
 */
import fs from "node:fs"
import path from "node:path"
import { execFileSync } from "node:child_process"
import { fileURLToPath } from "node:url"
import { zsmLoopTick } from "./gen_music.mjs"

const here = path.dirname(fileURLToPath(import.meta.url))
const vera = path.resolve(here, "..")
const gen = path.join(vera, "generated")
const build = path.join(vera, "build")
const zsmPath = path.join(vera, "assets", "othello.zsm")
const pcmPath = path.join(gen, "othello.pcm")
const tablePath = path.join(gen, "pcm_table.h")

const TARGET_RATE = 8010.864 // 21 * 48828.125 / 128, the VERA PCM clock
const CUTOFF_HZ = 3600 // below the 4,005 Hz Nyquist with room for the filter skirt
const TAPS = 33
const NORMALIZE_DB = -0.3

const candidateRenderers = [
  process.env.MELODIUS_EXE,
  "C:\\dev\\veramusic\\tools\\melodius.exe",
  path.join(vera, "tools", "melodius.exe"),
].filter(Boolean)

const findRenderer = () => candidateRenderers.find((p) => fs.existsSync(p))

/* ---- WAV reader: only what melodius emits (PCM 16-bit, 44.1/48 kHz, stereo) */
const readWav = (file) => {
  const buf = fs.readFileSync(file)
  if (buf.toString("ascii", 0, 4) !== "RIFF" || buf.toString("ascii", 8, 12) !== "WAVE")
    throw new Error(`${file}: not a RIFF/WAVE file`)
  let rate = 0
  let channels = 0
  let bits = 0
  let data = null
  let pos = 12
  while (pos + 8 <= buf.length) {
    const id = buf.toString("ascii", pos, pos + 4)
    const size = buf.readUInt32LE(pos + 4)
    const body = pos + 8
    if (id === "fmt ") {
      const format = buf.readUInt16LE(body)
      if (format !== 1) throw new Error(`${file}: unsupported WAV format ${format}`)
      channels = buf.readUInt16LE(body + 2)
      rate = buf.readUInt32LE(body + 4)
      bits = buf.readUInt16LE(body + 14)
    } else if (id === "data") {
      data = buf.subarray(body, body + size)
    }
    pos = body + size + (size & 1)
  }
  if (!data || !rate || bits !== 16) throw new Error(`${file}: unsupported WAV layout`)
  const frames = Math.floor(data.length / (2 * channels))
  const mono = new Float32Array(frames)
  for (let i = 0; i < frames; i++) {
    let sum = 0
    for (let c = 0; c < channels; c++) sum += data.readInt16LE((i * channels + c) * 2)
    mono[i] = sum / (32768 * channels)
  }
  return { rate, channels, frames, mono }
}

/* ---- 3.6 kHz Blackman-windowed sinc, applied at the source rate ---- */
const designLowpass = (taps, cutoff, rate) => {
  const kernel = new Float32Array(taps)
  const mid = (taps - 1) / 2
  const fc = cutoff / rate // normalised cutoff (cycles/sample)
  let sum = 0
  for (let i = 0; i < taps; i++) {
    const n = i - mid
    const sinc = n === 0 ? 2 * fc : Math.sin(2 * Math.PI * fc * n) / (Math.PI * n)
    const w = 0.42 - 0.5 * Math.cos((2 * Math.PI * i) / (taps - 1)) +
      0.08 * Math.cos((4 * Math.PI * i) / (taps - 1))
    kernel[i] = sinc * w
    sum += kernel[i]
  }
  for (let i = 0; i < taps; i++) kernel[i] /= sum
  return kernel
}

const filter = (x, kernel) => {
  const taps = kernel.length
  const mid = (taps - 1) / 2
  const y = new Float32Array(x.length)
  for (let i = 0; i < x.length; i++) {
    let acc = 0
    for (let k = 0; k < taps; k++) {
      const j = i - k + (taps - 1) - mid
      if (j >= 0 && j < x.length) acc += kernel[k] * x[j]
    }
    y[i] = acc
  }
  return y
}

/* ---- remove the DC offset of the PSG/FM mix (a pulse wave at 50 % duty has a
 * constant term, and the render is full of them) ---- */
const highpass = (x, rate, cutoff = 20) => {
  const a = 1 / (1 + (2 * Math.PI * cutoff) / rate)
  const y = new Float32Array(x.length)
  let prevX = 0
  let prevY = 0
  for (let i = 0; i < x.length; i++) {
    const v = a * (prevY + x[i] - prevX)
    y[i] = v
    prevX = x[i]
    prevY = v
  }
  return y
}

/* ---- linear-interpolation resample to the exact hardware rate ---- */
const resample = (x, srcRate, dstRate) => {
  const outLen = Math.round((x.length * dstRate) / srcRate)
  const out = new Float32Array(outLen)
  const step = srcRate / dstRate
  for (let i = 0; i < outLen; i++) {
    const pos = i * step
    const idx = Math.floor(pos)
    const frac = pos - idx
    const a = x[idx] ?? 0
    const b = x[idx + 1] ?? a
    out[i] = a + frac * (b - a)
  }
  return out
}

/* Deterministic TPDF dither (mulberry32, fixed seed). */
const rng = (() => {
  let s = 0x9e3779b9
  return () => {
    s = (s + 0x6d2b79f5) | 0
    let t = Math.imul(s ^ (s >>> 15), 1 | s)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
})()

const quantize = (x) => {
  let peak = 0
  for (let i = 0; i < x.length; i++) peak = Math.max(peak, Math.abs(x[i]))
  if (peak === 0) return { bytes: Buffer.alloc(x.length), peak: 0, gain: 0 }
  const gain = Math.pow(10, NORMALIZE_DB / 20) / peak
  const out = Buffer.alloc(x.length)
  for (let i = 0; i < x.length; i++) {
    const v = Math.round(x[i] * gain * 127 + (rng() - rng()))
    out[i] = Math.max(-128, Math.min(127, v)) & 0xff
  }
  return { bytes: out, peak, gain }
}

const writeTable = (size, loopBytes, seconds) => {
  const blocks = Math.ceil(size / 512)
  const lastLen = size - (blocks - 1) * 512
  const header =
    "/* Auto-generated by tools/gen_pcm.mjs - do not edit. */\n" +
    "#ifndef _PCM_TABLE_H\n#define _PCM_TABLE_H\n" +
    `#define PCM_SIZE         ${size}u\n` +
    `#define PCM_BLOCKS       ${size === 0 ? 0 : blocks}u\n` +
    `#define PCM_LAST_LEN     ${size === 0 ? 0 : lastLen}u\n` +
    `#define PCM_LOOP_OFFSET  ${loopBytes}u\n` +
    `#define PCM_SECONDS      ${seconds.toFixed(2)}f\n` +
    "#endif\n"
  fs.writeFileSync(tablePath, header)
}

const main = () => {
  fs.mkdirSync(gen, { recursive: true })
  fs.mkdirSync(build, { recursive: true })

  const zsm = fs.readFileSync(zsmPath)
  const loopTick = zsmLoopTick(zsm)
  const loopByte = (samples) => Math.min(samples, Math.round((loopTick / 60) * TARGET_RATE))

  const renderer = findRenderer()
  if (!renderer) {
    const hint = candidateRenderers.join(", ")
    if (fs.existsSync(pcmPath)) {
      /* Keep the PCM rendered earlier; the header is derived from it so a
       * build on a machine without melodius still ships a working image. */
      const size = fs.statSync(pcmPath).size
      writeTable(size, loopByte(size), size / TARGET_RATE)
      console.log(`gen_pcm: no melodius.exe (looked in ${hint})`)
      console.log(`gen_pcm: keeping generated/othello.pcm (${size} bytes)`)
      return 0
    }
    writeTable(0, 0, 0)
    console.log(`gen_pcm: WARNING - no melodius.exe (looked in ${hint})`)
    console.log("gen_pcm: building WITHOUT OTHELLO.PCM; the game will fall back to MUSIC.PSG")
    return 0
  }

  const wavPath = path.join(build, "othello-render.wav")

  console.log(`gen_pcm: rendering ${path.basename(zsmPath)} with ${renderer}`)
  execFileSync(renderer, [zsmPath, "--no-loop", "--wav", wavPath], { stdio: "inherit" })

  const { rate, channels, frames, mono } = readWav(wavPath)
  const seconds = frames / rate
  console.log(`gen_pcm: ${frames} frames @ ${rate} Hz, ${channels} ch, ${seconds.toFixed(2)} s`)

  const filtered = highpass(filter(mono, designLowpass(TAPS, CUTOFF_HZ, rate)), rate)
  const pcm = resample(filtered, rate, TARGET_RATE)
  const loopBytes = loopByte(pcm.length)
  const { bytes, peak, gain } = quantize(pcm)

  fs.writeFileSync(pcmPath, bytes)
  writeTable(bytes.length, loopBytes, bytes.length / TARGET_RATE)

  const dbfs = (v) => (v > 0 ? (20 * Math.log10(v)).toFixed(1) : "-inf")
  console.log(`gen_pcm: source peak ${dbfs(peak)} dBFS -> gain ${gain.toFixed(3)}`)
  console.log(`gen_pcm: ${bytes.length} bytes (${(bytes.length / TARGET_RATE).toFixed(2)} s @ ` +
    `${TARGET_RATE.toFixed(2)} Hz) = ${Math.ceil(bytes.length / 512)} blocks`)
  console.log(`gen_pcm: loop back to tick ${loopTick} = byte ${loopBytes} ` +
    `(${(loopBytes / TARGET_RATE).toFixed(3)} s)`)

  if (!process.argv.includes("--keep-wav")) fs.unlinkSync(wavPath)
  return 0
}

process.exit(main())
