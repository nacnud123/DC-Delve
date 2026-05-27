#include "game.h"
#include <dc/sound/sfxmgr.h>
#include <kos/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char hit_wav_data[];
extern const int hit_wav_size;

static sfxhnd_t hit_sfx = SFXHND_INVALID;

typedef struct {
    uint8_t ch;
    uint32_t lit_col;
    uint32_t dim_col;
} TileGlyph;

static const TileGlyph tile_glyph[] = {
    {'#', 0xFF6878A0, 0xFF202535},
    {'.', 0xFF403E35, 0xFF1C1B16},
    {'>', 0xFFFFDD44, 0xFF665500},
    {'+', 0xFFCC9944, 0xFF6E4411},
};

static const uint32_t tile_lit_bg[] = {
    0xFF0A0C16,
    0xFF0D0C08,
    0xFF0D0C08,
    0xFF0C0A06,
};

static unsigned int rng_state_g;

static unsigned int grng(void)
{
    rng_state_g = rng_state_g * 6364136223846793005ull + 1442695040888963407ull;
    return rng_state_g;
}
static int grng_range(int lo, int hi)
{
    return lo + (int)(grng() % (unsigned)(hi - lo + 1));
}

static void consume_item(GameState *g, int slot)
{
    EntityState *es = &g->es;
    es->carry_weight -= es->inv[slot].weight;
    if (es->carry_weight < 0)
    {
        es->carry_weight = 0;
    }

    memmove(&es->inv[slot], &es->inv[slot + 1], sizeof(Item) * (es->inv_count - slot - 1));
    es->inv_count--;
    if (es->weapon_slot == slot)
    {
        es->weapon_slot = -1;
        es->weapon_bonus = 0;
    }
    else if (es->weapon_slot > slot)
    {
        es->weapon_slot--;
    }

    if (es->shield_slot == slot)
    {
        es->shield_slot = -1;
        es->shield_bonus = 0;
    }
    else if (es->shield_slot > slot)
    {
        es->shield_slot--;
    }

    if (es->armor_slot == slot)
    {
        es->armor_slot = -1;
        es->armor_bonus = 0;
    }
    else if (es->armor_slot > slot)
    {
        es->armor_slot--;
    }

    if (g->inv_cursor >= es->inv_count && g->inv_cursor > 0)
    {
        g->inv_cursor = es->inv_count - 1;
    }
}

static void apply_fireball(GameState *g, int tx, int ty, int damage)
{
    EntityState *es = &g->es;
    int killed = 0, total = 0;
    for (int i = 0; i < es->mob_count; i++)
    {
        Entity *m = &es->mobs[i];
        if (!m->alive)
        {
            continue;
        }

        if (abs(m->x - tx) > 2 || abs(m->y - ty) > 2)
        {
            continue;
        }

        m->hp -= damage;
        total++;
        if (m->hp <= 0)
        {
            m->alive = 0;
            killed++;
            entity_log(es, "You kill the %s! (+%d xp)", m->name, m->xp_reward);
            entity_grant_xp(es, m->xp_reward);
        }
    }
    entity_log(es, "Fireball! Hit %d, killed %d.", total, killed);

    if (abs(es->player.x - tx) <= 2 && abs(es->player.y - ty) <= 2)
    {
        int self_dmg = damage / 2;
        es->player.hp -= self_dmg;
        entity_log(es, "The blast scorches you for %d HP!", self_dmg);
    }
}

void game_init(GameState *g)
{
    memset(g, 0, sizeof(GameState));
    g->world_seed = (unsigned int)timer_ms_gettime64();
    g->depth = 1;
    g->inv_cursor = 0;
    entity_state_init(&g->es);

    /* Write embedded WAV to /ram/ so snd_sfx_load can open it by path */
    if (hit_sfx == SFXHND_INVALID)
    {
        FILE *f = fopen("/ram/hit.wav", "wb");
        if (f)
        {
            fwrite(hit_wav_data, 1, (size_t)hit_wav_size, f);
            fclose(f);
            hit_sfx = snd_sfx_load("/ram/hit.wav");
        }
    }

    game_new_level(g);
    entity_log(&g->es, "Welcome! Find the stairs (>) to descend.");
}

