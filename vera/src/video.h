/* video.h - VERA tile/sprite rendering layer for the Apple II VERA port.
 *
 * The Commander X16 original already talks to VERA directly, so the layer,
 * tilemap, palette and sprite code ports unchanged; only the asset loading
 * (ProDOS instead of cbm_k_load) and the frame clock differ.
 */
#ifndef _VIDEO_H
#define _VIDEO_H

#include <stdint.h>
#include "apple2e.h"
#include "constants.h"

/* Frame clock: one tick per VERA VSYNC (60 Hz). */
extern uint32_t frameCount;
uint32_t frame_ms(void);
void     pump_frame(void);        /* wait VSYNC, service audio, scroll bg */

void init_screen(void);
void load_assets(void);
void set_background(uint8_t tile_id);
void clear_screen(void);
void clear_foreground(void);
void build_playfield(void);
void set_tile(uint8_t y, uint8_t x, uint8_t tile_id, uint8_t tile_data);
void set_bgtile(uint8_t y, uint8_t x, uint8_t tile_id, uint8_t tile_data);
void write_string(const char *str, uint8_t y, uint8_t x);
void write_string_n(const char *str, uint8_t n, uint8_t y, uint8_t x);
void build_board(uint8_t size, uint8_t offset_y, uint8_t offset_x);
void update_background_diagonal(void);
void assign_sprite(uint8_t sprite_id, uint8_t tile_id);
void set_sprite(uint8_t sprite_id, uint8_t posy, uint8_t posx);
void set_sprite_px(uint8_t sprite_id, uint16_t y, uint16_t x);
void reset_sprites(void);
void set_mouse_pointer(uint8_t tile_id);
void load_help_page(uint8_t page);

#endif /* _VIDEO_H */
