#ifndef WALLSHIP_GAME_RULES_H
#define WALLSHIP_GAME_RULES_H

#include "wallship_types.h"

#include <stddef.h>

/* The defender owns the board and shot history. Preview never changes them.
 * A fresh final hit returns SHOT_SUNK; a repeat or invalid position returns
 * SHOT_INVALID. Coordinates use board[y][x].
 */
static inline ShotResult WallshipRules_PreviewShot(const WallshipAgent *defender,
                                                   uint8_t x, uint8_t y)
{
    uint8_t id;
    const Ship *ship;
    if (defender == NULL || x >= BOARD_W || y >= BOARD_H ||
        defender->human_shot_board[y][x] != 0) {
        return SHOT_INVALID;
    }
    id = defender->own_board[y][x];
    if (id > SHIP_COUNT) {
        return SHOT_INVALID;
    }
    if (id == 0) {
        return SHOT_MISS;
    }
    ship = &defender->ships[id-1];
    if (ship->id != id || ship->length == 0 ||
        ship->length > MAX_SHIP_LENGTH || ship->hp == 0 ||
        ship->hp > ship->length || ship->sunk) {
        return SHOT_INVALID;
    }
    return ship->hp == 1 ? SHOT_SUNK : SHOT_HIT;
}

/* Apply a validated shot once. Invalid shots leave the defender unchanged.
 * sunk_ship_length is optional and is zero unless the result is SHOT_SUNK.
 */
static inline ShotResult WallshipRules_ProcessShot(WallshipAgent *defender,
                                                   uint8_t x, uint8_t y,
                                                   uint8_t *sunk_ship_length)
{
    const ShotResult result = WallshipRules_PreviewShot(defender, x, y);
    if (sunk_ship_length != NULL) {
        *sunk_ship_length = 0;
    }
    if (result == SHOT_INVALID) {
        return result;
    }
    defender->human_shot_board[y][x] = 1;
    if (result != SHOT_MISS) {
        Ship *ship = &defender->ships[defender->own_board[y][x]-1];
        --ship->hp;
        if (result == SHOT_SUNK) {
            ship->sunk = true;
            if (sunk_ship_length != NULL) {
                *sunk_ship_length = ship->length;
            }
        }
    }
    return result;
}

static inline bool WallshipRules_ShouldContinueTurn(ShotResult result)
{
    return result == SHOT_HIT || result == SHOT_SUNK;
}

/* A zero-initialized or incomplete fleet is not a victory. */
static inline bool WallshipRules_AllShipsSunk(const WallshipAgent *defender)
{
    int i;
    if (defender == NULL) {
        return false;
    }
    for (i = 0; i < SHIP_COUNT; ++i) {
        const Ship *ship = &defender->ships[i];
        if (ship->id != (uint8_t)(i + 1) || ship->length == 0 ||
            ship->hp != 0 || !ship->sunk) {
            return false;
        }
    }
    return true;
}

/* Start only with complete fleets. The caller may use WallshipAI_PlaceShips
 * or provide an equivalent board and ship records.
 */
static inline bool WallshipRules_FleetReady(const WallshipAgent *player)
{
    uint8_t cells[SHIP_COUNT] = {0};
    int i, j, x, y;
    if (player == NULL) {
        return false;
    }
    for (i = 0; i < SHIP_COUNT; ++i) {
        const Ship *ship = &player->ships[i];
        if (ship->id != (uint8_t)(i + 1) || ship->length == 0 ||
            ship->length > MAX_SHIP_LENGTH || ship->hp != ship->length ||
            ship->sunk ||
            (ship->orientation != SHIP_HORIZONTAL &&
             ship->orientation != SHIP_VERTICAL)) {
            return false;
        }
        for (j = 0; j < ship->length; ++j) {
            const int sx = ship->start_x +
                (ship->orientation == SHIP_HORIZONTAL ? j : 0);
            const int sy = ship->start_y +
                (ship->orientation == SHIP_VERTICAL ? j : 0);
            if (sx >= BOARD_W || sy >= BOARD_H ||
                player->own_board[sy][sx] != ship->id) {
                return false;
            }
        }
    }
    for (y = 0; y < BOARD_H; ++y) {
        for (x = 0; x < BOARD_W; ++x) {
            const uint8_t id = player->own_board[y][x];
            if (id > SHIP_COUNT || player->human_shot_board[y][x] != 0) {
                return false;
            }
            if (id != 0) {
                ++cells[id-1];
            }
        }
    }
    for (i = 0; i < SHIP_COUNT; ++i) {
        if (cells[i] != player->ships[i].length) {
            return false;
        }
    }
    return true;
}

typedef struct {
    WallshipAgent *players[2];
    uint8_t current_player;
    bool finished;
    uint8_t winner;
} WallshipTwoPlayerGame;

/* first_player is 0 or 1. Players must have separate, ready fleets. */
static inline bool WallshipTwoPlayerGame_Init(WallshipTwoPlayerGame *game,
                                               WallshipAgent *player0,
                                               WallshipAgent *player1,
                                               uint8_t first_player)
{
    if (game == NULL || player0 == NULL || player1 == NULL ||
        player0 == player1 || first_player > 1 ||
        !WallshipRules_FleetReady(player0) || !WallshipRules_FleetReady(player1)) {
        return false;
    }
    game->players[0] = player0;
    game->players[1] = player1;
    game->current_player = first_player;
    game->finished = false;
    game->winner = INVALID_COORD;
    return true;
}

/* shooter must be the active player (0 or 1). Shoot at the OTHER player's
 * board. MISS passes the turn; HIT and SUNK retain it. INVALID leaves turn
 * and boards unchanged. After the final ship sinks, finished is true and
 * winner is the shooting player's index.
 */
static inline ShotResult WallshipTwoPlayerGame_Shoot(WallshipTwoPlayerGame *game,
                                                     uint8_t shooter,
                                                     uint8_t x, uint8_t y,
                                                     uint8_t *sunk_ship_length)
{
    ShotResult result;
    WallshipAgent *defender;
    if (sunk_ship_length != NULL) {
        *sunk_ship_length = 0;
    }
    if (game == NULL || game->finished || game->current_player > 1 ||
        shooter != game->current_player ||
        game->players[0] == NULL || game->players[1] == NULL) {
        return SHOT_INVALID;
    }
    defender = game->players[1U - shooter];
    result = WallshipRules_ProcessShot(defender, x, y, sunk_ship_length);
    if (result == SHOT_INVALID) {
        return result;
    }
    if (result == SHOT_SUNK && WallshipRules_AllShipsSunk(defender)) {
        game->finished = true;
        game->winner = shooter;
    } else if (!WallshipRules_ShouldContinueTurn(result)) {
        game->current_player = (uint8_t)(1U - game->current_player);
    }
    return result;
}

#endif
