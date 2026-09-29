/* input.c - Apple //e keyboard, Apple Mouse Card and analog joystick. */
#include <stdint.h>
#include "apple2e.h"
#include "constants.h"
#include "input.h"

extern void mouse_init(void);
extern void mouse_poll(void);
extern volatile uint8_t mouse_x[2], mouse_y[2], mouse_buttons, mouse_slot;

/* ------------------------------- keyboard ------------------------------- */
static int16_t lastKey = -1;

void input_flush(void) {
    while (kbd_pressed()) kbd_clear();
    lastKey = -1;
}

int16_t input_key(void) {
    int16_t k = lastKey;
    lastKey = -1;
    return k;
}

uint8_t input_key_held(void) {
    return (KBD_DATA & 0x80) != 0;
}

/* ------------------------------- joystick ------------------------------- */
#define JOY_REPEAT_FIRST  14      /* frames held before auto-repeat starts */
#define JOY_REPEAT_RATE    6      /* frames between auto-repeats */

static uint8_t hasJoystick = 0;
static uint8_t joyPrev = 0;
static uint8_t joyEdge = 0;
static uint8_t joyHold = 0;
static uint8_t firePrev = 0;
static uint8_t fireEdge = 0;

/* Count the 555 one-shot period for paddle `pdl` (0 = PDL0/X, 1 = PDL1/Y).
 * Only ever called when a real paddle is attached - the spin loop is
 * ~2800 cycles and must never run on an emulated machine without paddles. */
static uint8_t read_pdl(uint8_t pdl) {
    uint8_t count = 0;
#ifdef __mos__
    __asm__ volatile(
        "ldx %1\n\t"
        "lda 0xC070\n\t"
        "ldy #0\n\t"
        "nop\n\t"
        "nop\n"
        "1:\n\t"
        "lda 0xC064,x\n\t"
        "bpl 2f\n\t"
        "iny\n\t"
        "bne 1b\n\t"
        "dey\n"
        "2:\n\t"
        "sty %0"
        : "=r"(count)
        : "r"(pdl)
        : "a", "x", "y"
    );
#else
    (void)pdl;
#endif
    return count;
}

static uint8_t read_buttons(void) {
    uint8_t b = 0;
    if ((*(volatile uint8_t *)0xC061 & 0x80) != 0) b |= 1;   /* PB0 */
    if ((*(volatile uint8_t *)0xC062 & 0x80) != 0) b |= 2;   /* PB1 */
    if ((*(volatile uint8_t *)0xC063 & 0x80) != 0) b |= 4;   /* PB2 */
    return b;
}

void input_init(void) {
    mouse_init();
    input_flush();

    /* Probe the paddles once.  A centred Apple II joystick reads ~128;
     * disconnected hardware times out at 255 (or reads 0). */
    uint8_t x = read_pdl(0);
    uint8_t y = read_pdl(1);
    hasJoystick = (x > 30 && x < 225 && y > 30 && y < 225);
}

void input_update(void) {
    /* keyboard */
    if (KBD_DATA & 0x80) {
        lastKey = KBD_DATA & 0x7F;
        (void)KBD_STROBE;
    }

    mouse_poll();

    if (!hasJoystick) return;

    uint8_t px = read_pdl(0);
    uint8_t py = read_pdl(1);
    uint8_t dir = 0;
    if (px < 110) dir |= JOY_LEFT;
    else if (px > 145) dir |= JOY_RIGHT;
    if (py < 110) dir |= JOY_UP;
    else if (py > 145) dir |= JOY_DOWN;

    if (dir != joyPrev) {
        joyEdge |= dir;
        joyPrev = dir;
        joyHold = JOY_REPEAT_FIRST;
    } else if (dir) {
        if (joyHold) {
            joyHold--;
            if (!joyHold) { joyEdge |= dir; joyHold = JOY_REPEAT_RATE; }
        }
    } else {
        joyHold = 0;
    }

    uint8_t fire = read_buttons() ? 1 : 0;
    if (fire && !firePrev) fireEdge = 1;
    firePrev = fire;
}

int16_t input_joystick_dir(void) {
    uint8_t e = joyEdge;
    joyEdge = 0;
    return e;
}

uint8_t input_joystick_fire(void) {
    uint8_t e = fireEdge;
    fireEdge = 0;
    return e;
}

/* --------------------------------- mouse -------------------------------- */
uint8_t input_mouse_present(void) { return mouse_slot != 0; }

void input_mouse(int16_t *x, int16_t *y, uint8_t *buttons) {
    *x = (int16_t)((uint16_t)mouse_x[0] | ((uint16_t)mouse_x[1] << 8));
    *y = (int16_t)((uint16_t)mouse_y[0] | ((uint16_t)mouse_y[1] << 8));
    *buttons = mouse_buttons;
}
