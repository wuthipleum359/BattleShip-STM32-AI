#ifndef WALLSHIP_TYPES_H
#define WALLSHIP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define BOARD_W 8
#define BOARD_H 7
#define SHIP_COUNT 6
#define MAX_SHIP_LENGTH 3
#define INVALID_COORD UINT8_C(255)
#define WALLSHIP_DEFAULT_SEED UINT32_C(0x6D2B79F5)

typedef enum { SHIP_HORIZONTAL = 0, SHIP_VERTICAL } ShipOrientation;
typedef enum { CELL_UNKNOWN = 0, CELL_MISS, CELL_HIT, CELL_SUNK } CellState;
typedef enum { SHOT_MISS = 0, SHOT_HIT, SHOT_SUNK, SHOT_INVALID } ShotResult;
typedef enum { AI_RANDOM = 0, AI_BALANCED, AI_AGGRESSIVE } AIProfile;

typedef struct {
    uint8_t x;
    uint8_t y;
} Position;

typedef struct {
    uint8_t id;
    uint8_t length;
    uint8_t hp;
    bool sunk;
    uint8_t start_x;
    uint8_t start_y;
    ShipOrientation orientation;
} Ship;

typedef struct {
    CellState enemy_board[BOARD_H][BOARD_W];
    uint8_t own_board[BOARD_H][BOARD_W];
    uint8_t human_shot_board[BOARD_H][BOARD_W];
    Ship ships[SHIP_COUNT];
    uint8_t remaining_ship_1;
    uint8_t remaining_ship_2;
    uint8_t remaining_ship_3;
    AIProfile profile;
    uint32_t rng_state;
    Position last_shot;
    bool last_shot_valid;
    bool awaiting_sunk_details;
} WallshipAgent;

#endif
