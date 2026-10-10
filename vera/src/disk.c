/* ProDOS file-level streaming for the Apple II VERA port. */
#include <stdint.h>
#include "apple2e.h"
#include "disk.h"
#include "asset_table.h"

extern uint8_t mli_status;
extern uint8_t mli_open_params[], mli_close_params[], mli_read_params[];
extern uint8_t mli_mark_params[], mli_prefix_params[], mli_eof_params[];
extern void mlib_open(void), mlib_close(void), mlib_read(void);
extern void mlib_set_mark(void), mlib_get_prefix(void), mlib_get_eof(void);

#define PATH_BUFFER       ((uint8_t *)0x0B00)
#define FILE_BUFFER       ((uint8_t *)0x0C00)
#define ASSET_IO_BUFFER   0x6000u
#define MUSIC_IO_BUFFER   0x6400u
#define MARKER_IO_BUFFER  0x6800u
#define FILE_BUFFER_SIZE  512u

static uint8_t asset_ref, music_ref;
static uint8_t asset_open, music_open, io_error;
static uint8_t failure_stage;
static uint8_t failure_status;
static uint32_t asset_size, music_size;
static uint16_t cached_asset_block = 0xFFFF;

static uint32_t get_eof(void) {
    return (uint32_t)mli_eof_params[2]
         | ((uint32_t)mli_eof_params[3] << 8)
         | ((uint32_t)mli_eof_params[4] << 16);
}

static uint8_t open_file(const char *name, uint16_t io_buffer,
                         uint8_t *ref, uint32_t *size) {
    uint8_t i, len;
    uintptr_t p = (uintptr_t)PATH_BUFFER;
    mli_prefix_params[1] = (uint8_t)p;
    mli_prefix_params[2] = (uint8_t)(p >> 8);
    mlib_get_prefix();
    if (mli_status) { failure_stage = 5; failure_status = mli_status; return 0; }
    if (PATH_BUFFER[0] > 63u) { failure_stage = 6; return 0; }
    len = PATH_BUFFER[0];
    /* ProDOS boots BASIC.SYSTEM with an empty prefix on the packaged volume.
       Use this HDV's known volume name in that case. File-level installs set
       their target prefix and therefore take the normal path below. */
    if (!len) {
        static const char boot_volume[] = "/CX16.OTHELLO";
        for (i = 0; boot_volume[i]; ++i) PATH_BUFFER[i + 1u] = (uint8_t)boot_volume[i];
        len = (uint8_t)i;
    }
    if (len && PATH_BUFFER[len] != '/') {
        if (len >= 63u) { failure_stage = 6; return 0; }
        PATH_BUFFER[++len] = '/';
    }
    for (i = 0; name[i]; ++i) {
        if (len >= 64u) { failure_stage = 6; return 0; }
        PATH_BUFFER[++len] = (uint8_t)name[i];
    }
    PATH_BUFFER[0] = len;

    p = (uintptr_t)PATH_BUFFER;
    mli_open_params[1] = (uint8_t)p;
    mli_open_params[2] = (uint8_t)(p >> 8);
    mli_open_params[3] = (uint8_t)io_buffer;
    mli_open_params[4] = (uint8_t)(io_buffer >> 8);
    mli_open_params[5] = 0;
    mlib_open();
    if (mli_status) { failure_stage = 7; failure_status = mli_status; return 0; }
    *ref = mli_open_params[5];

    mli_eof_params[1] = *ref;
    mlib_get_eof();
    if (mli_status) {
        failure_stage = 8; failure_status = mli_status;
        mli_close_params[1] = *ref;
        mlib_close();
        return 0;
    }
    *size = get_eof();
    return 1;
}

static uint8_t read_at(uint8_t ref, uint32_t offset, uint16_t count,
                       uint8_t *dest) {
    uintptr_t p = (uintptr_t)dest;
    mli_mark_params[1] = ref;
    mli_mark_params[2] = (uint8_t)offset;
    mli_mark_params[3] = (uint8_t)(offset >> 8);
    mli_mark_params[4] = (uint8_t)(offset >> 16);
    mlib_set_mark();
    if (mli_status) return 0;

    mli_read_params[1] = ref;
    mli_read_params[2] = (uint8_t)p;
    mli_read_params[3] = (uint8_t)(p >> 8);
    mli_read_params[4] = (uint8_t)count;
    mli_read_params[5] = (uint8_t)(count >> 8);
    mli_read_params[6] = mli_read_params[7] = 0;
    mlib_read();
    return mli_status == 0
        && mli_read_params[6] == (uint8_t)count
        && mli_read_params[7] == (uint8_t)(count >> 8);
}

