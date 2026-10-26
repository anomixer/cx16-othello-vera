/* audio.c - VERA 16-channel PSG engine plus the optional YM2151 FM chip.
 *
 * Two build-time conversions of the X16 ZSM soundtrack are shipped:
 *
 *   MUSIC.PSG   PSG-only stream (tools/zsm2psg.py): the native PSG voices plus
 *               the FM arrangement re-voiced onto PSG voices 0..3.  Played on
 *               a card without the FM daughterboard (the original behaviour).
 *   MUSIC.VZM   PSG + YM2151 stream (tools/zsm2vera.mjs / gen_music.mjs): every
 *               PSG write and every FM register pair in original order, so the
 *               FM chip plays the tracker arrangement on top of the PSG voices.
 *
 * The stream is chosen once, at startup, from the FM-chip probe result:
 *
 *   audio_detect_fm()   -> fm_present
 *   audio_start_music() -> MUSIC.VZM when fm_present, else MUSIC.PSG
 *
 * Both are streamed from the HDV one 512-byte block at a time and decoded at
 * 60 Hz.  MUSIC.PSG:
 *
 *   0x80|reg, value   write VERA PSG register `reg`
 *   0x00..0xFE        wait this many frames
 *   0xFF              end of stream -> seek to the loop offset
 *
 * MUSIC.VZM (all records kept inside one 512-byte block):
 *
 *   0x00..0x7F        delay: N further frames after this tick
 *   0x80..0xBF        PSG write: reg = byte & 0x3F, then value
 *   0xC0..0xDF        FM batch: count = (byte & 0x1F) + 1, count x (reg, value)
 *   0xE0..0xFE        block-alignment padding, consumes no frame
 *   0xFF              end of stream -> seek to the loop offset
 *
 * The stone "thumb" click is the PSG stream format, embedded in the binary.
 */
#include <stdint.h>
#include "apple2e.h"
#include "audio.h"
#include "constants.h"
#include "disk.h"
#include "music_table.h"
#include "music_vzm.h"
#include "sfx_table.h"

#define PSG_BASE        0xF9C0          /* bank 1 -> $1F9C0 */

#define MUSIC_FILE_PSG  "MUSIC.PSG"
#define MUSIC_FILE_VZM  "MUSIC.VZM"

#define YM_KEY_REG      0x08            /* OPM key on/off: opmask 6:3, ch 2:0 */

/* Streaming buffer: Text Page 2 area, free under ProDOS. */
static uint8_t *const musicBuf = (uint8_t *)0x0C00;

uint8_t music = YES;
uint8_t fm_present = NO;

static uint8_t  musicActive;
static uint8_t  musicDelay;
static uint8_t  musicFm;                /* 1 = MUSIC.VZM (PSG + FM) */
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

/* --------------------------- YM2151 FM helpers -------------------------- */
static void fm_reg(uint8_t reg, uint8_t val) {
    VERA_YM_REG  = reg;
    VERA_YM_DATA = val;
}

static void fm_key_off_all(void) {
    if (!fm_present) return;
    for (uint8_t ch = 0; ch < 8; ch++) fm_reg(YM_KEY_REG, ch);
}

/* Reset every tone register to zero.  MUSIC.VZM is de-duplicated against an
 * all-zero YM2151 image, so the chip must be cleared before playback starts
 * and whenever the stream restarts from offset 0. */
static void fm_silence(void) {
    if (!fm_present) return;
    fm_key_off_all();
    for (uint16_t reg = 0x20; reg < 0x100; reg++) fm_reg((uint8_t)reg, 0x00);
}

/* Probe for the optional YM2151 FM daughterboard.
 *
 * The VERA register window decodes only five address bits, so on a card with no
 * FM chip $Cs20/$Cs21 alias ADDR_L/ADDR_M and read back what is written there.
 * An FM card instead returns the YM2151 register-select (write-only -> 0) and
 * the status byte, never the written pattern.  The VERA address latches are
 * saved and restored so the caller's VRAM pointer is untouched. */
