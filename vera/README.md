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
| Cadius | bundled as `tools\cadius.exe` |

Six steps:

1. `tools\gen_assets.py` — reads the original X16 artwork PNGs
   (`../assets/tiles`, `../assets/font`, `../assets/palette`, `../assets/scripts`)
   and emits `generated/tiles.bin`, `font.bin`, `help.bin`, `palette.bin`,
   the concatenated `assets.bin` and `generated/asset_table.h`.
2. `tools\zsm2psg.py` — converts `../assets/sound/*.zsm` into the VERA PSG
   stream `generated/music.psg` plus `generated/music_table.h`. This is the
   **PSG-only** music used when no YM2151 FM chip is fitted.
3. `tools\gen_music.mjs` (which uses `tools\zsm2vera.mjs`) — converts the same
   ZSM into `generated/music.vzm`, the **PSG + YM2151** stream, and emits
   `generated/music_vzm.h` (`MUSIC_VZM_SIZE`, `MUSIC_VZM_LOOP`,
   `MUSIC_VZM_TICKS`).
4. `tools\gen_startup.mjs` — compiles `src/startup.bas` to an Applesoft binary
   `build/STARTUP` (type `$FC`, aux `$0801`).
5. Two compile passes with `src/link1000.ld`: `build/main.bin` (VERA at
   `$C200`, slot 2) and `build/main4.bin` (`-DVERA_BASE=0xC400`, slot 4).
6. `tools\build_hdv.py` — uses bundled Cadius to create an 800 KB
   `CX16.OTHELLO` volume and add the ProDOS system and game files.

`node tools\verify_music_streams.mjs` re-converts the ZSM, diffs the result
against the packaged `MUSIC.VZM`, and replays both streams tick by tick to check
that the 6502 player performs exactly the writes the converter intended. Run it
after touching `zsm2vera.mjs`, `src/audio.c` or the soundtrack.

`src/link1000_elf.ld` is a disassembly-only twin of `link1000.ld` (ELF output
so `llvm-objdump` can read it). It is not part of the shipping image.

## 2. Boot chain

```
ProDOS  ->  BASIC.SYSTEM  ->  STARTUP (Applesoft, type $FC)
                              |
                              |  probes $C200 (slot 2) then $C400 (slot 4)
                              |  by writing VERA ADDR_L/ADDR_H and reading back
                              |  probes the YM2151 at $Cx20/$Cx21 (write/read-back)
                              |  prints:  STATUS: YM2151 (OPM) FM CHIP FOUND
                              |            PSG + FM MUSIC ENABLED
                              |     or:   STATUS: NO YM2151 (OPM) FM CHIP
                              |            PSG MUSIC ONLY
                              v
                            PRINT CHR$(4);"BRUN MAIN.BIN"   (slot 2)
                            PRINT CHR$(4);"BRUN MAIN4.BIN"  (slot 4)
                              |
                              v
                        audio_detect_fm()  -> picks MUSIC.VZM or MUSIC.PSG
```

`MAIN.BIN` is a ProDOS type-`$06` binary, load address `$1400`. ProDOS `BRUN`
jumps straight to `$1400`.

Memory map (`src/link1000.ld`):

| Range | Use |
| --- | --- |
| `$0000-$001F` | LLVM-MOS imaginary registers `__rc0..__rc31` |
| `$0050-$00BF` | C zero-page globals |
| `$0800-$0AFF` | ProDOS workspace / free game RAM |
| `$0B00-$0B3F` | ProDOS pathname buffer |
| `$0C00-$0DFF` | music streaming buffer (512-byte record) |
| `$0E00-$0FFF` | help-page text buffer + ProDOS scratch |
| `$1400-$54E2` | current program payload, loaded (text + rodata + data) |
| `$54E3-$5722` | `.bss`, zeroed at startup (not in the BIN) |
| `$5723-$573B` | `.noinit` (static stack space) |
| `$573C` | `__heap_start` (payload end) |
| `$6000-$6BFF` | three ProDOS OPEN I/O buffers (1 KB each) |
| `$BE00` | soft-stack base (grows down) |

