/**************************************************************************
 *
 *   CX16-OTHELLO - Apple II VERA port
 *
 *   Game logic, ported from the Commander X16 original by
 *   Ivo Filot <ivo@ivofilot.nl> (GPL v3).  The rules, the AI and the
 *   rendering sequence are unchanged; only the platform layer differs:
 *     - malloc() -> static 10x10 arrays
 *     - KERNROUT keyboard/joystick/mouse -> Apple //e input layer
 *     - clock()/CLOCKS_PER_SEC -> VSYNC frame clock
 *     - zsmkit sound buffers -> audio_service() driven at 60 Hz
 *
 **************************************************************************/

#include <string.h>
#include "game.h"
#include "video.h"
#include "audio.h"
#include "input.h"

uint8_t board[BOARD_MAX_CELLS];
uint8_t edgefield[BOARD_MAX_CELLS];
uint8_t stonecounter[BOARD_MAX_CELLS];
int8_t curx = 0;
int8_t cury = 0;
uint8_t current_player = PLAYER_ONE;
uint8_t gamestate = GAME_MENU;
uint8_t player1_type = PLAYER_HUMAN;
uint8_t player2_type = PLAYER_CPU;
uint8_t board_type = BOARD_STONE;
uint8_t stone_color1 = STONE_RED;
uint8_t stone_color2 = STONE_TEAL;
uint8_t boardsize = 8;
uint8_t board_offset_x = 6;
uint8_t board_offset_y = 4;
uint8_t no_move_counter = 0;
uint16_t cpu_waittime = 200;
uint8_t background_scroll = YES;

/* Small deterministic PRNG: the AI only uses it to break ties between
 * equally good moves, so a 16-bit LCG is plenty and avoids pulling in
 * the full C library rand(). */
static uint16_t rng_state = 0xACE1u;
static uint8_t next_rand(uint8_t mod) {
    rng_state = (uint16_t)(rng_state * 1103515245u + 12345u);
    return (uint8_t)(((rng_state >> 8) & 0xFF) % mod);
}

