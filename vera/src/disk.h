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

/* Read a 512-byte window from MUSIC.PSG. */
uint8_t  disk_read_music_block(uint16_t block, uint8_t *buf);

#endif /* _DISK_H */
