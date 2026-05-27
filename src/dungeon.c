#include "dungeon.h"
#include <string.h>

/* LCG RNG local to dungeon gen (reproducible per seed) */
static unsigned int rng_state;

static unsigned int rng(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static int rng_range(int lo, int hi)
{
    return lo + (int)(rng() % (unsigned)(hi - lo + 1));
}

static void dig_room(Dungeon *d, int x, int y, int w, int h)
{
    for (int ry = y; ry < y + h; ry++)
    {
        for (int rx = x; rx < x + w; rx++)
        {
            d->tile[ry][rx] = TILE_FLOOR;
        }
    }
}

static void dig_htunnel(Dungeon *d, int x1, int x2, int y)
{
    int lo = x1 < x2 ? x1 : x2;
    int hi = x1 < x2 ? x2 : x1;
    for (int x = lo; x <= hi; x++)
    {
        if (dungeon_in_bounds(x, y))
        {
            d->tile[y][x] = TILE_FLOOR;
        }
    }
}

static void dig_vtunnel(Dungeon *d, int y1, int y2, int x)
{
    int lo = y1 < y2 ? y1 : y2;
    int hi = y1 < y2 ? y2 : y1;
    for (int y = lo; y <= hi; y++)
    {
        if (dungeon_in_bounds(x, y))
        {
            d->tile[y][x] = TILE_FLOOR;
        }
    }
}

static int rooms_overlap(const Room *a, const Room *b)
{
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x || a->y + a->h <= b->y || b->y + b->h <= a->y);
}

int dungeon_in_bounds(int x, int y)
{
    return x >= 0 && x < MAP_W && y >= 0 && y < MAP_H;
}

int dungeon_passable(const Dungeon *d, int x, int y)
{
    if (!dungeon_in_bounds(x, y))
    {
        return 0;
    }
    return d->tile[y][x] != TILE_WALL;
}

void dungeon_generate(Dungeon *d, int depth, unsigned int seed)
{
    rng_state = seed ^ (unsigned)(depth * 0x9e3779b9u);

    memset(d->tile, TILE_WALL, sizeof(d->tile));
    memset(d->visible, 0, sizeof(d->visible));
    memset(d->explored, 0, sizeof(d->explored));
    d->room_count = 0;

    int attempts = 60;
    while (attempts-- && d->room_count < MAX_ROOMS)
    {
        int rw = rng_range(5, 14);
        int rh = rng_range(4, 9);
        int rx = rng_range(1, MAP_W - rw - 2);
        int ry = rng_range(1, MAP_H - rh - 2);
        Room nr = {rx, ry, rw, rh};

        int ok = 1;
        for (int i = 0; i < d->room_count; i++)
        {
            Room pad = {d->rooms[i].x - 1, d->rooms[i].y - 1, d->rooms[i].w + 2, d->rooms[i].h + 2};
            if (rooms_overlap(&nr, &pad))
            {
                ok = 0;
                break;
            }
        }
        if (!ok)
        {
            continue;
        }

        dig_room(d, rx, ry, rw, rh);

        if (d->room_count > 0)
        {
            /* Connect to previous room with L-corridor */
            Room *prev = &d->rooms[d->room_count - 1];
            int cx1 = prev->x + prev->w / 2, cy1 = prev->y + prev->h / 2;
            int cx2 = rx + rw / 2, cy2 = ry + rh / 2;
            if (rng() & 1)
            {
                dig_htunnel(d, cx1, cx2, cy1);
                dig_vtunnel(d, cy1, cy2, cx2);
            }
            else
            {
                dig_vtunnel(d, cy1, cy2, cx1);
                dig_htunnel(d, cx1, cx2, cy2);
            }
        }
        d->rooms[d->room_count++] = nr;
    }

    if (d->room_count > 0)
    {
        Room *lr = &d->rooms[d->room_count - 1];
        d->stairs.x = lr->x + lr->w / 2;
        d->stairs.y = lr->y + lr->h / 2;
        d->tile[d->stairs.y][d->stairs.x] = TILE_STAIRS_DN;
    }
}
