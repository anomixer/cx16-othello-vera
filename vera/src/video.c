/* video.c - VERA tile/sprite rendering layer for the Apple II VERA port.
 *
 * Port of the Commander X16 original by Ivo Filot (GPL v3).  The VERA layer,
 * tilemap, palette and sprite code is unchanged; assets are streamed from the
 * ProDOS image and the frame clock comes from the VERA VSYNC flag.
 */
#include "video.h"
#include "audio.h"
#include "disk.h"
#include "input.h"
#include "asset_table.h"

extern uint8_t background_scroll;
extern uint8_t boardsize;
extern uint8_t board_offset_x;
extern uint8_t board_offset_y;
extern uint8_t board_type;
extern uint8_t stone_color1;
extern uint8_t stone_color2;

uint32_t frameCount = 0;
static uint8_t bgscroll = 0;

static uint8_t *const helpBuf = (uint8_t *)0x0E00;   /* 6 x 240 bytes */

uint32_t frame_ms(void) {
    return (uint32_t)((frameCount * 1000UL) / 60UL);
}

/* ------------------------------ frame pump ------------------------------ */
void pump_frame(void) {
    VERA.irq_flags = VERA_IRQ_VSYNC;
    while ((VERA.irq_flags & VERA_IRQ_VSYNC) == 0) { }
    VERA.irq_flags = VERA_IRQ_VSYNC;
    frameCount++;

    input_update();
    audio_service();
    update_background_diagonal();

    /* The X16 kernel moved the hardware mouse pointer; here sprite 0 is the
     * pointer and we move it ourselves. */
    if (input_mouse_present()) {
        int16_t mx, my;
        uint8_t buttons;
        input_mouse(&mx, &my, &buttons);
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        set_sprite_px(SPRITE_MOUSE_CURSOR, (uint16_t)my, (uint16_t)mx);
    }
}

/* ------------------------------ screen init ----------------------------- */
void init_screen(void) {
    VERA.control = 0x00;                 /* ADDRSEL = 0, DCSEL = 0 */
    VERA.display.video = 0x00;           /* all layers off while setting up */
    VERA.display.border = 0x00;
    /* The game renders a 320x240 source (20 x 15 tiles of 16x16) and the
     * composer magnifies it 2:1 so it fills the whole 640x480 screen.
     * The Apple II VERA (VidHD) composer treats 0x80 as 1:1 and 0x40 as 2:1
     * magnification -- the X16's 0x40 = 1:1 convention does not apply here.
     * Keeping the source at 320x240 is what makes the layout cheap (the tile
     * maps, the 20-column help pages, the score panel at column 20 and the
     * Apple Mouse Card clamp in mouse.s are all 320x240 based); the 2:1 step is
     * pure composer work, so the game never touches more pixels than it needs. */
    VERA.display.hscale = 0x40;          /* 320 -> 640 */
    VERA.display.vscale = 0x40;          /* 240 -> 480 */
    /* hstart/hstop/vstart/vstop are left at the reset defaults (0 .. 640x480),
     * so the magnified frame fills the whole screen.  Writing the DCSEL = 1
     * window registers is deliberately avoided: every write to the composer
     * window ($C209-$C20C) makes the renderer do a midline pass, and that
     * disturbs the VSYNC/raster timing the MLI streaming loop relies on. */

    VERA.layer0.config   = 0x03;                        /* tile mode, 8 bpp, 16x16 */
    VERA.layer0.tilebase = (uint8_t)((TILEBASE >> 11) | 0x03);
    VERA.layer0.mapbase  = (uint8_t)(MAPBASE0 >> 9);
    VERA.layer0.hscroll  = 0;
    VERA.layer0.vscroll  = 0;

    VERA.layer1.config   = 0x03;
    VERA.layer1.tilebase = (uint8_t)((TILEBASE >> 11) | 0x03);
    VERA.layer1.mapbase  = (uint8_t)(MAPBASE1 >> 9);
    VERA.layer1.hscroll  = 0;
    VERA.layer1.vscroll  = 0;

    /* This VERA card (VidHD style) uses bit0 = video out, bit4 = layer 0,
     * bit5 = layer 1, bit6 = sprites - not the X16 bit layout. */
    VERA.display.video = 0x71;
}

