/**************************************************************************
 *
 *   CX16-OTHELLO - Apple II VERA port
 *
 *   Title / settings / help screens, ported from the Commander X16 original
 *   by Ivo Filot <ivo@ivofilot.nl> (GPL v3).
 *
 **************************************************************************/

#include <string.h>
#include "menu.h"
#include "game.h"
#include "video.h"
#include "audio.h"
#include "input.h"

uint8_t helppage = 0;
static uint8_t mouse_was_down = 0;

/**
 * @brief Show game title screen
 */
void game_title(void) {
    static const uint8_t offsetx = 8;

    write_string("CX16-OTHELLO", 0, 0);
    write_string("music by Crisps", 1, 5);
    write_string("1.2.0", 0, 15);

    /* sample board */
    build_board(6, 4, offsetx);

    assign_sprite(1, stone_color1);
    assign_sprite(2, stone_color2);
    assign_sprite(3, stone_color1);
    assign_sprite(4, stone_color2);

    set_sprite(1, 6, (uint8_t)(offsetx + 2));
    set_sprite(2, 6, (uint8_t)(offsetx + 3));
    set_sprite(3, 7, (uint8_t)(offsetx + 3));
    set_sprite(4, 7, (uint8_t)(offsetx + 2));

    write_string("(S) SETTINGS", 11, 0);
    write_string("(H) HELP", 12, 0);
    write_string("(ENTER) START", 14, 0);

    for (;;) {
        int16_t k;
        pump_frame();

        if (input_mouse_present()) {
            int16_t mx, my;
            uint8_t buttons;
            input_mouse(&mx, &my, &buttons);
            if ((buttons & 0x80) && !mouse_was_down) {
                uint8_t row = (uint8_t)(my >> 4);
                if (row == 11) { gamestate = GAME_SETTINGS; mouse_was_down = 1; return; }
                if (row == 12) { gamestate = GAME_HELP; mouse_was_down = 1; return; }
                if (row >= 14) { gamestate = GAME_RUN; init_game(); mouse_was_down = 1; return; }
            }
            mouse_was_down = (buttons & 0x80) ? 1 : 0;
        }

        k = input_key();
        switch (k) {
            case 'S':
                gamestate = GAME_SETTINGS;
                return;
            case 'H':
                gamestate = GAME_HELP;
                return;
            case KEYCODE_RETURN:
                gamestate = GAME_RUN;
                init_game();
                return;
            default:
                break;
        }
    }
}

/**
 * @brief Show help screen
 */
void game_help(void) {
    char buf[4];

    assign_sprite(1, TILE_NONE);
    assign_sprite(2, TILE_NONE);
    assign_sprite(3, TILE_NONE);
    assign_sprite(4, TILE_NONE);

    write_string("HELP", 0, 0);
    write_string("(N) NEXT (P) PREV", 13, 0);
    write_string("(ESC) BACK  PAGE:", 14, 0);

    load_help_page(helppage);

    /* page number, e.g. "1/6" */
    buf[0] = (char)('0' + helppage + 1);
    buf[1] = '/';
    buf[2] = (char)('0' + HELPPAGES);
    buf[3] = '\0';
    write_string(buf, 14, 17);

    for (;;) {
        int16_t k;
        pump_frame();

        if (input_mouse_present()) {
            int16_t mx, my;
            uint8_t buttons;
            input_mouse(&mx, &my, &buttons);
            if ((buttons & 0x80) && !mouse_was_down) {
                uint8_t row = (uint8_t)(my >> 4);
                uint8_t col = (uint8_t)(mx >> 4);
                if (row == 13) {
                    if (col < 10) { if (helppage < (HELPPAGES - 1)) helppage++; mouse_was_down = 1; return; }
                    else { if (helppage > 0) helppage--; mouse_was_down = 1; return; }
                }
                if (row >= 14) { gamestate = GAME_MENU; mouse_was_down = 1; return; }
            }
            mouse_was_down = (buttons & 0x80) ? 1 : 0;
        }

        k = input_key();
        switch (k) {
            case KEYCODE_ESCAPE:
                gamestate = GAME_MENU;
                return;
            case 'N':
                if (helppage < (HELPPAGES - 1)) helppage++;
                return;
            case 'P':
                if (helppage > 0) helppage--;
                return;
            default:
                break;
        }
    }
}

/**
 * @brief Show game settings
 */