void game_new_level(GameState *g)
{
    rng_state_g = g->world_seed ^ (unsigned)(g->depth * 0x9e3779b9u);
    dungeon_generate(&g->dungeon, g->depth, g->world_seed);

    Room *r0 = &g->dungeon.rooms[0];
    g->es.player.x = r0->x + r0->w / 2;
    g->es.player.y = r0->y + r0->h / 2;

    g->es.mob_count = 0;
    g->es.item_count = 0;

    /* Inventory indices don't change between floors, so slots stay valid */
    if (g->es.weapon_slot >= 0 && g->es.weapon_slot < g->es.inv_count)
    {
        g->es.weapon_bonus = g->es.inv[g->es.weapon_slot].value;
    }
    else
    {
        g->es.weapon_slot = -1;
        g->es.weapon_bonus = 0;
    }

    if (g->es.shield_slot >= 0 && g->es.shield_slot < g->es.inv_count)
    {
        g->es.shield_bonus = g->es.inv[g->es.shield_slot].value;
    }
    else
    {
        g->es.shield_slot = -1;
        g->es.shield_bonus = 0;
    }

    if (g->es.armor_slot >= 0 && g->es.armor_slot < g->es.inv_count)
    {
        g->es.armor_bonus = g->es.inv[g->es.armor_slot].value;
    }
    else
    {
        g->es.armor_slot = -1;
        g->es.armor_bonus = 0;
    }

    g->es.carry_weight = 0;
    for (int i = 0; i < g->es.inv_count; i++)
    {
        g->es.carry_weight += g->es.inv[i].weight;
    }

    int mob_budget = 4 + g->depth * 2;
    for (int i = 1; i < g->dungeon.room_count && mob_budget > 0; i++)
    {
        Room *rm = &g->dungeon.rooms[i];
        int n = grng_range(1, 3);
        for (int j = 0; j < n && mob_budget > 0; j++)
        {
            int mx = grng_range(rm->x + 1, rm->x + rm->w - 2);
            int my = grng_range(rm->y + 1, rm->y + rm->h - 2);
            if (entity_at(&g->es, mx, my))
            {
                continue;
            }

            int roll = grng_range(0, 99);
            MobKind kind;
            if (g->depth <= 2)
            {
                kind = MOB_RAT;
            }
            else if (roll < 40)
            {
                kind = MOB_GOBLIN;
            }
            else if (roll < 75 || g->depth < 4)
            {
                kind = MOB_ORC;
            }
            else
            {
                kind = MOB_TROLL;
            }

            mob_spawn(&g->es, kind, mx, my);
            mob_budget--;
        }
    }

    int item_count = 3 + g->depth;
    for (int i = 0; i < item_count; i++)
    {
        int ri = grng_range(0, g->dungeon.room_count - 1);
        Room *rm = &g->dungeon.rooms[ri];
        int ix = grng_range(rm->x + 1, rm->x + rm->w - 2);
        int iy = grng_range(rm->y + 1, rm->y + rm->h - 2);

        int roll = grng_range(0, 99);
        ItemKind kind;
        if (roll < 38)
        {
            kind = ITEM_POTION_HP;
        }
        else if (roll < 52)
        {
            kind = ITEM_SWORD;
        }
        else if (roll < 62)
        {
            kind = ITEM_SHIELD;
        }
        else if (roll < 71)
        {
            kind = ITEM_LEATHER_ARMOR;
        }
        else if (roll < 78)
        {
            kind = (g->depth >= 2) ? ITEM_CHAIN_MAIL : ITEM_LEATHER_ARMOR;
        }
        else if (roll < 83)
        {
            kind = (g->depth >= 4) ? ITEM_PLATE_ARMOR : ITEM_CHAIN_MAIL;
        }
        else if (roll < 92)
        {
            kind = ITEM_SCROLL_FIREBALL;
        }
        else
        {
            kind = (g->depth >= 2) ? ITEM_WAND_FIREBALL : ITEM_SCROLL_FIREBALL;
        }

        item_spawn(&g->es, kind, ix, iy);
    }

    fov_compute(&g->dungeon, g->es.player.x, g->es.player.y, FOV_RADIUS);
    g->screen = SCREEN_PLAY;
    g->inv_cursor = 0;
}

