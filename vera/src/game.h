/* game.h - Apple II VERA port of CX16-OTHELLO game logic.
 *
 * Game rules are unchanged from the Commander X16 original by Ivo Filot
 * (GPL v3).  malloc() is replaced by static 10x10 arrays and the X16 KERNROUT
 * input calls by the Apple //e input layer.
 */
#ifndef _GAME_H
#define _GAME_H

#include <stdint.h>
#include "constants.h"

#define BOARD_MAX_CELLS   100          /* 10 x 10 */

extern uint8_t board[BOARD_MAX_CELLS];
extern uint8_t edgefield[BOARD_MAX_CELLS];
extern uint8_t stonecounter[BOARD_MAX_CELLS];
extern int8_t curx;
extern int8_t cury;
extern uint8_t current_player;
extern uint8_t gamestate;
extern uint8_t player1_type;
extern uint8_t player2_type;
extern uint8_t board_type;
extern uint8_t stone_color1;
extern uint8_t stone_color2;
extern uint8_t boardsize;
extern uint8_t board_offset_x;
extern uint8_t board_offset_y;
extern uint8_t no_move_counter;
extern uint16_t cpu_waittime;
extern uint8_t background_scroll;

void init_game(void);
void set_stone(uint8_t y, uint8_t x, uint8_t stone);
void set_cursor(uint8_t y, uint8_t x);
void move_cursor(int8_t y, int8_t x);
uint8_t place_stone(uint8_t y, uint8_t x, uint8_t probe, uint8_t player);
uint8_t search_and_turn(uint8_t y, uint8_t x, int8_t dy, int8_t dx, uint8_t probe, uint8_t player);
void count_stones(uint8_t end);
void computer_turn(void);
void human_turn(void);
void swap_turn(void);
void wait_till_space(void);
uint8_t check_valid_move(void);
void end_game_state(uint8_t black, uint8_t white);

#endif /* _GAME_H */