`$1000-$13FF` is deliberately untouched: ProDOS keeps its QUIT code at
`$1000-$12FF` and BASIC.SYSTEM scratches at `$1300-$13FF`.

## 3. ProDOS files and HDV

The bootable HDV is named `CX16.OTHELLO`. Cadius creates the volume and adds
the ProDOS system files and game content as normal ProDOS files in its root
directory: `OTHELLO` (game marker), `STARTUP`, `MAIN.BIN`, `MAIN4.BIN`,
`ASSETS.BIN`, `MUSIC.PSG` and `MUSIC.VZM`. Cadius handles directory entries,
file block allocation, and file dates. The extracted system files used by the
build are kept in `assets/prodos-system/`.

| File | Blocks | Used when |
| --- | ---: | --- |
| `MUSIC.PSG` | 309 | no YM2151 FM chip (PSG-only, FM channels re-voiced onto voices 0–3) |
| `MUSIC.VZM` | 388 | YM2151 FM chip present (native PSG voices 4–15 + real FM writes) |

The game verifies the `OTHELLO` marker before opening `ASSETS.BIN`; this keeps
unrelated copies of the common asset filename from being used accidentally.
Assets and music are streamed with ProDOS OPEN/READ/SET_MARK calls. The HDV
uses three distinct, page-aligned 1 KB ProDOS I/O buffers at `$6000`, `$6400`,
and `$6800`.

File-level installation has been tested successfully from another ProDOS
volume and subdirectory. Copy `OTHELLO`, `STARTUP`, `MAIN.BIN`, `MAIN4.BIN`,
`ASSETS.BIN`, `MUSIC.PSG` and `MUSIC.VZM` to the target directory, then set the
ProDOS prefix to that directory before launching `STARTUP` (or BRUN the matching
`MAIN.BIN` / `MAIN4.BIN`). The marker, assets, and music are resolved relative
to the active prefix. If `MUSIC.VZM` is missing, a machine with FM simply plays
`MUSIC.PSG` instead — the game does not fail.

## 4. VERA specifics on the Apple II

The Apple II VERA card is **not** bit-identical to the X16's VERA:

* **Register window.** Slot 2 → `$C200-$C205`, slot 4 → `$C400-$C405`. The
  32-register VERA map is exposed compressed (`reg & 0x1F`): `$00-$02` address,
  `$03/$04` DATA0/DATA1, `$05` CTRL, `$06/$07` IRQ, `$08` VCOUNT, `$09-$0C`
  display-control (selected by `DCSEL`), `$0D-$13` layer 0, `$14-$1A` layer 1,
  `$1B-$1D` PCM, `$1E/$1F` SPI, and `$20/$21` the optional YM2151 FM
  daughterboard (write-only `YM_REG` / `YM_DATA`; those two offsets alias
  `ADDR_L`/`ADDR_M` when the chip is absent).
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
* **16-bit scroll registers.** VERA's scroll registers are 12-bit (L and H). In the C layer structs (`apple2e.h`), `hscroll` and `vscroll` must be defined as 16-bit `unsigned int`, otherwise `vscroll` will map to `HSCROLL_H` instead of `VSCROLL_L` and break diagonal scrolling.
* **Mouse pointer sprite is not automatic.** The Apple II VERA lacks the X16 kernal that automatically updates Sprite 0. The game manually loads a hand cursor into VRAM, assigns it to Sprite 0, sets its z-depth, and polls the Apple Mouse Card.
* **Layer compositing.** Colour index 0 is transparent for both tile layers,
  which is what the original relies on for its "transparent foreground"
  (`clear_foreground()`).
* **Palette.** 512 bytes at bank 1 `$FA00` (`$1FA00` linear), written through
  DATA0 exactly like the X16.
