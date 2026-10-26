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
- **Build output**: `cx16-othello.hdv` — an 800 KB bootable ProDOS 2.4.3 fixed
  disk image (1600 blocks).
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
$0:B000-$F9BF   free (≈82 KB)
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
$0C00-$0DFF   Music 512-byte record buffer (VZM or PSG stream)
$0E00-$13FF   Help page buffer (240 B) + ProDOS scratch
$1000-$13FF   NOT TOUCHED      ProDOS QUIT / BASIC.SYSTEM scratch
$1400-$54E2   Current program payload, loaded (text + rodata + data)
$54E3-$5722   .bss, zeroed at startup (not in the BIN)
$5723-$573B   .noinit (static stack space)
$573C         __heap_start (payload end; only used if malloc is called)
$6000-$6BFF   Three page-aligned 1 KB ProDOS OPEN buffers
$BE00         __stack    soft stack grows down from here
$BF00         ProDOS MLI entry
$C200/$C400   VERA registers (+ $20/$21 YM2151 when the FM board is fitted)
```

Binary format: Apple II `BIN` header = 2-byte load address `$1400` + 2-byte
length; `main.bin` is 16,611 bytes of payload (16,615 bytes on disk, 33 blocks
of the HDV). `STARTUP` is 1,422 bytes (Applesoft type `$FC`, aux `$0801`).

Note that the payload is free to grow: `link1000.ld` declares a `ram` region of
`0x8800` bytes from `$1400` to `$9C00`, and the ProDOS OPEN buffers at `$6000`
and the MLI entry at `$BF00` are outside it. Do **not** let it grow past
`$5FFF`: `$6000-$6BFF` holds the ProDOS I/O buffers.

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

The X16 original plays YM2151 FM music through ZSMKit. Two different music
files ship on the HDV, and `audio_start_music()` picks one at boot:

| File | Size (HDV blocks) | Used when | Content |
| :--- | :--- | :--- | :--- |
| `MUSIC.VZM` | 198,656 B (388) | the YM2151 daughterboard is present | native PSG writes for voices **4–15** + real YM2151 register writes |
| `MUSIC.PSG` | 157,956 B (309) | no FM board | the FM voices re-voiced onto PSG voices **0–3**, native voices 4–15 passed through |

`MUSIC.PSG` is generated by `tools/zsm2psg.py`; `MUSIC.VZM` by
`tools/zsm2vera.mjs` + `tools/gen_music.mjs`. Both are streamed through the
single 512-byte buffer at `$0C00` and advanced by the VERA VSYNC at ~60 Hz,
including while tilemap work is in flight, so disk loading never changes the
tempo. Thumb SFX stay on PSG voice 3 (registers 12–15), which neither stream
touches: VZM's PSG mask is `$FFF0` and `MUSIC.PSG` only writes voices 0–3.

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

or `STATUS: NO YM2151 (OPM) FM CHIP` / `PSG MUSIC ONLY`, so a user can see which
build behaviour they are getting. The BASIC probe writes `$C200/$C201` and reads
`$C220/$C221` (slot 4: `$C400/$C401` / `$C420/$C421`) and restores `$C200/$C201`.

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

---

## 🛠️ 4. Build System

```bash
cd C:\dev\cx16-othello\vera
build.bat
```

Six steps:

| # | Step | Tool |
| :--- | :--- | :--- |
| 1 | Generate the asset blob | `tools\gen_assets.py` |
| 2 | Convert ZSM music → VERA PSG (PSG-only build) | `tools\zsm2psg.py` |
| 3 | Convert ZSM music → VZM (PSG + YM2151 build) | `tools\zsm2vera.mjs`, `tools\gen_music.mjs` |
| 4 | Compile the Applesoft launcher | `tools\gen_startup.mjs` |
| 5 | Compile slot-2 + slot-4 images | `mos-apple2e-clang` |
| 6 | Create the 800 KB ProDOS HDV and add files with Cadius | `tools\build_hdv.py`, `tools\cadius.exe` |

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
├── cx16-othello.hdv          # 800 KB bootable ProDOS image (deliverable)
├── build.bat                 # 6-step pipeline
├── README.md                 # user-facing port notes
├── AGENTS.md                 # this file
├── src/
│   ├── main.c                # entry, game state machine, frame pump
│   ├── game.c/.h             # board, AI, scoring, stone/cursor rendering
│   ├── menu.c/.h             # title / settings / help screens
│   ├── video.c/.h            # VERA init, tilemaps, fonts, sprites, help pages
│   ├── input.c/.h            # keyboard, joystick, mouse
│   ├── audio.c/.h            # FM probe, MUSIC.VZM / MUSIC.PSG streaming, SFX
│   ├── disk.c/.h             # ProDOS file-level MLI streaming
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
│   ├── music.psg (157,956) music.vzm (198,656)
│   └── asset_table.h music_table.h music_vzm.h sfx_table.h
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
ProDOS STARTUP (Applesoft, 1,422 B, type $FC, aux $0801)
  → probes VERA at slot 2 ($C200), then slot 4 ($C400) by write/read-back
  → probes the YM2151 at $Cx20/$Cx21 and PRINTs the STATUS lines
  → BRUN MAIN.BIN  (slot 2)  or  MAIN4.BIN (slot 4)
     → $1400: zero .bss, save zero page
     → audio_detect_fm() → disk_init() → init_screen() → load_assets()
     → audio_init() → input_init() (mouse probe) → init_mouse_sprite()
     → audio_start_music() (MUSIC.VZM if fm_present, else MUSIC.PSG)
     → main loop: pump_frame() → input → game/menu state machine
```

