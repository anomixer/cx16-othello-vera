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
#include "pcm_table.h"
#include "sfx_table.h"

#define PSG_BASE        0xF9C0          /* bank 1 -> $1F9C0 */

#define MUSIC_FILE_PSG  "MUSIC.PSG"
#define MUSIC_FILE_VZM  "MUSIC.VZM"

#define YM_KEY_REG      0x08            /* OPM key on/off: opmask 6:3, ch 2:0 */

/* PCM audio (VERA card register window offsets $1B..$1D).
 *
 * OTHELLO.PCM is the whole soundtrack pre-rendered at 8 kHz: the build renders
 * assets/othello.zsm through melodius (YM2151 + PSG) to a 48 kHz WAV and then
 * band-limits it into 8-bit signed mono.  On a card without the FM chip this
 * replaces the 16-voice PSG arrangement, which cannot carry the soundtrack's
 * low end (see tools/gen_pcm.mjs). */
#define PCM_FILE        "OTHELLO.PCM"
#define PCM_RATE_REG    21              /* 21 * 48828.125/128 = 8010.86 Hz */
/* Volume 12 of 15, not 15: the card's PCM volume (control bits 3:0, lookup
 * {0,1,2,3,4,5,6,8,11,14,18,23,30,38,49,64}) is the only mix control the
 * pre-rendered stream has, and the PSG sound effects have none -- their thumbs
 * already run at volume $3F.  At full scale the music sat at a peak of 23,552
 * and buried every effect; 12 puts it near 11,000, which leaves the effects
 * (replayed on 8 free PSG voices in PCM mode) at the top of the mix. */
#define PCM_CTRL_RESET  0x8C            /* bit7 = FIFO reset, mono 8-bit, vol 12 */
#define PCM_CTRL_RUN    0x0C            /* same without the reset */
#define PCM_CHUNK       128             /* bytes per pcm_feed_asm() call */
#ifndef PCM_FEED_CHUNKS
#define PCM_FEED_CHUNKS 4               /* cap: 512 B (~64 ms) caught up per frame */
#endif

/* Streaming buffers.  Both records are one 512-byte ProDOS block; they are
 * alternated so the next block can be read into the idle one while the current
 * one still plays.  Buffer A is the Text Page 2 area (free under ProDOS);
 * buffer B is regular .bss, which keeps the payload well below $6000.  The PCM
 * feed loop addresses either buffer with absolute,Y and a patched operand, so
 * both must stay page aligned. */
static uint8_t *const musicBufA = (uint8_t *)0x0C00;
static uint8_t musicBufB[BLOCK_BYTES] __attribute__((aligned(BLOCK_BYTES)));

uint8_t music = YES;
uint8_t fm_present = NO;

/* Diagnostics (see the comment above pump_frame's VERA_DEBUG_STATS block). */
uint32_t musicTicks;
uint32_t musicPrefetches;
uint32_t musicFallbacks;

static uint8_t  musicActive;
static uint8_t  musicDelay;
static uint8_t  musicFm;                /* 1 = MUSIC.VZM (PSG + FM) */
static uint8_t *musicCur = musicBufA;   /* record currently being decoded */
static uint8_t  musicPre;               /* 1 = the other buffer holds block+1 */
static uint16_t musicBlock;             /* block index loaded into musicCur */
static uint16_t musicPos;               /* next byte inside musicCur */
static uint8_t  musicNeedPrefetch;      /* 1 = read block musicBlock+1 later */

/* PCM stream state.  The two music buffers double as the PCM staging buffers
 * because the PSG/VZM stream and the PCM stream are never active together. */
static uint8_t  pcmActive;
static uint16_t pcmBlocks;              /* total 512-byte blocks in OTHELLO.PCM */
static uint16_t pcmLastLen;             /* bytes in the final block */
static uint16_t pcmBlock;               /* block currently held in pcmCur */
static uint8_t *pcmCur = musicBufA;

/* Feed state, owned by src/pcmfeed.s (see that file for the 15-cycle loop). */
extern uint8_t  pcm_src[2];
extern uint8_t  pcm_port[2];
extern uint16_t pcm_remaining;
extern uint8_t  pcm_n;
extern void     pcm_feed_asm(void);
static uint8_t  pcmPre;                 /* 1 = the other buffer holds block+1 */
static uint8_t  pcmNeedPrefetch;

uint32_t pcmBytesFed;
uint32_t pcmFallbacks;

/* Sound-effect voices.  One PSG voice tops out at s_psgVolumeLut[63]/8 = 2,044
 * (6 % of full scale), so the click was buried under the arrangement.  The
 * click is replayed on every PSG voice the music never writes, which is free:
 *
 *   0 .. 3        PCM music running - no PSG voice is used at all
 *   3 (+ 0, 1)    MUSIC.VZM - it only writes voices 2 and 4..15
 *   3             MUSIC.PSG re-voices the FM channels onto voices 0..3, so the
 *                 one voice the converter left spare is reused as before
 *
 * Amplitude scales with the voice count (3 copies = +9.5 dB, 4 = +12 dB). */
