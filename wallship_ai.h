#ifndef WALLSHIP_AI_H
#define WALLSHIP_AI_H

#include "wallship_types.h"
#include "wallship_heuristic.h"
#include "wallship_game_rules.h"

#include <stddef.h>
#include <string.h>

/* Override at compile time to exercise bounded placement failure. */
#ifndef MAX_SHIP_PLACEMENT_ATTEMPTS
#define MAX_SHIP_PLACEMENT_ATTEMPTS 100
#endif
#ifndef MAX_FLEET_PLACEMENT_ATTEMPTS
#define MAX_FLEET_PLACEMENT_ATTEMPTS 10
#endif

/* Call game APIs from one execution context, normally the main loop.
 * Init and NewGame do not place ships; check PlaceShips before play.
 * No HAL, allocation, floating point, or global RNG state is used.
 */
static inline bool WallshipAI_IsInsideBoard(int x, int y)
{
    return x >= 0 && x < BOARD_W && y >= 0 && y < BOARD_H;
}

static inline Position WallshipAI_InvalidPosition(void)
{
    const Position p = {INVALID_COORD, INVALID_COORD};
    return p;
}

static inline uint8_t *WallshipAI_RemainingCounter(WallshipAgent *ai, uint8_t length)
{
    switch (length) {
    case 1: return &ai->remaining_ship_1;
    case 2: return &ai->remaining_ship_2;
    case 3: return &ai->remaining_ship_3;
    default: return NULL;
    }
}

static inline void WallshipAI_ClearOwnBoard(WallshipAgent *ai)
{
    static const uint8_t fleet_lengths[SHIP_COUNT] = {3, 3, 2, 2, 1, 1};
    int i;
    memset(ai->own_board, 0, sizeof ai->own_board);
    memset(ai->human_shot_board, 0, sizeof ai->human_shot_board);
    memset(ai->ships, 0, sizeof ai->ships);
    for (i = 0; i < SHIP_COUNT; ++i) {
        ai->ships[i].id = (uint8_t)(i + 1);
        ai->ships[i].length = fleet_lengths[i];
    }
}

static inline void WallshipAI_ResetGame(WallshipAgent *ai, uint32_t seed)
{
    memset(ai->enemy_board, 0, sizeof ai->enemy_board);
    WallshipAI_ClearOwnBoard(ai);
    ai->remaining_ship_1 = 2;
    ai->remaining_ship_2 = 2;
    ai->remaining_ship_3 = 2;
    ai->rng_state = seed != 0 ? seed : WALLSHIP_DEFAULT_SEED;
    ai->last_shot = WallshipAI_InvalidPosition();
    ai->last_shot_valid = false;
    ai->awaiting_sunk_details = false;
}

static inline void WallshipAI_Init(WallshipAgent *ai, uint32_t seed,
                                  AIProfile profile)
{
    if (ai == NULL) {
        return;
    }
    memset(ai, 0, sizeof *ai);
    ai->profile = (profile >= AI_RANDOM && profile <= AI_AGGRESSIVE)
        ? profile : AI_BALANCED;
    WallshipAI_ResetGame(ai, seed);
}

static inline void WallshipAI_NewGame(WallshipAgent *ai, uint32_t seed)
{
    if (ai != NULL) {
        WallshipAI_ResetGame(ai, seed);
    }
}

static inline bool WallshipAI_IsValidShipPosition(const WallshipAgent *ai,
                                                 int x, int y, uint8_t length,
                                                 ShipOrientation orientation)
{
    const int dx = orientation == SHIP_HORIZONTAL ? 1 : 0;
    const int dy = orientation == SHIP_VERTICAL ? 1 : 0;
    int i;
    for (i = 0; i < length; ++i) {
        const int nx = x + dx * i;
        const int ny = y + dy * i;
        if (!WallshipAI_IsInsideBoard(nx, ny) || ai->own_board[ny][nx] != 0) {
            return false;
        }
    }
    return true;
}

