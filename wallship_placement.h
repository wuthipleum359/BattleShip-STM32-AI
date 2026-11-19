#ifndef WALLSHIP_PLACEMENT_H
#define WALLSHIP_PLACEMENT_H

#include "wallship_types.h"
#include "wallship_ai.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void WallshipPlacement_PrintBoard(const WallshipAgent *player)
{
    int x, y;
    puts("\nCURRENT BOARD");
    printf("    ");
    for (x = 0; x < BOARD_W; ++x) printf(" %d", x);
    putchar('\n');
    for (y = 0; y < BOARD_H; ++y) {
        printf(" %d  ", y);
        for (x = 0; x < BOARD_W; ++x) {
            uint8_t id = player->own_board[y][x];
            printf(" %c", id != 0 ? (char)('0' + id) : '.');
        }
        putchar('\n');
    }
}

static void WallshipPlacement_ManualSetup(WallshipAgent *player, int player_id, void (*wait_fn)(int))
{
    uint8_t pool[4] = {0, 2, 2, 2};
    uint8_t placed_count = 0;
    char line[80];
    WallshipAI_ClearOwnBoard(player);

    while (placed_count < SHIP_COUNT) {
        int length = 0;
        int x = 0, y = 0;
        char dir_char = 'h';
        ShipOrientation orientation = SHIP_HORIZONTAL;
        bool undo_ship = false;
        WallshipPlacement_PrintBoard(player);
        printf("\n[Player %d Fleet Setup - Ship %d/6]\n", player_id + 1, placed_count + 1);
        printf("Remaining ships -> Length 3: %d | Length 2: %d | Length 1: %d\n", pool[3], pool[2], pool[1]);
        for (;;) {
            printf("Choose ship length (1, 2, 3) or 'u' to Undo previous ship: ");
            fflush(stdout);
            if (fgets(line, sizeof line, stdin) == NULL) continue;
            if (tolower((unsigned char)line[0]) == 'u') {
                undo_ship = true;
                break;
            }
            if (sscanf(line, "%d", &length) == 1 && length >= 1 && length <= 3) {
                if (pool[length] > 0) {
                    break;
                } else {
                    printf("No length %d ships left! Pick another.\n", length);
                }
            } else {
                puts("Invalid input. Enter 1, 2, 3, or 'u'.");
            }
        }
        if (undo_ship) {
            if (placed_count == 0) {
                puts("No ships placed yet! Cannot undo.");
            } else {
                placed_count--;
                Ship *last_ship = &player->ships[placed_count];
                pool[last_ship->length]++;
                for (int i = 0; i < last_ship->length; ++i) {
                    int nx = last_ship->start_x + (last_ship->orientation == SHIP_HORIZONTAL ? i : 0);
                    int ny = last_ship->start_y + (last_ship->orientation == SHIP_VERTICAL ? i : 0);
                    player->own_board[ny][nx] = 0;
                }  
                memset(last_ship, 0, sizeof(Ship));
                printf(">>> Undo successful: Removed Ship %d <<<\n", placed_count + 1);
            }
            continue;
        }
        bool back_to_len = false;
        for (;;) {
            if (length == 1) {
                printf("Enter coord (x y, e.g. 2 3) or 'b' to go back: ");
                fflush(stdout);
                if (fgets(line, sizeof line, stdin) == NULL) continue;
                if (tolower((unsigned char)line[0]) == 'b') {
                    back_to_len = true;
                    break;
                }
                if (sscanf(line, "%d %d", &x, &y) != 2) {
                    puts("Invalid input. Format: x y");
                    continue;
                }
                dir_char = 'h';
            } else {
                printf("Enter coord and dir (x y h/v, e.g. 2 3 h) or 'b' to go back: ");
                fflush(stdout);
                if (fgets(line, sizeof line, stdin) == NULL) continue;
                if (tolower((unsigned char)line[0]) == 'b') {
                    back_to_len = true;
                    break;
                }
                if (sscanf(line, "%d %d %c", &x, &y, &dir_char) != 3) {
                    puts("Invalid input. Format: x y h/v");
                    continue;
                }
            }
            dir_char = (char)tolower((unsigned char)dir_char);
            if (dir_char != 'h' && dir_char != 'v') {
                puts("Direction must be 'h' (horizontal) or 'v' (vertical).");
                continue;
            }
            orientation = (dir_char == 'h') ? SHIP_HORIZONTAL : SHIP_VERTICAL;
            if (!WallshipAI_IsValidShipPosition(player, x, y, (uint8_t)length, orientation)) {
                puts("Invalid position! (Out of bounds or overlapping another ship). Try again.");
                continue;
            }
            break;
        }
        if (back_to_len) {
            continue;
        }
        Ship *s = &player->ships[placed_count];
        s->id = (uint8_t)(placed_count + 1);
        s->length = (uint8_t)length;
        s->hp = (uint8_t)length;
        s->sunk = false;
        s->start_x = (uint8_t)x;
        s->start_y = (uint8_t)y;
        s->orientation = orientation;
        for (int i = 0; i < length; ++i) {
            int nx = x + (orientation == SHIP_HORIZONTAL ? i : 0);
            int ny = y + (orientation == SHIP_VERTICAL ? i : 0);
            player->own_board[ny][nx] = s->id;
        }
        pool[length]--;
        placed_count++;
    }
    WallshipPlacement_PrintBoard(player);
    printf("\nPlayer %d fleet setup complete!\n", player_id + 1);
    if (wait_fn != NULL) {
        wait_fn(player_id == 0 ? 1 : 0);
    }
}

#endif