static uint8_t  sfxActive;
static uint8_t  sfxDelay;
static uint16_t sfxPos;
static uint8_t  sfxMask;                /* bit per PSG voice carrying the click */
static uint8_t  sfxVoices[8];           /* PSG voices that carry a sound effect */
static uint8_t  sfxVoiceCount;

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

/* The BUSY probe is identical to STARTUP's; page 3 is scratch after the
 * launcher's PCM file check. It leaves VERA's address latches untouched. */
#include "fm_probe.h"
void audio_detect_fm(void) {
    volatile uint8_t *scratch = (volatile uint8_t *)0x0300;
    for (uint8_t i = 0; i < sizeof(fm_probe); ++i) scratch[i] = fm_probe[i];
    ((void (*)(void))0x0300)();
    fm_present = *(volatile uint8_t *)0x02FF;
}

/* ------------------------------ PCM stream ------------------------------ */
/* The loop point is a compile-time byte offset; 1,162,243 B / 512 = 400.5
 * blocks, so the last block and the loop block are handled explicitly and no
 * 32-bit arithmetic is needed per byte. */
#define PCM_LOOP_BLOCK              ((uint16_t)(PCM_LOOP_OFFSET / BLOCK_BYTES))
#define PCM_LOOP_START              ((uint16_t)(PCM_LOOP_OFFSET % BLOCK_BYTES))

static uint16_t pcm_block_len(uint16_t block) {
    return (uint16_t)(block + 1) < pcmBlocks ? BLOCK_BYTES : pcmLastLen;
}

static uint16_t pcm_next_block(void) {
    uint16_t next = (uint16_t)(pcmBlock + 1);
    return next < pcmBlocks ? next : PCM_LOOP_BLOCK;
}

static void pcm_stop(void) {
    pcmActive = 0;
    pcm_remaining = 0;
    VERA.audio.rate = 0;
    VERA.audio.control = PCM_CTRL_RESET;   /* drop whatever is queued */
    VERA.audio.control = PCM_CTRL_RUN;
}

static uint8_t pcmMap;                     /* raw-block map resolved and verified */

/* Load file block `block` (512 B) into `dest`.  Returns 0 on success, $4C at
 * EOF.  The raw-block path reads one block with a single MLI READ_BLOCK (4.6 ms
 * measured), while a file-level READ costs 22.7 ms per call whatever its size --
 * that difference is what decides whether PCM playback runs at ~37 or ~27 fps. */
static uint8_t pcm_load_block(uint16_t block, uint8_t *dest) {
    if (pcmMap) return disk_pcm_read(block, dest) ? 0 : 0x4C;
    return disk_read_music_block(block, dest, PCM_FILE);
}

