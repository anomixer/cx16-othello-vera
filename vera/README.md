# CX16-OTHELLO — Apple II / VERA port

Othello (Reversi) for the Apple //e equipped with a VERA video card (VidHD /
AppleWin "VERA" card, Apple2TS VERA device), ported from the Commander X16
original by Ivo Filot (GPL v3).

Everything the X16 build does is preserved: player-vs-player / player-vs-CPU /
CPU-vs-CPU, 6×6, 8×8 and 10×10 boards, stone and wood board artwork, ten stone
colours, diagonal background scrolling, help pages, keyboard + joystick +
Apple Mouse Card input, sound effects and the "Corridors of Time" background
music.

The shipped artifact is `cx16-othello.hdv`, an 800 KB ProDOS hard-image that
boots from BASIC.SYSTEM.

---

## 1. Build

```bat
cd vera
build.bat
```

Requirements (all already present on this machine):

| Tool | Path used by `build.bat` |
| --- | --- |
| LLVM-MOS SDK | `C:\dev\llvm-mos-sdk\install\bin\mos-apple2e-clang.bat` |
| Python 3 + Pillow + NumPy | `python` on PATH |
| Node.js | `node` on PATH |

Five steps:

1. `tools\gen_assets.py` — reads the original X16 artwork PNGs
   (`../assets/tiles`, `../assets/font`, `../assets/palette`, `../assets/scripts`)
   and emits `generated/tiles.bin`, `font.bin`, `help.bin`, `palette.bin`,
   the concatenated `assets.bin` and `generated/asset_table.h`.
2. `tools\zsm2psg.py` — converts `../assets/sound/*.zsm` into the VERA PSG
   stream `generated/music.psg` plus `generated/music_table.h`.
3. `tools\gen_startup.mjs` — compiles `src/startup.bas` to an Applesoft binary
   `build/STARTUP` (type `$FC`, aux `$0801`).
4. Two compile passes with `src/link1000.ld`: `build/main.bin` (VERA at
   `$C200`, slot 2) and `build/main4.bin` (`-DVERA_BASE=0xC400`, slot 4).
5. `tools\build_hdv.py` — assembles `cx16-othello.hdv` on top of the stock
   `vera/assets/800kb.hdv` base image.

`src/link1000_elf.ld` is a disassembly-only twin of `link1000.ld` (ELF output
so `llvm-objdump` can read it). It is not part of the shipping image.

## 2. Boot chain

```
ProDOS  ->  BASIC.SYSTEM  ->  STARTUP (Applesoft, type $FC)
                              |
                              |  probes $C200 (slot 2) then $C400 (slot 4)
                              |  by writing VERA ADDR_L/ADDR_H and reading back
                              v
                            PRINT CHR$(4);"BRUN MAIN.BIN"   (slot 2)
                            PRINT CHR$(4);"BRUN MAIN4.BIN"  (slot 4)
```

`MAIN.BIN` is a ProDOS type-`$06` binary, load address `$1400`. ProDOS `BRUN`
jumps straight to `$1400`.

Memory map (`src/link1000.ld`):

| Range | Use |
| --- | --- |
| `$0000-$001F` | LLVM-MOS imaginary registers `__rc0..__rc31` |
| `$0050-$00BF` | C zero-page globals |
| `$0800-$09FF` | disk asset streaming buffer |
| `$0C00-$0DFF` | music streaming buffer |
| `$0E00-$0FFF` | help-page text buffer |
| `$1400-$4BF7` | program (text, rodata, data, bss) |
| `$BE00` | soft-stack base (grows down) |

`$1000-$13FF` is deliberately untouched: ProDOS keeps its QUIT code at
`$1000-$12FF` and BASIC.SYSTEM scratches at `$1300-$13FF`.

## 3. HDV layout

| Blocks | Content |
| --- | --- |
| 0–69 | ProDOS, `CLOCK.SYSTEM`, `BASIC.SYSTEM`, directory, volume bitmap |
| 70–899 | free |
| 900–983 | asset stream (tiles, font, help, palette) — `ASSET_START_BLOCK` |
| 1000–1303 | music PSG stream — `MUSIC_START_BLOCK` |

The two constants live in `src/disk.h` and must match `tools/build_hdv.py`.

Files in the volume: `STARTUP`, `MAIN.BIN`, `MAIN4.BIN`.

## 4. VERA specifics on the Apple II

The Apple II VERA card is **not** bit-identical to the X16's VERA:

* **Register window.** Slot 2 → `$C200-$C205`, slot 4 → `$C400-$C405`. The
  32-register VERA map is exposed compressed (`reg & 0x1F`): `$00-$02` address,
  `$03/$04` DATA0/DATA1, `$05` CTRL, `$06/$07` IRQ, `$08` VCOUNT, `$09-$0C`
  display-control (selected by `DCSEL`), `$0D-$13` layer 0, `$14-$1A` layer 1,
  `$1B-$1D` PCM, `$1E/$1F` SPI.
