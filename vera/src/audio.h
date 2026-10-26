/* audio.h - VERA 16-channel PSG engine + optional YM2151 FM chip.
 *
 *   * The Commander X16 ZSM soundtrack is converted at build time into two
 *     streams:
 *       MUSIC.PSG - PSG-only, used when the FM daughterboard is absent
 *                   (tools/zsm2psg.py: FM re-voiced onto PSG voices 0..3).
 *       MUSIC.VZM - PSG + YM2151 FM, used when the FM chip is detected
 *                   (tools/zsm2vera.mjs / tools/gen_music.mjs).
 *     Both are streamed from the HDV one 512-byte block at a time.
 *   * Sound effects: the short "thumb" click is a native PSG stream embedded
 *     in the binary.
 *
 * Everything is driven at 60 Hz from audio_service(), which the game calls once
 * per frame (synchronised to VERA VSYNC).
 */
#ifndef _AUDIO_H
#define _AUDIO_H

#include <stdint.h>

/* Call once before drawing anything (it only touches the VERA address
 * latches and restores them).  Sets fm_present. */
void audio_detect_fm(void);

void audio_init(void);
void audio_service(void);                 /* call once per frame */
void audio_start_music(void);
void audio_stop_music(void);
void audio_play_thumb(void);

/* Source-compatible aliases used by the ported game/menu code. */
#define play_thumb()      audio_play_thumb()
#define start_bgmusic()   audio_start_music()
#define stop_bgmusic()    audio_stop_music()
#define init_sound()      audio_init()

extern uint8_t music;                     /* YES = background music enabled */
extern uint8_t fm_present;                /* YES = YM2151 FM chip detected  */

#endif /* _AUDIO_H */