static inline bool WallshipAI_PlaceSingleShip(WallshipAgent *ai, Ship *ship)
{
    int attempt;
    for (attempt = 0; attempt < MAX_SHIP_PLACEMENT_ATTEMPTS; ++attempt) {
        const int x = (int)WallshipAI_RandomBelow(ai, BOARD_W);
        const int y = (int)WallshipAI_RandomBelow(ai, BOARD_H);
        const ShipOrientation orientation =
            (ShipOrientation)WallshipAI_RandomBelow(ai, 2);
        int i;
        if (!WallshipAI_IsValidShipPosition(ai, x, y, ship->length, orientation)) {
            continue;
        }
        ship->start_x = (uint8_t)x;
        ship->start_y = (uint8_t)y;
        ship->orientation = ship->length == 1 ? SHIP_HORIZONTAL : orientation;
        ship->hp = ship->length;
        ship->sunk = false;
        for (i = 0; i < ship->length; ++i) {
            const int nx = x + (ship->orientation == SHIP_HORIZONTAL ? i : 0);
            const int ny = y + (ship->orientation == SHIP_VERTICAL ? i : 0);
            ai->own_board[ny][nx] = ship->id;
        }
        return true;
    }
    return false;
}

/* Clear any previous fleet before each attempt. On failure, leave it empty. */
static inline bool WallshipAI_PlaceShips(WallshipAgent *ai)
{
    int attempt;
    if (ai == NULL) {
        return false;
    }
    for (attempt = 0; attempt < MAX_FLEET_PLACEMENT_ATTEMPTS; ++attempt) {
        int i;
        WallshipAI_ClearOwnBoard(ai);
        for (i = 0; i < SHIP_COUNT; ++i) {
            if (!WallshipAI_PlaceSingleShip(ai, &ai->ships[i])) {
                break;
            }
        }
        if (i == SHIP_COUNT) {
            return true;
        }
    }
    WallshipAI_ClearOwnBoard(ai);
    return false;
}

static inline Position WallshipAI_ChooseRandomUnknown(WallshipAgent *ai)
{
    Position best = WallshipAI_InvalidPosition();
    uint32_t count = 0;
    int x, y;
    for (y = 0; y < BOARD_H; ++y) {
        for (x = 0; x < BOARD_W; ++x) {
            if (ai->enemy_board[y][x] == CELL_UNKNOWN) {
                ++count;
                if (WallshipAI_RandomBelow(ai, count) == 0) {
                    best.x = (uint8_t)x;
                    best.y = (uint8_t)y;
                }
            }
        }
    }
    return best;
}

static inline Position WallshipAI_ChooseHighestScore(WallshipAgent *ai,
                                     int32_t score[BOARD_H][BOARD_W])
{
    Position best = WallshipAI_InvalidPosition();
    int32_t best_score = INT32_MIN;
    uint32_t ties = 0;
    int x, y;
    for (y = 0; y < BOARD_H; ++y) {
        for (x = 0; x < BOARD_W; ++x) {
            if (ai->enemy_board[y][x] != CELL_UNKNOWN) {
                continue;
            }
            if (score[y][x] > best_score) {
                best_score = score[y][x];
                ties = 1;
                best.x = (uint8_t)x;
                best.y = (uint8_t)y;
            } else if (score[y][x] == best_score) {
                ++ties;
                if (WallshipAI_RandomBelow(ai, ties) == 0) {
                    best.x = (uint8_t)x;
                    best.y = (uint8_t)y;
                }
            }
        }
    }
    return best;
}

