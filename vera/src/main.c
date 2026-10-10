/**************************************************************************
 *
 *   CX16-OTHELLO - Apple II VERA port
 *
 *   Port of the Commander X16 original by Ivo Filot <ivo@ivofilot.nl>
 *   (GPL v3).
 *
 *   Boot: ProDOS BASIC.SYSTEM RUNs MAIN.BIN at $1400 (STARTUP probes the
 *   VERA slot and BRUNs the matching MAIN.BIN / MAIN4.BIN image).
 *
 **************************************************************************/

#include <stdint.h>

#include "apple2e.h"
#include "constants.h"
#include "disk.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "game.h"
#include "menu.h"

static void disk_error_screen(void) {
    static const char *const messages[] = {
        "OTHELLO FILE ERROR", "MARKER NOT FOUND", "MARKER CLOSE ERROR",
        "ASSETS.BIN NOT FOUND", "ASSETS.BIN TOO SHORT", "GET PREFIX ERROR",
        "PREFIX TOO LONG", "PRODOS OPEN ERROR", "GET EOF ERROR"
    };
    volatile uint8_t *screen = (volatile uint8_t *)0x0400;
    const char *message = messages[disk_failure_stage()];
    *(volatile uint8_t *)0xC051 = 0;
    *(volatile uint8_t *)0xC054 = 0;
    VERA.display.video = 0;
    for (uint16_t i = 0; i < 40u * 24u; ++i) screen[i] = 0xA0;
    for (uint8_t i = 0; message[i]; ++i) screen[i] = (uint8_t)(message[i] | 0x80u);
    for (;;) { }
}

int main(void) {
    if (!disk_init()) disk_error_screen();

    init_screen();
    load_assets();
    if (disk_error()) disk_error_screen();

    audio_init();
    input_init();
    init_mouse_sprite();

    audio_start_music();
    if (disk_error()) disk_error_screen();

    for (;;) {
        while (gamestate == GAME_MENU) {
            clear_screen();
            game_title();
        }

        while (gamestate == GAME_SETTINGS) {
            clear_screen();
            game_settings();
        }

        while (gamestate == GAME_HELP) {
            clear_screen();
            game_help();
        }

        while (gamestate == GAME_RUN) {
            switch (current_player) {
                case PLAYER_ONE:
                    if (player1_type == PLAYER_CPU) computer_turn();
                    else                            human_turn();
                    break;
                case PLAYER_TWO:
                    if (player2_type == PLAYER_CPU) computer_turn();
                    else                            human_turn();
                    break;
                default:
                    break;
            }

            /* one frame: VSYNC + audio + diagonal background scroll */
            pump_frame();
        }
    }
}
