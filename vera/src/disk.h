/* ProDOS file-level streaming for the Apple II VERA port. */
#ifndef _DISK_H
#define _DISK_H

#include <stdint.h>

#define BLOCK_BYTES         512

uint8_t  disk_init(void);
uint8_t  disk_error(void);
uint8_t  disk_failure_stage(void);
uint8_t  disk_failure_status(void);
void     disk_copy_to_vram(uint32_t offset, uint16_t vram_addr, uint16_t length, uint8_t bank);
void     disk_copy_to_ram(uint32_t offset, uint8_t *dest, uint16_t length);

/* Read a 512-byte window from a music stream. `name` is "MUSIC.PSG" on cards
 * without an FM chip and "MUSIC.VZM" when the YM2151 is fitted; the file is
 * opened lazily and re-opened only if the name changes. */
uint8_t  disk_read_music_block(uint16_t block, uint8_t *buf, const char *name);

/* Clear the sticky I/O error flag (used when a missing optional stream is
 * replaced by its fallback and playback continues normally). */
void     disk_clear_error(void);

#endif /* _DISK_H */
