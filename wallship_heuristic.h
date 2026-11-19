#ifndef WALLSHIP_HEURISTIC_H
#define WALLSHIP_HEURISTIC_H

#include "wallship_types.h"

typedef struct {
    int32_t hunt;
    int32_t adjacent;
    int32_t line;
    uint32_t jitter;
} WallshipHeuristicConfig;

static inline WallshipHeuristicConfig WallshipAI_ScoreConfig(AIProfile profile)
{
    const WallshipHeuristicConfig balanced = {10, 400, 1200, 5};
    const WallshipHeuristicConfig aggressive = {6, 800, 2000, 3};
    return profile == AI_AGGRESSIVE ? aggressive : balanced;
}

static inline uint32_t WallshipAI_Xorshift32(WallshipAgent *ai)
{
    uint32_t x = ai->rng_state;
    if (x == 0) {
        x = WALLSHIP_DEFAULT_SEED;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ai->rng_state = x;
    return x;
}

/* xorshift32 has UINT32_MAX nonzero outputs. Shift to [0, UINT32_MAX-1]
 * and discard the incomplete bucket to avoid modulo bias. bound must be > 0.
 */
static inline uint32_t WallshipAI_RandomBelow(WallshipAgent *ai, uint32_t bound)
{
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % bound);
    uint32_t value;
    do {
        value = WallshipAI_Xorshift32(ai) - UINT32_C(1);
    } while (value >= limit);
    return value % bound;
}

/* Only UNKNOWN or HIT cells can belong to a candidate ship. Nearby HIT cells
 * outside the candidate do not affect its hit count.
 */
static inline bool WallshipAI_ScoreCandidateHits(const WallshipAgent *ai,
                                                 int x, int y, int dx, int dy,
                                                 int length, int *hits)
{
    int i;
    *hits = 0;
    for (i = 0; i < length; ++i) {
        const int nx = x + dx * i;
        const int ny = y + dy * i;
        CellState state;
        if (nx < 0 || nx >= BOARD_W || ny < 0 || ny >= BOARD_H) {
            return false;
        }
        state = ai->enemy_board[ny][nx];
        if (state != CELL_UNKNOWN && state != CELL_HIT) {
            return false;
        }
        if (state == CELL_HIT) {
            ++*hits;
        }
    }
    return true;
}

/* Computes the complete score map for BALANCED or AGGRESSIVE. ai and score
 * must be non-NULL. The jitter draws advance ai->rng_state. Target level
 * 0 = hunt, 1 = adjacent HIT, 2 = aligned HIT pair. Use the maximum target
 * level per cell so touching ships cannot multiply the target bonus.
 */
static inline void WallshipAI_CalculateScores(WallshipAgent *ai,
                                              int32_t score[BOARD_H][BOARD_W])
{
    const WallshipHeuristicConfig config = WallshipAI_ScoreConfig(ai->profile);
    uint8_t target[BOARD_H][BOARD_W] = {{0}};
    int length, direction, x, y;
    for (y = 0; y < BOARD_H; ++y) {
        for (x = 0; x < BOARD_W; ++x) {
            score[y][x] = 0;
        }
    }
    for (length = 1; length <= MAX_SHIP_LENGTH; ++length) {
        const uint8_t count = length == 1 ? ai->remaining_ship_1
            : (length == 2 ? ai->remaining_ship_2 : ai->remaining_ship_3);
        if (count == 0) {
            continue;
        }
        for (direction = 0; direction < (length == 1 ? 1 : 2); ++direction) {
            const int dx = direction == 0 ? 1 : 0;
            const int dy = direction == 1 ? 1 : 0;
            for (y = 0; y < BOARD_H; ++y) {
                for (x = 0; x < BOARD_W; ++x) {
                    int hits, i;
                    if (!WallshipAI_ScoreCandidateHits(ai, x, y, dx, dy,
                                                       length, &hits)) {
                        continue;
                    }
                    for (i = 0; i < length; ++i) {
                        const int nx = x + dx * i;
                        const int ny = y + dy * i;
                        uint8_t level = 0;
                        if (ai->enemy_board[ny][nx] != CELL_UNKNOWN) {
                            continue;
                        }
                        score[ny][nx] += config.hunt * count;
                        if (hits >= 2) {
                            level = 2;
                        } else if (hits == 1 &&
                            ((i > 0 && ai->enemy_board[ny-dy][nx-dx] == CELL_HIT) ||
                             (i+1 < length && ai->enemy_board[ny+dy][nx+dx] == CELL_HIT))) {
                            level = 1;
                        }
                        if (level > target[ny][nx]) {
                            target[ny][nx] = level;
                        }
                    }
                }
            }
        }
    }
    for (y = 0; y < BOARD_H; ++y) {
        for (x = 0; x < BOARD_W; ++x) {
            if (ai->enemy_board[y][x] == CELL_UNKNOWN) {
                score[y][x] += target[y][x] == 2 ? config.line
                    : (target[y][x] == 1 ? config.adjacent : 0);
                score[y][x] += (int32_t)WallshipAI_RandomBelow(ai, config.jitter + 1);
            }
        }
    }
}

#endif
