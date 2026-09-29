/* disk.c - ProDOS MLI READ_BLOCK streaming for the Apple II VERA port.
 *
 * Assets are packed into a single assets.bin at a fixed block range, read
 * directly through MLI (no pathname parsing).  Adapted from x16-hero-vera
 * src/disk.c, which is itself adapted from TimePilot-IIvera.
 */
#include <stdint.h>
#include "apple2e.h"
#include "disk.h"

extern uint8_t mli_unit;
extern uint8_t mli_buf_lo, mli_buf_hi;
extern uint8_t mli_blk_lo, mli_blk_hi;
extern uint8_t mli_status;
extern void mlib_read_block(void);
extern void mlib_write_block(void);

/* Dedicated 512-byte streaming buffer (Text Page 2, free under ProDOS). */
static uint8_t *const diskBuf = (uint8_t *)0x0800;

static uint16_t cached_abs_block = 0xFFFF;
static uint8_t  boot_unit = 0;

static void read_abs_block(uint16_t abs_block) {
    mli_unit    = boot_unit;
    mli_buf_lo  = (uint8_t)((uint16_t)diskBuf);
    mli_buf_hi  = (uint8_t)((uint16_t)diskBuf >> 8);
    mli_blk_lo  = (uint8_t)abs_block;
    mli_blk_hi  = (uint8_t)(abs_block >> 8);
    mlib_read_block();
    cached_abs_block = abs_block;
}

void disk_init(void) {
    boot_unit = *(volatile uint8_t *)0xBF30;   /* ProDOS global page: boot unit */
    cached_abs_block = 0xFFFF;
}

static uint8_t *disk_asset(uint32_t offset) {
    uint16_t abs = (uint16_t)(ASSET_START_BLOCK + offset / BLOCK_BYTES);
    if (abs != cached_abs_block) read_abs_block(abs);
    return &diskBuf[offset & (BLOCK_BYTES - 1)];
}

void disk_copy_to_vram(uint32_t offset, uint16_t vram_addr, uint16_t length, uint8_t bank) {
    vera_set_addr(bank ? VERA_INC_BANK1 : VERA_INC_BANK0, vram_addr);
    uint32_t off = 0;
    while (off < length) {
        uint8_t *chunk = disk_asset(offset + off);
        uint16_t in_blk = BLOCK_BYTES - (uint16_t)((offset + off) & (BLOCK_BYTES - 1));
        uint32_t rem = length - off;
        uint16_t n = (rem < in_blk) ? (uint16_t)rem : in_blk;
        for (uint16_t i = 0; i < n; i++) VERA.data0 = chunk[i];
        off += n;
    }
}

void disk_copy_to_ram(uint32_t offset, uint8_t *dest, uint16_t length) {
    uint32_t off = 0;
    while (off < length) {
        uint8_t *chunk = disk_asset(offset + off);
        uint16_t in_blk = BLOCK_BYTES - (uint16_t)((offset + off) & (BLOCK_BYTES - 1));
        uint32_t rem = length - off;
        uint16_t n = (rem < in_blk) ? (uint16_t)rem : in_blk;
        for (uint16_t i = 0; i < n; i++) dest[off + i] = chunk[i];
        off += n;
    }
}

uint8_t disk_read_music_block(uint16_t block, uint8_t *buf) {
    read_abs_block((uint16_t)(MUSIC_START_BLOCK + block));
    mli_unit    = boot_unit;
    mli_buf_lo  = (uint8_t)((uint16_t)buf);
    mli_buf_hi  = (uint8_t)((uint16_t)buf >> 8);
    mli_blk_lo  = (uint8_t)(MUSIC_START_BLOCK + block);
    mli_blk_hi  = (uint8_t)((MUSIC_START_BLOCK + block) >> 8);
    mlib_read_block();
    return mli_status;
}
