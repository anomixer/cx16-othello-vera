/* audio.h - VERA 16-channel PSG engine for the Apple II VERA port.
 *
 *   * Background music: the Commander X16 ZSM stream is converted at build
 *     time into a compact PSG register stream (tools/zsm2psg.py) and streamed
 *     from the HDV one 512-byte block at a time.
 *   * Sound effects: the short "thumb" click is a native PSG stream embedded
 *     in the binary.
 *
 * Both are driven at 60 Hz from audio_service(), which the game calls once
 * per frame (synchronised to VERA VSYNC).
 */
#ifndef _AUDIO_H
#define _AUDIO_H

#include <stdint.h>

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

#endif /* _AUDIO_H */
