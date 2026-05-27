#include "fov.h"
#include <string.h>

/* Recursive shadowcasting FOV (Bjorn Bergstrom's algorithm) */

static Dungeon *g_dungeon;
static int g_ox, g_oy, g_radius;

static int blocks_light(int x, int y)
{
    if (!dungeon_in_bounds(x, y))
    {
        return 1;
    }

    return g_dungeon->tile[y][x] == TILE_WALL;
}

static void set_visible(int x, int y)
{
    if (!dungeon_in_bounds(x, y))
    {
        return;
    }

    g_dungeon->visible[y][x] = 1;
    g_dungeon->explored[y][x] = 1;
}

/* Multiplier tables for the 8 octants */
static const int mult[4][8] = {
    {1, 0, 0, -1, -1, 0, 0, 1},
    {0, 1, -1, 0, 0, -1, 1, 0},
    {0, 1, 1, 0, 0, -1, -1, 0},
    {1, 0, 0, 1, -1, 0, 0, -1},
};

static void cast_light(int row, float start_slope, float end_slope, int xx, int xy, int yx, int yy)
{
    if (start_slope < end_slope)
    {
        return;
    }

    float next_start = start_slope;
    int blocked = 0;

    for (int dist = row; dist <= g_radius && !blocked; dist++)
    {
        int dy = -dist;
        for (int dx = -dist; dx <= 0; dx++)
        {
            float l_slope = (dx - 0.5f) / (dy + 0.5f);
            float r_slope = (dx + 0.5f) / (dy - 0.5f);
            if (start_slope < r_slope)
            {
                continue;
            }

            if (end_slope > l_slope)
            {
                break;
            }

            int mx = g_ox + dx * xx + dy * xy;
            int my = g_oy + dx * yx + dy * yy;
            set_visible(mx, my);

            if (blocked)
            {
                if (blocks_light(mx, my))
                {
                    next_start = r_slope;
                }
                else
                {
                    blocked = 0;
                    start_slope = next_start;
                }
            }
            else if (blocks_light(mx, my) && dist < g_radius)
            {
                blocked = 1;
                cast_light(dist + 1, start_slope, l_slope, xx, xy, yx, yy);
                next_start = r_slope;
            }
        }
    }
}

void fov_compute(Dungeon *d, int ox, int oy, int radius)
{
    memset(d->visible, 0, sizeof(d->visible));
    g_dungeon = d;
    g_ox = ox;
    g_oy = oy;
    g_radius = radius;
    set_visible(ox, oy);
    for (int oct = 0; oct < 8; oct++)
    {
        cast_light(1, 1.0f, 0.0f, mult[0][oct], mult[1][oct], mult[2][oct], mult[3][oct]);
    }
}
