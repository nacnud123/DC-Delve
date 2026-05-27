#pragma once
#include <stdint.h>

#define MAP_W 80
#define MAP_H 22 /* bottom 8 rows reserved for UI */

#define TILE_WALL 0
#define TILE_FLOOR 1
#define TILE_STAIRS_DN 2
#define TILE_DOOR 3

#define MAX_ROOMS 20
#define MAX_ITEMS 64
#define MAX_MOBS 32

typedef struct {
    int x, y;
} Point;

typedef struct {
    int x, y, w, h;
} Room;

typedef struct {
    uint8_t tile[MAP_H][MAP_W];
    uint8_t visible[MAP_H][MAP_W];  /* currently in FOV */
    uint8_t explored[MAP_H][MAP_W]; /* ever seen */
    Room rooms[MAX_ROOMS];
    int room_count;
    Point stairs;
} Dungeon;

void dungeon_generate(Dungeon *d, int depth, unsigned int seed);
int dungeon_passable(const Dungeon *d, int x, int y);
int dungeon_in_bounds(int x, int y);