static void draw_hp_bar(int x, int y, int hp, int hp_max, int bar_w)
{
    int filled = (hp_max > 0) ? (hp * bar_w / hp_max) : 0;
    if (filled < 0)
    {
        filled = 0;
    }

    if (filled > bar_w)
    {
        filled = bar_w;
    }

    uint32_t bar_col = (hp > hp_max / 2) ? COL_GREEN : (hp > hp_max / 4) ? COL_YELLOW
                                                                         : COL_RED;
    console_put(x, y, '[', COL_GREY, COL_PANEL_BG);
    for (int i = 0; i < bar_w; i++)
    {
        if (i < filled)
        {
            console_put(x + 1 + i, y, 0xDB, bar_col, COL_PANEL_BG);
        }
        else
        {
            console_put(x + 1 + i, y, 0xB0, COL_DARK_GREY, COL_PANEL_BG);
        }
    }
    console_put(x + 1 + bar_w, y, ']', COL_GREY, COL_PANEL_BG);
}

static void render_map(const GameState *g)
{
    const Dungeon *d = &g->dungeon;
    const EntityState *es = &g->es;

    for (int y = 0; y < MAP_H; y++)
    {
        for (int x = 0; x < MAP_W; x++)
        {
            if (!d->explored[y][x])
            {
                continue;
            }

            uint8_t tile = d->tile[y][x];
            int lit = d->visible[y][x];
            uint32_t fg = lit ? tile_glyph[tile].lit_col : tile_glyph[tile].dim_col;
            uint32_t bg = lit ? tile_lit_bg[tile] : COL_BLACK;
            console_put(x, y, tile_glyph[tile].ch, fg, bg);
        }
    }

    for (int i = 0; i < es->item_count; i++)
    {
        const Item *it = &es->items[i];
        if (it->kind == ITEM_NONE || !d->visible[it->y][it->x])
        {
            continue;
        }

        console_put(it->x, it->y, it->ch, it->col, tile_lit_bg[TILE_FLOOR]);
    }

    for (int i = 0; i < es->mob_count; i++)
    {
        const Entity *m = &es->mobs[i];
        if (!m->alive || !d->visible[m->y][m->x])
        {
            continue;
        }

        console_put(m->x, m->y, m->ch, m->col, tile_lit_bg[TILE_FLOOR]);
    }

    if (es->player.alive)
    {
        console_put(es->player.x, es->player.y, '@', COL_WHITE, 0xFF14100A);
    }
}