/* Compare two 512-byte blocks; used once, to verify the block map at startup. */
static uint8_t same_block(const uint8_t *a, const uint8_t *b) {
    uint16_t i;
    for (i = 0; i < BLOCK_BYTES; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

/* Open OTHELLO.PCM, prime the hardware FIFO and start the clock.  Returns 0
 * when the file is missing or empty, so the caller can fall back to MUSIC.PSG.
 * The 4 KB FIFO holds 0.5 s of audio: filling it before the clock starts means
 * the first frames, and any later disk stall, cannot underrun the stream. */
static uint8_t pcm_start(void) {
    uint16_t i;
    uint8_t *p;
    uint16_t left;

    if (!PCM_SIZE) return 0;               /* build had no renderer */

    VERA.audio.control = PCM_CTRL_RESET;
    VERA.audio.control = PCM_CTRL_RUN;     /* mono 8-bit, volume 15 */
    VERA.audio.rate = 0;                   /* clock stopped while the FIFO fills */

    if (disk_read_music_block(0, musicBufA, PCM_FILE)) return 0;
    pcmBlocks  = (uint16_t)PCM_BLOCKS;
    pcmLastLen = (uint16_t)PCM_LAST_LEN;

    /* Resolve the file's ProDOS block map and verify it against the block just
     * read through the file manager.  A copied or rebuilt image that moved the
     * file's blocks fails that check and keeps the slow, always-correct path,
     * so the fast path cannot play the wrong data. */
    pcmMap = 0;
    if (disk_pcm_map_init(PCM_FILE)
        && disk_pcm_read(0, musicBufB)
        && same_block(musicBufA, musicBufB))
        pcmMap = 1;

    pcm_port[0] = (uint8_t)(VERA_BASE + 0x1D);
    pcm_port[1] = (uint8_t)((VERA_BASE + 0x1D) >> 8);

    pcmCur = musicBufA;
    pcmBlock = 0;
    pcmPre = 0;
    pcmNeedPrefetch = 1;
    pcmBytesFed = 0;
    pcmFallbacks = 0;

    /* Prefill 2 KB = 0.26 s of audio before the clock starts.  Writing past the
     * queue is impossible here: the FIFO holds 4 KB and nothing drains yet. */
    p = pcmCur;
    left = pcm_block_len(0);
    for (i = 0; i < 2048u; i++) {
        if (!left) {
            uint16_t next = pcm_next_block();
            if (pcm_load_block(next, pcmCur)) break;
            pcmBlock = next;
            p = pcmCur;
            left = pcm_block_len(next);
        }
        VERA.audio.data = *p++;
        left--;
        pcmBytesFed++;
    }

    /* Hand the remaining bytes of the staged block to the feed loop. */
    pcm_src[0] = (uint8_t)(uint16_t)p;
    pcm_src[1] = (uint8_t)((uint16_t)p >> 8);
    pcm_remaining = left;

    VERA.audio.rate = PCM_RATE_REG;
    pcmActive = 1;
    return 1;
}

/* Make pcmCur the next block, preferring the one audio_refill() staged. */
static uint8_t pcm_advance(void) {
    uint16_t next = pcm_next_block();
    uint16_t start = 0;                    /* first byte to play in that block */
    uint16_t addr;


    if ((uint16_t)(pcmBlock + 1) >= pcmBlocks) {
        /* End of file: resume at the authored loop point, which is not
         * necessarily block aligned (tick 1536 = 25.6 s). */
        start = PCM_LOOP_START;
    }

    if (pcmPre) {
        pcmCur = (pcmCur == musicBufA) ? musicBufB : musicBufA;
        pcmPre = 0;
    } else {
        pcmFallbacks++;
        if (pcm_load_block(next, pcmCur)) return 0;
    }
    pcmBlock = next;
    addr = (uint16_t)(pcmCur + start);
    pcm_src[0] = (uint8_t)addr;
    pcm_src[1] = (uint8_t)(addr >> 8);
    pcm_remaining = (uint16_t)(pcm_block_len(next) - start);
    pcmNeedPrefetch = 1;
    return 1;
}

/* Top the FIFO up while it reports fewer than 1,024 bytes queued (ISR bit 3 is
 * a live level on the real card and in AppleWin).  Each step feeds a 128-byte
 * chunk with the asm loop, so the stream self-regulates: a frame that overruns
 * its slot simply catches up on the next visit, and the cap of four chunks
 * bounds the worst case to 512 B (~64 ms of audio) per frame. */
void audio_feed_pcm(void) {
    uint8_t i;

    if (!pcmActive) return;

    for (i = 0; i < PCM_FEED_CHUNKS; i++) {
        if ((VERA.irq_flags & VERA_IRQ_AUDIO_LOW) == 0) break;
        if (!pcm_remaining && !pcm_advance()) { pcm_stop(); return; }
        pcm_feed_asm();
        pcmBytesFed += pcm_n;
    }
}

static void sfx_set_voices(uint8_t mode) {
    uint8_t i;
    if (mode == 2) {
        /* PCM music: the soundtrack is in the PCM FIFO, so every PSG voice is
         * free and the thumb can be replayed on four of them (measured: the
         * four sum to a ~16,500 peak, i.e. about 1.5x the music peak at volume
         * 12, so an effect is clearly audible over the bed without clipping;
         * eight voices reach 33,000 and clip). */
        for (i = 0; i < 4; i++) sfxVoices[i] = i;
        sfxVoiceCount = 4;
        sfxMask = 0x0F;
    } else if (mode == 1) {                /* MUSIC.VZM: voices 2 and 4..15 */
        sfxVoices[0] = 3; sfxVoices[1] = 0; sfxVoices[2] = 1;
        sfxVoiceCount = 3;
        sfxMask = 0x0B;
    } else {                               /* MUSIC.PSG writes 0..15 */
        sfxVoices[0] = 3;
        sfxVoiceCount = 1;
        sfxMask = 0x08;
    }
}

/* ------------------------------ music stream ---------------------------- */
static void music_seek(uint16_t offset) {
    musicBlock = offset / BLOCK_BYTES;
    musicPos   = offset % BLOCK_BYTES;
    musicCur   = musicBufA;
    musicPre   = 0;
    musicNeedPrefetch = 1;
    disk_read_music_block(musicBlock, musicCur,
                          musicFm ? MUSIC_FILE_VZM : MUSIC_FILE_PSG);
}
static void music_start(void) {
    musicActive = 1;
    musicDelay  = 0;
    music_seek(0);
}

void audio_start_music(void) {
    if (!music) return;

    if (fm_present) {
        musicFm = 1;
        music_start();
        if (disk_error()) {
            /* Copied installation without MUSIC.VZM: keep the music alive on the
             * PSG-only stream instead of stopping at the error screen. */
            disk_clear_error();
            musicFm = 0;
            music_seek(0);
        }
        sfx_set_voices(musicFm ? 1 : 0);
        return;
    }

    /* No FM chip: prefer the pre-rendered PCM soundtrack (it carries the bass
     * and the FM timbre the 16-voice re-voicing cannot produce), and fall back
     * to MUSIC.PSG when the file is missing. */
    musicFm = 0;
    if (pcm_start()) { sfx_set_voices(2); return; }
    disk_clear_error();
    music_start();
    sfx_set_voices(0);
}

void audio_stop_music(void) {
    musicActive = 0;
    musicDelay  = 0;
    if (pcmActive) pcm_stop();
    /* Silence only the music voices plus the re-voiced FM channels, leaving the
     * sound-effect voices alone; release any held FM note. */
    for (uint8_t v = 0; v < 16; v++) {
        if (sfxMask & (uint8_t)(1u << v)) continue;
        psg_reg((uint8_t)(v * 4 + 2), 0x00);
    }
    fm_key_off_all();
}

static uint8_t music_peek(void) {
    if (musicPos >= BLOCK_BYTES) {
        if (musicPre) {
            /* The next record was staged by audio_refill(): swap buffers and
             * keep playing, so a block boundary costs no disk I/O at all. */
            musicCur = (musicCur == musicBufA) ? musicBufB : musicBufA;
            musicBlock++;
            musicPre = 0;
        } else {
            /* Prefetch missed (e.g. the frame that would have staged it was
             * delayed): fall back to the synchronous read. */
            musicBlock++;
            musicFallbacks++;
            disk_read_music_block(musicBlock, musicCur,
                                  musicFm ? MUSIC_FILE_VZM : MUSIC_FILE_PSG);
        }
        musicPos = 0;
        musicNeedPrefetch = 1;
    }
    return musicCur[musicPos++];
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
    musicTicks++;
    if (musicFm) music_tick_vzm();
    else         music_tick_psg();
}

/* Stage the next music block while the frame is otherwise idle.
 *
 * pump_frame() calls this at the very end of a frame, so the MLI read (several
 * milliseconds on a real drive) overwrites the VSYNC wait window instead of
 * sitting between two music ticks.  Reading one block inside a tick is what
 * made playback stumble once per 512-byte record; with the read moved into the
 * idle window the tick that crosses a block boundary just swaps buffers. */
void audio_refill(void) {
    uint8_t *idle;

    /* PCM: stage the next 512-byte block while the FIFO plays the current one.
     * The read is the same ~4 ms MLI round trip, so it belongs in the frame
     * tail for exactly the same reason as the music prefetch. */
    if (pcmActive) {
        if (!pcmNeedPrefetch || pcmPre) return;
        pcmNeedPrefetch = 0;
        idle = (pcmCur == musicBufA) ? musicBufB : musicBufA;
        if (!pcm_load_block(pcm_next_block(), idle)) {
            pcmPre = 1;
            musicPrefetches++;
        }
        return;
    }

    if (!musicActive || !musicNeedPrefetch || musicPre) return;
    musicNeedPrefetch = 0;

    idle = (musicCur == musicBufA) ? musicBufB : musicBufA;
    if (!disk_read_music_block((uint16_t)(musicBlock + 1), idle,
                               musicFm ? MUSIC_FILE_VZM : MUSIC_FILE_PSG)) {
        musicPre = 1;
        musicPrefetches++;
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
            /* Replay the write on every voice that carries the click.  The
             * stream is encoded against voice 3 (registers 12..15), so the
             * voice index is simply reg & 3. */
            uint8_t reg = (uint8_t)(b & 0x3F);
            uint8_t val = sfx_thumb[sfxPos++];
            uint8_t n = sfxVoiceCount;
            uint8_t i = 0;
            while (n--) psg_reg((uint8_t)((sfxVoices[i++] << 2) | (reg & 3)), val);
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
    musicCur    = musicBufA;
    musicPre    = 0;
    musicNeedPrefetch = 0;
    pcmActive   = 0;
    pcmBlocks   = 0;
    pcmLastLen  = 0;
    pcmBlock    = 0;
    pcmCur      = musicBufA;
    pcm_remaining = 0;
    pcmPre      = 0;
    pcmNeedPrefetch = 0;
    pcmMap      = 0;
    sfxActive   = 0;
    sfxDelay    = 0;
    sfxPos      = 0;
    sfx_set_voices(0);
    psg_silence_all();
    fm_silence();
}