void game_settings(void) {
    write_string("SETTINGS", 0, 0);

    write_string("(1) Player 1:", 1, 0);
    write_string("(2) Player 2:", 2, 0);
    write_string("(B) Board:", 4, 0);

    write_string("(S) Size:", 6, 0);

    write_string("(C) Disc player 1:", 9, 0);
    write_string("(V) Disc player 2:", 10, 0);

    write_string("(G) BG scroll:", 12, 0);
    write_string("(M) Music:", 13, 0);

    write_string("(ESCAPE) BACK", 14, 0);

    print_choice();

    for (;;) {
        int16_t k;
        pump_frame();

        k = input_key();

        if (input_mouse_present()) {
            int16_t mx, my;
            uint8_t buttons;
            input_mouse(&mx, &my, &buttons);
            if ((buttons & 0x80) && !mouse_was_down) {
                uint8_t row = (uint8_t)(my >> 4);
                if (row == 1) k = '1';
                else if (row == 2) k = '2';
                else if (row == 4) k = 'B';
                else if (row == 6) k = 'S';
                else if (row == 9) k = 'C';
                else if (row == 10) k = 'V';
                else if (row == 12) k = 'G';
                else if (row == 13) k = 'M';
                else if (row >= 14) k = KEYCODE_ESCAPE;
            }
            mouse_was_down = (buttons & 0x80) ? 1 : 0;
        }

        switch (k) {
            case '1':
                player1_type = (player1_type == PLAYER_HUMAN ? PLAYER_CPU : PLAYER_HUMAN);
                print_choice();
                break;
            case '2':
                player2_type = (player2_type == PLAYER_HUMAN ? PLAYER_CPU : PLAYER_HUMAN);
                print_choice();
                break;
            case 'B':
                board_type = (board_type == BOARD_STONE ? BOARD_WOOD : BOARD_STONE);
                print_choice();
                break;
            case 'G':
                background_scroll = (background_scroll == YES ? NO : YES);
                print_choice();
                break;
            case 'M':
                music = (music == YES ? NO : YES);
                if (music == YES) {
                    audio_start_music();
                } else {
                    audio_stop_music();
                }
                print_choice();
                break;
            case 'C':
                stone_color1++;
                if (stone_color1 == stone_color2) stone_color1++;
                if (stone_color1 > STONE_ORANGE) stone_color1 = STONE_RED;
                if (stone_color1 == stone_color2) stone_color1++;
                print_choice();
                break;
            case 'V':
                stone_color2++;
                if (stone_color2 == stone_color1) stone_color2++;
                if (stone_color2 > STONE_ORANGE) stone_color2 = STONE_RED;
                if (stone_color2 == stone_color1) stone_color2++;
                print_choice();
                break;
            case 'S':
                boardsize += 2;
                if (boardsize > 10) boardsize = 6;
                board_offset_x = (uint8_t)(10 - (boardsize / 2));
                board_offset_y = (uint8_t)(8 - (boardsize / 2));
                print_choice();
                break;
            case KEYCODE_ESCAPE:
                gamestate = GAME_MENU;
                return;
            default:
                break;
        }
    }
}

/**
 * @brief Print current game configuration to the screen
 */
void print_choice(void) {
    char buf[8];
    static const uint8_t offsetx = 12;

    write_string(player1_type == PLAYER_HUMAN ? "HUMAN" : "CPU  ", 1, 14);
    write_string(player2_type == PLAYER_HUMAN ? "HUMAN" : "CPU  ", 2, 14);
    write_string(board_type == BOARD_STONE ? "STONE" : "WOOD  ", 5, 1);
    write_string(background_scroll == YES ? "YES" : "NO  ", 12, 15);
    write_string(music == YES ? "ON " : "OFF  ", 13, 11);

    switch (boardsize) {
        case 6:  memcpy(buf, "6 x 6  ", 8); break;
        case 8:  memcpy(buf, "8 x 8  ", 8); break;
        default: memcpy(buf, "10 x 10", 8); break;
    }
    write_string(buf, 7, 1);

    /* sample board showing the two disc colours */
    build_board(4, 4, offsetx);

    assign_sprite(1, stone_color1);
    assign_sprite(2, stone_color2);
    assign_sprite(3, stone_color1);
    assign_sprite(4, stone_color2);

    set_sprite(1, 5, (uint8_t)(offsetx + 1));
    set_sprite(2, 5, (uint8_t)(offsetx + 2));
    set_sprite(3, 6, (uint8_t)(offsetx + 2));
    set_sprite(4, 6, (uint8_t)(offsetx + 1));

    set_tile(9, 18, stone_color1, 0x00);
    set_tile(10, 18, stone_color2, 0x00);
}