static void render_ui(const GameState *g)
{
    const EntityState *es = &g->es;
    const Entity *p = &es->player;

    for (int y = MAP_H; y < CON_H; y++)
    {
        console_hline(0, y, CON_W, ' ', COL_WHITE, COL_PANEL_BG);
    }

    console_hline(0, MAP_H, CON_W, 0xC4, 0xFF3A3A5A, COL_PANEL_BG);

    int row = MAP_H + 1;
    int total_atk = p->atk + es->weapon_bonus;
    int total_def = p->def + es->shield_bonus + es->armor_bonus;

    console_print(0, row, "HP", COL_WHITE, COL_PANEL_BG);
    draw_hp_bar(2, row, p->hp, p->hp_max, 16);
    console_printf(20, row, COL_WHITE, COL_PANEL_BG, "%d/%d", p->hp, p->hp_max);
    console_printf(28, row, COL_DARK_GREY, COL_PANEL_BG, "ATK");
    console_printf(32, row, COL_YELLOW, COL_PANEL_BG, "%-3d", total_atk);
    console_printf(36, row, COL_DARK_GREY, COL_PANEL_BG, "DEF");
    console_printf(40, row, COL_CYAN, COL_PANEL_BG, "%-3d", total_def);
    console_printf(44, row, COL_DARK_GREY, COL_PANEL_BG, "Lv");
    console_printf(47, row, COL_YELLOW, COL_PANEL_BG, "%-2d", es->player_level);
    console_printf(59, row, COL_DARK_GREY, COL_PANEL_BG, "Floor");
    console_printf(65, row, COL_CYAN, COL_PANEL_BG, "%-2d", g->depth);

    row = MAP_H + 2;

    if (es->weapon_slot >= 0 && es->weapon_slot < es->inv_count)
    {
        const Item *it = &es->inv[es->weapon_slot];
        console_put(0, row, it->ch, it->col, COL_PANEL_BG);
        console_printf(2, row, COL_WHITE, COL_PANEL_BG, "%-13.13s", it->name);
        console_printf(16, row, COL_GREEN, COL_PANEL_BG, "+%datk", es->weapon_bonus);
    }
    else
    {
        console_printf(0, row, COL_DARK_GREY, COL_PANEL_BG, "/  (no weapon)      ");
    }

    if (es->shield_slot >= 0 && es->shield_slot < es->inv_count)
    {
        const Item *it = &es->inv[es->shield_slot];
        console_put(27, row, it->ch, it->col, COL_PANEL_BG);
        console_printf(29, row, COL_WHITE, COL_PANEL_BG, "%-12.12s", it->name);
        console_printf(42, row, COL_GREEN, COL_PANEL_BG, "+%ddef", es->shield_bonus);
    }
    else
    {
        console_printf(27, row, COL_DARK_GREY, COL_PANEL_BG, "]  (no shield)      ");
    }

    if (es->armor_slot >= 0 && es->armor_slot < es->inv_count)
    {
        const Item *it = &es->inv[es->armor_slot];
        console_put(54, row, it->ch, it->col, COL_PANEL_BG);
        console_printf(56, row, COL_WHITE, COL_PANEL_BG, "%-12.12s", it->name);
        console_printf(69, row, COL_GREEN, COL_PANEL_BG, "+%ddef", es->armor_bonus);
    }
    else
    {
        console_printf(54, row, COL_DARK_GREY, COL_PANEL_BG, "[  (no armour)     ");
    }

    row = MAP_H + 3;
    Item *ground = item_at((EntityState *)&g->es, g->es.player.x, g->es.player.y);
    if (ground && ground->kind != ITEM_NONE)
    {
        console_printf(0, row, COL_YELLOW, COL_PANEL_BG, " [A] pick up: ");
        console_put(14, row, ground->ch, ground->col, COL_PANEL_BG);
        console_printf(16, row, COL_WHITE, COL_PANEL_BG, "%s", ground->name);
    }
    else
    {
        console_print(0, row, " D-pad:move  A:pickup  B:inv  R:stairs", COL_DARK_GREY, COL_PANEL_BG);
    }

    uint32_t wt_col = (es->carry_weight < es->max_carry * 3 / 4) ? COL_GREY : (es->carry_weight < es->max_carry) ? COL_YELLOW
                                                                                                                 : COL_RED;
    console_printf(68, row, wt_col, COL_PANEL_BG, "Wt:%d/%d", es->carry_weight, es->max_carry);

    console_hline(0, MAP_H + 4, CON_W, 0xC4, 0xFF3A3A5A, COL_PANEL_BG);

    static const uint32_t log_fade[] = {
        COL_WHITE,
        COL_LIGHT_GREY,
        COL_GREY,
        COL_DARK_GREY,
        COL_DARK_GREY,
    };

    int log_rows = CON_H - (MAP_H + 5);
    int start = es->log_count > log_rows ? es->log_count - log_rows : 0;
    for (int i = start, r = MAP_H + 5; i < es->log_count && r < CON_H; i++, r++)
    {
        int age = es->log_count - 1 - i;
        uint32_t col = (age >= 0 && age < 5) ? log_fade[age] : COL_DARK_GREY;
        console_print(1, r, es->log[i], col, COL_PANEL_BG);
    }
}

