#!/usr/bin/env python3
"""Generate the Apple II VERA asset blob for CX16-OTHELLO.

Outputs (into vera/generated):
  tiles.bin   64 x 16x16 8bpp tiles  (VRAM $00000, tile indices 0..63)
  font.bin    96 x 16x16 8bpp glyphs (VRAM $04000, tile indices 64..159)
  help.bin    6 help pages, 240 bytes each
  palette.bin 512-byte VERA palette (VRAM $1FA00)
  assets.bin  tiles + font + help + palette, concatenated
  asset_table.h  C header with the offsets/lengths used by the game

The source artwork is the same PNGs the Commander X16 build uses, so the
Apple II port renders pixel-identical graphics.
"""
from __future__ import annotations
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
GEN = ROOT / "generated"

TILE_PX = 16
TILE_BYTES = TILE_PX * TILE_PX          # 8 bpp -> 256 bytes per tile
GAME_TILES = 4 * 16                     # 64 board/stone/UI tiles
FONT_CHARS = 96                         # 0x20..0x7F
FONT_COLOR = 0x51                       # same colour the X16 build uses


def load_cx16_palette() -> np.ndarray:
    """256 RGB triples read from the CX16 palette sheet (16x16 swatches)."""
    img = Image.open(ASSETS / "cx16palette.png").convert("RGBA")
    colors = []
    for i in range(16):
        for j in range(16):
            colors.append(img.getpixel((j * 64 + 32, i * 64 + 32)))
    return np.array(colors, np.uint8)


def vera_palette(pal: np.ndarray) -> bytes:
    """CX16 RGB triples -> VERA 12-bit palette bytes: [G4<<4|B4][0|R4]."""
    out = bytearray(512)
    for i in range(256):
        r, g, b = int(pal[i][0]), int(pal[i][1]), int(pal[i][2])
        out[i * 2] = ((g >> 4) << 4) | (b >> 4)
        out[i * 2 + 1] = (r >> 4) & 0x0F
    return bytes(out)


def closest_color_index(col, pal: np.ndarray) -> int:
    """Mirror of the X16 build: transparent -> 0, else nearest palette entry 1..255."""
    if col[3] < 50:
        return 0
    d = np.linalg.norm(pal[1:, 0:3].astype(np.int32) - np.array(col[0:3], np.int32), axis=1)
    return int(np.argmin(d)) + 1


def build_tiles(pal: np.ndarray) -> bytes:
    img = Image.open(ASSETS / "tiles.png").convert("RGBA")
    data = bytearray()
    for row in range(4):
        for col in range(16):
            for y in range(TILE_PX):
                for x in range(TILE_PX):
                    data.append(closest_color_index(img.getpixel((col * 16 + x, row * 16 + y)), pal))
    assert len(data) == GAME_TILES * TILE_BYTES
    return bytes(data)


def build_font() -> bytes:
    """1bpp 8x8 glyph sheet -> 16x16 8bpp tiles, glyph in the upper-left 8x8.

    Matches assets/scripts/fontmap.py + video.c::load_fontmap() exactly.
    """
    img = Image.open(ASSETS / "16x16_sm_ascii.png").convert("RGBA")
    data = bytearray()
    for i in range(6):
        for j in range(16):
            for y in range(16):
                left = 0
                for k, x in enumerate(range(0, 8)):
                    if img.getpixel((j * 16 + x, (i + 2) * 16 + y))[3] > 150:
                        left |= 1 << k
                right = 0
                for k, x in enumerate(range(8, 16)):
                    if img.getpixel((j * 16 + x, (i + 2) * 16 + y))[3] > 150:
                        right |= 1 << k
                for byte in (left, right):
                    for bit in range(8):
                        data.append(FONT_COLOR if (byte & (1 << bit)) else 0)
    assert len(data) == FONT_CHARS * TILE_BYTES
    return bytes(data)


def build_help() -> bytes:
    src = ROOT.parent / "src" / "help.hlp"
    if not src.exists():
        src = ROOT / "assets" / "help.hlp"
    pages, cur, parsing = [], bytearray(), False
    for line in src.read_text(encoding="utf-8").splitlines():
        if line.startswith("#"):
            continue
        if line.startswith("|PAGE"):
            parsing, cur = True, bytearray()
            continue
        if line.startswith("|ENDPAGE|"):
            parsing = False
            cur.extend([0x20] * (20 * 12 - len(cur)))
            pages.append(cur)
            continue
        if parsing:
            line = line.strip().replace("_", " ")
            cur += line[:20].encode("ascii") + bytearray([0x20] * max(0, 20 - len(line)))
    data = b"".join(pages)
    assert len(data) == 240 * len(pages), len(data)
    return data


def main() -> int:
    GEN.mkdir(exist_ok=True)
    pal = load_cx16_palette()

    tiles = build_tiles(pal)
    font = build_font()
    helpd = build_help()
    palbin = vera_palette(pal)

    (GEN / "tiles.bin").write_bytes(tiles)
    (GEN / "font.bin").write_bytes(font)
    (GEN / "help.bin").write_bytes(helpd)
    (GEN / "palette.bin").write_bytes(palbin)

    blob = tiles + font + helpd + palbin
    (GEN / "assets.bin").write_bytes(blob)

    off = 0
    table = []
    for name, data in (("TILES", tiles), ("FONT", font), ("HELP", helpd), ("PALETTE", palbin)):
        table.append((name, off, len(data)))
        off += len(data)

    lines = [
        "/* Auto-generated by tools/gen_assets.py - do not edit. */",
        "#ifndef _ASSET_TABLE_H",
        "#define _ASSET_TABLE_H",
        "",
        f"#define ASSETS_SIZE       {len(blob)}u",
    ]
    for name, o, n in table:
        lines.append(f"#define ASSET_{name}_OFF  {o}u")
        lines.append(f"#define ASSET_{name}_LEN  {n}u")
    lines += ["", "#endif", ""]
    (GEN / "asset_table.h").write_text("\n".join(lines), encoding="utf-8")

    print("  assets.bin: %d bytes" % len(blob))
    for name, o, n in table:
        print(f"    {name:8s} off={o:6d} len={n:6d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
