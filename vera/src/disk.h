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

/* Read a 512-byte window from a streamed file. `name` is "OTHELLO.PCM" on
 * cards without an FM chip, "MUSIC.VZM" when the YM2151 is fitted and
 * "MUSIC.PSG" as the fallback; the file is opened lazily and re-opened only if
 * the name changes. */
uint8_t  disk_read_music_block(uint16_t block, uint8_t *buf, const char *name);

/* Raw-block PCM streaming (see disk.c).  disk_pcm_map_init() locates the
 * file's ProDOS block map through the volume directory and returns the number
 * of blocks allocated (0 = no usable map, keep using file-level reads).
 * disk_pcm_read() then loads one 512-byte block with a single MLI READ_BLOCK,
 * which is 5x cheaper than a file-level READ. */
uint16_t disk_pcm_map_init(const char *name);
uint8_t  disk_pcm_read(uint16_t block, uint8_t *buf);

uint8_t disk_read_block_raw(uint8_t unit, uint16_t block, uint8_t *buf);


/* Clear the sticky I/O error flag (used when a missing optional stream is
 * replaced by its fallback and playback continues normally). */
void     disk_clear_error(void);

#endif /* _DISK_H */





