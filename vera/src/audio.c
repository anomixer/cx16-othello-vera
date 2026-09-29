/* audio.c - VERA 16-channel PSG engine for the Apple II VERA port.
 *
 * Background music is the X16 ZSM stream converted to a compact PSG register
 * stream (tools/zsm2psg.py).  It is streamed from the HDV one 512-byte block
 * at a time and decoded at 60 Hz:
 *
 *   0x80|reg, value   write VERA PSG register `reg`
 *   0x00..0xFE        wait this many frames
 *   0xFF              end of stream -> seek to the loop offset
 *
 * The stone "thumb" click is the same format, embedded in the binary.
 */
#include <stdint.h>
#include "apple2e.h"
#include "audio.h"
#include "constants.h"
#include "disk.h"
#include "music_table.h"
#include "sfx_table.h"

#define PSG_BASE        0xF9C0          /* bank 1 -> $1F9C0 */

/* Streaming buffer: Text Page 2 area, free under ProDOS. */
static uint8_t *const musicBuf = (uint8_t *)0x0C00;

uint8_t music = YES;

static uint8_t  musicActive;
static uint8_t  musicDelay;
static uint16_t musicBlock;
static uint16_t musicPos;

static uint8_t  sfxActive;
static uint8_t  sfxDelay;
static uint16_t sfxPos;

extern const uint8_t sfx_thumb[];      /* provided by src/sfx_data.c */

/* ---------------------------- PSG primitives ---------------------------- */
static void psg_reg(uint8_t reg, uint8_t val) {
    VERA.control = 0;
    vera_set_addr(VERA_INC_BANK1, (uint16_t)(PSG_BASE + reg));
    VERA.data0 = val;
}

static void psg_silence_all(void) {
    VERA.control = 0;
    vera_set_addr(VERA_INC_BANK1, PSG_BASE);
    for (uint8_t i = 0; i < 64; i++) VERA.data0 = 0x00;
}

/* ------------------------------ music stream ---------------------------- */
static void music_seek(uint16_t offset) {
    musicBlock = offset / BLOCK_BYTES;
    musicPos   = offset % BLOCK_BYTES;
    disk_read_music_block(musicBlock, musicBuf);
}

static void music_start(void) {
    musicActive = 1;
    musicDelay  = 0;
    music_seek(0);
}

void audio_start_music(void) {
    if (music) music_start();
}

void audio_stop_music(void) {
    musicActive = 0;
    musicDelay  = 0;
    /* Silence only the music voices (4..15) plus the re-voiced FM channels,
     * leaving the sound-effect voice alone. */
    for (uint8_t v = 0; v < 16; v++) {
        if (v == 3) continue;
        psg_reg((uint8_t)(v * 4 + 2), 0x00);
    }
}

static uint8_t music_peek(void) {
    if (musicPos >= BLOCK_BYTES) {
        musicPos = 0;
        musicBlock++;
        disk_read_music_block(musicBlock, musicBuf);
    }
    return musicBuf[musicPos++];
}

static void music_tick(void) {
    if (!musicActive) return;
    if (musicDelay) { musicDelay--; return; }

    for (;;) {
        uint8_t b = music_peek();
        if (b == 0xFF) {                       /* end: loop */
            music_seek(MUSIC_LOOP);
            continue;
        }
        if (b >= 0x80) {
            psg_reg((uint8_t)(b & 0x3F), music_peek());
            continue;
        }
        musicDelay = b;
        return;
    }
}

/* ------------------------------- sound fx ------------------------------- */
void audio_play_thumb(void) {
    sfxActive = 1;
    sfxDelay  = 0;
    sfxPos    = 0;
}

static void sfx_tick(void) {
    if (!sfxActive) return;
    if (sfxDelay) { sfxDelay--; return; }

    for (;;) {
        if (sfxPos >= SFX_THUMB_SIZE) { sfxActive = 0; return; }
        uint8_t b = sfx_thumb[sfxPos++];
        if (b == 0xFF) { sfxActive = 0; return; }
        if (b >= 0x80) {
            psg_reg((uint8_t)(b & 0x3F), sfx_thumb[sfxPos++]);
            continue;
        }
        sfxDelay = b;
        return;
    }
}

/* -------------------------------- service ------------------------------- */
void audio_service(void) {
    sfx_tick();
    if (music) music_tick();
}

void audio_init(void) {
    musicActive = 0;
    musicDelay  = 0;
    sfxActive   = 0;
    sfxDelay    = 0;
    sfxPos      = 0;
    psg_silence_all();
}
