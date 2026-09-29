/* input.h - Apple //e keyboard, Apple Mouse Card and analog joystick input.
 *
 * The Commander X16 original polled KERNROUT ($FFE4 keyboard, $FF56 joystick,
 * $FF6B mouse).  On the Apple II the equivalents are:
 *   keyboard : KEYD $C000 / strobe $C010
 *   joystick : PTRIG $C070, PDL0 $C064, PDL1 $C065, PB0..PB2 $C061..$C063
 *   mouse    : Apple Mouse Card ROM entry points (INITMOUSE/SETMOUSE/READMOUSE)
 */
#ifndef _INPUT_H
#define _INPUT_H

#include <stdint.h>

/* Direction bits reported by input_joystick_dir() (edge triggered). */
#define JOY_LEFT      0x01
#define JOY_RIGHT     0x02
#define JOY_UP        0x04
#define JOY_DOWN      0x08

void    input_init(void);
void    input_update(void);             /* call once per frame */
void    input_flush(void);

int16_t input_key(void);                /* edge-triggered key, -1 when none */
uint8_t input_key_held(void);           /* any key currently down */

int16_t input_joystick_dir(void);       /* JOY_* bits, edge triggered */
uint8_t input_joystick_fire(void);      /* 1 = fire pressed (edge triggered) */

void    input_mouse(int16_t *x, int16_t *y, uint8_t *buttons);
uint8_t input_mouse_present(void);

#endif /* _INPUT_H */