/* Reserve one UNKNOWN cell. Return {255,255} while waiting or exhausted. */
static inline Position WallshipAI_SelectShot(WallshipAgent *ai)
{
    Position selected;
    if (ai == NULL || ai->last_shot_valid || ai->awaiting_sunk_details) {
        return WallshipAI_InvalidPosition();
    }
    if (ai->profile == AI_RANDOM) {
        selected = WallshipAI_ChooseRandomUnknown(ai);
    } else {
        int32_t score[BOARD_H][BOARD_W];
        WallshipAI_CalculateScores(ai, score);
        selected = WallshipAI_ChooseHighestScore(ai, score);
    }
    ai->last_shot = selected;
    ai->last_shot_valid = WallshipAI_IsInsideBoard(selected.x, selected.y);
    return selected;
}

/* Only feedback for the pending coordinate is accepted. SUNK awaits details. */
static inline void WallshipAI_ReportShotResult(WallshipAgent *ai,
                                               uint8_t x, uint8_t y,
                                               ShotResult result)
{
    if (ai == NULL || !WallshipAI_IsInsideBoard(x, y) || !ai->last_shot_valid ||
        ai->awaiting_sunk_details || ai->last_shot.x != x || ai->last_shot.y != y ||
        ai->enemy_board[y][x] != CELL_UNKNOWN ||
        (result != SHOT_MISS && result != SHOT_HIT && result != SHOT_SUNK)) {
        return;
    }
    ai->enemy_board[y][x] = result == SHOT_MISS ? CELL_MISS : CELL_HIT;
    ai->awaiting_sunk_details = result == SHOT_SUNK;
    ai->last_shot_valid = ai->awaiting_sunk_details;
}

/* After SHOT_SUNK, supply every cell of the straight contiguous ship. */
static inline void WallshipAI_ReportSunkShip(WallshipAgent *ai,
                                             uint8_t ship_length,
                                             const Position *ship_cells,
                                             uint8_t cell_count)
{
    uint8_t *remaining;
    int i, j;
    int min_x = BOARD_W, min_y = BOARD_H, max_x = -1, max_y = -1;
    bool includes_last = false;
    if (ai == NULL || ship_cells == NULL || !ai->last_shot_valid ||
        !ai->awaiting_sunk_details || ship_length < 1 ||
        ship_length > MAX_SHIP_LENGTH || cell_count != ship_length) {
        return;
    }
    remaining = WallshipAI_RemainingCounter(ai, ship_length);
    if (*remaining == 0) {
        return;
    }
    for (i = 0; i < cell_count; ++i) {
        const int x = ship_cells[i].x;
        const int y = ship_cells[i].y;
        if (!WallshipAI_IsInsideBoard(x, y) || ai->enemy_board[y][x] != CELL_HIT) {
            return;
        }
        for (j = 0; j < i; ++j) {
            if (ship_cells[j].x == x && ship_cells[j].y == y) {
                return;
            }
        }
        if (ai->last_shot.x == x && ai->last_shot.y == y) {
            includes_last = true;
        }
        if (x < min_x) { min_x = x; }
        if (x > max_x) { max_x = x; }
        if (y < min_y) { min_y = y; }
        if (y > max_y) { max_y = y; }
    }
    if (!includes_last ||
        !((min_y == max_y && max_x-min_x+1 == ship_length) ||
          (min_x == max_x && max_y-min_y+1 == ship_length))) {
        return;
    }
    for (i = 0; i < cell_count; ++i) {
        ai->enemy_board[ship_cells[i].y][ship_cells[i].x] = CELL_SUNK;
    }
    --*remaining;
    ai->last_shot_valid = false;
    ai->awaiting_sunk_details = false;
}

/* Compatibility names for the shared game rules. */
static inline ShotResult WallshipAI_ProcessHumanShot(WallshipAgent *ai,
                                                     uint8_t x, uint8_t y,
                                                     uint8_t *sunk_ship_length)
{
    return WallshipRules_ProcessShot(ai, x, y, sunk_ship_length);
}

static inline bool WallshipAI_ShouldContinueTurn(ShotResult result)
{
    return WallshipRules_ShouldContinueTurn(result);
}

#endif