void audio_detect_fm(void) {
    uint16_t saved_addr = VERA.address;
    uint8_t  saved_hi   = VERA.address_hi;
    uint8_t  saved_ctrl = VERA.control;

    VERA.control = 0x00;               /* ADDRSEL = 0: alias port 0 / port 1 */
    VERA.address = 0xA55A;             /* ADDR_L = $5A, ADDR_M = $A5 */
    fm_present = (VERA_YM_REG != 0x5A || VERA_YM_DATA != 0xA5) ? YES : NO;

    VERA.address    = saved_addr;
    VERA.address_hi = saved_hi;
    VERA.control    = saved_ctrl;
}

/* ------------------------------ music stream ---------------------------- */
static void music_seek(uint16_t offset) {
    musicBlock = offset / BLOCK_BYTES;
    musicPos   = offset % BLOCK_BYTES;
    disk_read_music_block(musicBlock, musicBuf,
                          musicFm ? MUSIC_FILE_VZM : MUSIC_FILE_PSG);
}

static void music_start(void) {
    musicActive = 1;
    musicDelay  = 0;
    music_seek(0);
}

void audio_start_music(void) {
    if (!music) return;
    musicFm = fm_present;
    music_start();
    if (musicFm && disk_error()) {
        /* Copied installation without MUSIC.VZM: keep the music alive on the
         * PSG-only stream instead of stopping at the error screen. */
        disk_clear_error();
        musicFm = 0;
        music_seek(0);
    }
}

void audio_stop_music(void) {
    musicActive = 0;
    musicDelay  = 0;
    /* Silence only the music voices (4..15) plus the re-voiced FM channels,
     * leaving the sound-effect voice alone; release any held FM note. */
    for (uint8_t v = 0; v < 16; v++) {
        if (v == 3) continue;
        psg_reg((uint8_t)(v * 4 + 2), 0x00);
    }
    fm_key_off_all();
}

static uint8_t music_peek(void) {
    if (musicPos >= BLOCK_BYTES) {
        musicPos = 0;
        musicBlock++;
        disk_read_music_block(musicBlock, musicBuf,
                              musicFm ? MUSIC_FILE_VZM : MUSIC_FILE_PSG);
    }
    return musicBuf[musicPos++];
}

/* MUSIC.PSG: 0x80|reg,value writes; 0x00..0xFE waits; 0xFF loops. */
static void music_tick_psg(void) {
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

/* MUSIC.VZM: PSG writes, YM2151 pair batches, delays and block padding. */
static void music_tick_vzm(void) {
    for (;;) {
        uint8_t b = music_peek();
        if (b == 0xFF) {
            if (MUSIC_VZM_LOOP) {
                /* The arrangement loops back to its authored point; register
                 * state carries over, exactly like the X16 stream. */
                music_seek((uint16_t)MUSIC_VZM_LOOP);
            } else {
                /* Looping from offset 0: the stream is compressed against an
                 * all-zero PSG/FM image, so both engines must be cleared. */
                psg_silence_all();
                fm_silence();
                music_seek(0);
            }
            continue;
        }
        if (b < 0x80) {                        /* delay: N further frames */
            musicDelay = b;
            return;
        }
        if (b < 0xC0) {                        /* PSG write */
            psg_reg((uint8_t)(b & 0x3F), music_peek());
            continue;
        }
        if (b < 0xE0) {                        /* FM register-pair batch */
            uint8_t count = (uint8_t)((b & 0x1F) + 1);
            while (count) {
                uint8_t reg = music_peek();
                uint8_t val = music_peek();
                if (fm_present) fm_reg(reg, val);
                count--;
            }
            continue;
        }
        /* 0xE0..0xFE: alignment padding, consumes no frame. */
    }
}

static void music_tick(void) {
    if (!musicActive) return;
    if (musicDelay) { musicDelay--; return; }
    if (musicFm) music_tick_vzm();
    else         music_tick_psg();
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
    musicFm     = 0;
    sfxActive   = 0;
    sfxDelay    = 0;
    sfxPos      = 0;
    psg_silence_all();
    fm_silence();
}