### 4.3 HDV layout (1600 blocks)

The ProDOS volume name is `CX16.OTHELLO`. Cadius creates the 800 KB volume and
adds ten files to its root:

| File | Blocks | Bytes | Purpose |
| :--- | ---: | ---: | :--- |
| `PRODOS` | 33 | 17,128 | ProDOS 2.4.3 kernel |
| `BASIC.SYSTEM` | 20 | 10,240 | interpreter for `STARTUP` |
| `CLOCK.SYSTEM` | 6 | 2,678 | clock driver |
| `OTHELLO` | 1 | 7 | installation marker (7-byte file, only its presence matters) |
| `STARTUP` | 3 | 1,422 | Applesoft launcher (type `$FC`, aux `$0801`) |
| `MAIN.BIN` | 33 | 16,615 | slot-2 payload (type `$06`, aux `$1400`) |
| `MAIN4.BIN` | 33 | 16,615 | slot-4 payload (`-DVERA_BASE=0xC400`) |
| `ASSETS.BIN` | 84 | 42,912 | tiles + font + help + palette blob |
| `MUSIC.PSG` | 309 | 157,956 | PSG-only music, used when no FM board |
| `MUSIC.VZM` | 388 | 198,656 | PSG + YM2151 music, used when FM is present |

That is 680 blocks free on the 1600-block volume, so both music streams fit
comfortably. Cadius owns the directory entries, storage allocation and file
dates. The builder stages Apple II BIN payloads without their four-byte load
headers and uses Cadius' file-name suffix to set each ProDOS file type and
auxiliary address. System files are kept in `assets/prodos-system/`; no
prebuilt 800 KB base image is needed.

`disk.c` checks the marker, opens the asset file, then lazily opens the music
file. It uses ProDOS OPEN/GET_EOF/SET_MARK/READ rather than reserved raw block
ranges. Three aligned 1 KB OPEN buffers live at `$6000`, `$6400`, and `$6800`;
keep them outside ProDOS-owned memory and the game payload.

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
  20 seconds** of the mixed PSG+FM output. The 20 s cap is hard-coded
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
re-measured on the current build (`main.bin` 16,611 B, `MUSIC.VZM` 388 blocks)
with a 40 M-cycle slot-2 run unless stated otherwise.

| Run | Result |
| :--- | :--- |
| FM = 1, slot 2, 40 M cycles | title screen in the VRAM dump: layer-0 map has all 1024 entries set to tile 1 (the solid background tile, moved by the diagonal scroll), layer-1 map has 129 non-zero entries (title text, the 6×6 sample board with its border, and the three menu lines), palette `00 00 ff 0f`, ≈42 of 64 game tiles written; WAV (20 s) is silent for the first ≈4 s (asset load), then per-second rms ≈1,200–1,750 / 800–1,100 / 340–500 / 140–200 / 1,265–1,800 / … / 3,700–4,600, max sample 15,346–16,049 across runs (no clipping). Spectrum of the last second: ≈0.33 below 500 Hz, 0.33 at 500 Hz–2 kHz, 0.23 at 2–6 kHz, 0.11 at 6–20 kHz — the FM partials `MUSIC.PSG` cannot make |
| FM = 0, slot 2 | title screen identical (layer-1 map 129 non-zero); WAV max sample 8,176, rms per second 0 / 0 / 0 / 0 / 1,940 / 1,441 / 735 / 375 / 186 / 1,290 / 2,234 / … / 817. Spectrum: 0.03 / 0.34 / 0.23 / 0.37 — almost no sub-500 Hz energy, which is the audible difference between `MUSIC.PSG` re-voicing and real FM |
| FM = 1, slot 4, 40 M cycles | `VERA.log` shows the slot-4 launch (`$C405`, `$C400`/`$C401` write/read-back, then tile/map/PSG traffic, ≈53 k lines); WAV rms per 4 s: 0 / 1,053 / 1,267 / 3,591 / 4,133 |
| pristine HEAD HDV, either setting | WAV is **all zeros** — the `mouse_init()` branch overflow (§3.14) stops the boot before `audio_start_music()` |

`$C22x` appears **zero** times in `VERA.log` in *every* run above, including the
FM = 1 ones (§5.7.3). A quick discriminator: FM = 1 has substantial energy below
500 Hz (≈0.3), FM = 0 has almost none (≈0.03).

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
    `VERA.log` never shows `$C22x` even when FM is active (§5.7).
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

---

## ⚠️ 8. Known Constraints & Open Items

- **VRAM headroom.** ~82 KB of bank 0 is unused. A native 640×480 **4bpp**
  tilemap is feasible: 160 tiles × 128 B = 20,480 B, two 64×32 maps = 8,192 B,
  plus palette/sprites/PSG ≈ 29.6 KB total — *less* than the current 8bpp
  320×240 layout. Colour feasibility is already measured: all 96 font tiles use
  2 colours, and 59 of 64 game tiles use ≤16 colours; only `0x32`, `0x34`,
  `0x35`, `0x37`, `0x39` (the stone-board greyscale gradients) exceed 16 and
  would need quantising. **Not implemented — the current display is accepted.**
- **Mouse mapping** is mathematically consistent at 320×240 (`mx >> 4`, clamped
  to 319×239) and was exercised end-to-end in AppleWin — a Mouse Card in slot 5
  made the title menu respond to real host clicks (see §7 #25). The jest/apple2ts
  harness still does not inject mouse events, so that path has no automated
  regression test.
- **Joystick** paddle-driven movement is untested on a paddle-emulating harness.
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
