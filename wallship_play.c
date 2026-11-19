/* Desktop-only terminal game. The Wallship AI core stays HAL-independent. */
/*to demo run "powershell -NoProfile -ExecutionPolicy Bypass -File .\play_wallship.ps1 balanced 123" in powershell*/
//add --reveal to show AI generate position ship
#include "wallship_ai.h"
#include "wallship_game_rules.h"
#include "wallship_placement.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void print_grid_header(void)
{
    int x;
    printf("    ");
    for (x = 0; x < BOARD_W; ++x) {
        printf(" %d", x);
    }
    printf("          ");
    for (x = 0; x < BOARD_W; ++x) {
        printf(" %d", x);
    }
    putchar('\n');
}

static char radar_symbol(const WallshipAgent *ai, int x, int y, bool reveal)
{
    const uint8_t id = ai->own_board[y][x];
    if (ai->human_shot_board[y][x] == 0) {
        return reveal && id != 0 ? (char)('0' + id) : '.';
    }
    return id == 0 ? 'o' : (ai->ships[id-1].sunk ? '#' : 'X');
}

static void print_boards(const WallshipAgent *human, const WallshipAgent *ai,
                         bool reveal)
{
    int x, y;
    puts("\nYOUR FLEET (ship IDs shown)       ENEMY WATERS (your attacks)");
    print_grid_header();
    for (y = 0; y < BOARD_H; ++y) {
        printf(" %d  ", y);
        for (x = 0; x < BOARD_W; ++x) {
            const uint8_t id = human->own_board[y][x];
            const bool fired = human->human_shot_board[y][x] != 0;
            const char symbol = !fired ? (id != 0 ? (char)('0' + id) : '.')
                : (id == 0 ? 'o' : (human->ships[id-1].sunk ? '#' : 'X'));
            printf(" %c", symbol);
        }
        printf("       %d  ", y);
        for (x = 0; x < BOARD_W; ++x) {
            printf(" %c", radar_symbol(ai, x, y, reveal));
        }
        putchar('\n');
    }
    puts("Legend: . unknown/water, o miss, X hit, # sunk, 1..6 your ships");
}

static const char *result_name(ShotResult result)
{
    switch (result) {
    case SHOT_MISS: return "MISS";
    case SHOT_HIT: return "HIT - shoot again";
    case SHOT_SUNK: return "SUNK - shoot again";
    default: return "INVALID";
    }
}

static uint8_t ship_cells(const Ship *ship, Position cells[MAX_SHIP_LENGTH])
{
    uint8_t i;
    for (i = 0; i < ship->length; ++i) {
        cells[i].x = (uint8_t)(ship->start_x +
            (ship->orientation == SHIP_HORIZONTAL ? i : 0));
        cells[i].y = (uint8_t)(ship->start_y +
            (ship->orientation == SHIP_VERTICAL ? i : 0));
    }
    return ship->length;
}

/* Only the CLI reads stdin. A malformed line or duplicate shot does not
 * consume the human turn. An EOF/quit request ends the simulation cleanly.
 */
static bool parse_shot_line(const char *line, Position *p)
{
    char *end;
    const char *start;
    long x, y;
    errno = 0;
    x = strtol(line, &end, 10);
    if (end == line || errno == ERANGE || x < 0 || x >= BOARD_W) {
        return false;
    }
    start = end;
    errno = 0;
    y = strtol(start, &end, 10);
    if (end == start || errno == ERANGE || y < 0 || y >= BOARD_H) {
        return false;
    }
    while (isspace((unsigned char)*end)) { ++end; }
    if (*end != '\0') { return false; }
    p->x = (uint8_t)x;
    p->y = (uint8_t)y;
    return true;
}

static bool read_human_shot(const WallshipAgent *ai, Position *p)
{
    char line[80];
    for (;;) {
        fputs("Your shot (x y, e.g. 3 2; q to quit): ", stdout);
        fflush(stdout);
        if (fgets(line, sizeof line, stdin) == NULL) {
            return false;
        }
        if (line[0] == 'q' || line[0] == 'Q') {
            return false;
        }
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) { }
            puts("Input is too long. Enter two coordinates.");
            continue;
        }
        if (!parse_shot_line(line, p)) {
            puts("Use x=0..7 and y=0..6, separated by a space.");
            continue;
        }
        if (ai->human_shot_board[p->y][p->x] != 0) {
            puts("You have already attacked that cell. Choose another.");
            continue;
        }
        return true;
    }
}

static bool parse_profile(const char *arg, AIProfile *profile)
{
    if (strcmp(arg, "random") == 0) { *profile = AI_RANDOM; }
    else if (strcmp(arg, "balanced") == 0) { *profile = AI_BALANCED; }
    else if (strcmp(arg, "aggressive") == 0) { *profile = AI_AGGRESSIVE; }
    else { return false; }
    return true;
}