static void render_target_overlay(const GameState *g)
{
    console_put(g->target_x, g->target_y, 'X', COL_BLACK, COL_YELLOW);

    for (int dy = -2; dy <= 2; dy++)
    {
        for (int dx = -2; dx <= 2; dx++)
        {
            if (dx == 0 && dy == 0)
            {
                continue;
            }

            int ax = g->target_x + dx;
            int ay = g->target_y + dy;
            if (ax < 0 || ax >= MAP_W || ay < 0 || ay >= MAP_H)
            {
                continue;
            }

            if (!g->dungeon.explored[ay][ax])
            {
                continue;
            }

            console_put(ax, ay, '~', COL_DARK_RED, 0xFF110000);
        }
    }

    const EntityState *es = &g->es;
    for (int i = 0; i < es->mob_count; i++)
    {
        const Entity *m = &es->mobs[i];
        if (!m->alive || !g->dungeon.visible[m->y][m->x])
        {
            continue;
        }

        if (abs(m->x - g->target_x) > 2 || abs(m->y - g->target_y) > 2)
        {
            continue;
        }

        console_put(m->x, m->y, m->ch, m->col, 0xFF330000);
    }
    if (abs(es->player.x - g->target_x) <= 2 && abs(es->player.y - g->target_y) <= 2)
    {
        console_put(es->player.x, es->player.y, '@', COL_WHITE, 0xFF330000);
    }

    /* Redraw cursor last so it's always on top */
    console_put(g->target_x, g->target_y, 'X', COL_BLACK, COL_YELLOW);

    console_hline(0, MAP_H + 3, CON_W, ' ', COL_WHITE, COL_PANEL_BG);
    Entity *ce = entity_at((EntityState *)&g->es, g->target_x, g->target_y);
    if (ce && ce->kind != MOB_PLAYER)
    {
        console_printf(0, MAP_H + 3, COL_YELLOW, COL_PANEL_BG, "  Target: %s  HP:%d/%d   A:fire  B:cancel", ce->name, ce->hp, ce->hp_max);
    }
    else
    {
        console_printf(0, MAP_H + 3, COL_YELLOW, COL_PANEL_BG, "  [TARGETING]  Blast radius: 2 tiles   A:fire  B:cancel");
    }
}

#define INV_X 16
#define INV_W 48
#define INV_Y 4
#define INV_H 18

static const char *item_action_label(const EntityState *es, int slot)
{
    if (slot < 0 || slot >= es->inv_count)
    {
        return "";
    }
    const Item *it = &es->inv[slot];

    if (it->kind == ITEM_SWORD && es->weapon_slot == slot)
    {
        return "unequip";
    }

    if (it->kind == ITEM_SHIELD && es->shield_slot == slot)
    {
        return "unequip";
    }

    if ((it->kind == ITEM_LEATHER_ARMOR || it->kind == ITEM_CHAIN_MAIL || it->kind == ITEM_PLATE_ARMOR) && es->armor_slot == slot)
    {
        return "unequip";
    }

    if (it->kind == ITEM_WAND_FIREBALL)
    {
        static char wlabel[16];
        snprintf(wlabel, sizeof(wlabel), "aim (%dc)", it->value);
        return wlabel;
    }

    return it->action;
}

static void render_inventory(const GameState *g)
{
    const EntityState *es = &g->es;

    for (int y = INV_Y; y < INV_Y + INV_H; y++)
    {
        console_hline(INV_X, y, INV_W, ' ', COL_WHITE, COL_PANEL_BG);
    }

    console_border(INV_X, INV_Y, INV_W, INV_H, COL_GOLD, COL_PANEL_BG);
    console_print(INV_X + INV_W / 2 - 5, INV_Y, " INVENTORY ", COL_GOLD, COL_PANEL_BG);

    uint32_t wt_col = (es->carry_weight < es->max_carry * 3 / 4) ? COL_GREEN : (es->carry_weight < es->max_carry) ? COL_YELLOW
                                                                                                                  : COL_RED;

    console_printf(INV_X + INV_W - 12, INV_Y, wt_col, COL_PANEL_BG, " %d/%d ", es->carry_weight, es->max_carry);

    if (es->inv_count == 0)
    {
        console_print(INV_X + 2, INV_Y + 2, "(inventory is empty)", COL_DARK_GREY, COL_PANEL_BG);
    }
    else
    {
        int display_rows = INV_H - 6;
        int scroll_start = 0;
        if (g->inv_cursor >= display_rows)
        {
            scroll_start = g->inv_cursor - display_rows + 1;
        }

        for (int i = scroll_start; i < es->inv_count && (i - scroll_start) < display_rows; i++)
        {
            const Item *it = &es->inv[i];
            int row = INV_Y + 2 + (i - scroll_start);
            int selected = (i == g->inv_cursor);
            int is_equip = (es->weapon_slot == i || es->shield_slot == i || es->armor_slot == i);

            uint32_t row_bg = selected ? 0xFF1A1600 : is_equip ? 0xFF071007
                                                               : COL_PANEL_BG;

            console_hline(INV_X + 1, row, INV_W - 2, ' ', COL_WHITE, row_bg);
            console_put(INV_X + 1, row, selected ? '>' : ' ', COL_YELLOW, row_bg);

            uint32_t slot_col = selected ? COL_YELLOW : COL_GREY;
            console_printf(INV_X + 3, row, slot_col, row_bg, "%c)", 'a' + i);

            console_put(INV_X + 6, row, it->ch, it->col, row_bg);

            uint32_t name_col = selected ? COL_WHITE : COL_GREY;
            console_printf(INV_X + 8, row, name_col, row_bg, "%-16.16s", it->name);

            console_printf(INV_X + 25, row, COL_DARK_GREY, row_bg, "%dkg", it->weight);

            if (is_equip)
            {
                console_put(INV_X + 30, row, '*', COL_GREEN, row_bg);
            }

            if (selected)
            {
                const char *lbl = item_action_label(es, i);
                int lx = INV_X + INV_W - (int)strlen(lbl) - 2;
                console_print(lx, row, lbl, COL_CYAN, row_bg);
            }
        }
    }

    int desc_row = INV_Y + INV_H - 4;
    console_hline(INV_X + 1, desc_row, INV_W - 2, 0xC4, 0xFF3A3A5A, COL_PANEL_BG);

    if (es->inv_count > 0 && g->inv_cursor < es->inv_count)
    {
        console_print(INV_X + 2, desc_row + 1, es->inv[g->inv_cursor].desc, COL_GREY, COL_PANEL_BG);
    }

    console_hline(INV_X + 1, INV_Y + INV_H - 2, INV_W - 2, 0xC4, 0xFF3A3A5A, COL_PANEL_BG);
    console_print(INV_X + 2, INV_Y + INV_H - 1, "Up/Dn:nav  A:use  X:drop  B:close", COL_DARK_GREY, COL_PANEL_BG);
}