* **Audio.** The X16 build plays YM2151 FM music through the ZSMKit library.
  A VERA card with the optional YM2151 daughterboard exposes the chip at
  `$Cx20` (register select) and `$Cx21` (data), so the game can play the real FM
  soundtrack there; on a card without the chip it plays the PSG-only stream.
  Both are streamed from ProDOS at `$0C00` and advanced by VSYNC at ~60 Hz.
  * `MUSIC.VZM` — the FM version. PSG voices 4–15 are passed through natively and
    every YM2151 register write is replayed. At start-up the game keys off all
    eight FM channels and zeroes their registers so the de-duplicated stream
    starts from a known state; the stream loops at its authored point (tick
    1536 ≈ 25.6 s) rather than restarting.
  * `MUSIC.PSG` — the fallback. `tools/zsm2psg.py` re-voices the four FM
    channels onto PSG voices 0–3 (channels 3–6) and passes native voices 4–15
    through unchanged.
  * The thumb-placement SFX lives on PSG voice 3 (registers 12–15), which
    neither stream touches, so it never collides with the music.
  * Detection is a write/read-back test at boot: `$Cx20`/`$Cx21` alias
    `ADDR_L`/`ADDR_M` on a card without the chip, so writing `$5A/$A5` and
    reading both bytes back identifies the card. `STARTUP` performs the same
    probe in Applesoft and prints the result before `BRUN` (see §2).
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
* **Mouse Interactions.** In addition to selecting a move, clicking the mouse cursor over the "hit ENTER to go to menu" text at the end of a game will simulate an ENTER keypress and return to the main menu.

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
ProDOS MLI by intercepting `JSR $BF00`, drives the VERA renderer, injects
keyboard codes and dumps the 640×480 framebuffer as 24-bit BMP. The checked-in
apple2ts harness still models the earlier fixed-block disk calls; update its
MLI emulation to cover OPEN/READ/SET_MARK/GET_EOF/GET_PREFIX before relying on
those automated disk-loading tests with the file-level build.

What the five headless tests prove:

1. **Title screen** — boots, streams every asset, renders the title, sample
   board and four stones, and asserts the frame is filled edge to edge (every
   16×16 block of the 640×480 framebuffer has lit pixels, so a quarter-screen
   or clipped render fails); 0 MLI errors.
2. **8×8 game** — `RETURN` starts a game and the playfield renders.
3. **Settings + full 10×10 CPU-vs-CPU game** — drives the settings menu
   (player types, board size 10, wood board, both stone colours), starts a
   game, plays it to the game-over state (57 + 43 = 100 stones) and reads both
   streamed assets and music from ProDOS files.
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

### AppleWin: VERA + YM2151 FM