void load_assets(void) {
    disk_copy_to_vram(ASSET_TILES_OFF,   TILEBASE,    ASSET_TILES_LEN,   0);
    disk_copy_to_vram(ASSET_FONT_OFF,    FONTBASE,    ASSET_FONT_LEN,    0);
    disk_copy_to_vram(ASSET_PALETTE_OFF, PALETTEBASE, ASSET_PALETTE_LEN, 1);
    disk_copy_to_ram(ASSET_HELP_OFF,     helpBuf,     ASSET_HELP_LEN);
    reset_sprites();
    set_mouse_pointer(TILE_MOUSE_CURSOR);
}

/* ------------------------------- tilemaps ------------------------------- */
/* ------------------------------ tilemaps --------------------------------
 * The full 32x32 map is always filled.  The renderer builds a 640-pixel layer
 * line from the map with the scroll applied in *output* pixels
 * ((x + hscroll) & (mapw*tilew - 1)), so the diagonal background scroll reads
 * tiles from the whole map, wrapping at 32 tiles.  Filling only the visible
 * 20x15 area leaves the rest stale and the scrolled edge shows the border
 * colour through the transparent tile index 0. */
void set_background(uint8_t tile_id) {
    vera_set_addr(VERA_INC_BANK0, MAPBASE0);
    for (uint16_t i = 0; i < MAPWIDTH * MAPHEIGHT; i++) {
        VERA.data0 = tile_id;
        VERA.data0 = PALETTEBYTE;
    }
}

void clear_foreground(void) {
    vera_set_addr(VERA_INC_BANK0, MAPBASE1);
    for (uint16_t i = 0; i < MAPWIDTH * MAPHEIGHT; i++) {
        VERA.data0 = TILE_NONE;
        VERA.data0 = PALETTEBYTE;
    }
}

void clear_screen(void) {
    set_background(TILE_BG);
    clear_foreground();
}

void set_tile(uint8_t y, uint8_t x, uint8_t tile_id, uint8_t tile_data) {
    vera_set_addr(VERA_INC_BANK0, (uint16_t)(MAPBASE1 + (y * MAPWIDTH + x) * 2));
    VERA.data0 = tile_id;
    VERA.data0 = tile_data;
}

void set_bgtile(uint8_t y, uint8_t x, uint8_t tile_id, uint8_t tile_data) {
    vera_set_addr(VERA_INC_BANK0, (uint16_t)(MAPBASE0 + (y * MAPWIDTH + x) * 2));
    VERA.data0 = tile_id;
    VERA.data0 = tile_data;
}

/* Write at most `n` characters of a fixed-width field.  The help pages are
 * 20-character rows packed back to back in a 240-byte block with no NUL
 * between them, so an unbounded strlen() write runs off the end of the row,
 * wraps at the 32-tile map stride and lands on the following rows -- that is
 * what makes the text overflow into the (N) NEXT / (ESC) BACK lines. */
void write_string_n(const char *str, uint8_t n, uint8_t y, uint8_t x) {
    const uint8_t *s = (const uint8_t *)str;
    uint8_t i = 0;

    if (x >= MAPWIDTH) return;
    if (n > (uint8_t)(MAPWIDTH - x)) n = (uint8_t)(MAPWIDTH - x);

    vera_set_addr(VERA_INC_BANK0, (uint16_t)(MAPBASE1 + (y * MAPWIDTH + x) * 2));
    while (i < n && *s) {
        VERA.data0 = (*s >= ' ' && *s <= '~') ? (uint8_t)(*s - 0x20 + 0x40) : 0x40;
        VERA.data0 = 0x00;
        s++;
        i++;
    }
}

void write_string(const char *str, uint8_t y, uint8_t x) {
    write_string_n(str, 0xFF, y, x);
}

void build_board(uint8_t size, uint8_t offset_y, uint8_t offset_x) {
    uint8_t i, j;

    for (j = offset_y; j < offset_y + size; j++) {
        vera_set_addr(VERA_INC_BANK0, (uint16_t)(MAPBASE1 + (j * MAPWIDTH + offset_x) * 2));
        for (i = 0; i < size; i++) {
            VERA.data0 = (uint8_t)(board_type + (((j + i) & 1) ? TILE_BC2 : TILE_BC1));
            VERA.data0 = PALETTEBYTE;
        }
    }

    for (i = 0; i < size; i++) {
        set_tile(offset_y - 1,      offset_x + i,   board_type + TILE_BEDGT,  PALETTEBYTE);
        set_tile(offset_y + i,      offset_x - 1,   board_type + TILE_BEDGL,  PALETTEBYTE);
        set_tile(offset_y + size,   offset_x + i,   board_type + TILE_BEDGB,  PALETTEBYTE);
        set_tile(offset_y + i,      offset_x + size,board_type + TILE_BEDGR,  PALETTEBYTE);
        set_tile(offset_y - 1,      offset_x - 1,   board_type + TILE_BEDGLT, PALETTEBYTE);
        set_tile(offset_y - 1,      offset_x + size,board_type + TILE_BEDGRT, PALETTEBYTE);
        set_tile(offset_y + size,   offset_x - 1,   board_type + TILE_BEDGBL, PALETTEBYTE);
        set_tile(offset_y + size,   offset_x + size,board_type + TILE_BEDGBR, PALETTEBYTE);
    }
}