void game_render(const GameState *g)
{
    console_clear();
    render_map(g);
    render_ui(g);

    if (g->screen == SCREEN_INVENTORY)
    {
        render_inventory(g);
    }
    if (g->screen == SCREEN_TARGET)
    {
        render_target_overlay(g);
    }

    if (g->screen == SCREEN_DEAD)
    {
        int bx = 21, bw = 38, by = 7, bh = 10;

        uint32_t ibg = 0xFF1A0000;

        for (int y = by + 1; y < by + bh - 1; y++)
        {
            console_hline(bx + 1, y, bw - 2, ' ', COL_RED, ibg);
        }

        console_border(bx, by, bw, bh, COL_RED, ibg);
        const char *t1 = "*  YOU HAVE DIED  *";

        console_print(bx + (bw - (int)strlen(t1)) / 2, by + 2, t1, COL_RED, ibg);

        char tmp[40];

        snprintf(tmp, sizeof(tmp), "Slain on floor %d", g->depth);

        console_print(bx + (bw - (int)strlen(tmp)) / 2, by + 4, tmp, 0xFF886666, ibg);
        const char *t3 = "START to begin anew";

        console_print(bx + (bw - (int)strlen(t3)) / 2, by + 7, t3, COL_DARK_GREY, ibg);
    }
    if (g->screen == SCREEN_VICTORY)
    {
        int bx = 16, bw = 48, by = 7, bh = 10;
        uint32_t ibg = 0xFF1A1400;
        for (int y = by + 1; y < by + bh - 1; y++)
        {
            console_hline(bx + 1, y, bw - 2, ' ', COL_YELLOW, ibg);
        }
        console_border(bx, by, bw, bh, COL_GOLD, ibg);

        const char *t1 = "* YOU ESCAPED THE DUNGEON! *";
        console_print(bx + (bw - (int)strlen(t1)) / 2, by + 2, t1, COL_GOLD, ibg);

        const char *t2 = "Your legend will be remembered.";
        console_print(bx + (bw - (int)strlen(t2)) / 2, by + 4, t2, COL_WHITE, ibg);

        const char *t3 = "START to begin anew";
        console_print(bx + (bw - (int)strlen(t3)) / 2, by + 7, t3, COL_DARK_GREY, ibg);
    }

    console_render();
}

