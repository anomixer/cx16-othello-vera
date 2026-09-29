/* disk.h - ProDOS fixed-block asset streaming for the Apple II VERA port.
 *
 * All game data lives at fixed block ranges in the HDV (kept in sync with
 * tools/build_hdv.py) and is read with MLI READ_BLOCK, so no ProDOS pathname
 * parsing is needed at runtime.  Adapted from TimePilot-IIvera / x16-hero-vera.
 */
#ifndef _DISK_H
#define _DISK_H

#include <stdint.h>

#define BLOCK_BYTES         512
#define ASSET_START_BLOCK   900     /* assets.bin  (42912 B  =  84 blocks)  */
#define MUSIC_START_BLOCK   1000    /* music.psg   (155428 B = 304 blocks)  */

void     disk_init(void);
void     disk_copy_to_vram(uint32_t offset, uint16_t vram_addr, uint16_t length, uint8_t bank);
void     disk_copy_to_ram(uint32_t offset, uint8_t *dest, uint16_t length);

/* Stream one 512-byte music block into `buf` (music offsets are block based). */
uint8_t  disk_read_music_block(uint16_t block, uint8_t *buf);

#endif /* _DISK_H */