* **`ADDR_H` encoding** is `bit0 = VRAM bank`, `bit1 = fx_nibble_bit`,
  `bit2 = fx_nibble_incr`, `bits 7:3 = stride index`. The X16 puts the bank in
  bit 7. `VERA_INC_1 = $10`, `VERA_INC_BANK1 = $11` (see `src/apple2e.h`).
* **Scaling / resolution.** `HSCALE/VSCALE = $80` is 1:1 and `$40` is 2:1
  *magnification* — the opposite of the X16 convention. The game draws a
  **320×240 source (20×15 tiles of 16×16) and the composer magnifies it 2:1 so
  it fills the whole 640×480 screen**. 320×240 is the resolution the layout is
  built for — the help pages are 20 columns wide, the score panel is
  right-aligned at column 20, and `src/mouse.s` clamps the Apple Mouse Card to
  320×240, which is what makes `mx >> 4` land on the right tile. Sprites are
  positioned in source pixels, so they land directly on the 16-pixel tile grid.
* **The full 32×32 map must always be filled.** The renderer builds a
  640-pixel layer line and applies the scroll in *output* pixels
  (`(x + hscroll) & (mapw*tilew - 1)`), so the diagonal background scroll reads
  tiles from the whole map, wrapping at 32 tiles. Filling only the visible
  20×15 area leaves the rest stale and the scrolled edge shows the border
  colour through transparent tile index 0 — that is the "background effect
  breaks" symptom. `set_background()` / `clear_foreground()` therefore fill all
  1024 entries.
* **Help pages are fixed-width fields, not C strings.** `assets/help.hlp` is
  parsed into 240-byte pages of 12 rows × 20 characters, padded with spaces and
  with **no NUL between rows**. `write_string()` stops at the NUL, so feeding it
  a help row writes the whole rest of the page: the write runs past column 19,
  wraps at the 32-tile map stride and lands on the rows below — which is exactly
  where the `(N) NEXT (P) PREV` / `(ESC) BACK PAGE:` lines live, so the help
  text appears to overflow the screen. `load_help_page()` therefore uses
  `write_string_n(src, HELP_PAGE_COLS, y, 0)`, which clamps to the field width
  and to the map row. Any new fixed-width text must go through `write_string_n`.
* **Do not write the DCSEL = 1 composer window registers.** Every write to the
  composer window (`$C209-$C20C`) makes the renderer run a midline pass
  (`video_step(MHZ, 0, true)`), which disturbs the VSYNC/raster timing the MLI
  streaming loop depends on and crashes the boot. The reset defaults
  (`hstart=0`, `hstop=640`, `vstart=0`, `vstop=480`) are already the full
  screen, so `init_screen()` only sets `hscale`/`vscale`.
* **Tile size.** The tile-height field only encodes 8×8 and 16×16 on this
  implementation; `$03` in the tilebase register selects 16×16.
* **Layer compositing.** Colour index 0 is transparent for both tile layers,
  which is what the original relies on for its "transparent foreground"
  (`clear_foreground()`).
* **Palette.** 512 bytes at bank 1 `$FA00` (`$1FA00` linear), written through
  DATA0 exactly like the X16.
* **Audio.** The X16 build plays YM2151 FM music through the ZSMKit library.
  The Apple II has no YM2151, so `tools/zsm2psg.py` re-voices the four FM
  channels onto PSG voices 0–3 (channels 3–6) and passes the native PSG voices
  4–15 through unchanged. The thumb-placement SFX lives on PSG voice 3 so it
  never collides with the music.
* **Sprite X/Y are raw pixels.** The X16 stores sprite `X_LO = X >> 1`; this
  implementation uses `byte2 | (byte3 & 3) << 8` directly as the pixel
  coordinate, so `set_sprite()` writing `posx << 4` lands exactly on the 16-pixel
  tile grid.
* **Cursor axes.** `move_cursor(y, x)` takes a row delta and row 0 is the top of
  the screen, so a positive row delta moves down. The X16 original pairs its
  `KEYCODE_UP` / `KEYCODE_DOWN` names with the PETSCII cursor-control codes
  (`$11` = cursor down, `$91` = cursor up), so its names are already inverted and
  its `move_cursor(1,0)` on "up" is correct there. The Apple //e arrow codes
  (`$08` left, `$0B` up, `$0A` down, `$15` right) are not inverted, so
  `src/game.c` maps the vertical axis the other way; the joystick block matches.
  `othello_headless.test.ts` asserts all four directions against both `curx/cury`
  and the cursor sprite's pixel box.

## 5. Automated verification (headless)

Verification runs inside the **apple2ts** repository, because that is where the
Apple II + VERA core and the Jest/ts-jest module resolution live:

* `C:\dev\apple2ts\src\worker\devices\vera\othello_headless.test.ts`
* `C:\dev\apple2ts\src\worker\devices\vera\othello_diag.test.ts`

Both read their inputs from absolute paths under `C:/dev/cx16-othello/vera` and
write their results to `C:/dev/cx16-othello/vera/build/headless/`.

Run them (from `C:\dev\apple2ts`; `npx` is blocked by the execution policy, so
invoke Jest directly):

```powershell
cd C:\dev\apple2ts
node node_modules/jest/bin/jest.js src/worker/devices/vera/othello_headless.test.ts --testTimeout=3000000
node node_modules/jest/bin/jest.js src/worker/devices/vera/othello_diag.test.ts  --testTimeout=1800000
```

The harness boots the real `build/main.bin` payload at `$1400`, services the
ProDOS MLI (`READ_BLOCK`/`WRITE_BLOCK`) out of the shipped `.hdv` by
intercepting `JSR $BF00`, drives the VERA renderer, injects keyboard codes and
dumps the 640×480 framebuffer as 24-bit BMP.

What the five headless tests prove:

1. **Title screen** — boots, streams every asset, renders the title, sample
   board and four stones, and asserts the frame is filled edge to edge (every
   16×16 block of the 640×480 framebuffer has lit pixels, so a quarter-screen
   or clipped render fails); 0 MLI errors.
2. **8×8 game** — `RETURN` starts a game and the playfield renders.
3. **Settings + full 10×10 CPU-vs-CPU game** — drives the settings menu
   (player types, board size 10, wood board, both stone colours), starts a
   game, plays it to the game-over state (57 + 43 = 100 stones) and reads both
   the asset blocks (900+) and the music blocks (1000+).
4. **Cursor directions** — each Apple //e arrow key moves the cursor one cell
   in the matching screen direction, checked against `curx/cury` and the cursor
   sprite's pixel box.
5. **Help screen text** — decodes every rendered glyph back against `font.bin`,
   compares all 11 help rows with `help.bin`, and asserts the `(N) NEXT (P) PREV`
   and `(ESC) BACK  PAGE:1/6` lines are intact on rows 13/14. This is the
   fixed-width-field overflow guard.

`build/headless/play.txt` records the state transitions,
`build/headless/cursor.txt` the per-key cursor deltas,
`build/headless/05_help.txt` the decoded help-screen text, and
`build/headless/04_game_*.bmp` are the mid-game frames.

### Looking at the frames

This agent has no image input, so frames are inspected as ASCII. Two helpers:

```bat
python tools\show_screen.py build\headless\01_title.bmp 96 28
```

prints a luminance map plus the top-10 colour histogram. The game fills the
whole 640×480 framebuffer (320×240 source magnified 2:1), so classify pixels
against the palette over the full frame; a 16-pixel sampling grid gives the
40×30 tile-level view. Text is easiest to read by masking the font colour
`RGB(34,17,0)` (palette index `$51`) — each glyph is 32×32 screen pixels, so
sample at 4 pixels.

### Diagnostics

`othello_diag.test.ts` dumps the VERA register file, the tilemaps, tiles,
palette and sprite attributes, compares VRAM byte-for-byte against
`generated/tiles.bin` / `palette.bin` / `font.bin`, renders each layer in
isolation, and traces every `DATA0` write of the running program bucketed by
VRAM region (`diag_trace.txt`, `diag_trace_summary.txt`).

## 6. Running it for real

* **AppleWin**: point a hard-disk slot at `cx16-othello.hdv`, boot ProDOS,
  `RUN STARTUP`.
* **Apple2TS**: load the `.hdv` as a hard-disk image in slot 7 with the VERA
  card in slot 2.

## 7. Known constraints

* Asset streaming costs about 600 k cycles (tiles + font + palette + help are
  pushed to VRAM one byte at a time). Expect a visible pause of a couple of
  seconds between `BRUN` and the title screen.
* The 320×240 source is 20×15 tiles, which is exactly the layout the game draws
  into. The 10×10 board plus its border spans rows 2–13 and columns 4–15, so
  the tightest case still leaves the score panel (rows 0–2, columns 15–19) and
  the `TURN` row (row 14) on screen. The headless title-screen test asserts that
  every 16×16 block of the 640×480 frame contains lit pixels, so a clipped or
  quarter-screen render fails the suite.
* Joystick detection probes the 555 one-shot paddles once at startup; on a
  machine without paddles the probe times out and joystick handling is
  disabled, so the paddle spin loop never runs in normal play.
* `tools/applewin_ctl.ps1` / `tools/applewin_probe.ps1` are leftovers from the
  abandoned AppleWin screenshot approach (AppleWin's VERA output replaces the
  Apple II framebuffer only once VERA video is enabled, and its window is a
  modeless dialog that PrintWindow cannot capture). They are kept for reference
  only; the headless harness is the supported path.