static uint8_t load_asset_block(uint16_t block) {
    uint32_t offset = (uint32_t)block * FILE_BUFFER_SIZE;
    uint16_t count = (asset_size - offset > FILE_BUFFER_SIZE)
                   ? FILE_BUFFER_SIZE : (uint16_t)(asset_size - offset);
    if (!read_at(asset_ref, offset, count, FILE_BUFFER)) {
        io_error = 1;
        return 0;
    }
    cached_asset_block = block;
    return 1;
}

uint8_t disk_init(void) {
    uint8_t marker_ref;
    uint32_t marker_size;
    asset_open = music_open = io_error = failure_stage = failure_status = 0;
    cached_asset_block = 0xFFFF;

    /* A dedicated marker prevents unrelated ASSETS.BIN files from another
       ProDOS directory from being mistaken for this game. */
    if (!open_file("OTHELLO", MARKER_IO_BUFFER, &marker_ref, &marker_size)) {
        if (!failure_stage) failure_stage = 1;
        return 0;
    }
    mli_close_params[1] = marker_ref;
    mlib_close();
    if (mli_status) { failure_stage = 2; return 0; }

    if (!open_file("ASSETS.BIN", ASSET_IO_BUFFER, &asset_ref, &asset_size)) {
        if (!failure_stage) failure_stage = 3;
        return 0;
    }
    if (asset_size < ASSETS_SIZE) { failure_stage = 4; return 0; }
    asset_open = 1;
    return 1;
}

uint8_t disk_error(void) { return io_error; }
uint8_t disk_failure_stage(void) { return failure_stage; }
uint8_t disk_failure_status(void) { return failure_status; }

static uint8_t *asset_bytes(uint32_t offset) {
    uint16_t block = (uint16_t)(offset / FILE_BUFFER_SIZE);
    if (!asset_open || offset >= asset_size) {
        io_error = 1;
        return FILE_BUFFER;
    }
    if (block != cached_asset_block && !load_asset_block(block))
        return FILE_BUFFER;
    return FILE_BUFFER + (offset & (FILE_BUFFER_SIZE - 1u));
}

void disk_copy_to_vram(uint32_t offset, uint16_t vram_addr,
                       uint16_t length, uint8_t bank) {
    uint32_t copied = 0;
    vera_set_addr(bank ? VERA_INC_BANK1 : VERA_INC_BANK0, vram_addr);
    while (copied < length) {
        uint8_t *src = asset_bytes(offset + copied);
        uint16_t in_block = (uint16_t)((offset + copied) & (FILE_BUFFER_SIZE - 1u));
        uint16_t n = FILE_BUFFER_SIZE - in_block;
        if ((uint32_t)n > (uint32_t)length - copied)
            n = (uint16_t)((uint32_t)length - copied);
        for (uint16_t i = 0; i < n; ++i) VERA.data0 = src[i];
        copied += n;
    }
}

void disk_copy_to_ram(uint32_t offset, uint8_t *dest, uint16_t length) {
    uint32_t copied = 0;
    while (copied < length) {
        uint8_t *src = asset_bytes(offset + copied);
        uint16_t in_block = (uint16_t)((offset + copied) & (FILE_BUFFER_SIZE - 1u));
        uint16_t n = FILE_BUFFER_SIZE - in_block;
        if ((uint32_t)n > (uint32_t)length - copied)
            n = (uint16_t)((uint32_t)length - copied);
        for (uint16_t i = 0; i < n; ++i) dest[copied + i] = src[i];
        copied += n;
    }
}

uint8_t disk_read_music_block(uint16_t block, uint8_t *buf) {
    uint32_t offset = (uint32_t)block * FILE_BUFFER_SIZE;
    uint16_t count;
    if (!music_open) {
        if (!open_file("MUSIC.PSG", MUSIC_IO_BUFFER, &music_ref, &music_size)) {
            io_error = 1;
            return mli_status ? mli_status : 0x4C;
        }
        music_open = 1;
    }
    if (offset >= music_size) {
        io_error = 1;
        return 0x4C;
    }
    count = music_size - offset > FILE_BUFFER_SIZE
          ? FILE_BUFFER_SIZE : (uint16_t)(music_size - offset);
    if (!read_at(music_ref, offset, count, buf)) {
        io_error = 1;
        return mli_status ? mli_status : 0x4C;
    }
    return 0;
}
