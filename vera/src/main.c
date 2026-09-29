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

int main(void) {
    disk_init();

    init_screen();
    load_assets();

    audio_init();
    input_init();
    set_mouse_pointer(TILE_MOUSE_CURSOR);

    audio_start_music();

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
