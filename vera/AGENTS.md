# CX16-OTHELLO → Apple II / VERA — Developer & Agent Guide

Comprehensive architectural reference, hardware semantics, porting history and
verification record for the **Apple II VERA port of `cx16-othello`** (Commander
X16 Othello/Reversi by Ivo Filot, GPL v3).

Read this file before touching anything in `vera/`. Several sections record
hardware behaviour that is **the opposite of the Commander X16 convention** —
each one was discovered by breaking the game, and each has a regression test.

---

## 📖 1. Project Overview

- **Source game**: `C:\dev\cx16-othello` — Commander X16 Othello/Reversi by
  Ivo Filot `<ivo@ivofilot.nl>`, GPL v3.
- **Port target**: Apple II (//e, //c, IIgs, Laser 128) with a VERA FPGA
  expansion card in **Slot 2 (`$C200`)** or **Slot 4 (`$C400`)**, plus the
  [Apple2TS](https://apple2ts.com) and [AppleWin](https://github.com/anomixer/AppleWin)
  emulators.
- **Build output**: `cx16-othello.hdv` — a 1700 KB bootable ProDOS 2.4.3 fixed
  disk image (3,400 blocks, 185 free). It no longer fits an 800 KB floppy: the
  pre-rendered soundtrack `OTHELLO.PCM` (1,162,243 B = 2,271 blocks) takes two
  thirds of the volume.
- **Toolchain**: `llvm-mos-sdk` (`mos-apple2e-clang`), Python 3 + Pillow +
  numpy, Node.js. No `gcc`, no `cl65`, no X16 emulator is required.
- **Feature parity**: PvP / PvC / CvC, 6×6 / 8×8 / 10×10 boards, stone and wood
  board styles, 10 stone colours, keyboard + joystick + mouse input, sound
  effects and streaming music, 6 help pages, animated background.

### Reference ports consulted

| Repo | What it contributed |
| :--- | :--- |
| `C:\dev\veratest` | VERA register map on Apple II, PSG register layout, Applesoft `STARTUP` launcher pattern, slot-2/slot-4 dual image pattern |
| `C:\dev\time-pilot` | ProDOS MLI patterns, 512-byte music streaming, VSYNC-driven music tick |
| `C:\dev\freegemas` | Tilemap + sprite composition for a board game, palette packing |
| `C:\dev\x16-hero-vera` | X16 → Apple II asset conversion pipeline, ZSM → PSG re-voicing |

---

## ⚙️ 2. VERA on the Apple II

### 2.1 Register window

VERA occupies the Apple II slot I/O space at `VERA_BASE + $00..$21`
(`VERA_BASE = $C200` for slot 2, `$C400` for slot 4). The optional YM2151 FM
daughterboard adds two write-only registers at `$20`/`$21`.

| Offset | Register | Notes |
| :--- | :--- | :--- |
| `$00-$02` | `ADDR_L / ADDR_M / ADDR_H` | `ADDR_H` bits [3:0] = address [19:16], bit 4 = DECR, bits [7:5] = stride index |
| `$03` / `$04` | `DATA0` / `DATA1` | Auto-incrementing data ports |
| `$05` | `CONTROL` | bit 0 = `ADDRSEL`, bits [6:1] = `DCSEL`, bit 7 = `RESET` |
| `$06` / `$07` | `IEN` / `ISR` | Interrupt enable / status |
| `$08` | `IRQLINE_L` | Raster IRQ target |
| `$09-$0C` | `DC0 / DC1 / DC2 / DC3` | video enable, `HSCALE`, `VSCALE`, border — **DCSEL-selected** |
| `$0D-$13` | Layer 0 | config, mapbase, tilebase, hscroll(2), vscroll(2), reserved(2) |
| `$14-$1A` | Layer 1 | same 7-byte shape |
| `$1B-$1D` | `AUDIO_CTRL` / `AUDIO_RATE` / `AUDIO_DATA` | PCM FIFO: `CTRL` bit 7 = FIFO reset, bit 6 = restart, bit 5 = 16-bit, bit 4 = stereo, bits 3:0 = volume through the card LUT `{0,1,2,3,4,5,6,8,11,14,18,23,30,38,49,64}`, `(v & $C0) == $C0` loops; `RATE` 21 = 48828.125/128 × 21 = **8010.86 Hz**; `DATA` = one mono 8-bit signed sample. FIFO 4,096 B, and **it never reports full** |
| `$20` / `$21` | `YM_REG` / `YM_DATA` | YM2151 FM daughterboard, **write-only**. With no daughterboard fitted the card aliases these to `ADDR_L` / `ADDR_M` (`reg & 0x1F`), which is exactly what `audio_detect_fm()` exploits. |

**Composer window registers are DCSEL-selected.** To touch
`hstart / hstop / vstart / vstop` you must set `CONTROL = $02` (DCSEL 1), write
window offsets `$09-$0C`, then restore `CONTROL = $00`. Layer registers
`$0D-$1A` are *not* DCSEL-selected.

### 2.2 VRAM map (128 KB)

```
$0:0000-$3FFF   64 game tiles        16x16, 8bpp        16,384 B
$0:4000-$9FFF   96 font tiles        16x16, 8bpp        24,576 B
$0:A000-$A7FF   Layer 0 tilemap      32x32 entries       2,048 B
$0:A800-$AFFF   Layer 1 tilemap      32x32 entries       2,048 B
$0:B000-$B0FF   mouse pointer tile (16x16 8bpp, written by init_mouse_sprite)
$0:B100-$F9BF   free (≈80 KB)
$1:B000-$B01B   debug counters, -DVERA_DEBUG_STATS builds only (never shipped)
$1:F9C0-$F9FF   16-voice PSG registers                      64 B
$1:FA00-$FBFF   512-entry palette (256 colours)            512 B
$1:FC00-$FFFF   128 sprite attributes (8 B each)         1,024 B
                                              total ≈ 45.6 KB / 128 KB
```

Palette packing is `[G4<<4 | B4][0 | R4]` little-endian, generated from
`assets/palette/cx16palette.png` (1024×1024, 16×16 swatches sampled at
`(j*64+32, i*64+32)`).

### 2.3 Apple II CPU-side memory map

```
$0000-$00FF   Zero page          LLVM-MOS imaginary registers __rc0..__rc31 at $02-$1F
                               .zp.data $50-$9A (75 B), .zp.bss $9B-$9D
$0100-$01FF   Hardware stack page
$0800-$0AFF   ProDOS workspace / free game RAM
$0B00-$0B3F   ProDOS pathname buffer
$0C00-$0DFF   Music record buffer A (512 B, ProDOS Text Page 2 area)
$0E00-$139F   Help page buffer (6 x 240 B at $0E00); it runs into the
                               ProDOS QUIT / BASIC.SYSTEM scratch area below,
                               which is safe only because mlib_quit is never
                               called (see the note after this map)
$13A0-$13FF   ProDOS scratch tail, also overlapped by the help buffer
$1400-$5CE0   Current program payload, loaded (text 18,589 B + rodata 841 B +
                               data 50 B); __data_end = $605C
$6200-$6A77   .bss, zeroed at startup (not in the BIN), 512-aligned so that
                               pcmfeed.s can address a buffer from offset 0:
                               musicBufB (512), pcm_master and pcm_index
                               (512 each), then the streamer state;
                               __bss_end = $6A78
$6A78-$6A90   .noinit (static stack space)
$6A91         __heap_start (payload end; only used if malloc is called)
$7000-$7BFF   Three page-aligned 1 KB ProDOS OPEN buffers
$BE00         __stack    soft stack grows down from here
$BF00         ProDOS MLI entry
$C200/$C400   VERA registers (+ $20/$21 YM2151 when the FM board is fitted)
```

Binary format: Apple II `BIN` header = 2-byte load address `$1400` + 2-byte
length; `main.bin` is 19,548 bytes of payload (`__data_end` = `$605C`, `.bss`
ends at `$6A78`, 39 blocks of the HDV). `STARTUP` is 2,538 bytes (Applesoft
type `$FC`, aux `$0801`, 5 blocks).

Note that the payload is free to grow: `link1000.ld` declares a `ram` region of
`0x8800` bytes from `$1400` to `$9C00`, and the ProDOS OPEN buffers and the MLI
entry at `$BF00` are outside it. Do **not** let it grow past `$6FFF`:
`$7000-$7BFF` holds the ProDOS I/O buffers, and `link1000.ld` ends with an
`ASSERT(__heap_start < 0x7000)` so an overflow fails the link instead of
corrupting a file read at run time.

The one deliberate overlap in that map: the help-page buffer is 1,440 B at
`$0E00`, so it runs to `$139F` and overwrites the ProDOS QUIT code at
`$1000-$12FF` and BASIC.SYSTEM's scratch at `$1300-$13FF`. That is safe here
only because the game never returns to ProDOS — `mlib_quit` exists in `mli.s`
but nothing calls it, and users leave via RESET. If a quit path is ever added,
move `helpBuf` out of `$1000-$13FF` first (bank-1 VRAM, or the free
`$0B40-$0BFF` gap, are both unused).

---

## 🧱 3. Apple II VERA ≠ Commander X16 VERA

This is the section that matters. Every entry below was a real bug.

### 3.1 Scaling is inverted

`HSCALE / VSCALE`: **`$80` = 1.0 (1:1)**, **`$40` = 2:1 magnification**.

The X16 convention (`$40` = 1:1) does **not** apply. The renderer computes
`eff_x_fp += (scale << 9)` per output pixel and `eff_x = eff_x_fp >> 16`.

The game renders a **320×240 source (20×15 tiles of 16×16) and the composer
magnifies it 2:1 so it fills the whole 640×480 screen**:

```c
VERA.display.hscale = 0x40;          /* 320 -> 640 */
VERA.display.vscale = 0x40;          /* 240 -> 480 */
```

320×240 is the resolution the *layout* is built for: help pages are 20 columns,
the score panel is right-aligned at column 20, and `src/mouse.s` clamps the
Apple Mouse Card to 320×240 — which is what makes `mx >> 4` land on the correct
tile. Sprites are positioned in source pixels, so they land directly on the
16-pixel tile grid.

### 3.2 Never write the DCSEL = 1 composer window registers

Every write to the composer window (`$C209-$C20C`) makes the renderer run a
midline pass (`video_step(MHZ, 0, true)`), which disturbs the VSYNC/raster
timing the MLI streaming loop depends on. Symptom: the boot dies inside the
first `mlib_read_block`, whose `RTS` returns to `$ffff`.

The reset defaults (`hstart=0`, `hstop=640`, `vstart=0`, `vstop=480`) are
already the full screen, so `init_screen()` only sets `hscale`/`vscale`.

### 3.3 The full 32×32 tilemap must always be filled

The renderer builds a **640-pixel** layer line and applies the scroll in
*output* pixels:

```js
for (let x = 0; x < SCREEN_WIDTH; x++) {
    const eff_x = (x + props.hscroll) & (props.mapw * props.tilew - 1)
```

So the diagonal background scroll reads tiles from the whole map, wrapping at 32
tiles. Filling only the visible 20×15 area leaves the rest stale, and the
scrolled edge shows the border colour through transparent tile index 0 — the
"background effect breaks" symptom (measured: 26,952 black pixels with a partial
fill, 0 with the full fill).

`set_background()` / `clear_foreground()` therefore write all 1024 entries.

### 3.4 Colour index 0 is transparent for layers

The layer compositor is `col_index = l2_col_index ? l2_col_index : l1_col_index`.
Index 0 lets the layer below (and ultimately the border colour) through. Font
glyphs and board tiles rely on this: their ink is index `$51`, their background
is index 0.

### 3.5 Sprite X/Y are raw pixel coordinates

`sprite_x = data[2] | (data[3] & 3) << 8`, `sprite_y = data[4] | (data[5] & 3) << 8`
— 10-bit **pixel** coordinates. The X16 `X_LO = X >> 1` convention does not
apply. `set_sprite()` writes `posx << 4`, which lands exactly on the 16-px grid.

### 3.6 Tile size and map width encoding

`tilew_log2 = 3 + (tilebase & 1)`, `tileh_log2 = 3 + ((tilebase >> 1) & 1)` →
only 8×8 and 16×16 are supported.
`mapw_log2 = 5 + ((config >> 4) & 3)` → the map width must be a power of two, so
the map is declared 32×32 even though only 20×15 is on screen.
`color_depth = config & 3`, `bits_per_pixel = 1 << color_depth` → `3` = 8bpp,
`2` = 4bpp, `0` = text mode.

### 3.7 `ADDR_H` stride is an index, not a bit count

The stride table is indexed by `ADDR_H >> 5`. Index 2 (`$10`) = +1. Bit 0 of
`ADDR_H` selects VRAM bank 1 (`VERA_INC_BANK1 = $11`).

### 3.8 Palette bank aliasing

The write path is `palette[address & 0x1FF]`, so bank-1 `$FA00`
(`0x1FA00 & 0x1FF = 0`) maps to indices 0..511. Identical in AppleWin.

### 3.9 Keyboard codes are not PETSCII

The X16 build used PETSCII cursor codes (`$11` = cursor **down**, `$91` = cursor
**up** — the names are already inverted there, and its `move_cursor(1,0)` on
"up" is correct for X16). The Apple //e arrow keys are:

| Key | KEYD |
| :--- | :--- |
| Left | `$08` |
| Up | `$0B` |
| Down | `$0A` |
| Right | `$15` |

`move_cursor(int8_t y, int8_t x)` takes a **row delta** and row 0 is the top, so
a positive row delta moves **down**. The port maps:

```c
case KEYCODE_DOWN:  move_cursor( 1, 0); return;
case KEYCODE_UP:    move_cursor(-1, 0); return;
case KEYCODE_LEFT:  move_cursor(0, -1); return;
case KEYCODE_RIGHT: move_cursor(0,  1); return;
```

The joystick block uses the same mapping.

### 3.10 Fixed-width text is not a C string

`assets/help.hlp` parses into 240-byte pages of 12 rows × 20 characters, padded
with spaces and with **no NUL between rows**. `write_string()` stops at the NUL,
so feeding it a help row writes the whole rest of the page: the write runs past
column 19, wraps at the 32-tile map stride and lands on the rows below — exactly
where `(N) NEXT (P) PREV` / `(ESC) BACK PAGE:` live, so the help text appears to
overflow the screen.

All fixed-width fields go through `write_string_n(str, n, y, x)`, which clamps
to the field width **and** to the map row:

```c
if (x >= MAPWIDTH) return;
if (n > (uint8_t)(MAPWIDTH - x)) n = (uint8_t)(MAPWIDTH - x);
while (i < n && *s) { ... }
```

`write_string()` is kept for NUL-terminated literals only.

### 3.11 Audio: PSG, YM2151 FM, and the two music streams

The X16 original plays YM2151 FM music through ZSMKit. Three music files ship on
the HDV and `audio_start_music()` picks one at boot — `MUSIC.VZM` when the FM
daughterboard is fitted, `OTHELLO.PCM` (§3.11.1) when it is not, and `MUSIC.PSG`
if the PCM file is missing:

| File | Size (HDV blocks) | Used when | Content |
| :--- | :--- | :--- | :--- |
| `MUSIC.VZM` | 198,656 B (388) | the YM2151 daughterboard is present | native PSG writes for voices **4–15** + real YM2151 register writes |
| `MUSIC.PSG` | 157,956 B (309) | no FM board *and* no `OTHELLO.PCM` | the FM voices re-voiced onto PSG voices **0–3**, native voices 4–15 passed through |
| `OTHELLO.PCM` | 1,162,243 B (2,271) | no FM board (the normal no-FM path) | the whole soundtrack pre-rendered to 8 kHz mono and played through the PCM FIFO |

`MUSIC.PSG` is generated by `tools/zsm2psg.py`; `MUSIC.VZM` by
`tools/zsm2vera.mjs` + `tools/gen_music.mjs`. Both are streamed in 512-byte
ProDOS records through **two** buffers — A at `$0C00` (the ProDOS Text Page 2
area) and B in `.bss` (512 B) — and advanced by the VERA VSYNC at ~60 Hz,
including while tilemap work is in flight, so disk loading never changes the
tempo. Thumb SFX stay on PSG voice 3 (registers 12–15), which neither stream
touches: VZM's PSG mask is `$FFF0` and `MUSIC.PSG` only writes voices 0–3.

**Double buffering / prefetch (the stutter fix).** Playing a record straight
from disk used to make the frame that crossed a 512-byte boundary read *inside*
the music tick. That read is a ProDOS OPEN/READ round trip of several
milliseconds (≈4 ms at 1 MHz for 512 B), so that one frame took ~20 ms instead
of ~17 ms and the next tick landed late: an audible stumble every 22 frames
(≈0.4 s), i.e. exactly "the music jerks a little". The player now keeps one
record buffered ahead:

- `music_peek()` never does I/O at a block boundary when the prefetch exists:
  it just swaps `musicCur` between A and B (`musicPre` is a 1-bit flag, *not* a
  pointer — a pointer version silently stopped prefetching after the first swap
  and replayed two buffers forever, worth remembering).
- `audio_refill()`, called at the very end of `pump_frame()`, reads
  `musicBlock + 1` into the buffer that is *not* being decoded. The MLI read
  therefore happens in the tail of a frame, where the cost is absorbed instead
  of landing between two ticks. A missed prefetch falls back to the old
  synchronous read, and reading *one block past EOF* returns `$4C` without
  raising the sticky I/O error (that probe is how the prefetch discovers EOF).

Frame pacing matters as much as the buffering — see §3.15.

**FM detection (`audio_detect_fm()`, first thing in `main()`).** The VERA
daughterboard maps the YM2151 at `$Cx20`/`$Cx21`, but the card decodes only five
address bits (`reg & 0x1F`), so on a card *without* the FM chip those reads
alias `ADDR_L`/`ADDR_M`. The probe is therefore a write/read-back test:

```c
VERA.control = 0x00;               /* ADDRSEL = 0, DCSEL = 0 */
VERA.address = 0xA55A;             /* ADDR_L = $5A, ADDR_M = $A5 */
fm_present = (VERA_YM_REG != 0x5A || VERA_YM_DATA != 0xA5) ? YES : NO;
/* then restore the address latches that were saved on entry */
```

Both bytes must be checked: one of them alone can match by accident. Measured in
AppleWin: FM enabled → reads `$00/$00` → `fm_present = 1`; FM disabled →
reads `$5A/$A5` → `fm_present = 0`. `STARTUP` runs the same probe in Applesoft
(before `BRUN`) and prints either

```
STATUS: YM2151 (OPM) FM CHIP FOUND
PSG + FM MUSIC ENABLED
```

or `STATUS: NO YM2151 (OPM) FM CHIP` followed by the **music source** line, so a
user can see which build behaviour they are getting. The BASIC probe writes
`$C200/$C201` and reads `$C220/$C221` (slot 4: `$C400/$C401` / `$C420/$C421`) and
restores `$C200/$C201`.

**The polarity of the BASIC flag is the opposite of the C one.** `STARTUP` starts
with `F = 1` (*assume a chip is fitted*) and clears it to 0 only when the reads
come back as `$5A/$A5` — which is the **aliasing** case, i.e. **no chip**. So
`F = 1` means "FM present", and the branch that prints the FOUND lines must test
`IF F = 1`. Testing `F = 0` there inverts both lines: an FM machine reports
`NO YM2151` + `PCM MUSIC STREAMING` while actually playing `MUSIC.VZM`, and a
no-FM machine reports `FM CHIP FOUND` + `PSG + FM` while playing `OTHELLO.PCM`.
That is a banner-only bug — the audio path comes from the C probe, which is
independent — and it is easy to miss precisely because the music sounds right.
See §7 #34 for the fix and the text-page read-back that proves it.

**The no-FM line is a file probe, not an assumption.** `STARTUP` used to print
`PSG MUSIC ONLY` on every no-FM machine, which the port had already outgrown: the
normal no-FM path is the pre-rendered `OTHELLO.PCM` (§3.11.1). It now prints
`PCM MUSIC STREAMING`, and `PSG MUSIC ONLY - NO PCM FILE` only when
`OTHELLO.PCM` is genuinely absent (a copied installation). The check is a
**read-only MLI OPEN + CLOSE** issued from BASIC — `OPEN` (`$C8`) never creates a
file when the caller supplies no access byte, which is exactly what
`disk_start_music()` relies on for its own missing-file fallback:

- Stubs and parameter blocks are poked into page 3 by `STARTUP`
  (`$0300` = `JSR $BF00 / .byte $C8 / .word $0330 / STA $0320 / ...`, `$0310` =
  the same shape for `CLOSE` `$CC`), following the **inline** MLI convention: the
  command byte and the parameter-block word sit directly after the `JSR`, and MLI
  returns to `ret+3`. A/X/Y-based calls are wrong — the layout and the
  `ret+4` resume arithmetic the harness relies on are in §5.2.
- `$0330` = `count 3`, path pointer, I/O buffer pointer `$7000` (free until the
  payload loads), ref out. The pathname is a Pascal string at `$0340`.
- Two attempts, both read-only: the absolute path `/CX16.OTHELLO/OTHELLO.PCM`
  first, then the bare `OTHELLO.PCM`. The absolute attempt is needed because
  ProDOS boots `BASIC.SYSTEM` with an **empty prefix** on the packaged HDV, and an
  empty prefix cannot resolve a relative name — a relative-only probe reported
  `$40` on every run. The relative attempt keeps copied installations working
  when their prefix points somewhere else.
- A successful OPEN is always followed by `CLOSE`, so no ProDOS file slot or I/O
  buffer is left behind. Measured: PCM image → status `$00`; 800 KB no-PCM image
  → status `$40` on both attempts. The probe costs ≈0.3 s of boot (one tree-file
  `OPEN`), and a failed OPEN does **not** abort the later
  `PRINT CHR$(4);"BRUN MAIN.BIN"` — the no-PCM image still boots to the title and
  plays `MUSIC.PSG` (verified by WAV: max 8,176).
- The earlier CATALOG-screen scan approach was dropped: it flashed the directory,
  needed `HOME` afterwards, and its 1,013-iteration Applesoft compare loop made
  the launcher far slower than the whole payload boot.

**VZM ("VERA ZSM") stream format** — `tools/zsm2vera.mjs` emits it, `audio.c`
decodes it. There is no header; each byte is a command:

| Byte | Meaning |
| :--- | :--- |
| `$00-$7F` | delay: N further ticks (the tick the byte itself is consumed on is not counted) |
| `$80-$BF` | PSG: `reg = b & $3F`, then one value byte |
| `$C0-$DF` | FM batch: `count = (b & $1F) + 1`, then `count` × (reg, value) pairs |
| `$E0-$FE` | NOP padding (the converter pads records to 512 bytes) |
| `$FF` | end of stream → seek to the loop point |

The converter de-duplicates writes against an all-zero PSG+YM register image
(except `$08` key writes, which are always forwarded), pads each record to a
512-byte boundary so every seek address is block-aligned, and requires
`tickRate == 60`. A VZM record must therefore describe the writes relative to
whatever state the previous record left — there is no "reset" marker.

Consequences implemented in `audio.c`:

- **At music start with FM present**, `audio_init()` calls `fm_silence()`: key-off
  all eight channels (`$08 = $00..$07`) and zero every YM register `$20-$FF`, so
  the de-duplicated stream starts from the same all-zero image the converter
  assumed. PSG is silenced the same way (`psg_silence_all()` writes 0 to all 16
  voices' registers at `$1F9C0`).
- **The loop point is not 0.** `MUSIC_VZM_LOOP` is a byte offset (16,788 =
  tick 1536 ≈ 25.6 s); on `$FF` the player SET_MARKs to it and continues, so the
  PSG/FM state carries across the loop exactly as the converter assumed. Only
  when the loop offset is 0 does a loop reset PSG + FM.
- **Missing file fallback.** A copied installation may lack `MUSIC.VZM`;
  `audio_start_music()` then falls back to `MUSIC.PSG` and calls
  `disk_clear_error()` so the sticky ProDOS error does not pop the error screen.
- **Stopping music** (`audio_stop_music()`, the menu `(M)` toggle) writes the
  PSG volume-off pattern to voices 0–2 and 4–15 **and** keys off all YM
  channels; voice 3 is left alone so a thumb SFX can still be heard.

Verification: `tools/verify_music_streams.mjs` re-converts `othello.zsm`, diffs
it against the packaged `MUSIC.VZM`, and replays both streams, comparing the
PSG/FM/`$08` write sequence tick by tick. Last run: **8705 ticks, 388 blocks,
61,977 PSG / 29,293 FM / 7,360 key writes, 0 mismatched ticks**.

**YM2151 Register Parsing Bug**: An earlier version of `zsm2psg.py` mistakenly
mapped the YM2151 Pan/Algorithm register (`$20-$27`) as the Key Code register,
and the Key Code register (`$28-$2F`) as Total Level. This caused the script to
synthesize completely random frequencies (screeching/high pitches) derived from
stereo pan data instead of musical notes. This was fixed by correctly parsing
Key Code from `$28-$2F` and Total Level from `$60-$7F`.

**MIDI & PSG Utilities**:
- `tools/zsm2midi.py`: Added to extract YM2151 Key On events to a standard `.mid` file for testing alternate synthesis engines (e.g. `veramusic` tools).
- `tools/psg_to_veramusic.py`: Added to convert the native Othello PSG format (which streams `0x80|reg, val` + delays) into the standard `veramusic` block-count PSG format so it can be previewed in `psgplay.exe`.
- `tools/zsm_probe.py`: prints the command histogram of a ZSM file; useful to
  check the tick rate and how many FM writes a song has before converting.

### 3.11.1 `OTHELLO.PCM`: the pre-rendered soundtrack (no-FM machines)

`MUSIC.PSG` has to squeeze the FM channels onto four PSG voices, and that is
exactly where the soundtrack's low end lives: measured sub-500 Hz energy is
**0.30** with real FM versus **0.03** for `MUSIC.PSG`, which is what "the music
sounds dry" meant. A 1 MHz 6502 cannot synthesise that bass live, so the build
pre-renders the whole song:

1. `tools/gen_pcm.mjs` renders `assets/othello.zsm` through **melodius**
   (YM2151 + PSG) to a 48 kHz WAV — the file a speaker hears on an X16.
2. It mixes to mono, applies a 33-tap Blackman-windowed sinc low-pass (3,600 Hz
   cutoff) plus a 20 Hz high-pass, resamples to **8,010.864 Hz** (48828.125 × 21
   / 128, the card's PCM clock at rate register 21), normalises to −0.3 dBFS,
   dithers with TPDF noise and quantises to mono 8-bit signed.
3. It writes `generated/othello.pcm` (1,162,243 B) and
   `generated/pcm_table.h` (`PCM_SIZE`, `PCM_BLOCKS`, `PCM_LAST_LEN`,
   `PCM_LOOP_OFFSET` = 205,078 B ≈ 25.6 s, `PCM_SECONDS` 145.08).

8 kHz is a deliberate ceiling: Nyquist is 4 kHz, so only the top octave is lost
(the *bass* was what was missing, not the highs), and 16 kHz playback would need
2.32 MB of stream plus ~35 % of the CPU just to feed the FIFO at 1 MHz.

Boot order in `audio_start_music()`: FM chip present → `MUSIC.VZM` (live FM, no
PCM); no FM → `OTHELLO.PCM` through the PCM FIFO; `OTHELLO.PCM` missing (a copied
installation) → `MUSIC.PSG` as the last resort. All three share the `(M) MUSIC
ON/OFF` toggle.

Playback: `audio.rate = 21`, `audio.control = $0C` (mono 8-bit, volume 12 of 15),
then 128-byte chunks pushed by `src/pcmfeed.s` — a 15-cycle/byte `(dp),y` loop;
the C equivalent costs ~30 cycles/byte and does not fit the frame. Flow control
is ISR bit 3 (`$08`), which on this card is a **live level**, not a latched flag:
it reads set while the FIFO holds fewer than 1,024 bytes, so `audio_feed_pcm()`
feeds up to `PCM_FEED_CHUNKS` (4) chunks per frame and catches up on the next
frame after an overrun. The PCM clock is independent of VSYNC, so the tempo stays
correct even when the frame rate drops. At EOF the stream `SET_MARK`s to
`PCM_LOOP_OFFSET`, which is **not** block aligned, so the first block after the
loop is played from its middle byte.

**Volume 12, not 15.** The card's PCM volume (control bits 3:0) is the only mix
control the pre-rendered stream has, while the PSG effects have none at all —
their thumb data already writes volume `$3F` (`0x8E,0xFF`), i.e. full scale. At
volume 15 the music peaked at 23,552 and buried every effect. §3.11.3 has the
numbers for the mix that ships.

**SFX voices follow the music mode** (`sfx_set_voices()`): `MUSIC.PSG` writes
voices 0–15, so the effect gets voice 3 alone (mask `$08`); `MUSIC.VZM` leaves
0, 1 and 3 (mask `$0B`); PCM mode leaves all sixteen, and the thumb is replayed
on four of them (0–3, mask `$0F`). Each PSG voice has its own phase counter, so
four voices sum to ≈16,500 peak — about 1.5× the music peak at volume 12, with
no clipping; eight voices measured 33,008 (full scale, clipped), which is why it
is four and not eight. `sfx_tick()` repeats every `>= $80` register write on
each voice in the list, so duplicating the thumb costs nothing in stream size.

### 3.11.2 Streaming `OTHELLO.PCM`: MLI READ_BLOCK and the block map

The obvious implementation — `disk_read_music_block()` in a loop — cannot keep
up. A file-level ProDOS read costs **22.7 ms regardless of size** (SET_MARK plus
a READ round trip through `JSR $BF00`, zero-page save/restore included), while a
raw MLI `READ_BLOCK` costs **4.6 ms**. One 512-byte block per frame on the file
path therefore caps the game at ~24 fps while the FIFO drains 8,011 B/s and
starves.

So the PCM stream bypasses the file layer entirely:

- `disk_pcm_map_init(name)` reads the **volume directory** (block 2, then the
  following directory blocks), matches the `OTHELLO.PCM` entry by its
  `entry[0] & $0F` name length and reads the file's **master index block** from
  the key pointer. It validates that the entry is a tree file
  (`blk[4] & $F0 == $F0`) and that block 0 of the file matches what a file-level
  read returns; anything unexpected sets `pcm_map_ok = 0` and the player degrades
  to the slow but correct path (`pcmFallbacks` counts those).
- `disk_pcm_read(file_block, buf)` does the whole translation: sapling files are
  `key + n`; for a tree file the master index gives the index block for
  `n >> 8` (cached in `pcm_cached_slot`, so the 512-byte index block is re-read
  once per 256 data blocks), and that index block gives the data block for
  `n & $FF`, which `disk_read_block_raw()` then reads.
- **Index blocks are split halves, not words.** Every 256-entry pointer table in
  a ProDOS index block stores the 256 *low* bytes at `$000-$0FF` and the 256
  *high* bytes at `$100-$1FF`:
  `master[slot] | (master[256 + slot] << 8)` and
  `index[j] | (index[256 + j] << 8)`. Reading it as 2-byte words — the natural
  guess — yields pointers like 41,890 and garbage audio. Verified against a
  Cadius image: master entry 0 = `$A2`/`$03` → block 930, index entry 0 =
  `$AB`/`$03` → block 939, and `OTHELLO.PCM` is one contiguous 2,271-block run
  at blocks 939–3,209.
- The two index buffers are 512 B each and must be 256-aligned (`pcm_master`,
  `pcm_index`), because `pcmfeed.s` reaches buffer bytes with `lda $xx00,y` and
  C indexes them as `[j]` / `[256 + j]`. They cost 1 KB of `.bss`, which is why
  the payload budget is tighter than it used to be.

Measured (AppleWin, slot 2, FM off, 25 M cycles ≈ 24.49 s, `-DVERA_DEBUG_STATS`
build): 833 frames → **34.1 fps**, 147,968 B fed, `pcmFallbacks` 0. The same
build with the file-level reader manages 24.1 fps; the two WAV dumps are
equivalent (same envelope and spectrum), which is what proves the block map reads
the right bytes.

### 3.11.3 The mix that ships (SFX audibility)

Reported as "the effects are too quiet, the music drowns them". One PSG voice
peaks at 2,044 and the PCM stream at volume 15 peaked at 23,552, so even a
four-voice thumb sat ~9 dB down. Two changes, measured by subtracting a run with
the effect triggered from one without it (the stream is deterministic, so the
difference *is* the effect):

| Mix | Music | SFX |
| :--- | ---: | ---: |
| before: PCM volume 15, 4 PSG voices | 23,552 peak, rms ≈7,000 | ≈17,000 peak, drowned |
| shipped: PCM volume 12 (−6.6 dB), 4 PSG voices | 11,040 peak, rms 3,000–3,300 | 17,072 peak, rms ≈4,100 |

The effect now sits at or above the music bed in every measurement (in a quiet
passage: music rms 156 versus effect rms 4,345) and the sum still peaks at
25,232, well under 32,768 — the eight-voice variant clipped there. `MUSIC.VZM`
machines keep live FM music and the 3-voice thumb, unchanged.

### 3.12 16-bit scroll registers

The Apple II `mos-apple2e-clang` compiler defines `unsigned int` as 16-bit. VERA's scroll registers (`HSCROLL_L`/`HSCROLL_H` and `VSCROLL_L`/`VSCROLL_H`) are 12-bit, spanning two bytes. If the C struct defines `hscroll` and `vscroll` as `unsigned char`, writing to `vscroll` maps to `HSCROLL_H` (offset 4) instead of `VSCROLL_L` (offset 5). The layer structs in `apple2e.h` must declare them as `unsigned int` so they properly map to offsets 3+4 and 5+6 to ensure diagonal scrolling works.

### 3.13 Mouse pointer sprite is not automatic

The Commander X16 kernal automatically reads the mouse hardware and assigns an empty sprite (Sprite 0) with a hardware pointer graphic. The Apple II VERA has no such kernal. The game must manually load a mouse pointer tile into VRAM (e.g., at `$B000`), map Sprite 0 to it with `assign_sprite()`, set its z-depth, and poll the Apple Mouse Card (`buttons & 0x80` for the primary button) to move it.

`mouse_init()` probes slots **5, 3, 6, 7, 1, 4, 2** (in that order, so the VERA card in slot 2 or 4 is looked at last) for the Apple Mouse ROM signature: `$Cn0C == $20` and `$CnFB == $D6`, with the slot number patched into the two `LDA $C0xx` operands. When no signature matches it must simply store slot 0 and return. **That exit path is a long branch** — see §3.14.

### 3.14 6502 relative branches silently wrap past ±127 bytes

The assembler does *not* diagnose an out-of-range `Bcc`. It writes the
(untruncated) displacement into one byte, so a branch that needs +130 bytes is
encoded as `$82`, which the CPU reads as **−126** and jumps backwards into the
middle of unrelated code. The result is a boot hang with no error message.

This is exactly what happened to `mouse_init()`. In the pristine HEAD build the
no-mouse exit is **141 bytes** forward, so the assembler wrote `$8D`, whose
signed value is −115:

```
15e1: e0 07      cpx #$7
15e3: b0 8d      bcs $1573     ; intended target $1672 (disp +141), lands in the
                              ; middle of mouse_restore_zp and wipes zero page
```

Because `mouse_init()` is called from `input_init()` *before*
`audio_start_music()`, every machine without a mouse card stopped there: no
title screen, no music, and ProDOS never got control back. This is why "even the
original HDV does not boot" in AppleWin (measured: `-fm-wav-dump` is silent for
a pristine HEAD build, non-silent after the fix). Fixed by making the exit an
absolute jump, which cannot overflow:

```asm
mouse_probe_loop:
        CPX #7
        BCC mouse_probe_slot
        JMP mouse_init_none      ; 141 B away; a BCS here would wrap
mouse_probe_slot:
        ...
```

Rules learned from this:

- Any branch whose two endpoints are more than ~100 bytes apart in a file that
  is still being edited should be written as `Bcc label` + `JMP` (or an inverted
  branch), not as a bare `Bcc far_label`.
- The linker does **not** relax these branches. `mos-apple2e-clang` produced a
  broken binary with exit code 0 and no warning.
- To audit a build, disassemble it and check every branch target against the
  symbol table (`llvm-nm` + `llvm-objdump -d`), or better, exercise the *cold*
  path. The mouse "no card" path is the cold path, and it is where the bug hid.

### 3.15 Frame budget: VSYNC catch-up, input cadence, prefetch

The music tempo *is* the frame rate: `pump_frame()` waits for one VERA VSYNC
edge and advances exactly one stream tick, so anything that makes a frame
overrun its 16.7 ms slot slows the song down and shows up as a hitch. The frame
work was therefore cut until it fits, and the wait was made non-destructive:

- **Catch-up instead of clear-and-wait.** The ISR flag is read, not cleared,
  unless it is clear. If the previous frame already overran, the edge is
  *already pending*, so the loop must not throw it away:

  ```c
  if ((VERA.irq_flags & VERA_IRQ_VSYNC) == 0) {
      VERA.irq_flags = VERA_IRQ_VSYNC;               /* writing 1 clears */
      while ((VERA.irq_flags & VERA_IRQ_VSYNC) == 0) { }
  } else {
      frameOverruns++;                               /* keep the pending edge */
  }
  ```

  Before this change every overrun cost a whole extra frame: with a Mouse Card
  in slot 5 a 40 M-cycle run drew only ~1408 frames (≈36 fps) instead of ~1967.
  That is a 40 % slow song plus a visible hitch every frame that overran — the
  "music jerks a little" report.
- **Poll the slow devices less often.** Reading the Apple Mouse Card costs a
  far call that saves/restores all 192 bytes of `$00-$BF` (`mouse.s`), and
  sampling the 555 paddles costs two ~100 µs spins. Neither needs 60 Hz for a
  menu pointer: `input_update()` now polls the mouse every 2nd frame (30 Hz)
  and the paddles every 3rd frame (20 Hz), while the keyboard and joystick
  buttons are still read every frame. Movement/repeat logic is unchanged.
- **Prefetch in the tail of the frame** (§3.11) instead of blocking on a
  512-byte ProDOS read inside a music tick.

Measured in AppleWin, 40 M cycles, slot-2 VERA, `main.bin` + `MUSIC.VZM`:

| Configuration | Frames | Overruns | Effective fps |
| :--- | ---: | ---: | ---: |
| 60 Hz mouse + paddles, clear-and-wait | 1408 | 532 | ~36 |
| 60 Hz mouse + paddles, catch-up | 1780 | 1780 (100 %) | 53.8 |
| 30 Hz mouse / 20 Hz paddles, catch-up (shipping) | 1921–1967 | ~58 % | 58.3–59.4 |
| same, no Mouse Card fitted | 1971 | 2 | 59.5 |

The VSYNC edge itself arrives at ~59.5 Hz, so the shipping build is within ~2 %
of the ceiling even with a Mouse Card fitted; the remaining overruns are the
asset-load disc reads and the 4 ms prefetch read every 512 bytes (in a 40 M-cycle
run `musicFallbacks` stayed 0 and `musicPrefetches` reached 47, so no music tick
ever had to wait for its own read).

**Measuring it.** `-DVERA_DEBUG_STATS` appends to `pump_frame()` a block that
stores seven little-endian `uint32` at bank-1 `$1B000` — `frameCount`,
`frameOverruns`, `musicTicks`, `musicPrefetches`, `musicFallbacks`,
`pcmBytesFed`, `pcmFallbacks` — which `-vera-dump` exposes at file offset
`0x1B000`:

```python
fr, ov, ticks, pre, fb, pcmb, pcmfb = struct.unpack('<7I', dump[0x1B000:0x1B01C])
```

`frameOverruns / frames` should stay well below 1.0, `musicFallbacks` must stay
0 (nonzero means a music tick had to do its own disk read), and `musicTicks`
should track `frames` once the music has started.

What the `#ifdef` guards is only the **VRAM write-back loop**. The counters
themselves are declared unconditionally (`video.c` and `audio.c`), so they cost
seven `uint32_t` = 28 B of `.bss` — visible in `build/main.map` as
`.bss.frameOverruns`, `.bss.pcmBytesFed`, `.bss.pcmFallbacks`, … — plus a handful
of increments per frame. That is deliberately kept: ~40 cycles per frame is
nothing and the numbers are what caught the prefetch bug (§7.28). Never ship the
write-back loop: re-storing 28 bytes to bank-1 VRAM every frame is a far bigger
cost than the counters, and the shipping image has no `-DVERA_DEBUG_STATS`, so
bank-1 `$1B000` is simply unused there.

---

## 🛠️ 4. Build System

```bash
cd C:\dev\cx16-othello\vera
build.bat
```

Seven steps:

| # | Step | Tool |
| :--- | :--- | :--- |
| 1 | Generate the asset blob | `tools\gen_assets.py` |
| 2 | Convert ZSM music → VERA PSG (PSG-only build) | `tools\zsm2psg.py` |
| 3 | Convert ZSM music → VZM (PSG + YM2151 build) | `tools\zsm2vera.mjs`, `tools\gen_music.mjs` |
| 4 | Render the soundtrack to 8 kHz PCM (`OTHELLO.PCM`) | `tools\gen_pcm.mjs` (melodius) |
| 5 | Compile the Applesoft launcher | `tools\gen_startup.mjs` |
| 6 | Compile slot-2 + slot-4 images (`src\pcmfeed.s` included) | `mos-apple2e-clang` |
| 7 | Create the 1700 KB ProDOS HDV and add files with Cadius | `tools\build_hdv.py`, `tools\cadius.exe` |

Step 3 also emits `generated/music_vzm.h` with `MUSIC_VZM_SIZE`, `MUSIC_VZM_LOOP`
and `MUSIC_VZM_TICKS`; `gen_music.mjs` fails the build when the stream is not a
whole number of 512-byte blocks or the loop offset does not fit 16 bits.
`node tools\verify_music_streams.mjs` re-converts and replays both streams for a
tick-by-tick equivalence check (see §3.11); run it after touching
`zsm2vera.mjs`, `audio.c` or the ZSM assets.

Compile flags:

```
-Os -mcpu=mos65c02 -Isrc -Igenerated -T src\link1000.ld -Wl,-Map=build\main.map
```

The slot-4 image adds `-DVERA_BASE=0xC400`. The linker map is emitted so the
headless tests can resolve symbol addresses (see §5.3).

`src/link1000_elf.ld` is a disassembly-only twin of `src/link1000.ld` (no
`OUTPUT_FORMAT` block → ELF for `llvm-objdump -d` / `llvm-nm`). It is **not**
part of the shipping image.

### 4.1 Directory structure

```text
vera/
├── cx16-othello.hdv          # 1700 KB bootable ProDOS image (deliverable)
├── build.bat                 # 7-step pipeline
├── README.md                 # user-facing port notes
├── AGENTS.md                 # this file
├── src/
│   ├── main.c                # entry, game state machine, frame pump
│   ├── game.c/.h             # board, AI, scoring, stone/cursor rendering
│   ├── menu.c/.h             # title / settings / help screens
│   ├── video.c/.h            # VERA init, tilemaps, fonts, sprites, help pages
│   ├── input.c/.h            # keyboard, joystick, mouse
│   ├── audio.c/.h            # FM probe, VZM/PSG/PCM streams, SFX, PCM FIFO
│   ├── disk.c/.h             # ProDOS file-level streaming + PCM block map
│   ├── pcmfeed.s             # 15-cycles/byte PCM FIFO feed loop
│   ├── mli.s                 # JSR $BF00 file calls, block calls, and QUIT
│   ├── mouse.s               # Apple Mouse Card driver
│   ├── apple2e.h             # VERA register structs (Apple II semantics)
│   ├── constants.h           # VRAM layout, tile ids, key codes
│   ├── sfx_data.c            # thumb SFX on PSG voice 3
│   ├── link1000.ld           # shipping linker script ($1400 load, $BE00 stack)
│   ├── link1000_elf.ld       # disassembly-only twin
│   └── startup.bas           # Applesoft launcher source
├── tools/
│   ├── gen_assets.py         # tiles + font + help + palette → assets.bin
│   ├── zsm2psg.py            # ZSM → PSG-only stream (MUSIC.PSG)
│   ├── zsm2vera.mjs          # ZSM → VZM (PSG + YM2151) stream encoder
│   ├── gen_music.mjs         # runs zsm2vera.mjs, writes music.vzm + music_vzm.h
│   ├── gen_pcm.mjs           # melodius render -> 8 kHz OTHELLO.PCM + pcm_table.h
│   ├── verify_music_streams.mjs # convert/replay diff of MUSIC.VZM (build check)
│   ├── gen_startup.mjs       # Applesoft BASIC → binary STARTUP
│   ├── applebasic.mjs        # Applesoft tokenizer
│   ├── build_hdv.py          # Cadius-based ProDOS HDV packager
│   ├── cadius.exe            # Creates volumes and adds ProDOS files
│   ├── show_screen.py        # framebuffer → ASCII inspection
│   └── zsm_probe.py          # ZSM command inspector
├── generated/                # build-time assets + tables
│   ├── tiles.bin (16,384) font.bin (24,576) help.bin (1,440)
│   ├── palette.bin (512) assets.bin (42,912)
│   ├── music.psg (157,956) music.vzm (198,656) othello.pcm (1,162,243)
│   └── asset_table.h music_table.h music_vzm.h pcm_table.h sfx_table.h
├── assets/                   # source art, palette png, help.hlp, zsm music
│   └── prodos-system/         # PRODOS, BASIC.SYSTEM, CLOCK.SYSTEM for Cadius
└── build/
    ├── main.bin main4.bin    # slot 2 / slot 4 payloads
    ├── main.elf main.bin.elf main4.bin.elf
    ├── main.map              # linker map (consumed by the tests)
    └── headless/             # test output: .bmp frames + .txt reports
```

### 4.2 Boot chain

```
ProDOS STARTUP (Applesoft, 2,538 B, type $FC, aux $0801)
  → read-only MLI OPEN/CLOSE probe of OTHELLO.PCM (absolute, then prefix-relative)
  → probes VERA at slot 2 ($C200), then slot 4 ($C400) by write/read-back
  → probes the YM2151 at $Cx20/$Cx21 and PRINTs the STATUS + music-source lines
    ("PSG + FM MUSIC ENABLED" when the chip is present, otherwise "PCM MUSIC
     STREAMING", or "PSG MUSIC ONLY - NO PCM FILE" when the OPEN failed)
  → PRINTs "INITIALIZING... PLEASE WAIT ..." and waits ~3 s (180 VSYNC edges)
  → PRINTs "RUNNING CX16-OTHELLO ..."
  → BRUN MAIN.BIN  (slot 2)  or  MAIN4.BIN (slot 4)
     → $1400: zero .bss, save zero page
     → audio_detect_fm() → disk_init() → init_screen() → load_assets()
     → audio_init() → input_init() (mouse probe) → init_mouse_sprite()
     → audio_start_music() (VZM if fm_present, else PCM, else MUSIC.PSG)
     → main loop: pump_frame() → input → game/menu state machine
```

`STARTUP` lines 12-13 + subroutines 700/900/1200 in `src/startup.bas` do the
file probe (`$C8` OPEN / `$CC` CLOSE stubs in page 3, see §3.11); subroutine
200-280 then runs the splash: it picks the VERA slot (`S = 2` / `S = 4`), maps it
to the ISR address (`V = 49671` = `$C207` / `V = 50183` = `$C407`), then spins
`FOR F = 1 TO 180` clearing the VSYNC flag with `POKE V,1` and waiting for bit 0
with `PEEK(V) AND 1`. At ~59.5 Hz that is 3.02 s of "PLEASE WAIT" on screen. The
same pattern is used by `C:\dev\Time-Pilot\TimePilot-IIvera`, which keeps the
boot feel consistent.

### 4.3 HDV layout (3,400 blocks)

The ProDOS volume name is `CX16.OTHELLO`. Cadius creates the 1700 KB volume and
adds eleven files to its root:

| File | Blocks | Bytes | Purpose |
| :--- | ---: | ---: | :--- |
| `PRODOS` | 33 | 17,128 | ProDOS 2.4.3 kernel |
| `BASIC.SYSTEM` | 20 | 10,240 | interpreter for `STARTUP` |
| `CLOCK.SYSTEM` | 6 | 2,678 | clock driver |
| `OTHELLO` | 1 | 7 | installation marker (7-byte file, only its presence matters) |
| `STARTUP` | 5 | 2,538 | Applesoft launcher (type `$FC`, aux `$0801`) |
| `MAIN.BIN` | 39 | 19,548 | slot-2 payload (type `$06`, aux `$1400`) |
| `MAIN4.BIN` | 39 | 19,548 | slot-4 payload (`-DVERA_BASE=0xC400`) |
| `ASSETS.BIN` | 84 | 42,912 | tiles + font + help + palette blob |
| `MUSIC.PSG` | 309 | 157,956 | PSG-only music, the last-resort fallback |
| `MUSIC.VZM` | 388 | 198,656 | PSG + YM2151 music, used when FM is present |
| `OTHELLO.PCM` | 2,271 | 1,162,243 | 8 kHz soundtrack, used when no FM board |

That is 185 blocks free on the 3,400-block volume. The 1700 KB size is forced by
`OTHELLO.PCM`: at 800 KB the image would not hold the soundtrack, so the shipping
HDV no longer fits a floppy (packaging option A — keep VZM + PSG as fallbacks).

Cadius owns the directory entries, storage allocation and file
dates. The builder stages Apple II BIN payloads without their four-byte load
headers and uses Cadius' file-name suffix to set each ProDOS file type and
auxiliary address. System files are kept in `assets/prodos-system/`; no
prebuilt 800 KB base image is needed.

`disk.c` checks the marker, opens the asset file, then lazily opens the music
file. It uses ProDOS OPEN/GET_EOF/SET_MARK/READ rather than reserved raw block
ranges. Three aligned 1 KB OPEN buffers live at `$7000`, `$7400`, and `$7800`
(the link script asserts the payload stays below them); `OTHELLO.PCM` is the one
exception — it is read with raw MLI `READ_BLOCK` calls through the block map
(§3.11.2), with a file-level fallback when the map cannot be trusted.

Copied installation was confirmed working from another ProDOS volume and
subdirectory. Set the ProDOS prefix to the target volume root or game
subdirectory before launching. An empty prefix on the packaged direct-boot HDV
falls back to `/CX16.OTHELLO`. Copy `MUSIC.VZM` as well when the target machine
has the FM daughterboard — without it the game silently falls back to
`MUSIC.PSG` (§3.11).

Asset offsets inside `assets.bin` (`generated/asset_table.h`):

| Offset | Length | Content | VRAM target |
| :--- | :--- | :--- | :--- |
| 0 | 16,384 | tiles | `$0:0000` |
| 16,384 | 24,576 | font | `$0:4000` |
| 40,960 | 1,440 | help pages | RAM `helpBuf` |
| 42,400 | 512 | palette | `$1:FA00` |

---

## 🔬 5. Verification Harness (headless, no GUI)

The verification path is the **apple2ts** emulator driven headless by jest.
AppleWin screen capture proved unusable for automated checks, but AppleWin *is*
the reference emulator for the VERA card **and the YM2151 daughterboard**, so
audio/FM verification uses it headless as well (§5.7).

```bash
cd C:\dev\apple2ts
node node_modules/jest/bin/jest.js src/worker/devices/vera/othello_headless.test.ts --testTimeout=3000000
node node_modules/jest/bin/jest.js src/worker/devices/vera/othello_diag.test.ts   --testTimeout=3000000
```

(`npx` is blocked by the execution policy — call `node node_modules/jest/bin/jest.js`.)

A `[exit code: 1]` after a jest run is PowerShell `NativeCommandError` from
stderr text, **not** a test failure; read the `Tests:` line.

### 5.1 Headless boot recipe

```ts
doSetRunMode(RUN_MODE.PAUSED, false)
doBoot()
initVera(); resetVera(); enableVera(true, 2)
memSet(0xbf30, 0x70)                 // ProDOS global page: boot unit = slot 7 HD
loadImage("main.bin")                // payload at $1400
s6502.StackPtr = 0x00; s6502.PStatus = 0x24; setPC(0x1400)
```

### 5.2 MLI interception

`serviceMli()` fires when `PC == $BF00`. The return address lives on the stack
page, and apple2ts `RTS` jumps to exactly the popped value (`address(lo,hi)` has
no `+1`), so the resume address must be pushed as `ret + 4`:

```ts
const sp  = s6502.StackPtr
const ret = memGet(0x0100 + ((sp+1)&0xff)) | (memGet(0x0100 + ((sp+2)&0xff)) << 8)
const op  = memGet(ret + 1)              // $80 READ_BLOCK / $81 WRITE_BLOCK
const pp  = memGet(ret + 2) | (memGet(ret + 3) << 8)
// ... service op from the HDV image, write status to pp+6 ...
s6502.StackPtr = (sp + 2) & 0xff         // pop the JSR return address
s6502.StackPtr = (s6502.StackPtr - 2) & 0xff
memSet(0x0100 + ((s6502.StackPtr+1)&0xff), (ret + 4) & 0xff)
memSet(0x0100 + ((s6502.StackPtr+2)&0xff), ((ret + 4) >> 8) & 0xff)
setPC(ret + 4)
```

### 5.3 Symbol resolution

Hard-coded symbol addresses go stale on every rebuild. Parse the linker map:

```ts
const m = line.match(/^\s+([0-9a-fA-F]{2,4})\s+([0-9a-fA-F]+)\s+\d+\s+\d+\s+(\w+)\s*$/)
```

That is why `build.bat` passes `-Wl,-Map=build\main.map`.

### 5.4 Key injection

apple2ts only strobes a buffered key when the keyboard latch is clear
(`nCalled > 2` and `memGetC000(0xC000) < 128`), so a single send can be
swallowed. Re-send with a bounded run between attempts, and check the target
value **before** sending:

```ts
const pressTo = (code, getter, target, tries = 8) => {
  for (let i = 0; i < tries; i++) {
    if (getter() === target) return true
    sendTextToEmulator(code); run(20000)
  }
  return getter() === target
}
```

A 4000-instruction budget is too short: the predicate is evaluated before the
key lands, which causes double toggles.

### 5.5 Timing budgets

Asset streaming alone is ~600k cycles. Use **≥ 2,000,000** instructions before
judging a screen; the full 10×10 game needs 160 iterations × 250,000.

### 5.6 Frame inspection without image input

The agent model has no image input, so frames are inspected as ASCII via
colour **classification**, not a luminance ramp (a ramp saturates on the tan
background):

```python
cols=[(204,187,102),(170,153,85),(34,17,0),(187,187,187), ...]
d=np.stack([np.abs(a-np.array(c)).sum(2) for c in cols])
idx=np.argmin(d,axis=0).astype(np.int64)
```

Text is read by masking the font colour `RGB(34,17,0)` (palette index `$51`).
Glyphs can also be decoded back to characters by downsampling each 32×32 screen
glyph to 16×16 and taking the minimum Hamming distance against `font.bin`.

---

### 5.7 AppleWin: VERA + YM2151 verification

The AppleWin fork (`c:\dev\applewin`, `release\AppleWin.exe`) implements both the
VERA card and the optional YM2151 daughterboard, so it is the only emulator that
can exercise the FM path. It runs headless and self-terminates after a cycle
budget:

```powershell
cd C:\dev\applewin\release
.\AppleWin.exe -h1 <hdv> -s2 vera -run-cycles 40000000 `
    -log -fm-wav-dump -vera-dump <out.vram>
```

- `-h1 <hdv>` boots the ProDOS image from the slot-7 hard-disk controller.
- `-s2 vera` / `-s4 vera` installs the VERA card in slot 2 / slot 4.
- `-run-cycles N` emulates N cycles (~1.02 M cycles per emulated second at 1 MHz,
  so 40 M ≈ 39 s) and then shuts down. Use ≥ 25 M to reach music start (asset
  streaming dominates the first seconds).
- `-log` writes `VERA.log` next to the exe.
- `-fm-wav-dump` writes `VERA-fm.wav` (stereo, 16-bit, 44.1 kHz) with the **first
  20 seconds** of the mixed PSG+FM+PCM output, which is what makes the no-FM
  `OTHELLO.PCM` path verifiable without a speaker. The 20 s cap is hard-coded
  (`kWavCapSamples = 44100 * 20`), so longer runs do not extend the capture —
  use it as a spectral/rms probe, not as a full-song renderer.
- `-vera-dump <file>` writes the full 128 KB VRAM as raw bytes. **It only works
  for a card in slot 2**: `VERADumpToFile()` goes through
  `GetCardMgr().GetVERACard()`, which resolves slot 2 only, so a slot-4 run
  silently produces no dump (and no WAV, for the same reason). Verify slot 4 via
  `VERA.log` writes instead (§5.7.3).
- **The desktop session must be unlocked.** Emulation is driven from the
  window/frame loop, so a locked or disconnected Windows session stalls the
  process before its first frame: it sits in its message loop with ≈0.1 s of CPU
  time, no window, and never writes `VERA.log`, `VERA-fm.wav` or the VRAM dump.
  This reproduces with `-noreg`, without `-h1`, without `-s2 vera`, and with the
  packaged `AppleWin1.32.1.0-verafm` build — so it is host state, not the image
  or the configuration. If a batch run suddenly produces no files, check the
  session before blaming the build.

**5.7.1 Switching FM on and off.** The daughterboard is not a command-line
switch; it is the per-slot registry value

```
HKCU\Software\AppleWin\CurrentVersion\Configuration\Slot N
    "FM Card Enabled"  REG_SZ  "1" | "0"
```

Set it before starting AppleWin (`reg add ... /v "FM Card Enabled" /d 1 /f`).
AppleWin defaults the value to **enabled** when it is missing
(`VERACard.cpp:106`), so an old profile can differ from a fresh one. The value
applies to the slot the card is installed in — set both `Slot 2` and `Slot 4` if
you test both.

**5.7.2 Expected measurements (shipping build).** All figures below were
re-measured on the current build (`main.bin` 19,548 B, `STARTUP` 2,538 B,
`MUSIC.VZM` 388 blocks, `OTHELLO.PCM` 2,271 blocks) with a 40 M-cycle slot-2 run
unless stated otherwise.
"Splash" = the 3 s `PLEASE WAIT` window, so audio starts ≈7 s into the
capture instead of ≈4.4 s. The launcher's `OTHELLO.PCM` OPEN probe accounts
for ≈0.3 s of that (6.78 s with the probe disabled); the rest is the 3 s
splash plus the asset load.

| Run | Result |
| :--- | :--- |
| FM = 1, slot 2, 40 M cycles | title screen in the VRAM dump: layer-0 map has all 1024 entries set to tile 1 (the solid background tile, moved by the diagonal scroll), layer-1 map has 129 non-zero entries (title text, the 6×6 sample board with its border, and the three menu lines), palette `00 00 ff 0f`, ≈42 of 64 game tiles written, sprite 0 at (0,0); WAV (20 s) is silent for the first **6.96 s** (3.0 s splash + asset load), then per-second rms ≈1,827 / 763 / 325 / 124 / 2,224 / 1,049 / 447 / 175 / 3,179 / 3,937 / 4,366 / 3,697 / 4,368, max sample **13,863–15,851** across runs (no clipping). Spectrum of the last second: ≈0.29 below 500 Hz, 0.30 at 500 Hz–2 kHz, 0.19 at 2–6 kHz, 0.20 at 6–20 kHz — the FM partials `MUSIC.PSG` cannot make |
| FM = 0, slot 2 | title screen identical (layer-1 map 129 non-zero, palette `00 00 ff 0f`). This build now plays `OTHELLO.PCM`: WAV first audible **7.10 s**, max sample **11,040** (volume 12), per-second rms 994 / 471 / 221 / 140 / 1,128 / 588 / 269 / 152 / 2,141 / 3,064 / 3,465 / 2,913 / 3,516, spectrum 0.30 / 0.30 / 0.13 / 0.26 — the bass is back, which is what the PSG-only stream could not do (0.03) |
| FM = 0, PCM stream rate | 833 frames per 25 M cycles = **34.1 fps**, 147,968 B fed, `pcmFallbacks` 0 (the file-level reader gives 24.1 fps); the PCM FIFO drains 8,011 B/s and playback starts ≈6.2 s in |
| FM = 1, slot 4, 40 M cycles | `VERA.log` shows the slot-4 launch (`$C405`, `$C400`/`$C401` write/read-back, then tile/map/PSG traffic, ≈53 k lines); WAV rms per 4 s: 0 / 1,053 / 1,267 / 3,591 / 4,133 |
| FM = 0, no `OTHELLO.PCM` (800 KB image) | the launcher takes the fallback branch (`PSG MUSIC ONLY - NO PCM FILE`) and the failed read-only `OPEN` does **not** disturb the boot: title screen identical, WAV first audible **7.09 s**, max sample **8,176** (`MUSIC.PSG`, the same value measured before the PCM soundtrack existed) |
| pristine HEAD HDV, either setting | WAV is **all zeros** — the `mouse_init()` branch overflow (§3.14) stops the boot before `audio_start_music()` |

`$C22x` appears **zero** times in `VERA.log` in *every* run above, including the
FM = 1 ones (§5.7.3). Note that the old "bass" discriminator is gone: FM = 0 now
also has ≈0.30 below 500 Hz, because that machine plays `OTHELLO.PCM` instead of
`MUSIC.PSG`. From the WAV alone the two music paths are told apart by the top
octaves — PCM is band-limited to 4 kHz (0.13 at 2–6 kHz, 0.26 above 6 kHz) while
FM keeps its partials (0.19 / 0.20) — and by `pcmBytesFed` climbing in the debug
counters.

**5.7.3 `VERA.log` cannot prove FM is unused.** The YM2151 branch in
`VERACard::IOWrite` returns *before* the logging call, so a working FM stream
produces **zero** `$C220`/`$C221` lines in `VERA.log`. Do not conclude "FM is
dead" from the absence of those lines — use the WAV dump (fm-on vs fm-off
envelope), the probe read-back values, or the `fm_present` result itself.

**5.7.4 Probing the probe.** The detection recipe can be verified from the
outside by writing the raw reads to unused bank-1 VRAM (e.g. `$1B400..`): with FM
enabled the reads are `$00/$00`; without it they are `$5A/$A5`. Both were
observed with the shipping build, and `STARTUP`'s BASIC probe prints the same
verdict on screen before `BRUN`. Temporary debug writes must be removed again —
they cost payload bytes and are easy to forget.

Reading the launcher's banner back — **this is what caught the inverted FM
verdict (§7 #34)**, and it is the only way to check the text without a screenshot:
a `-DVERA_DEBUG_STATS` build copies the Applesoft text page (`$0400-$07FF`) into
bank-1 VRAM from `main.c`, right after `audio_start_music()`, and `-vera-dump`
then exposes it at file offset `0x1C000` (24 rows × 40 columns, `char | $80`):

```python
page = open('run.vram', 'rb').read()[0x1C000:0x1C000 + 960]
rows = [''.join(chr(b & 0x7F) for b in page[r*40:r*40+40]) for r in range(24)]
print('\n'.join(rows))
```

Two caveats, both measured: (a) VRAM is not blank at `$1C000` — AppleWin
pre-fills it with a ramp (`00 07 0e 15 1c …` at every `$1000` page start), so pick
a region your own writes cover, and do not read "unwritten" areas as data; (b)
the copy leaves 8 bytes of that pre-fill at the end of every 128-byte block, so
the captured page is shifted relative to the real screen — rows 3, 9, 12 and 16
read correctly in the FM = 1 run and the two `STATUS:` lines plus the music line
come out verbatim, but do **not** trust column positions. Grep for complete lines
(`STATUS: YM2151 (OPM) FM CHIP FOUND`, `PSG + FM MUSIC ENABLED`,
`PCM MUSIC STREAMING`, `PSG MUSIC ONLY - NO PCM FILE`), never for a fixed offset.
Shipping builds have no capture block (it is inside the `#ifdef`), so this costs
nothing in the released payload.

---

## 🧪 6. Test Suite

Test files live in the apple2ts repo by necessity (jest/ts-jest module
resolution); they read assets from absolute paths under `C:/dev/cx16-othello/vera`
and write to `C:/dev/cx16-othello/vera/build/headless/`.

The recorded headless pass predates the ProDOS file-level conversion. The
apple2ts harness currently emulates READ_BLOCK/WRITE_BLOCK only; update its MLI
handler for OPEN/READ/SET_MARK/GET_EOF/GET_PREFIX before using it to verify the
current disk-loading path. AppleWin boot and a copied file-level installation
were verified after the conversion.

### `othello_headless.test.ts` (5 tests)

1. **Title screen** — boots, streams every asset, renders the title, sample
   board and four stones, and asserts the frame is filled edge to edge: every
   16×16 block of the 640×480 framebuffer must contain lit pixels, so a
   quarter-screen or clipped render fails. 0 MLI errors.
2. **8×8 game** — `RETURN` starts a game and the playfield renders.
3. **Settings + full 10×10 CPU-vs-CPU game** — drives the settings menu (player
   types, size 10, wood board, both stone colours), plays to game-over
   (57 + 43 = 100 stones), and streams assets and music from ProDOS files.
4. **Cursor directions** — each Apple //e arrow key moves the cursor one cell in
   the matching screen direction, checked against **both** `curx/cury` and the
   cursor sprite's pixel box (16-px steps), so an inverted axis fails.
5. **Help screen text** — decodes every rendered glyph back against `font.bin`,
   compares all 11 help rows with `help.bin`, and asserts `(N) NEXT (P) PREV`
   and `(ESC) BACK  PAGE:1/6` are intact on rows 13/14.

### `othello_diag.test.ts` (3 tests)

6. Register / map / tile / palette / sprite dump plus per-layer isolation renders.
7. Raw 6502 DATA0 tile-stream proof.
8. DATA0 write trace bucketed by VRAM region (`diag_trace.txt`,
   `diag_trace_summary.txt`).

### Known-good measurements

| Metric | Value |
| :--- | :--- |
| Title screen lit fraction | `1.0` (0 black pixels) |
| Blank 16×16 blocks of 1200 | `0` |
| Text colour pixels on title | 20,036 |
| Background colours on title | 120,960 / 117,644 |
| MLI errors across all tests | `0` |
| Full-run DATA0 buckets | `tiles 16384, font 16384, gap8 8192, sprites 3708, psg 2571, map1 2346, map0 2048, palette 512` |
| Title test wall time | ~5.4 s |
| 10×10 game test wall time | ~21 s |
| Cursor test wall time | ~3.1 s |
| Help test wall time | ~3.3 s |

---

## 📜 7. Development Log

1. **Toolchain bring-up** — established `mos-apple2e-clang` with
   `-mcpu=mos65c02 -T src/link1000.ld`; confirmed inline asm crashes the 6502
   backend, so MLI and the mouse driver are separate `.s` files.
2. **Asset pipeline** — `gen_assets.py` converts the X16 tileset, font, help
   pages and palette into a single 42,912-byte blob with a generated offset
   table; `zsm2psg.py` re-voices the YM2151 ZSM soundtrack onto PSG voices 0–3.
3. **ProDOS streaming** — marker-checked `OTHELLO`, `ASSETS.BIN`, and
   `MUSIC.PSG` files read with OPEN/SET_MARK/READ; the HDV builder writes
   timestamps and supports sapling/tree files. `mli.s` saves/restores zero page
   `$40-$4F` around every `JSR $BF00`.
4. **Launcher** — Applesoft `STARTUP` compiled by a hand-written tokenizer
   (`applebasic.mjs`), probing slot 5,3,6,7,1,4,2 for the VERA signature and
   `BRUN`-ing the matching payload.
5. **"Noise screen" bug** — `hscale/vscale = 0x40` was assumed to be 1:1. On the
   Apple II it is 2:1 magnification, so the 512×512 tilemap became 1024×1024
   clipped to 640×480. Fixed by using the correct scale semantics.
6. **apple2ts harness built** — MLI interception, instruction-budget runner, BMP
   writer, colour statistics. Established that asset streaming needs ≥600k
   cycles and that AppleWin capture is not usable for automation.
7. **Layer struct size bug** — `apple2e.h` layer structs must be exactly 7 bytes
   (`config, mapbase, tilebase, hscroll, vscroll, reserved[2]`) so layer 1 lands
   at register `$14`.
8. **MLI return-address bug** — the handler must read the return address from the
   stack page `$0100`, and apple2ts `RTS` returns exactly the popped value (no
   `+1`), so the resume address is `ret + 4`.
9. **LTO dead-code elimination** — `input_update()` was being removed; fixed by
   calling it from `pump_frame()`.
10. **Inverted cursor axes** — reported as "left key go right, right go left".
    Measured against the cursor sprite's pixel box: left/right were already
    correct, **up/down were inverted** (inherited X16 PETSCII naming). Fixed in
    both the keyboard and joystick blocks, and locked in by test 4.
11. **Stale symbol addresses** — hard-coded `$4aaa/$4aab` broke after a rebuild.
    Fixed by parsing `build/main.map` and adding `-Wl,-Map` to `build.bat`.
12. **Swallowed key presses** — single `sendTextToEmulator` calls were lost to
    the keyboard latch. Fixed with `press` / `pressTo` re-sending with
    `run(20000)` per attempt; a 4000-instruction budget caused double toggles
    because the predicate ran before the key landed.
13. **Resolution: native 320×240 attempt** — set `hscale/vscale = $80` plus a
    DCSEL-1 window of `hstop = 320>>2`, `vstop = 240>>1`. This produced a native
    320×240 output, i.e. the game in the **top-left quarter** of the screen.
    Rejected: the requirement is a 320×240 *source* filling the screen.
14. **Composer-window boot crash** — writing `$C209-$C20C` triggers a midline
    render pass per write, which breaks the MLI streaming timing. Located with a
    PC trail probe (`rts` returning to `$ffff`), fixed by not writing the window
    registers at all (the reset defaults are already 640×480).
15. **Background effect corruption** — a "fill only the visible 20×15 tiles"
    optimisation left map columns 21–31 and rows 16–31 stale. The renderer
    applies the scroll in output pixels and wraps at 32 tiles, so the scrolled
    edge read stale tiles and showed the border colour through transparent
    index 0. Measured 26,952 black pixels; restored the full 32×32 fill → 0.
16. **Full-screen fill locked in** — test 1 now asserts every 16×16 block of the
    640×480 framebuffer has lit pixels, so a quarter-screen or clipped render
    fails the suite.
17. **Help page text overflow** — reported from a screenshot. `write_string()`
    is `strlen`-bounded, but help rows are 20-character fixed-width fields with
    no NUL, so each write ran to the end of the page, wrapped at the 32-tile
    stride and overwrote rows 13/14. Fixed with `write_string_n` (field width +
    row clamp) and locked in by test 5, which decodes the rendered glyphs back
    against `font.bin` and diffs them against `help.bin`.

18. **Struct alignment issue** — changing `apple2e.h` scroll registers to `unsigned int` caused 16-bit alignment padding, shifting VERA register offsets and causing random crashes. Reverted to 8-bit `hscroll_l`/`hscroll_h` assignments.
19. **Mouse cursor disappearance** — `reset_sprites()` cleared sprite 1 to 127. When ESC was pressed to leave the game and return to the menu, the mouse cursor (Sprite 0) was accidentally cleared because the original X16 code expected Sprite 0 to be hardware-managed. Fixed `reset_sprites()` to preserve Sprite 0.
20. **AI calculation freeze** — `human_turn()` repeatedly called `check_valid_move()` on every frame waiting for input. On a 1MHz CPU, this caused a 150ms delay per frame, making background scrolling choppy. Added `valid_move_checked` flag to only evaluate valid moves once per turn.
21. **Mouse click as ENTER** — `end_game_state` did not support mouse clicks to return to the menu. Added bounds checking on mouse coordinates to treat clicks on the "hit ENTER" text area as a `KEYCODE_RETURN` event.
22. **YM2151 FM music** — the AppleWin VERA fork (and the real card with the
    daughterboard) exposes the YM2151 at `$Cx20`/`$Cx21`, so the ZSM soundtrack
    no longer has to be re-voiced for every machine. Added a second stream,
    `MUSIC.VZM` (native PSG voices 4–15 + genuine YM register writes), while
    keeping `MUSIC.PSG` as the no-FM fallback. `zsm2vera.mjs` encodes it and
    `gen_music.mjs` packages it; `verify_music_streams.mjs` proves conversion and
    playback agree tick for tick.
23. **Boot hang: out-of-range branch — the real cause of "the HDV does not
    boot"** — the mouse probe's "no card" exit was a `BCS` 141 bytes away, which
    the assembler silently truncated into a backwards jump into
    `mouse_restore_zp`. Every machine without a mouse card died in
    `input_init()` before the title screen and music. Found by bisecting with
    VRAM beacons, then confirmed on a pristine HEAD build (its WAV dump is all
    zeros while the fixed build is not). Fixed with `BCC`+`JMP`; documented as
    §3.14. **This was not an FM problem at all**, even though it surfaced while
    bringing FM up.
24. **Headless FM verification** — `-fm-wav-dump` (first 20 s of the mixed
    PSG+FM audio) plus the per-slot registry value `FM Card Enabled` make the FM
    path testable without a GUI. This is what established that FM=1 plays
    (max sample 15,300–16,000 across runs), FM=0 falls back to `MUSIC.PSG`
    (max 8,176), slot 4 works (`VERA.log` traffic, WAV envelope), and
    `VERA.log` never shows `$C22x` even when FM is active (§5.7). The absolute
    peaks dropped slightly on later builds (`main.bin` grew, the splash was
    added); current figures are in §5.7.2.
25. **Deterministic title screen, and the mouse path exercised for real** — ten
    repeat runs (plus 25/30/35/40 M-cycle budgets) all decode to the same title
    dump: `CX16-OTHELLO`, `music by Crisps`, `1.2.0`, the 6×6 sample board at
    (160,96)/(176,96)/(176,112)/(160,112), layer-1 map 129 non-zero entries,
    sprite 0 at (0,0) — the boot is reproducible, not flaky. Two dumps that
    looked like a help page and a started game were **not** boot bugs: AppleWin
    had a Mouse Card in slot 5 and the host pointer was over the window, so a
    click on the `(H) HELP` row opened help and a click on `(ENTER) START`
    started a game. That accident is the end-to-end proof of the Mouse Card
    path (pointer sprite follows the host position, title hit-test picks the row
    that was clicked).
26. **Three-second splash in `STARTUP`** — asked for parity with
    `TimePilot-IIvera`: the launcher now prints `INITIALIZING... PLEASE WAIT
    ...`, spins 180 VSYNC edges (~3.02 s) via the VERA ISR, then prints
    `RUNNING CX16-OTHELLO ...` before `BRUN`. Verified from the WAV onset: with
    no wait it is 4.425 s, with 180 edges 6.185 s, with 600 edges 10.235 s —
    180 edges are the right number. Note that the wait runs *before* `BRUN`, so
    the payload load and the music start are shifted by it.
27. **Music stutter in PSG+FM mode — the frame budget, not the stream** — the
    complaint was "the music jerks a little" with FM enabled. Two causes, both
    on the frame path: (a) the frame that crossed a 512-byte boundary did a
    blocking ProDOS read inside the music tick; (b) `pump_frame()` cleared the
    VSYNC flag and then waited, so every frame that overran its slot *lost* a
    whole frame, and with a Mouse Card in slot 5 every frame overran (measured
    100 % overruns, 53.8 fps, and only ~1408 frames per 40 M cycles when the
    clear-and-wait was still in place, i.e. ~36 fps). Fixed by the two-buffer
    prefetch (§3.11), VSYNC catch-up, and polling the mouse at 30 Hz / the
    paddles at 20 Hz (§3.15). Result: 58.3–59.4 fps with the Mouse Card,
    59.5 fps without, `musicFallbacks` 0.
28. **Prefetch bookkeeping bug caught by the debug counters** — the first
    implementation stored the *pointer* of the prefetched buffer in `musicPre`;
    after the first swap it pointed at the buffer that had just been consumed,
    so `audio_refill()` never fired again (`musicPrefetches` stuck at 1) and
    `music_peek()` then swapped between a stale and a fresh buffer, repeating
    1,024 bytes of stream every 22 frames. The `-DVERA_DEBUG_STATS` counters at
    `$1B000` showed it immediately; `musicPre` is now a 1-bit flag and
    `musicPrefetches` climbs (47 in a 40 M-cycle run). Lesson: a cached
    "other buffer" must be derived, not stored.
29. **PCM soundtrack for no-FM machines** — `MUSIC.PSG` re-voices the FM
    channels onto four PSG voices and therefore cannot carry the bass (measured:
    sub-500 Hz energy 0.30 with FM versus 0.03 without), which is the "the music
    sounds dry" complaint. The build now renders the ZSM through melodius to 8 kHz
    and ships it as `OTHELLO.PCM` (1,162,243 B), played through the VERA PCM FIFO
    on machines without the FM chip; FM machines keep live `MUSIC.VZM`, and
    `MUSIC.PSG` stays as the last-resort fallback. The image had to grow to
    1700 KB (packaging option A), so it no longer fits a floppy — accepted
    trade-off, and §3.11.1 has the full chain (ZSM → WAV → 8 kHz PCM).
30. **PCM streaming at 24 fps, and the two bugs behind it** — the first
    implementation read `OTHELLO.PCM` in 512-byte file records inside the frame,
    which costs 22.7 ms per call and capped the game at 24.1 fps while the FIFO
    drained 8,011 B/s. Replaced with a ProDOS block map driven by raw MLI
    `READ_BLOCK` (4.6 ms) → 34.1 fps. Two mistakes made the first measurements
    nonsense: (a) `pcm_start()` tested `!disk_pcm_read(...)` while the reader
    returns *nonzero on success*, so `pcmMap` stayed 0 and every "map" run was
    silently the slow file path; (b) ProDOS index blocks store each 256-entry
    table as split halves (low bytes `$000-$0FF`, high bytes `$100-$1FF`), not as
    2-byte words, so the first decoder produced block numbers like 41,890 and
    garbage audio. Both are now covered by validation (block 0 is compared against
    a file-level read) and documented in §3.11.2.
31. **Sound effects drowned by the music** — reported after playing: "the
    effects are too quiet, the music drowns them". The thumb's PSG volume is
    already `$3F`, so the only headroom was the mix itself: PCM music volume 15 →
    **12** (−6.6 dB, peak 23,552 → 11,040) and the thumb replayed on four PSG
    voices in PCM mode (all sixteen are free there; four measured a 17,072 peak
    against the 11,040 music peak, eight clipped at 33,008). Measured by
    subtracting a run with the effect triggered from one without it — see
    §3.11.3.
32. **One-off EOF probe popping the error screen** — `disk_read_music_block()`
    treats a read past the end of the stream as an error, but the prefetch
    deliberately reads one block *past* EOF to discover where the loop point
    is. The first attempt therefore set the sticky `io_error` and the boot ended
    on the disk-error screen. Fixed by returning `$4C` from the EOF branch
    without touching `io_error`.
33. **Launcher banner claims the wrong music mode** — reported as "PSG MUSIC ONLY
    => 你不是改成 PCM 了嗎?". The no-FM line was still the pre-PCM wording, so a
    normal no-FM machine announced the last-resort fallback it would never use.
    `STARTUP` now derives the verdict from the card and the volume: a read-only
    MLI `OPEN($C8)`/`CLOSE($CC)` pair issued from page-3 stubs tests
    `OTHELLO.PCM` (absolute `/CX16.OTHELLO/...` path first, then the current
    prefix) before `BRUN`, and `P` selects `PCM MUSIC STREAMING` or
    `PSG MUSIC ONLY - NO PCM FILE`. Measured on AppleWin: PCM image → `$00`,
    banner says PCM; 800 KB no-PCM image → `$40` on both attempts, banner shows
    the fallback, and the failed `OPEN` leaves the boot untouched (title screen
    reached, `MUSIC.PSG` audible, WAV max 8,176). The probe adds ≈0.3 s to boot
    (6.78 s → 7.10 s to first audio), and `STARTUP` grew to 2,538 B / 5 blocks,
    so §4.2, §4.3 and §5.7.2 were refreshed with it.
34. **Launcher FM verdict printed backwards** — reported as "你是不是顯示反了, 有FM
    應該是 PSG+FM, 沒FM才是 PCM Streaming", then "你是 FM 有開時, 顯示沒偵測到
    (但遊戲是用FM播放); 沒開FM時, 你顯示有偵測到". Exactly right, and it was the
    BASIC flag polarity, not the probe: `STARTUP` keeps `F` at 1 while the
    `$C220/$C221` read-back does **not** alias `$5A/$A5`, i.e. `F = 1` means
    *chip present* — but the branch to the FOUND lines tested `IF F = 0`. So an
    FM machine announced `STATUS: NO YM2151` + `PCM MUSIC STREAMING` while
    `MUSIC.VZM` was playing, and a no-FM machine announced `FM CHIP FOUND` +
    `PSG + FM` while streaming `OTHELLO.PCM`. One byte, line 72:
    `IF F = 0 THEN GOTO 76` → `IF F = 1 THEN GOTO 76`; `STARTUP` stays 2,538 B /
    5 blocks, 185 blocks free, and the audio path is untouched (it comes from the
    independent C probe). Verified by reading the *text* back, not the flags: a
    `-DVERA_DEBUG_STATS` build copies `$0400-$07FF` to bank-1 `$1C000` and
    `-vera-dump` decodes it (§5.7.4) — FM = 1 → `STATUS: YM2151 (OPM) FM CHIP
    FOUND` / `PSG + FM MUSIC ENABLED`; FM = 0 → `STATUS: NO YM2151 (OPM) FM CHIP`
    / `PCM MUSIC STREAMING`. The shipping image was then re-run end to end on
    AppleWin (40 M cycles, slot 2): both settings still reach the identical title
    screen (layer-0 map all tile 1, 129 non-zero layer-1 entries, palette
    `00 00 ff 0f`), and the WAV still separates the paths — FM = 1 first audible
    6.96 s, peak 15,641; FM = 0 7.11 s, peak 11,040 (§5.7.2). Lesson: the banner
    is part of the product, so verifying a probe means reading the bytes it
    printed — the music was always right, which is exactly why nobody noticed.

---

## ⚠️ 8. Known Constraints & Open Items

- **VRAM headroom.** ~82 KB of bank 0 is unused. A native 640×480 **4bpp**
  tilemap is feasible: 160 tiles × 128 B = 20,480 B, two 64×32 maps = 8,192 B,
  plus palette/sprites/PSG ≈ 29.6 KB total — *less* than the current 8bpp
  320×240 layout. Colour feasibility is already measured: all 96 font tiles use
  2 colours, and 59 of 64 game tiles use ≤16 colours; only `0x32`, `0x34`,
  `0x35`, `0x37`, `0x39` (the stone-board greyscale gradients) exceed 16 and
  would need quantising. **Not implemented — the current display is accepted.**
- **CPU memory headroom.** The payload now ends at `__heap_start` = `$6A91`
  (`.text` 18,589 B from `$1400` to `$5CE0`, `.rodata` `$5CE1-$6029`, `.data`
  `$602A-$605B`, `.bss` `$6200-$6A77`, `.noinit` `$6A78-$6A90`), so there are
  **1,391 bytes** left before the ProDOS I/O buffers at `$7000`. That is the real
  ceiling, not the `0x8800` linker region. The PCM block map costs 1 KB of it
  (`pcm_master` + `pcm_index`, 256-aligned) and the `.bss` half of the music
  double buffer another 512 B.
- **Boot to title is ≈7 s of emulated time**: ~0.3 s for the launcher's
  `OTHELLO.PCM` OPEN probe, 3.0 s for the `PLEASE WAIT` splash, and the rest is
  the 42,912-byte asset stream into VRAM (≈600 k cycles). That is the honest
  floor without moving asset loading off the critical path; do not shorten the
  splash without re-measuring, it is what hides the VRAM fill.
- **Music tempo is the frame rate** for the PSG/FM streams. One VSYNC edge =
  one stream tick, so the song plays 2 % slow while the game runs at 58.3 fps
  (Mouse Card fitted) and on tempo at 59.5 fps. The **PCM** soundtrack has its
  own hardware clock, so it keeps correct tempo in PCM mode (34.1 fps) — only the
  game's own animation slows with the frame rate. Do not "fix" this by decoupling the tick from the frame
  — the tempo then drifts against the VSYNC-locked background scroll instead.
  If more frame headroom is ever needed, the next candidates are: polling the
  mouse every 3rd frame, dropping the 192-byte ZP save/restore in `mouse.s` to
  the registers the firmware actually clobbers, and moving the 512-byte
  prefetch read to a coarser granularity (1 KB every 2 blocks).
- **Mouse mapping** is mathematically consistent at 320×240 (`mx >> 4`, clamped
  to 319×239) and was exercised end-to-end in AppleWin — a Mouse Card in slot 5
  made the title menu respond to real host clicks (see §7 #25). The jest/apple2ts
  harness still does not inject mouse events, so that path has no automated
  regression test.
- **Joystick** paddle-driven movement is untested on a paddle-emulating harness.
- **Soundtrack on a 1 MHz 6502.** `OTHELLO.PCM` is 145 s of 8 kHz mono; the
  block-map reader plus the PCM FIFO feed cost ~15.4 cycles/byte, which is why
  the PCM build runs at 34.1 fps instead of 58–59. There is no cheaper option:
  the PSG can carry the tune but not the bass, and live FM music needs the
  daughterboard. `pcmFallbacks` must stay 0 in a healthy run — a nonzero value
  means the block map was rejected and the slow file path is in use.
- **Music loop point** (`MUSIC_VZM_LOOP` = tick 1536 ≈ 25.6 s) is verified
  structurally: `verify_music_streams.mjs` replays the stream across the loop and
  the packaged bytes match, but the AppleWin WAV capture is capped at 20 s, so
  the loop has not been *heard* end-to-end. If a loop bug is ever suspected,
  temporarily lower the loop offset in `gen_music.mjs` (e.g. to the tick-768
  record) and re-run the WAV capture — that brings the loop inside the window.
- **FM on/off is per-slot registry state, not a CLI switch** (§5.7.1), and
  `-vera-dump` / `-fm-wav-dump` only work with the card in slot 2 (§5.7). Slot-4
  behaviour must be judged from `VERA.log` plus the audio WAV of a slot-2 run.
- The diag test's `vramRead`-vs-file byte comparison is unreliable; trust the
  DATA0 trace and the framebuffer. The first 32 bytes of `generated/tiles.bin`
  are legitimately zero (tile 0 = `TILE_NONE`).
- `build/` holds only `main.bin`, `main.bin.elf`, `main.elf`, `main.map`,
  `main4.bin`, `main4.bin.elf`, `STARTUP`, and `headless/`.