static bool parse_seed(const char *arg, uint32_t *seed)
{
    char *end;
    unsigned long value;
    if (arg[0] == '\0' || arg[0] == '-') { return false; }
    errno = 0;
    value = strtoul(arg, &end, 10);
    if (errno == ERANGE || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    *seed = (uint32_t)value;
    return true;
}

static uint32_t automatic_seed(void)
{
    uint32_t seed = (uint32_t)time(NULL) ^ (uint32_t)clock();
#ifdef _WIN32
    FILETIME wall_time;
    LARGE_INTEGER counter;
    GetSystemTimeAsFileTime(&wall_time);
    seed ^= wall_time.dwLowDateTime ^ wall_time.dwHighDateTime;
    if (QueryPerformanceCounter(&counter) != 0) {
        const uint64_t ticks = (uint64_t)counter.QuadPart;
        seed ^= (uint32_t)ticks ^ (uint32_t)(ticks >> 32);
    }
#endif
    return seed != 0 ? seed : WALLSHIP_DEFAULT_SEED;
}

static unsigned sunk_count(const WallshipAgent *player)
{
    unsigned count = 0;
    int i;
    for (i = 0; i < SHIP_COUNT; ++i) {
        if (player->ships[i].sunk) { ++count; }
    }
    return count;
}

static void wait_for_next_player(int next_player_id) {
    printf("\n--- Press ENTER to pass turn to Player %d ---\n", next_player_id + 1);
    while (getchar() != '\n');
    system("cls");
}

static int play_pve(AIProfile profile, uint32_t seed, bool reveal)
{
    WallshipAgent ai, human;
    WallshipTwoPlayerGame game;
    WallshipAI_Init(&ai, seed, profile);
    WallshipAI_Init(&human, seed ^ UINT32_C(0x9E3779B9), AI_RANDOM);
    if (!WallshipAI_PlaceShips(&ai)) {
        fputs("Could not place AI ships. Start again with another seed.\n", stderr);
        return 1;
    }
    WallshipPlacement_ManualSetup(&human, 0, NULL);
    if (!WallshipTwoPlayerGame_Init(&game, &human, &ai, 0)) {
        fputs("Could not start game; fleet state is inconsistent.\n", stderr);
        return 1;
    }
    printf("Wallship: 8x7, fleets 3+3+2+2+1+1; seed=%lu\n",
           (unsigned long)seed);
    puts("You start. HIT and SUNK grant another shot; MISS passes the turn.");
    if (reveal) {
        puts("DEBUG REVEAL: AI ship IDs are visible on the right board.");
    }
    print_boards(&human, &ai, reveal);

    while (!game.finished) {
        if (game.current_player == 0) {
            Position p;
            uint8_t length = 0;
            ShotResult result;
            if (!read_human_shot(&ai, &p)) {
                puts("Game ended by player.");
                return 0;
            }
            result = WallshipTwoPlayerGame_Shoot(&game, 0, p.x, p.y, &length);
            if (result == SHOT_INVALID) {
                puts("Shot rejected. Try another cell.");
                continue;
            }
            printf("You fired at (%u,%u): %s", (unsigned)p.x, (unsigned)p.y,
                   result_name(result));
            if (result == SHOT_SUNK) {
                printf(" (length %u, %u/6 ships)", (unsigned)length,
                       sunk_count(&ai));
            }
            putchar('\n');
            print_boards(&human, &ai, reveal);
        } else {
            Position p = WallshipAI_SelectShot(&ai);
            uint8_t length = 0;
            ShotResult result;
            if (p.x == INVALID_COORD || p.y == INVALID_COORD) {
                fputs("AI has no available target; game state is inconsistent.\n", stderr);
                return 1;
            }
            result = WallshipTwoPlayerGame_Shoot(&game, 1, p.x, p.y, &length);
            if (result == SHOT_INVALID) {
                fputs("AI shot was rejected; game state is inconsistent.\n", stderr);
                return 1;
            }
            WallshipAI_ReportShotResult(&ai, p.x, p.y, result);
            if (result == SHOT_SUNK) {
                Position cells[MAX_SHIP_LENGTH];
                const uint8_t id = human.own_board[p.y][p.x];
                const uint8_t count = ship_cells(&human.ships[id-1], cells);
                WallshipAI_ReportSunkShip(&ai, length, cells, count);
                if (ai.awaiting_sunk_details) {
                    fputs("SUNK details were rejected; game state is inconsistent.\n", stderr);
                    return 1;
                }
            }
            printf("AI fired at (%u,%u): %s", (unsigned)p.x, (unsigned)p.y,
                   result_name(result));
            if (result == SHOT_SUNK) {
                printf(" (length %u, %u/6 ships)", (unsigned)length,
                       sunk_count(&human));
            }
            putchar('\n');
            print_boards(&human, &ai, reveal);
        }
    }
    puts(game.winner == 0 ? "You win!" : "AI wins!");
    return 0;
}

static int play_pvp(uint32_t seed, bool reveal)
{
    WallshipAgent player1, player2;
    WallshipTwoPlayerGame game;

    WallshipAI_Init(&player1, seed, AI_RANDOM);
    WallshipAI_Init(&player2, seed ^ UINT32_C(0x9E3779B9), AI_RANDOM);
    printf("=== PLAYER 1 FLEET SETUP ===\n");
    WallshipPlacement_ManualSetup(&player1, 0, wait_for_next_player);
    printf("=== PLAYER 2 FLEET SETUP ===\n");
    WallshipPlacement_ManualSetup(&player2, 1, wait_for_next_player);
    if (!WallshipTwoPlayerGame_Init(&game, &player1, &player2, 0)) {
        fputs("Could not start game; fleet state is inconsistent.\n", stderr);
        return 1;
    }
    printf("Wallship PvP: 8x7, fleets 3+3+2+2+1+1; seed=%lu\n", (unsigned long)seed);
    puts("Player 1 starts. HIT and SUNK grant another shot; MISS passes the turn.");
    
    wait_for_next_player(0);

    while (!game.finished) {
        Position p;
        uint8_t length = 0;
        ShotResult result;
        if (game.current_player == 0) {
            printf("\n>>> PLAYER 1 TURN <<<\n");
            print_boards(&player1, &player2, reveal);
            if (!read_human_shot(&player2, &p)) {
                puts("Game ended by player.");
                return 0;
            }
            result = WallshipTwoPlayerGame_Shoot(&game, 0, p.x, p.y, &length);
            if (result == SHOT_INVALID) {
                puts("Shot rejected. Try another cell.");
                continue;
            }
            printf("Player 1 fired at (%u,%u): %s", (unsigned)p.x, (unsigned)p.y, result_name(result));
            if (result == SHOT_SUNK) {
                printf(" (length %u, %u/6 ships)", (unsigned)length, sunk_count(&player2));
            }
            putchar('\n');
            if (result == SHOT_MISS && !game.finished) {
                wait_for_next_player(1);
            }
        } else {
            printf("\n>>> PLAYER 2 TURN <<<\n");
            print_boards(&player2, &player1, reveal);
            if (!read_human_shot(&player1, &p)) {
                puts("Game ended by player.");
                return 0;
            }
            result = WallshipTwoPlayerGame_Shoot(&game, 1, p.x, p.y, &length);
            if (result == SHOT_INVALID) {
                puts("Shot rejected. Try another cell.");
                continue;
            }
            printf("Player 2 fired at (%u,%u): %s", (unsigned)p.x, (unsigned)p.y, result_name(result));
            if (result == SHOT_SUNK) {
                printf(" (length %u, %u/6 ships)", (unsigned)length, sunk_count(&player1));
            }
            putchar('\n');
            if (result == SHOT_MISS && !game.finished) {
                wait_for_next_player(0);
            }
        }
    }
    
    puts(game.winner == 0 ? "\n*** Player 1 wins! ***" : "\n*** Player 2 wins! ***");
    return 0;
}

int main(int argc, char **argv)
{
    AIProfile profile = AI_BALANCED;
    uint32_t seed = automatic_seed();
    bool profile_given = false, seed_given = false, reveal = false;
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--reveal") == 0 && !reveal) {
            reveal = true;
        } else if (!profile_given && parse_profile(argv[i], &profile)) {
            profile_given = true;
        } else if (!seed_given && parse_seed(argv[i], &seed)) {
            seed_given = true;
        } else {
            fprintf(stderr,
                    "Usage: %s [random|balanced|aggressive] [seed: 0..4294967295] [--reveal]\n",
                    argv[0]);
            return 2;
        }
    }
    if (seed == 0) { seed = WALLSHIP_DEFAULT_SEED; }
    int mode = 0;
    char line[80];
    for (;;) {
        printf("\n================================\n");
        printf("   WALLSHIP TERMINAL EDITION    \n");
        printf("================================\n");
        printf(" 1. Player vs AI (PvE)\n");
        printf(" 2. Player vs Player (PvP Hotseat)\n");
        printf("================================\n");
        printf("Select game mode (1 or 2): ");
        fflush(stdout);
        if (fgets(line, sizeof line, stdin) == NULL) {
            return 0;
        }
        if (sscanf(line, "%d", &mode) == 1 && (mode == 1 || mode == 2)) {
            break;
        }
        puts("Invalid choice. Please enter 1 or 2.");
    }
    if (mode == 1) {
        int diff = 0;
        for (;;) {
            printf("\n--- Select AI Difficulty ---\n");
            printf(" 1. Random (Easy)\n");
            printf(" 2. Balanced (Normal)\n");
            printf(" 3. Aggressive (Hard)\n");
            printf("Choice (1-3): ");
            fflush(stdout);
            
            if (fgets(line, sizeof line, stdin) == NULL) {
                return 0;
            }
            if (sscanf(line, "%d", &diff) == 1 && diff >= 1 && diff <= 3) {
                if (diff == 1) profile = AI_RANDOM;
                else if (diff == 2) profile = AI_BALANCED;
                else if (diff == 3) profile = AI_AGGRESSIVE;
                break;
            }
            puts("Invalid choice. Please enter 1, 2, or 3.");
        }
        return play_pve(profile, seed, reveal);
    } else {
        return play_pvp(seed, reveal);
    }
}