int game_player_move(GameState *g, int dx, int dy)
{
    if (g->screen != SCREEN_PLAY)
    {
        return 0;
    }
    EntityState *es = &g->es;
    int nx = es->player.x + dx;
    int ny = es->player.y + dy;

    Entity *target = entity_at(es, nx, ny);
    if (target && target != &es->player)
    {
        entity_attack(es, &es->player, target);
        if (hit_sfx != SFXHND_INVALID)
        {
            snd_sfx_play(hit_sfx, 255, 128);
        }
    }
    else if (dungeon_passable(&g->dungeon, nx, ny))
    {
        es->player.x = nx;
        es->player.y = ny;
    }
    else
    {
        return 0;
    }

    fov_compute(&g->dungeon, es->player.x, es->player.y, FOV_RADIUS);

    if (es->player.hp <= 0)
    {
        es->player.alive = 0;
        g->screen = SCREEN_DEAD;
    }
    return 1;
}

void game_player_use_stairs(GameState *g)
{
    if (g->screen != SCREEN_PLAY)
    {
        return;
    }

    EntityState *es = &g->es;
    if (es->player.x != g->dungeon.stairs.x || es->player.y != g->dungeon.stairs.y)
    {
        entity_log(es, "No stairs here. Find > to descend.");
        return;
    }

    if (g->depth >= 10)
    {
        g->screen = SCREEN_VICTORY;
        entity_log(es, "You found the exit! You win!");
    }
    else
    {
        g->depth++;
        entity_log(es, "You descend deeper into the dungeon...");
        game_new_level(g);
    }
}

void game_player_pickup(GameState *g)
{
    if (g->screen != SCREEN_PLAY)
    {
        return;
    }

    EntityState *es = &g->es;
    Item *it = item_at(es, es->player.x, es->player.y);
    if (!it)
    {
        entity_log(es, "Nothing here to pick up.");
        return;
    }

    if (es->inv_count >= MAX_INV)
    {
        entity_log(es, "No more room in your pack!");
        return;
    }

    if (es->carry_weight + it->weight > es->max_carry)
    {
        entity_log(es, "Too heavy! You can't carry the %s. (%d/%d)", it->name, es->carry_weight, es->max_carry);
        return;
    }

    es->carry_weight += it->weight;
    es->inv[es->inv_count++] = *it;
    entity_log(es, "You pick up the %s. (%dkg)", it->name, it->weight);
    it->kind = ITEM_NONE;
}

void game_open_inventory(GameState *g)
{
    if (g->screen != SCREEN_PLAY)
    {
        return;
    }

    g->screen = SCREEN_INVENTORY;
    if (g->inv_cursor >= g->es.inv_count)
    {
        g->inv_cursor = g->es.inv_count > 0 ? g->es.inv_count - 1 : 0;
    }
}

void game_inventory_cursor(GameState *g, int dir)
{
    if (g->screen != SCREEN_INVENTORY || g->es.inv_count == 0)
    {
        return;
    }

    g->inv_cursor += dir;
    if (g->inv_cursor < 0)
    {
        g->inv_cursor = g->es.inv_count - 1;
    }

    if (g->inv_cursor >= g->es.inv_count)
    {
        g->inv_cursor = 0;
    }
}