[AppleWin](https://github.com/anomixer/AppleWin) is the only emulator with the
VERA card *and* the optional YM2151 daughterboard, so it is used to verify the FM
path. It runs unattended and exits after a cycle budget:

```powershell
cd C:\dev\applewin\release
.\AppleWin.exe -h1 C:\dev\cx16-othello-vera\vera\cx16-othello.hdv `
    -s2 vera -run-cycles 40000000 -log -fm-wav-dump
```

* `-run-cycles 40000000` is about 39 emulated seconds — enough to pass the asset
  load and start the music. Shorter runs only show the title screen or silence.
* `-fm-wav-dump` writes `VERA-fm.wav` (stereo 16-bit 44.1 kHz) holding the
  **first 20 seconds** of the mixed PSG+FM output. That cap is built in, so the
  file is a quick "is FM audible and does it clip" probe, not a full recording.
* `-vera-dump <file>` saves the 128 KB of VRAM; it only works when the card is in
  slot 2 (the dump helper looks the card up there), so slot-4 runs are checked
  from `VERA.log` instead.
* The FM chip is switched on/off per slot in the registry, not on the command
  line: `HKCU\Software\AppleWin\CurrentVersion\Configuration\Slot 2` →
  `FM Card Enabled` = `"1"` or `"0"` (same key under `Slot 4` for slot 4).
  AppleWin treats a missing value as *enabled*.
* `VERA.log` never shows `$C22x` writes even when FM is working — that branch
  returns before the logging call. Judge FM from the WAV and from the
  `STATUS:` line STARTUP prints on screen.
* Run it with the desktop unlocked. AppleWin drives emulation from its window /
  frame loop, so a locked or disconnected Windows session makes the process sit
  in its message loop and never produce `VERA.log`, `VERA-fm.wav` or the VRAM
  dump. That reproduces with `-noreg`, with no `-h1`, with no `-s2 vera`, and
  with the packaged `AppleWin1.32.1.0-verafm` build, so it is host state and not
  the image.
* An Apple Mouse Card in slot 5 is a useful extra check: the title menu then
  follows the mouse and responds to clicks (`(H) HELP` opens the help page,
  `(ENTER) START` starts a game), which exercises the whole mouse path.

Measured with the shipping image (AppleWin 1.32.1.0, the `anomixer` VERA fork,
40 M cycles, slot 2): FM on →
title screen plus music with a peak sample around 15,300–16,000 (no clipping)
and audible sub-500 Hz energy that `MUSIC.PSG` cannot produce; FM off → the same
tune from `MUSIC.PSG`, peak 8,176, thin in the bass; slot 4 → the game boots and
plays as well (verified from the `$C4xx` traffic in `VERA.log` and the matching
audio envelope).

## 6. Running it for real

* **AppleWin**: point a hard-disk slot at `cx16-othello.hdv` and boot ProDOS;
  the startup program launches automatically. A direct `RUN STARTUP` also works.
  Set the FM chip for the VERA slot in the AppleWin configuration first — the
  `STATUS:` lines printed by STARTUP tell you which music stream you will get.
* **Apple2TS**: load the `.hdv` as a hard-disk image in slot 7 with the VERA
  card in slot 2. (The hosted player at `apple2ts.com` needs the disk to be
  reachable over HTTP, e.g. the copy in this repo's GitHub release.)
* **Real hardware**: the card may sit in slot 2 (`$C200`) or slot 4 (`$C400`);
  STARTUP probes both. With the YM2151 daughterboard fitted you get FM+PSG
  music, without it PSG-only music — no configuration needed on the game side.

## 7. Known constraints

* Asset streaming costs about 600 k cycles (tiles + font + palette + help are
  pushed to VRAM one byte at a time). Expect a visible pause of a couple of
  seconds between `BRUN` and the title screen.
* Both music streams are streamed from disk 512 bytes at a time, so `MUSIC.VZM`
  costs 388 blocks and `MUSIC.PSG` 309; the 800 KB volume still has 680 blocks
  free after both are installed.
* The FM loop point is tick 1536 (≈ 25.6 s of music). Because the AppleWin audio
  capture stops at 20 s, the loop is verified by replaying the stream
  (`tools/verify_music_streams.mjs`, 0 mismatched ticks) rather than by ear.
* An earlier build hung before the title screen on every machine without an
  Apple Mouse Card: a `BCS` in the mouse-slot probe was 141 bytes too far and
  the assembler silently encoded it as a backwards jump into unrelated code.
  Fixed in `src/mouse.s` (the exit is an absolute `JMP` now); the pristine
  HEAD image is the one that hangs, not this build.
* The 320×240 source is 20×15 tiles, which is exactly the layout the game draws
  into. The 10×10 board plus its border spans rows 2–13 and columns 4–15, so
  the tightest case still leaves the score panel (rows 0–2, columns 15–19) and
  the `TURN` row (row 14) on screen. The headless title-screen test asserts that
  every 16×16 block of the 640×480 frame contains lit pixels, so a clipped or
  quarter-screen render fails the suite.
* Joystick detection probes the 555 one-shot paddles once at startup; on a
  machine without paddles the probe times out and joystick handling is
  disabled, so the paddle spin loop never runs in normal play.
* Mouse input is not covered by the jest harness (it injects keyboard codes
  only). It was exercised end-to-end in AppleWin with an Apple Mouse Card in
  slot 5: the pointer followed the host cursor and clicks selected the title
  menu rows (`(H) HELP` → help page, `(ENTER) START` → game), so the
  `mx >> 4` tile mapping and the menu hit test are known good.
