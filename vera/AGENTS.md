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
| `C:\dev\time-pilot` | ProDOS fixed-block MLI `$80` streaming, single 512-byte music buffer, VSYNC-driven music tick |
| `C:\dev\freegemas` | Tilemap + sprite composition for a board game, palette packing |
| `C:\dev\x16-hero-vera` | X16 → Apple II asset conversion pipeline, ZSM → PSG re-voicing |

---

## ⚙️ 2. VERA on the Apple II

### 2.1 Register window

VERA occupies the Apple II slot I/O space at `VERA_BASE + $00..$1A`
(`VERA_BASE = $C200` for slot 2, `$C400` for slot 4).

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
$0800-$0BFF   Disk block buffer
$0C00-$0DFF   Music 512-byte streaming buffer
$0E00-$13FF   Help page buffer (240 B) + ProDOS scratch
$1000-$13FF   NOT TOUCHED      ProDOS QUIT / BASIC.SYSTEM scratch
$1400-$48D7   Program payload  loaded by BRUN
$1400         .text   13,005 B (0x32CD)
$4718         .rodata    433 B
$48C9         .data       15 B
$48D8         .bss       561 B  (zero-filled at runtime)
$4B09         .noinit     26 B  -> program ends $4B23
$BE00         __stack    soft stack grows down from here
$BF00         ProDOS MLI entry
$C200/$C400   VERA registers
```

Binary format: Apple II `BIN` header = 2-byte load address `$1400` + 2-byte
length `13,528`; `main.bin` is 13,532 bytes on disk. `STARTUP` is 1,115 bytes
(Applesoft type `$FC`, aux `$0801`).

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

### 3.11 Audio: YM2151 FM → PSG & Conversion Bug Fix

The X16 original plays YM2151 FM music. The Apple II VERA has no FM operator
path, so `tools/zsm2psg.py` re-voices the ZSM stream onto PSG voices 0–3
(channels 3–6); native PSG channels 3–6 pass through, and thumb SFX use voice 3.
Music is streamed through a single 512-byte buffer and advanced by the VERA
VSYNC at ~60 Hz, including while tilemap work is in flight, so disk loading
never changes the tempo.

**YM2151 Register Parsing Bug**: An earlier version of `zsm2psg.py` mistakenly mapped the YM2151 Pan/Algorithm register (`$20-$27`) as the Key Code register, and the Key Code register (`$28-$2F`) as Total Level. This caused the script to synthesize completely random frequencies (screeching/high pitches) derived from stereo pan data instead of musical notes. This was fixed by correctly parsing Key Code from `$28-$2F` and Total Level from `$60-$7F`.

**MIDI & PSG Utilities**:
- `tools/zsm2midi.py`: Added to extract YM2151 Key On events to a standard `.mid` file for testing alternate synthesis engines (e.g. `veramusic` tools).
- `tools/psg_to_veramusic.py`: Added to convert the native Othello PSG format (which streams `0x80|reg, val` + delays) into the standard `veramusic` block-count PSG format so it can be previewed in `psgplay.exe`.

### 3.12 16-bit scroll registers

The Apple II `mos-apple2e-clang` compiler defines `unsigned int` as 16-bit. VERA's scroll registers (`HSCROLL_L`/`HSCROLL_H` and `VSCROLL_L`/`VSCROLL_H`) are 12-bit, spanning two bytes. If the C struct defines `hscroll` and `vscroll` as `unsigned char`, writing to `vscroll` maps to `HSCROLL_H` (offset 4) instead of `VSCROLL_L` (offset 5). The layer structs in `apple2e.h` must declare them as `unsigned int` so they properly map to offsets 3+4 and 5+6 to ensure diagonal scrolling works.

### 3.13 Mouse pointer sprite is not automatic

The Commander X16 kernal automatically reads the mouse hardware and assigns an empty sprite (Sprite 0) with a hardware pointer graphic. The Apple II VERA has no such kernal. The game must manually load a mouse pointer tile into VRAM (e.g., at `$B000`), map Sprite 0 to it with `assign_sprite()`, set its z-depth, and poll the Apple Mouse Card (`buttons & 0x80` for the primary button) to move it.

---

## 🛠️ 4. Build System

```bash
cd C:\dev\cx16-othello\vera
build.bat
```

Five steps:

| # | Step | Tool |
| :--- | :--- | :--- |
| 1 | Generate the asset blob | `tools\gen_assets.py` |
| 2 | Convert ZSM music → VERA PSG | `tools\zsm2psg.py` |
| 3 | Compile the Applesoft launcher | `tools\gen_startup.mjs` |
| 4 | Compile slot-2 + slot-4 images | `mos-apple2e-clang` |
| 5 | Assemble the ProDOS HDV | `tools\build_hdv.py` |

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
├── build.bat                 # 5-step pipeline
├── README.md                 # user-facing port notes
├── AGENTS.md                 # this file
├── src/
│   ├── main.c                # entry, game state machine, frame pump
│   ├── game.c/.h             # board, AI, scoring, stone/cursor rendering
│   ├── menu.c/.h             # title / settings / help screens
│   ├── video.c/.h            # VERA init, tilemaps, fonts, sprites, help pages
│   ├── input.c/.h            # keyboard, joystick, mouse
│   ├── audio.c/.h            # PSG music streaming + SFX
│   ├── disk.c/.h             # ProDOS fixed-block MLI streaming
│   ├── mli.s                 # JSR $BF00 READ_BLOCK / WRITE_BLOCK / QUIT
│   ├── mouse.s               # Apple Mouse Card driver
│   ├── apple2e.h             # VERA register structs (Apple II semantics)
│   ├── constants.h           # VRAM layout, tile ids, key codes
│   ├── sfx_data.c            # thumb SFX on PSG voice 3
│   ├── link1000.ld           # shipping linker script ($1400 load, $BE00 stack)
│   ├── link1000_elf.ld       # disassembly-only twin
│   └── startup.bas           # Applesoft launcher source
├── tools/
│   ├── gen_assets.py         # tiles + font + help + palette → assets.bin
│   ├── zsm2psg.py            # ZSM → VERA PSG stream
│   ├── gen_startup.mjs       # Applesoft BASIC → binary STARTUP
│   ├── applebasic.mjs        # Applesoft tokenizer
│   ├── build_hdv.py          # ProDOS volume writer
│   ├── show_screen.py        # framebuffer → ASCII inspection
│   ├── zsm_probe.py          # ZSM command inspector
│   ├── applewin_ctl.ps1      # AppleWin automation
│   └── applewin_probe.ps1
├── generated/                # build-time assets + tables
│   ├── tiles.bin (16,384) font.bin (24,576) help.bin (1,440)
│   ├── palette.bin (512) assets.bin (42,912) music.psg (155,428)
│   └── asset_table.h music_table.h sfx_table.h
├── assets/                   # source art, palette png, help.hlp, zsm music
└── build/
    ├── main.bin main4.bin    # slot 2 / slot 4 payloads
    ├── main.elf main.bin.elf main4.bin.elf
    ├── main.map              # linker map (consumed by the tests)
    └── headless/             # test output: .bmp frames + .txt reports
```

