/**************************************************************************
 *
 *   CX16-OTHELLO - Apple II VERA port
 *
 *   Platform constants.  The VERA register semantics, tile layout, sprite
 *   layout and palette indices are identical to the Commander X16 original;
 *   only the keyboard codes and the asset/VRAM placement differ.
 *
 *   Based on the original work by Ivo Filot <ivo@ivofilot.nl>, licensed under
 *   the GNU General Public License version 3.
 *
 **************************************************************************/

#ifndef _CONSTANTS_H
#define _CONSTANTS_H

/* ---------------- VERA VRAM layout (bank 0) ----------------
 * $0:0000-$3FFF  64 game tiles      (16x16, 8bpp)
 * $0:4000-$9FFF  96 font tiles      (16x16, 8bpp, tile index 64..159)
 * $0:A000-$A7FF  Layer 0 tilemap    (32x32)
 * $0:A800-$AFFF  Layer 1 tilemap    (32x32)
 * $0:B000-$F9BF  free
 * $1:F9C0-$F9FF  PSG registers
 * $1:FA00-$FBFF  palette
 * $1:FC00-$FFFF  128 sprite attributes
 */
#define TILEBASE            0x0000      /* 64 tiles at 8bpp 16x16 */
#define FONTBASE            0x4000      /* 96 tiles at 8bpp 16x16 */
#define MAPBASE0            0xA000      /* 32 x 32 tiles */
#define MAPBASE1            0xA800      /* 32 x 32 tiles */
#define PALETTEBASE         0xFA00      /* bank 1 -> $1FA00 */
#define PALETTEBYTE         0x00
#define SPRITEBASE          0xFC00      /* bank 1 -> $1FC00, 128 entries */

#define STONE_NONE          0x00
#define STONE_PLAYER1       0x01
#define STONE_PLAYER2       0x02

/* Game source geometry: 320x240, magnified 2:1 by the composer so it fills the
 * whole 640x480 screen. */
#define SCREEN_W            320
#define SCREEN_H            240

/* Tilemap grid.  The full map is always filled: the renderer applies the
 * background scroll in output pixels and wraps at mapw*tilew, so every tile
 * can end up on screen. */
#define MAPHEIGHT           32
#define MAPWIDTH            32

#define TILE_NONE           0x00
#define TILE_EMPTY          0x00
#define TILE_BG             0x01
#define TILE_EMPTY_CURSOR   0x02
#define TILE_BLACK          0x03
#define TILE_WHITE          0x04
#define TILE_MOUSE_CURSOR   0x0F
#define TILE_HIGHLIGHT      0x12

/* board offsets */
#define BOARD_WOOD          0x20
#define BOARD_STONE         0x30
#define TILE_BEDGLT         0x00
#define TILE_BEDGT          0x01
#define TILE_BEDGRT         0x02
#define TILE_BEDGL          0x03
#define TILE_BC1            0x04
#define TILE_BC2            0x05
#define TILE_BEDGR          0x06
#define TILE_BEDGBL         0x07
#define TILE_BEDGB          0x08
#define TILE_BEDGBR         0x09

/* sprites */
#define SPRITE_MOUSE_CURSOR 0x00
#define SPRITE_TILE_CURSOR  0x65
#define SPRITE_HIGHLIGHT    0x66
#define SPRITE_MAX          128

/* stone colors */
#define STONE_RED           0x03
#define STONE_TEAL          0x04
#define STONE_WHITE         0x05
#define STONE_GREY          0x06
#define STONE_YELLOW        0x07
#define STONE_GREEN         0x08
#define STONE_PURPLE        0x09
#define STONE_CYAN          0x0A
#define STONE_ORANGE        0x0B

/* Apple //e keyboard codes (KEYD, low 7 bits).
 * The X16 build used PETSCII arrow codes; the Apple II cursor keys are
 * 0x08 (left) 0x0B (up) 0x0A (down) 0x15 (right). */
#define KEYCODE_DOWN        0x0A
#define KEYCODE_UP          0x0B
#define KEYCODE_LEFT        0x08
#define KEYCODE_RIGHT       0x15
#define KEYCODE_RETURN      0x0D
#define KEYCODE_SPACE       0x20
#define KEYCODE_ESCAPE      0x1B

#define PLAYER_ONE          0x00
#define PLAYER_TWO          0x01

#define EDGEFIELD_INACTIVE  0x00
#define EDGEFIELD_ACTIVE    0x01

#define PROBE_NO            0x00
#define PROBE_YES           0x01

#define GAME_STOP           0x00
#define GAME_RUN            0x01
#define GAME_MENU           0x02
#define GAME_SETTINGS       0x03
#define GAME_HELP           0x04

#define PLAYER_HUMAN        0x00
#define PLAYER_CPU          0x01

#define COUNT_NO_GAME_END   0x00
#define COUNT_END_GAME      0x01

#define NO                  0x00
#define YES                 0x01

#define SCROLL_BACKGROUND   1
#define SCROLLSPEED         1       /* pixels per frame (X16: 1 per call) */

#define HELPPAGES           6
#define HELP_PAGE_COLS      20
#define HELP_PAGE_BYTES     240     /* 20 columns x 12 rows */

#endif /* _CONSTANTS_H */