void game_inventory_use(GameState *g)
{
    if (g->screen != SCREEN_INVENTORY)
    {
        return;
    }

    EntityState *es = &g->es;
    int slot = g->inv_cursor;

    if (slot < 0 || slot >= es->inv_count)
    {
        return;
    }

    Item *it = &es->inv[slot];
    switch (it->kind)
    {
    case ITEM_POTION_HP:
    {
        int gained = it->value;
        es->player.hp += gained;
        if (es->player.hp > es->player.hp_max)
            es->player.hp = es->player.hp_max;
        entity_log(es, "You drink the potion. Restored %d HP.", gained);
        consume_item(g, slot);
        break;
    }
    case ITEM_SWORD:
    {
        if (es->weapon_slot == slot)
        {
            es->weapon_slot = -1;
            es->weapon_bonus = 0;
            entity_log(es, "You unequip the %s.", it->name);
        }
        else
        {
            es->weapon_slot = slot;
            es->weapon_bonus = it->value;
            entity_log(es, "You equip the %s. (+%d ATK)", it->name, it->value);
        }
        break;
    }
    case ITEM_SHIELD:
    {
        if (es->shield_slot == slot)
        {
            es->shield_slot = -1;
            es->shield_bonus = 0;
            entity_log(es, "You unequip the %s.", it->name);
        }
        else
        {
            es->shield_slot = slot;
            es->shield_bonus = it->value;
            entity_log(es, "You equip the %s. (+%d DEF)", it->name, it->value);
        }
        break;
    }
    case ITEM_LEATHER_ARMOR:
    case ITEM_CHAIN_MAIL:
    case ITEM_PLATE_ARMOR:
    {
        if (es->armor_slot == slot)
        {
            es->armor_slot = -1;
            es->armor_bonus = 0;
            entity_log(es, "You remove the %s.", it->name);
        }
        else
        {
            if (es->armor_slot >= 0)
                entity_log(es, "You remove the %s.", es->inv[es->armor_slot].name);
            es->armor_slot = slot;
            es->armor_bonus = it->value;
            entity_log(es, "You put on the %s. (+%d DEF)", it->name, it->value);
        }
        break;
    }
    case ITEM_SCROLL_FIREBALL:
    case ITEM_WAND_FIREBALL:
    {
        g->target_x = es->player.x;
        g->target_y = es->player.y;
        g->target_item_slot = slot;
        g->screen = SCREEN_TARGET;
        return;
    }
    default:
        break;
    }

    if (g->inv_cursor >= es->inv_count)
    {
        g->inv_cursor = es->inv_count > 0 ? es->inv_count - 1 : 0;
    }

    mobs_take_turn(es, &g->dungeon);
    if (es->player.hp <= 0)
    {
        es->player.alive = 0;
        g->screen = SCREEN_DEAD;
    }
}

void game_inventory_close(GameState *g)
{
    g->screen = SCREEN_PLAY;
}

void game_inventory_drop(GameState *g)
{
    if (g->screen != SCREEN_INVENTORY)
    {
        return;
    }
    EntityState *es = &g->es;
    int slot = g->inv_cursor;
    if (slot < 0 || slot >= es->inv_count)
    {
        return;
    }

    if (es->weapon_slot == slot || es->shield_slot == slot || es->armor_slot == slot)
    {
        entity_log(es, "Unequip it before dropping.");
        return;
    }
    if (es->item_count >= MAX_ENTITIES)
    {
        entity_log(es, "No room on the ground here!");
        return;
    }

    Item *it = &es->inv[slot];
    es->items[es->item_count] = *it;
    es->items[es->item_count].x = es->player.x;
    es->items[es->item_count].y = es->player.y;
    es->item_count++;

    entity_log(es, "You drop the %s.", it->name);
    consume_item(g, slot);
}

void game_target_move(GameState *g, int dx, int dy)
{
    g->target_x += dx;
    g->target_y += dy;
    if (g->target_x < 0)
    {
        g->target_x = 0;
    }
    if (g->target_x >= MAP_W)
    {
        g->target_x = MAP_W - 1;
    }
    if (g->target_y < 0)
    {
        g->target_y = 0;
    }
    if (g->target_y >= MAP_H)
    {
        g->target_y = MAP_H - 1;
    }
}

void game_target_confirm(GameState *g)
{
    EntityState *es = &g->es;
    int slot = g->target_item_slot;
    if (slot < 0 || slot >= es->inv_count)
    {
        g->screen = SCREEN_PLAY;
        return;
    }

    Item *it = &es->inv[slot];

    if (it->kind == ITEM_SCROLL_FIREBALL || it->kind == ITEM_WAND_FIREBALL)
    {
        apply_fireball(g, g->target_x, g->target_y, 12);
    }

    if (it->kind == ITEM_SCROLL_FIREBALL)
    {
        consume_item(g, slot);
    }
    else if (it->kind == ITEM_WAND_FIREBALL)
    {
        if (slot < es->inv_count)
        {
            es->inv[slot].value--;
            if (es->inv[slot].value <= 0)
            {
                entity_log(es, "The wand crumbles to dust.");
                consume_item(g, slot);
            }
            else
            {
                entity_log(es, "The wand has %d charges left.", es->inv[slot].value);
            }
        }
    }

    mobs_take_turn(es, &g->dungeon);

    if (es->player.hp <= 0)
    {
        es->player.alive = 0;
        g->screen = SCREEN_DEAD;
    }
    else
    {
        g->screen = SCREEN_PLAY;
    }
}

void game_target_cancel(GameState *g)
{
    g->screen = SCREEN_PLAY;
}