### 4.2 Boot chain

```
ProDOS STARTUP (Applesoft, 1,115 B, type $FC, aux $0801)
  → probes VERA signature at $C200, then $C400 (order 5,3,6,7,1,4,2)
  → BRUN MAIN.BIN  (slot 2)  or  MAIN4.BIN (slot 4)
     → $1400: zero .bss, save zero page, init_screen(), load_assets()
     → main loop: pump_frame() → input → game/menu state machine
```

### 4.3 HDV layout (1600 blocks)

| Blocks | Content |
| :--- | :--- |
| 0–15 | ProDOS volume header, bit map, root directory |
| 100 | `STARTUP` index block |
| 101–103 | `STARTUP` data (1,115 B) |
| 104 | `MAIN.BIN` index block |
| 105–131 | `MAIN.BIN` data (13,528 B, slot 2) |
| 132 | `MAIN4.BIN` index block |
| 133–159 | `MAIN4.BIN` data (13,528 B, slot 4) |
| 900–983 | `assets.bin` — 42,912 B = 84 blocks (raw, no directory entry) |
| 1000–1303 | `music.psg` — 155,428 B = 304 blocks (raw, no directory entry) |

`tools/build_hdv.py` prints these ranges on every build.

`src/disk.h` and `tools/build_hdv.py` must stay in sync; the runtime reads
these ranges with MLI `$80` `READ_BLOCK` only (no pathname parsing).

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
AppleWin screen capture proved unusable for automated checks.

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

## 🧪 6. Test Suite

Test files live in the apple2ts repo by necessity (jest/ts-jest module
resolution); they read assets from absolute paths under `C:/dev/cx16-othello/vera`
and write to `C:/dev/cx16-othello/vera/build/headless/`.

**Current status: `Test Suites: 2 passed, Tests: 8 passed`.**

### `othello_headless.test.ts` (5 tests)

1. **Title screen** — boots, streams every asset, renders the title, sample
   board and four stones, and asserts the frame is filled edge to edge: every
   16×16 block of the 640×480 framebuffer must contain lit pixels, so a
   quarter-screen or clipped render fails. 0 MLI errors.
2. **8×8 game** — `RETURN` starts a game and the playfield renders.
3. **Settings + full 10×10 CPU-vs-CPU game** — drives the settings menu (player
   types, size 10, wood board, both stone colours), plays to game-over
   (57 + 43 = 100 stones), and reads both the asset blocks (900+) and the music
   blocks (1000+).
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
3. **ProDOS streaming** — fixed-block MLI `$80` reads at blocks 900 / 1000,
   adapted from TimePilot-IIvera; `mli.s` saves and restores zero page `$40-$4F`
   around every `JSR $BF00`.
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
  to 319×239) but has not been exercised end-to-end in the headless harness.
- **Joystick** paddle-driven movement is untested on a paddle-emulating harness.
- The diag test's `vramRead`-vs-file byte comparison is unreliable; trust the
  DATA0 trace and the framebuffer. The first 32 bytes of `generated/tiles.bin`
  are legitimately zero (tile 0 = `TILE_NONE`).
- `build/` holds only `main.bin`, `main.bin.elf`, `main.elf`, `main.map`,
  `main4.bin`, `main4.bin.elf`, `STARTUP`, and `headless/`.
