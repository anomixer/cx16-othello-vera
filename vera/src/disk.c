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
/* Page-aligned 1 KB ProDOS OPEN buffers.  They sit above the payload (which
 * grew past $6000 when the PCM player was added) and below the soft stack at
 * $BE00; link1000.ld asserts the payload stays clear of $7000. */
#define ASSET_IO_BUFFER   0x7000u
#define MUSIC_IO_BUFFER   0x7400u
#define MARKER_IO_BUFFER  0x7800u
#define FILE_BUFFER_SIZE  512u

static uint8_t asset_ref, music_ref;
static uint8_t asset_open, music_open, io_error;
static const char *music_name;
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
    music_name = 0;
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
void disk_clear_error(void) { io_error = 0; }
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

static uint8_t name_is(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

uint8_t disk_read_music_block(uint16_t block, uint8_t *buf, const char *name) {    uint32_t offset = (uint32_t)block * FILE_BUFFER_SIZE;
    uint16_t count;
    if (music_open && !name_is(music_name, name)) {
        mli_close_params[1] = music_ref;
        mlib_close();
        music_open = 0;
    }
    if (!music_open) {
        if (!open_file(name, MUSIC_IO_BUFFER, &music_ref, &music_size)) {
            io_error = 1;
            return mli_status ? mli_status : 0x4C;
        }
        music_name = name;
        music_open = 1;
    }
    if (offset >= music_size) {
        /* Reading one block past the end is how audio_refill() discovers EOF
         * while staging the next record; that is not an I/O error, so it must
         * not raise the sticky flag (which would pop the error screen). */
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
/* =============================================================================
 * Raw-block PCM streaming
 *
 * A file-level ProDOS READ costs ~22.7 ms whatever its size (the file manager
 * walks the Tree index on every call), which is far too much for 8,010 bytes/s
 * of PCM.  Resolving the file's block map once and then reading data blocks
 * with MLI READ_BLOCK ($80) costs 4.6 ms per 512-byte block, so the streamer can
 * feed one block in the spare tail of a frame instead of stalling a whole one.
 *
 * The map comes from the volume directory: block 2 holds the entries (39 bytes
 * each), and the entry's key pointer (+$11) is the master index block of a Tree
 * file.  Master entry n names the index block covering data blocks n*256.., and
 * that index block names the data blocks themselves.  Both index blocks are
 * cached, so a data read costs one MLI call and the cache reload happens once
 * per 128 KB.
 * ========================================================================== */

#define PCM_DIR_BLOCK        2u
#define PCM_ENTRY_SIZE       39u
#define PCM_ENTRY_KEY        0x11u      /* key pointer (word) inside an entry */
#define PCM_ENTRY_USED       0x13u      /* blocks used (word) */
#define STOR_SAPLING         2u
#define STOR_TREE            3u

extern uint8_t mli_unit, mli_buf_lo, mli_buf_hi, mli_blk_lo, mli_blk_hi, mli_status;
extern void mlib_read_block(void);

static uint8_t  pcm_unit;                  /* block device: slot*16 + drive */
static uint8_t  pcm_tree;                  /* 2 = sapling, 3 = tree */
static uint16_t pcm_key;                   /* first block / master index block */
static uint16_t pcm_used;                  /* blocks allocated to the file */
static uint16_t pcm_cached_slot = 0xFFFF;  /* master entry mirrored in index[] */
static uint8_t  pcm_map_ok;
static uint8_t  pcm_master[512] __attribute__((aligned(256)));
static uint8_t  pcm_index[512] __attribute__((aligned(256)));

/* MLI READ_BLOCK: one 512-byte block, no file manager, no SET_MARK. */
uint8_t disk_read_block_raw(uint8_t unit, uint16_t block, uint8_t *buf) {
    mli_unit = unit;
    mli_buf_lo = (uint8_t)(uintptr_t)buf;
    mli_buf_hi = (uint8_t)((uintptr_t)buf >> 8);
    mli_blk_lo = (uint8_t)block;
    mli_blk_hi = (uint8_t)(block >> 8);
    mlib_read_block();
    return mli_status == 0;
}

/* Case-insensitive compare of a 15-character directory name field. */
static uint8_t entry_is(const uint8_t *entry, const char *name) {
    uint8_t len = entry[0] & 0x0Fu;
    uint8_t i;

    if (!len) return 0;
    for (i = 0; i < len; i++) {
        char c = (char)entry[1u + i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c != name[i]) return 0;
    }
    return name[len] == 0;
}

/* Find the volume-directory entry for `name` and cache its block map.  Returns
 * the number of blocks allocated, or 0 when the file has no usable map. */
uint16_t disk_pcm_map_init(const char *name) {
    uint8_t tries;
    uint8_t *blk = pcm_master;             /* scratch until the master is read */

    pcm_map_ok = 0;
    pcm_cached_slot = 0xFFFF;
    pcm_unit = *(volatile uint8_t *)0xBF30u;   /* ProDOS: unit of the boot device */


    for (tries = 0; tries < 2; tries++) {
        uint16_t dir_block = PCM_DIR_BLOCK;
        uint8_t first = 1;
        uint8_t pages;

        if (tries) pcm_unit = 0x70u;       /* ProDOS: slot 7, drive 1 */
        if (pcm_unit == 0) continue;
        if (!disk_read_block_raw(pcm_unit, dir_block, blk)) continue;

        for (pages = 0; pages < 4; pages++) {
            uint16_t off = first ? 4u : 0u;

            /* A volume-directory key block starts with the volume entry, whose
             * storage type is $F; that also validates the device unit. */
            if (first && (blk[4] & 0xF0u) != 0xF0u) break;

            for (; off + PCM_ENTRY_SIZE <= 512u; off += PCM_ENTRY_SIZE) {
                uint8_t stor = (uint8_t)(blk[off] >> 4);
                if (stor != STOR_SAPLING && stor != STOR_TREE) continue;
                if (!entry_is(blk + off, name)) continue;

                pcm_tree = stor;
                pcm_key  = (uint16_t)(blk[off + PCM_ENTRY_KEY]
                           | ((uint16_t)blk[off + PCM_ENTRY_KEY + 1u] << 8));
                pcm_used = (uint16_t)(blk[off + PCM_ENTRY_USED]
                           | ((uint16_t)blk[off + PCM_ENTRY_USED + 1u] << 8));
                if (!pcm_key || !pcm_used) return 0;
                if (pcm_tree == STOR_TREE) {
                    if (!disk_read_block_raw(pcm_unit, pcm_key, pcm_master))
                        return 0;
                }
                pcm_map_ok = 1;

                return pcm_used;
            }

            dir_block = (uint16_t)(blk[2] | ((uint16_t)blk[3] << 8));
            first = 0;
            if (!dir_block) break;
            if (!disk_read_block_raw(pcm_unit, dir_block, blk)) break;
        }
    }
    return 0;
}

/* Read file block `block` (512 B, counted from the start of the file) into buf. */
uint8_t disk_pcm_read(uint16_t block, uint8_t *buf) {
    uint16_t slot;

    if (!pcm_map_ok) return 0;
    if (pcm_tree != STOR_TREE)
        return disk_read_block_raw(pcm_unit, (uint16_t)(pcm_key + block), buf);

    /* An index block holds its 256 pointers as two 256-byte halves: the low
     * bytes in $000-$0FF and the high bytes in $100-$1FF.  (Checked against the
     * packaged image: the master's entry 0 reads $A2/$03 -> block 930, and that
     * index block's entry 0 reads $AB/$03 -> block 939, the first PCM block.) */
    slot = (uint16_t)(block >> 8);
    if (slot != pcm_cached_slot) {
        uint16_t index_block = (uint16_t)(pcm_master[slot]
                               | ((uint16_t)pcm_master[256u + slot] << 8));
        if (!index_block) return 0;
        if (!disk_read_block_raw(pcm_unit, index_block, pcm_index)) return 0;
        pcm_cached_slot = slot;
    }
    {
        uint16_t j = (uint16_t)(block & 0xFFu);
        uint16_t data_block = (uint16_t)(pcm_index[j]
                               | ((uint16_t)pcm_index[256u + j] << 8));
        if (!data_block) return 0;
        return disk_read_block_raw(pcm_unit, data_block, buf);
    }
}