static void utoa(uint8_t v, char *buf) {
    char tmp[4];
    uint8_t i = 0, j = 0;
    if (v == 0) { tmp[i++] = '0'; }
    while (v) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/**
 * @brief Initialize the game scene
 */
void init_game(void) {
    uint8_t i;

    memset(board, STONE_NONE, sizeof(board));
    memset(edgefield, EDGEFIELD_INACTIVE, sizeof(edgefield));
    memset(stonecounter, 0, sizeof(stonecounter));

    no_move_counter = 0;

    /* hide all stone sprites */
    for (i = 0; i < (uint8_t)(boardsize * boardsize); i++) {
        assign_sprite((uint8_t)(i + 1), 0x00);
    }

    assign_sprite(SPRITE_TILE_CURSOR, TILE_EMPTY_CURSOR);

    build_playfield();

    set_stone((uint8_t)(boardsize / 2 - 1), (uint8_t)(boardsize / 2 - 1), STONE_PLAYER1);
    set_stone((uint8_t)(boardsize / 2 - 1), (uint8_t)(boardsize / 2), STONE_PLAYER2);
    set_stone((uint8_t)(boardsize / 2), (uint8_t)(boardsize / 2 - 1), STONE_PLAYER2);
    set_stone((uint8_t)(boardsize / 2), (uint8_t)(boardsize / 2), STONE_PLAYER1);

    count_stones(COUNT_NO_GAME_END);

    set_cursor((uint8_t)(boardsize / 2 - 1), (uint8_t)(boardsize / 2 - 1));

    set_tile(14, 5, stone_color1, PALETTEBYTE);

    current_player = PLAYER_ONE;

    if (player1_type == PLAYER_HUMAN || player2_type == PLAYER_HUMAN) {
        cpu_waittime = 500;
    } else {
        cpu_waittime = 100;
    }

    gamestate = GAME_RUN;
}

void set_stone(uint8_t y, uint8_t x, uint8_t stone) {
    int8_t sx, sy;
    uint8_t tile_idx = (uint8_t)(y * boardsize + x);
    uint8_t sprite_id = (uint8_t)(tile_idx + 1);

    board[tile_idx] = stone;
    edgefield[tile_idx] = EDGEFIELD_INACTIVE;

    switch (stone) {
        case STONE_PLAYER1:
            assign_sprite(sprite_id, stone_color1);
            set_sprite(sprite_id, (uint8_t)(y + board_offset_y), (uint8_t)(x + board_offset_x));
            break;
        case STONE_PLAYER2:
            assign_sprite(sprite_id, stone_color2);
            set_sprite(sprite_id, (uint8_t)(y + board_offset_y), (uint8_t)(x + board_offset_x));
            break;
        default:
            break;
    }

    for (sy = (int8_t)y - 1; sy <= (int8_t)y + 1; sy++) {
        for (sx = (int8_t)x - 1; sx <= (int8_t)x + 1; sx++) {
            if (sx < 0 || sy < 0 || sx >= (int8_t)boardsize || sy >= (int8_t)boardsize) {
                continue;
            }
            if (board[sy * boardsize + sx] == STONE_NONE) {
                edgefield[sy * boardsize + sx] = EDGEFIELD_ACTIVE;
            }
        }
    }
}

void set_cursor(uint8_t y, uint8_t x) {
    set_sprite(SPRITE_TILE_CURSOR, (uint8_t)(board_offset_y + y), (uint8_t)(board_offset_x + x));
    curx = (int8_t)x;
    cury = (int8_t)y;
}

void move_cursor(int8_t y, int8_t x) {
    x += curx;
    y += cury;

    if (x == -1) x = (int8_t)boardsize - 1;
    if (x == (int8_t)boardsize) x = 0;
    if (y == -1) y = (int8_t)boardsize - 1;
    if (y == (int8_t)boardsize) y = 0;

    set_cursor((uint8_t)y, (uint8_t)x);
}

uint8_t place_stone(uint8_t y, uint8_t x, uint8_t probe, uint8_t player) {
    int8_t sx, sy, k;
    uint8_t dircheck = 0xFF;
    uint8_t stones_turned = 0;
    uint8_t setstone    = (player == PLAYER_ONE ? STONE_PLAYER1 : STONE_PLAYER2);
    uint8_t searchstone = (player == PLAYER_ONE ? STONE_PLAYER2 : STONE_PLAYER1);

    if (board[y * boardsize + x] != STONE_NONE) {
        return 0;
    }

    /* check for at least one adjacent opposing stone
     *
     *  0 1 2
     *  3 X 4
     *  5 6 7
     */
    k = -1;
    for (sy = (int8_t)y - 1; sy <= (int8_t)y + 1; sy++) {
        for (sx = (int8_t)x - 1; sx <= (int8_t)x + 1; sx++) {
            if (sy == (int8_t)y && sx == (int8_t)x) continue;
            k++;
            if (sx < 0 || sy < 0 || sx >= (int8_t)boardsize || sy >= (int8_t)boardsize) {
                continue;
            }
            if (board[sy * boardsize + sx] == searchstone) {
                dircheck &= (uint8_t)~(1 << k);
            }
        }
    }

    if (dircheck == 0xFF) {
        return 0;
    }

    for (k = 0; k < 8; k++) {
        if ((dircheck & (uint8_t)(1 << k)) == 0) {
            if (k < 3) {
                stones_turned += search_and_turn(y, x, -1, (int8_t)(k - 1), probe, player);
            } else if (k > 4) {
                stones_turned += search_and_turn(y, x, 1, (int8_t)(k - 6), probe, player);
            } else {
                stones_turned += search_and_turn(y, x, 0, (k == 3 ? -1 : 1), probe, player);
            }
        }
    }

    if (probe == PROBE_YES) {
        return stones_turned;
    }

    if (stones_turned == 0) {
        return stones_turned;
    }

    play_thumb();
    set_stone(y, x, setstone);

    swap_turn();
    count_stones(COUNT_NO_GAME_END);

    return stones_turned;
}

uint8_t search_and_turn(uint8_t y, uint8_t x, int8_t dy, int8_t dx, uint8_t probe, uint8_t player) {
    uint8_t buffer[20];
    uint8_t *bufferptr = buffer;
    int8_t sx = (int8_t)x + dx;
    int8_t sy = (int8_t)y + dy;
    uint8_t stop = 1;
    uint8_t stones_turned = 0;
    uint8_t searchstone = (player == PLAYER_ONE ? STONE_PLAYER2 : STONE_PLAYER1);
    uint8_t setstone    = (player == PLAYER_ONE ? STONE_PLAYER1 : STONE_PLAYER2);

    while (sx >= 0 && sy >= 0 && sx < (int8_t)boardsize && sy < (int8_t)boardsize && stop == 1) {
        if (board[sy * boardsize + sx] == searchstone) {
            *(bufferptr++) = (uint8_t)sy;
            *(bufferptr++) = (uint8_t)sx;
        } else if (board[sy * boardsize + sx] == setstone) {
            stop = 0;
        } else {
            return 0;
        }
        sx += dx;
        sy += dy;
    }

    if (stop == 1) {
        return 0;
    }

    *(bufferptr++) = 0xFF;
    *(bufferptr++) = 0xFF;

    bufferptr = buffer;
    while (*bufferptr != 0xFF) {
        uint8_t by = *(bufferptr++);
        uint8_t bx = *(bufferptr++);
        if (probe == PROBE_NO) {
            set_stone(by, bx, setstone);
        }
        stones_turned++;
    }

    return stones_turned;
}

void count_stones(uint8_t end) {
    uint8_t i;
    uint8_t black = 0, white = 0;
    char buf[4];

    for (i = 0; i < (uint8_t)(boardsize * boardsize); i++) {
        switch (board[i]) {
            case STONE_PLAYER1: black++; break;
            case STONE_PLAYER2: white++; break;
            default: break;
        }
    }

    utoa(black, buf);
    write_string("   ", 1, 17);
    write_string(buf, 1, (uint8_t)(20 - strlen(buf)));
    utoa(white, buf);
    write_string("   ", 2, 17);
    write_string(buf, 2, (uint8_t)(20 - strlen(buf)));

    if (black + white == (uint8_t)(boardsize * boardsize) ||
        end == COUNT_END_GAME ||
        black == 0 || white == 0) {
        gamestate = GAME_STOP;
        end_game_state(black, white);
    }
}

void computer_turn(void) {
    uint8_t besty[BOARD_MAX_CELLS];
    uint8_t bestx[BOARD_MAX_CELLS];
    uint8_t bestvalue = 0;
    uint8_t i, j, k, v;
    uint32_t start;

    for (j = 0; j < boardsize; j++) {
        for (i = 0; i < boardsize; i++) {
            if (edgefield[j * boardsize + i] == EDGEFIELD_ACTIVE) {
                stonecounter[j * boardsize + i] = place_stone(j, i, PROBE_YES, current_player);
            } else {
                stonecounter[j * boardsize + i] = 0;
            }
        }
    }

    /* corners are worth more */
    if (stonecounter[0] > 0)                                       stonecounter[0] += 10;
    if (stonecounter[boardsize - 1] > 0)                           stonecounter[boardsize - 1] += 10;
    if (stonecounter[boardsize * boardsize - 1] > 0)              stonecounter[boardsize * boardsize - 1] += 10;
    if (stonecounter[boardsize * (boardsize - 1)] > 0)            stonecounter[boardsize * (boardsize - 1)] += 10;

    for (j = 0; j < boardsize; j++) {
        for (i = 0; i < boardsize; i++) {
            v = stonecounter[j * boardsize + i];
            if (v > bestvalue) {
                bestvalue = v;
                k = 0;
                besty[k] = j;
                bestx[k] = i;
                k++;
            } else if (v == bestvalue && v != 0) {
                besty[k] = j;
                bestx[k] = i;
                k++;
            }
        }
    }

    if (bestvalue != 0x00) {
        /* artificial "thinking" delay, keeps the picture and music alive */
        start = frame_ms();
        do {
            pump_frame();
        } while ((uint32_t)(frame_ms() - start) < cpu_waittime);

        if (k > 1) {
            k = next_rand(k);                 /* pick one of the k best moves */
        } else {
            k = 0;
        }

        place_stone(besty[k], bestx[k], PROBE_NO, current_player);
        no_move_counter = 0;
    } else {
        no_move_counter++;
        if (no_move_counter == 2) {
            count_stones(COUNT_END_GAME);
        } else {
            swap_turn();
        }
    }
}

void human_turn(void) {
    int16_t key = input_key();
    int16_t mx, my;
    uint8_t buttons;
    int8_t ccurx, ccury;
    int16_t dir;

    /* Can this player move at all? */
    if (check_valid_move() == NO) {
        no_move_counter++;
        if (no_move_counter == 2) {
            count_stones(COUNT_END_GAME);
        } else {
            swap_turn();
        }
        return;
    }
    no_move_counter = 0;

    /* --- keyboard ---
     * move_cursor(y, x) takes a row delta, and row 0 is the top of the screen,
     * so a positive row delta moves DOWN.  The X16 original pairs its
     * KEYCODE_UP/KEYCODE_DOWN names with the PETSCII cursor-control codes
     * ($11 = cursor down, $91 = cursor up), i.e. the names are already
     * inverted; the Apple //e arrow-key codes ($08 left, $0B up, $0A down,
     * $15 right) are not, so the vertical axis is mapped the other way here. */
    switch (key) {
        case KEYCODE_DOWN:  move_cursor(1, 0);  return;
        case KEYCODE_UP:    move_cursor(-1, 0); return;
        case KEYCODE_LEFT:  move_cursor(0, -1); return;
        case KEYCODE_RIGHT: move_cursor(0, 1);  return;
        case KEYCODE_SPACE:
            place_stone((uint8_t)cury, (uint8_t)curx, PROBE_NO, current_player);
            return;
        case KEYCODE_ESCAPE:
            gamestate = GAME_MENU;
            reset_sprites();
            return;
        default:
            break;
    }

    /* --- joystick (edge triggered, auto-repeat handled by the input layer) --- */
    dir = input_joystick_dir();
    if (dir) {
        if (dir & JOY_UP)    { move_cursor(-1, 0); return; }
        if (dir & JOY_DOWN)  { move_cursor(1, 0);  return; }
        if (dir & JOY_LEFT)  { move_cursor(0, -1); return; }
        if (dir & JOY_RIGHT) { move_cursor(0, 1);  return; }
    }
    if (input_joystick_fire()) {
        place_stone((uint8_t)cury, (uint8_t)curx, PROBE_NO, current_player);
        return;
    }

    /* --- mouse --- */
    if (!input_mouse_present()) return;

    input_mouse(&mx, &my, &buttons);
    ccurx = (int8_t)((mx >> 4) - board_offset_x);
    ccury = (int8_t)((my >> 4) - board_offset_y);

    if (ccurx >= 0 && ccurx < (int8_t)boardsize && ccury >= 0 && ccury < (int8_t)boardsize) {
        if (buttons & 1) {
            /* wait for the button to be released, keeping the frame alive */
            do {
                pump_frame();
                input_mouse(&mx, &my, &buttons);
            } while (buttons != 0x00);
            place_stone((uint8_t)ccury, (uint8_t)ccurx, PROBE_NO, current_player);
        }
    }
}

void swap_turn(void) {
    if (current_player == PLAYER_ONE) {
        current_player = PLAYER_TWO;
        set_tile(14, 5, stone_color2, PALETTEBYTE);
    } else {
        current_player = PLAYER_ONE;
        set_tile(14, 5, stone_color1, PALETTEBYTE);
    }
}

void wait_till_space(void) {
    for (;;) {
        pump_frame();
        if (input_key() == KEYCODE_SPACE) return;
    }
}

void end_game_state(uint8_t black, uint8_t white) {
    if (black + white < (uint8_t)(boardsize * boardsize)) {
        write_string("No more moves", 13, 0);
    }

    if (black > white) {
        write_string("PLAYER 1 WINS!", 14, 0);
        set_background((uint8_t)(stone_color1 + 0x10));
    } else if (white > black) {
        write_string("PLAYER 2 WINS!", 14, 0);
        set_background((uint8_t)(stone_color2 + 0x10));
    } else {
        write_string("IT'S A TIE!", 14, 0);
    }

    write_string("hit ENTER to", 0, 0);
    write_string("go to menu", 1, 0);

    for (;;) {
        pump_frame();
        if (input_key() == KEYCODE_RETURN) {
            gamestate = GAME_MENU;
            reset_sprites();
            return;
        }
    }
}

uint8_t check_valid_move(void) {
    uint8_t i, j;

    for (j = 0; j < boardsize; j++) {
        for (i = 0; i < boardsize; i++) {
            if (edgefield[j * boardsize + i] == EDGEFIELD_ACTIVE) {
                if (place_stone(j, i, PROBE_YES, current_player) > 0) {
                    return YES;
                }
            }
        }
    }

    return NO;
}