void build_playfield(void) {
    clear_screen();
    build_board(boardsize, board_offset_y, board_offset_x);
    write_string("TURN", 14, 0);
    write_string("SCORE", 0, 15);
    set_tile(1, 16, stone_color1, PALETTEBYTE);
    set_tile(2, 16, stone_color2, PALETTEBYTE);
}

/* ------------------------------ help pages ------------------------------ */
void load_help_page(uint8_t page) {
    const uint8_t *src = helpBuf + (uint16_t)page * HELP_PAGE_BYTES;
    for (uint8_t y = 1; y < 12; y++) {
        write_string_n((const char *)src, HELP_PAGE_COLS, y, 0);
        src += HELP_PAGE_COLS;
    }
}

/* ---------------------------- background scroll -------------------------- */
void update_background_diagonal(void) {
    if (!background_scroll) return;
    bgscroll = (uint8_t)((bgscroll - 1) & 0x0F);
    VERA.layer0.hscroll = bgscroll;
    VERA.layer0.vscroll = bgscroll;
}

/* -------------------------------- sprites ------------------------------- */
void assign_sprite(uint8_t sprite_id, uint8_t tile_id) {
    uint16_t sprite_addr = (uint16_t)(SPRITEBASE + ((uint16_t)sprite_id << 3));
    uint16_t graph_addr  = (uint16_t)(TILEBASE + ((uint16_t)tile_id << 8));

    vera_set_addr(VERA_INC_BANK1, sprite_addr);
    VERA.data0 = (uint8_t)(graph_addr >> 5);
    VERA.data0 = (uint8_t)((graph_addr >> 13) | (1 << 7));   /* 8 bpp pattern */
    VERA.data0 = 16;                       /* x low  */
    VERA.data0 = 0x00;                     /* x high */
    VERA.data0 = 16;                       /* y low  */
    VERA.data0 = 0x00;                     /* y high */
    VERA.data0 = 0b00001100;               /* z-depth 3, no flip, no collide */
    VERA.data0 = 0b01010000;               /* 16x16, palette 0 */
}

void set_sprite(uint8_t sprite_id, uint8_t posy, uint8_t posx) {
    set_sprite_px(sprite_id, (uint16_t)posy << 4, (uint16_t)posx << 4);
}

void set_sprite_px(uint8_t sprite_id, uint16_t y, uint16_t x) {
    uint16_t sprite_addr = (uint16_t)(SPRITEBASE + ((uint16_t)sprite_id << 3) | 2);
    vera_set_addr(VERA_INC_BANK1, sprite_addr);
    VERA.data0 = (uint8_t)(x & 0xFF);
    VERA.data0 = (uint8_t)(x >> 8);
    VERA.data0 = (uint8_t)(y & 0xFF);
    VERA.data0 = (uint8_t)(y >> 8);
}

void reset_sprites(void) {
    vera_set_addr(VERA_INC_BANK1, SPRITEBASE);
    for (uint8_t i = 0; i < 128; i++) {
        VERA.data0 = 0x00;
        VERA.data0 = 0x00;
        VERA.data0 = 16;
        VERA.data0 = 0x00;
        VERA.data0 = 16;
        VERA.data0 = 0x00;
        VERA.data0 = 0b00000000;           /* z-depth 0 -> hidden */
        VERA.data0 = 0b01010000;
    }
}

void set_mouse_pointer(uint8_t tile_id) {
    uint16_t sprite_addr = SPRITEBASE;
    uint16_t graph_addr  = (uint16_t)(TILEBASE + ((uint16_t)tile_id << 8));

    vera_set_addr(VERA_INC_BANK1, sprite_addr);
    VERA.data0 = (uint8_t)(graph_addr >> 5);
    VERA.data0 = (uint8_t)((graph_addr >> 13) | (1 << 7));
}
