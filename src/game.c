#include "game.h"
#include <dc/sound/sfxmgr.h>
#include <kos/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char hit_wav_data[];
extern const int hit_wav_size;

static sfxhnd_t hit_sfx = SFXHND_INVALID;

typedef struct
{
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

static void teleport_player(GameState* g)
{
    EntityState* es = &g->es;
    for (int tries = 0; tries < 200; tries++)
    {
        int nx = grng_range(1, MAP_W - 2);
        int ny = grng_range(1, MAP_H - 2);
        if (dungeon_passable(&g->dungeon, nx, ny) && !entity_at(es, nx, ny))
        {
            es->player.x = nx;
            es->player.y = ny;
            fov_compute(&g->dungeon, nx, ny, FOV_RADIUS);
            entity_log(es, "You are suddenly elsewhere!");
            return;
        }
    }
}

static void consume_item(GameState* g, int slot)
{
    EntityState* es = &g->es;
    es->carry_weight -= es->inv[slot].weight;
    if (es->carry_weight < 0) es->carry_weight = 0;

    memmove(&es->inv[slot], &es->inv[slot + 1],
            sizeof(Item) * (es->inv_count - slot - 1));
    es->inv_count--;

    /* Adjust tracked slots */
#define ADJ_SLOT(s) do { \
        if ((s) == slot) { (s) = -1; } \
        else if ((s) > slot) { (s)--; } \
    } while(0)

    ADJ_SLOT(es->weapon_slot);
    ADJ_SLOT(es->armor_slot);
    ADJ_SLOT(es->ring_left_slot);
    ADJ_SLOT(es->ring_right_slot);
#undef ADJ_SLOT

    entity_recompute_rings(es);

    if (g->inv_cursor >= es->inv_count && g->inv_cursor > 0)
    {
        g->inv_cursor = es->inv_count - 1;
    }
}

static Entity* nearest_mob_in_fov(GameState* g)
{
    EntityState* es = &g->es;
    Entity* best = NULL;
    int best_dist = 999;
    for (int i = 0; i < es->mob_count; i++)
    {
        Entity* m = &es->mobs[i];
        if (!m->alive || !g->dungeon.visible[m->y][m->x]) continue;
        int dx = m->x - es->player.x, dy = m->y - es->player.y;
        int d2 = dx * dx + dy * dy;
        if (d2 < best_dist)
        {
            best_dist = d2;
            best = m;
        }
    }
    return best;
}

static void zap_at_target(GameState* g, int tx, int ty, ItemKind wand_kind)
{
    EntityState* es = &g->es;
    Entity* ce = entity_at(es, tx, ty);
    Entity* target = (ce && ce->kind != MOB_PLAYER) ? ce : nearest_mob_in_fov(g);

    switch (wand_kind)
    {
    case ITEM_WAND_LIGHTNING:
    case ITEM_WAND_FIRE:
    case ITEM_WAND_COLD:
        {
            const char* names[] = {"lightning", "fire", "cold"};
            int idx = (wand_kind == ITEM_WAND_LIGHTNING)
                          ? 0
                          : (wand_kind == ITEM_WAND_FIRE)
                          ? 1
                          : 2;
            if (target)
            {
                int dmg = grng_range(6, 18);
                target->hp -= dmg;
                entity_log(es, "A bolt of %s hits the %s for %d!", names[idx], target->name, dmg);
                if (target->hp <= 0)
                {
                    target->alive = 0;
                    entity_log(es, "You kill the %s! (+%d xp)", target->name, target->xp_reward);
                    entity_grant_xp(es, target->xp_reward);
                }
            }
            else
            {
                entity_log(es, "The bolt of %s dissipates.", names[idx]);
            }
            break;
        }
    case ITEM_WAND_MAGIC_MISSILE:
        if (target)
        {
            int dmg = grng_range(4, 16); /* 4d4 */
            target->hp -= dmg;
            entity_log(es, "Magic missile hits the %s for %d!", target->name, dmg);
            if (target->hp <= 0)
            {
                target->alive = 0;
                entity_log(es, "You kill the %s! (+%d xp)", target->name, target->xp_reward);
                entity_grant_xp(es, target->xp_reward);
            }
        }
        else
        {
            entity_log(es, "The missile misses.");
        }
        break;
    case ITEM_WAND_SLOW_MONSTER:
        if (target)
        {
            target->held = grng_range(4, 8);
            entity_log(es, "The %s slows to a crawl.", target->name);
        }
        break;
    case ITEM_WAND_HASTE_MONSTER:
        if (target)
        {
            target->held = 0;
            entity_log(es, "The %s speeds up!", target->name);
        }
        break;
    case ITEM_WAND_DRAIN_LIFE:
        if (target)
        {
            target->hp /= 2;
            entity_log(es, "The %s's life is drained!", target->name);
            if (target->hp <= 0)
            {
                target->alive = 0;
                entity_log(es, "You kill the %s! (+%d xp)", target->name, target->xp_reward);
                entity_grant_xp(es, target->xp_reward);
            }
        }
        break;
    case ITEM_WAND_TELEPORT_AWAY:
        if (target)
        {
            /* Move it somewhere random */
            for (int t = 0; t < 100; t++)
            {
                int nx = grng_range(1, MAP_W - 2), ny = grng_range(1, MAP_H - 2);
                if (dungeon_passable(&g->dungeon, nx, ny) && !entity_at(es, nx, ny))
                {
                    target->x = nx;
                    target->y = ny;
                    entity_log(es, "The %s vanishes!", target->name);
                    break;
                }
            }
        }
        break;
    case ITEM_WAND_TELEPORT_TO:
        if (target)
        {
            /* Pull to adjacent tile */
            int px = es->player.x, py = es->player.y;
            int placed = 0;
            int dx[] = {0, 0, 1, -1, 1, -1, 1, -1};
            int dy[] = {1, -1, 0, 0, 1, -1, -1, 1};
            for (int k = 0; k < 8 && !placed; k++)
            {
                int nx = px + dx[k], ny = py + dy[k];
                if (dungeon_passable(&g->dungeon, nx, ny) && !entity_at(es, nx, ny))
                {
                    target->x = nx;
                    target->y = ny;
                    entity_log(es, "The %s is pulled toward you!", target->name);
                    placed = 1;
                }
            }
        }
        break;
    case ITEM_WAND_CANCELLATION:
        if (target)
        {
            target->flags = 0;
            target->satt = 0;
            entity_log(es, "The %s's abilities are cancelled!", target->name);
        }
        break;
    case ITEM_WAND_POLYMORPH:
        if (target)
        {
            /* Turn it into a random weak mob */
            MobKind kinds[] = {MOB_BAT, MOB_EMU, MOB_SNAKE, MOB_HOBGOBLIN, MOB_ZOMBIE};
            int k = grng_range(0, 4);
            int sx = target->x, sy = target->y;
            *target = (Entity){0};
            mob_spawn(es, kinds[k], sx, sy);
            entity_log(es, "The monster is polymorphed!");
        }
        break;
    case ITEM_WAND_INVIS:
        if (target)
        {
            target->flags |= MOB_INVIS;
            entity_log(es, "The %s turns invisible!", target->name);
        }
        break;
    case ITEM_WAND_NOTHING:
        entity_log(es, "Nothing happens.");
        break;
    default:
        entity_log(es, "The wand fizzles.");
        break;
    }
}

static void wand_hold_all(GameState* g)
{
    EntityState* es = &g->es;
    int n = 0;
    for (int i = 0; i < es->mob_count; i++)
    {
        Entity* m = &es->mobs[i];
        if (!m->alive || !g->dungeon.visible[m->y][m->x]) continue;
        m->held = grng_range(3, 7);
        n++;
    }
    entity_log(&g->es, "All nearby monsters are held! (%d)", n);
}

void game_init(GameState* g, unsigned int entropy)
{
    memset(g, 0, sizeof(GameState));
    uint64_t t = timer_ms_gettime64();
    g->world_seed = (unsigned int)(t ^ (t >> 17)) ^ (entropy * 2654435761u);
    g->depth = 1;
    g->inv_cursor = 0;
    g->screen = SCREEN_TITLE;

    if (hit_sfx == SFXHND_INVALID)
    {
        FILE* f = fopen("/ram/hit.wav", "wb");
        if (f)
        {
            fwrite(hit_wav_data, 1, (size_t)hit_wav_size, f);
            fclose(f);
            hit_sfx = snd_sfx_load("/ram/hit.wav");
        }
    }
}

static void give_item(EntityState* es, ItemKind kind)
{
    if (es->inv_count >= MAX_INV)
    {
        return;
    }
    int old = es->item_count;
    item_spawn(es, kind, 0, 0);
    if (es->item_count > old)
    {
        Item it = es->items[old];
        es->item_count = old;
        it.x = -1;
        it.y = -1;
        es->carry_weight += it.weight;
        es->inv[es->inv_count++] = it;
    }
}

void game_start(GameState* g)
{
    entity_state_init(&g->es);
    g->depth = 1;
    g->inv_cursor = 0;

    give_item(&g->es, ITEM_MACE);
    g->es.weapon_slot = 0;
    g->es.weapon_bonus = g->es.inv[0].value;

    give_item(&g->es, ITEM_ARMOR_LEATHER);
    g->es.armor_slot = 1;
    g->es.armor_bonus = g->es.inv[1].value;

    give_item(&g->es, ITEM_FOOD);

    game_new_level(g);
    entity_log(&g->es, "Welcome! Find > to descend. Reach floor 26 for the Amulet!");
}


/* Return a random mob kind appropriate for a given dungeon depth.    */
/* Uses the monster's level field as the earliest depth it appears.   */
static MobKind pick_mob_for_depth(int depth)
{
    /* Eligible mobs: those with level <= depth */
    typedef struct
    {
        MobKind kind;
        int weight;
    } Candidate;
    Candidate pool[32];
    int n = 0;
    int total_w = 0;

    /* Ordered list of all mobs with their level and weight */
    static const struct
    {
        MobKind kind;
        int lvl;
        int w;
    } catalogue[] = {
        {MOB_BAT, 1, 8},
        {MOB_EMU, 1, 8},
        {MOB_HOBGOBLIN, 1, 8},
        {MOB_ICE_MONSTER, 1, 6},
        {MOB_KESTREL, 1, 8},
        {MOB_SNAKE, 1, 8},
        {MOB_RATTLESNAKE, 2, 7},
        {MOB_ZOMBIE, 2, 7},
        {MOB_CENTAUR, 4, 6},
        {MOB_LEPRECHAUN, 3, 5},
        {MOB_NYMPH, 3, 4},
        {MOB_QUAGGA, 3, 6},
        {MOB_YETI, 4, 5},
        {MOB_ORC, 1, 7},
        {MOB_AQUATOR, 5, 4},
        {MOB_WRAITH, 5, 4},
        {MOB_TROLL, 6, 4},
        {MOB_UNICORN, 7, 3},
        {MOB_XEROC, 7, 4},
        {MOB_FLYTRAP, 8, 3},
        {MOB_MEDUSA, 8, 3},
        {MOB_PHANTOM, 8, 3},
        {MOB_VAMPIRE, 8, 3},
        {MOB_DRAGON, 10, 2},
        {MOB_GRIFFIN, 13, 1},
        {MOB_JABBERWOCK, 15, 1},
    };
    int nc = (int)(sizeof(catalogue) / sizeof(catalogue[0]));

    for (int i = 0; i < nc && n < 32; i++)
    {
        if (catalogue[i].lvl <= depth)
        {
            pool[n].kind = catalogue[i].kind;
            pool[n].weight = catalogue[i].w;
            total_w += catalogue[i].w;
            n++;
        }
    }

    if (n == 0) return MOB_SNAKE;

    int roll = (int)(grng() % (unsigned)total_w);
    for (int i = 0; i < n; i++)
    {
        roll -= pool[i].weight;
        if (roll < 0) return pool[i].kind;
    }
    return pool[n - 1].kind;
}

static ItemKind pick_item_for_depth(int depth)
{
    int roll = grng_range(0, 99);

    /* Food: 15% */
    if (roll < 15) return ITEM_FOOD;

    /* Gold: 10% */
    if (roll < 25) return ITEM_GOLD;

    /* Potions: 20% */
    if (roll < 45)
    {
        static const ItemKind pots[] = {
            ITEM_POT_HEALING, ITEM_POT_HEALING, ITEM_POT_EXTRA_HEALING,
            ITEM_POT_GAIN_STR, ITEM_POT_RESTORE_STR, ITEM_POT_HASTE_SELF,
            ITEM_POT_RAISE_LEVEL, ITEM_POT_SEE_INVIS, ITEM_POT_LEVITATION,
            ITEM_POT_CONFUSION, ITEM_POT_BLINDNESS, ITEM_POT_HALLUCINATION,
            ITEM_POT_POISON, ITEM_POT_MON_DETECT,
        };
        return pots[grng_range(0, 13)];
    }

    /* Scrolls: 15% */
    if (roll < 60)
    {
        static const ItemKind scrs[] = {
            ITEM_SCR_MAGIC_MAP, ITEM_SCR_TELEPORT, ITEM_SCR_ENCH_WEAPON,
            ITEM_SCR_ENCH_ARMOR, ITEM_SCR_HOLD_MONSTER, ITEM_SCR_SLEEP,
            ITEM_SCR_REMOVE_CURSE, ITEM_SCR_SCARE_MONSTER, ITEM_SCR_ID_POTION,
            ITEM_SCR_ID_WEAPON, ITEM_SCR_ID_ARMOR, ITEM_SCR_PROTECT_ARMOR,
            ITEM_SCR_FOOD_DETECT, ITEM_SCR_CREATE_MONSTER,
        };
        return scrs[grng_range(0, 13)];
    }

    /* Weapons: 12% */
    if (roll < 72)
    {
        int wr = grng_range(0, 99);
        if (wr < 25) return ITEM_MACE;
        if (wr < 40) return ITEM_DAGGER;
        if (wr < 55) return ITEM_LONGSWORD;
        if (wr < 65) return (depth >= 4) ? ITEM_TWO_HANDED_SWORD : ITEM_LONGSWORD;
        if (wr < 72) return ITEM_SPEAR;
        if (wr < 80) return ITEM_ARROW;
        if (wr < 87) return ITEM_DART;
        if (wr < 93) return ITEM_SHURIKEN;
        return ITEM_SHORTBOW;
    }

    /* Armor: 12% */
    if (roll < 84)
    {
        if (depth <= 2) return grng_range(0, 1) ? ITEM_ARMOR_LEATHER : ITEM_ARMOR_RING_MAIL;
        if (depth <= 4) return (grng_range(0, 1)) ? ITEM_ARMOR_SCALE_MAIL : ITEM_ARMOR_STUDDED;
        if (depth <= 7) return (grng_range(0, 1)) ? ITEM_ARMOR_CHAIN_MAIL : ITEM_ARMOR_SPLINT_MAIL;
        if (depth <= 12) return (grng_range(0, 1)) ? ITEM_ARMOR_BANDED_MAIL : ITEM_ARMOR_SPLINT_MAIL;
        return ITEM_ARMOR_PLATE_MAIL;
    }

    /* Rings: 6% */
    if (roll < 90)
    {
        static const ItemKind rings[] = {
            ITEM_RING_PROTECTION, ITEM_RING_ADD_STR, ITEM_RING_DEXTERITY,
            ITEM_RING_INCREASE_DMG, ITEM_RING_REGENERATION, ITEM_RING_SLOW_DIGEST,
            ITEM_RING_SEE_INVIS, ITEM_RING_STEALTH, ITEM_RING_SUSTAIN_STR,
            ITEM_RING_MAINT_ARMOR, ITEM_RING_SEARCHING, ITEM_RING_ADORNMENT,
        };
        return rings[grng_range(0, 11)];
    }

    /* Wands: 10% */
    {
        static const ItemKind wands[] = {
            ITEM_WAND_MAGIC_MISSILE, ITEM_WAND_FIRE, ITEM_WAND_LIGHTNING,
            ITEM_WAND_COLD, ITEM_WAND_SLOW_MONSTER, ITEM_WAND_DRAIN_LIFE,
            ITEM_WAND_TELEPORT_AWAY, ITEM_WAND_CANCELLATION, ITEM_WAND_LIGHT,
            ITEM_WAND_POLYMORPH, ITEM_WAND_NOTHING,
        };
        return wands[grng_range(0, 10)];
    }
}

void game_new_level(GameState* g)
{
    rng_state_g = g->world_seed ^ (unsigned)(g->depth * 0x9e3779b9u);
    dungeon_generate(&g->dungeon, g->depth, g->world_seed);

    Room* r0 = &g->dungeon.rooms[0];
    g->es.player.x = r0->x + r0->w / 2;
    g->es.player.y = r0->y + r0->h / 2;

    g->es.mob_count = 0;
    g->es.item_count = 0;

    /* Re-validate equipment slots */
#define REVALIDATE(slot, bonus) do { \
        if ((slot) >= 0 && (slot) < g->es.inv_count) \
            (bonus) = g->es.inv[(slot)].value + g->es.inv[(slot)].enchant; \
        else { (slot) = -1; (bonus) = 0; } \
    } while(0)

    REVALIDATE(g->es.weapon_slot, g->es.weapon_bonus);
    REVALIDATE(g->es.armor_slot, g->es.armor_bonus);
#undef REVALIDATE

    entity_recompute_rings(&g->es);

    g->es.carry_weight = 0;
    for (int i = 0; i < g->es.inv_count; i++)
    {
        g->es.carry_weight += g->es.inv[i].weight;
    }

    /* Spawn mobs */
    int mob_budget = 3 + g->depth * 2;
    if (mob_budget > 30) mob_budget = 30;

    for (int i = 1; i < g->dungeon.room_count && mob_budget > 0; i++)
    {
        Room* rm = &g->dungeon.rooms[i];
        int n = grng_range(1, 3);
        for (int j = 0; j < n && mob_budget > 0; j++)
        {
            int mx = grng_range(rm->x + 1, rm->x + rm->w - 2);
            int my = grng_range(rm->y + 1, rm->y + rm->h - 2);
            if (entity_at(&g->es, mx, my)) continue;
            mob_spawn(&g->es, pick_mob_for_depth(g->depth), mx, my);
            mob_budget--;
        }
    }

    /* Spawn items */
    int item_count = 3 + g->depth / 2;
    if (item_count > 12) item_count = 12;

    for (int i = 0; i < item_count; i++)
    {
        int ri = grng_range(0, g->dungeon.room_count - 1);
        Room* rm = &g->dungeon.rooms[ri];
        int ix = grng_range(rm->x + 1, rm->x + rm->w - 2);
        int iy = grng_range(rm->y + 1, rm->y + rm->h - 2);

        ItemKind kind = pick_item_for_depth(g->depth);
        if (kind == ITEM_GOLD)
        {
            int amount = grng_range(g->depth * 5, g->depth * 20);
            item_spawn_gold(&g->es, amount, ix, iy);
        }
        else
        {
            item_spawn(&g->es, kind, ix, iy);
        }
    }

    /* Spawn Amulet of Yendor on floor 26 */
    if (g->depth == 26)
    {
        int ri = grng_range(g->dungeon.room_count / 2, g->dungeon.room_count - 1);
        Room* rm = &g->dungeon.rooms[ri];
        item_spawn(&g->es, ITEM_AMULET_YENDOR,
                   rm->x + rm->w / 2, rm->y + rm->h / 2);
        entity_log(&g->es, "You sense a powerful artifact nearby...");
    }

    fov_compute(&g->dungeon, g->es.player.x, g->es.player.y, FOV_RADIUS);
    g->screen = SCREEN_PLAY;
    g->inv_cursor = 0;
}

static void draw_hp_bar(int x, int y, int hp, int hp_max, int bar_w)
{
    int filled = (hp_max > 0) ? (hp * bar_w / hp_max) : 0;
    if (filled < 0) filled = 0;
    if (filled > bar_w) filled = bar_w;
    uint32_t bar_col = (hp > hp_max / 2) ? COL_GREEN : (hp > hp_max / 4) ? COL_YELLOW : COL_RED;
    console_put(x, y, '[', COL_GREY, COL_PANEL_BG);
    for (int i = 0; i < bar_w; i++)
    {
        console_put(x + 1 + i, y, (i < filled) ? 0xDB : 0xB0,
                    (i < filled) ? bar_col : COL_DARK_GREY, COL_PANEL_BG);
    }
    console_put(x + 1 + bar_w, y, ']', COL_GREY, COL_PANEL_BG);
}

static void render_map(const GameState* g)
{
    const Dungeon* d = &g->dungeon;
    const EntityState* es = &g->es;

    for (int y = 0; y < MAP_H; y++)
    {
        for (int x = 0; x < MAP_W; x++)
        {
            if (!d->explored[y][x]) continue;
            uint8_t tile = d->tile[y][x];
            int lit = d->visible[y][x];
            uint32_t fg = lit ? tile_glyph[tile].lit_col : tile_glyph[tile].dim_col;
            uint32_t bg = lit ? tile_lit_bg[tile] : COL_BLACK;
            console_put(x, y, tile_glyph[tile].ch, fg, bg);
        }
    }

    for (int i = 0; i < es->item_count; i++)
    {
        const Item* it = &es->items[i];
        if (it->kind == ITEM_NONE || !d->visible[it->y][it->x]) continue;
        console_put(it->x, it->y, it->ch, it->col, tile_lit_bg[TILE_FLOOR]);
    }

    for (int i = 0; i < es->mob_count; i++)
    {
        const Entity* m = &es->mobs[i];
        if (!m->alive || !d->visible[m->y][m->x]) continue;
        /* Invisible mobs only shown when adjacent or player has see-invis */
        if ((m->flags & MOB_INVIS) && !es->ring_see_invis)
        {
            int dx = m->x - es->player.x, dy = m->y - es->player.y;
            if (dx * dx + dy * dy > 1) continue;
        }
        console_put(m->x, m->y, m->ch, m->col, tile_lit_bg[TILE_FLOOR]);
    }

    if (es->player.alive)
    {
        console_put(es->player.x, es->player.y, '@', COL_WHITE, 0xFF14100A);
    }
}

static void render_ui(const GameState* g)
{
    const EntityState* es = &g->es;
    const Entity* p = &es->player;

    for (int y = MAP_H; y < CON_H; y++)
    {
        console_hline(0, y, CON_W, ' ', COL_WHITE, COL_PANEL_BG);
    }

    console_hline(0, MAP_H, CON_W, 0xC4, 0xFF3A3A5A, COL_PANEL_BG);

    int row = MAP_H + 1;
    int total_atk = p->atk + es->weapon_bonus + es->ring_atk_bonus;
    int total_def = p->def + es->armor_bonus + es->ring_def_bonus;

    console_print(0, row, "HP", COL_WHITE, COL_PANEL_BG);
    draw_hp_bar(2, row, p->hp, p->hp_max, 14);
    console_printf(18, row, COL_WHITE, COL_PANEL_BG, "%d/%d", p->hp, p->hp_max);
    console_printf(26, row, COL_DARK_GREY, COL_PANEL_BG, "ATK");
    console_printf(30, row, COL_YELLOW, COL_PANEL_BG, "%-3d", total_atk);
    console_printf(34, row, COL_DARK_GREY, COL_PANEL_BG, "DEF");
    console_printf(38, row, COL_CYAN, COL_PANEL_BG, "%-3d", total_def);
    console_printf(43, row, COL_DARK_GREY, COL_PANEL_BG, "Lv");
    console_printf(46, row, COL_YELLOW, COL_PANEL_BG, "%-2d", es->player_level);
    console_printf(50, row, COL_GOLD, COL_PANEL_BG, "$%-5d", es->gold);
    console_printf(57, row, COL_DARK_GREY, COL_PANEL_BG, "Fl");
    console_printf(60, row, COL_CYAN, COL_PANEL_BG, "%-2d", g->depth);

    /* Status conditions */
    if (es->confused_turns > 0) console_print(63, row, "Conf ", COL_MAGENTA, COL_PANEL_BG);
    else if (es->blind_turns > 0) console_print(63, row, "Blind", COL_DARK_GREY,COL_PANEL_BG);
    else if (es->halluc_turns > 0) console_print(63, row, "Halluc", COL_MAGENTA, COL_PANEL_BG);
    else if (es->hasted_turns > 0) console_print(63, row, "Haste ", COL_CYAN, COL_PANEL_BG);
    else if (es->held_turns > 0) console_print(63, row, "Held  ", COL_RED, COL_PANEL_BG);
    else if (es->frozen_turns > 0) console_print(63, row, "Frozen", COL_CYAN, COL_PANEL_BG);
    else if (es->poisoned) console_print(63, row, "Poisnd", COL_DARK_GREEN,COL_PANEL_BG);
    else if (es->hunger == 0) console_print(63, row, "Starvg", COL_RED, COL_PANEL_BG);
    else if (es->hunger < 200) console_print(63, row, "VHngry", COL_ORANGE, COL_PANEL_BG);
    else if (es->hunger < 400) console_print(63, row, "Hungry", COL_YELLOW, COL_PANEL_BG);

    row = MAP_H + 2;

    /* Weapon slot */
    if (es->weapon_slot >= 0 && es->weapon_slot < es->inv_count)
    {
        const Item* it = &es->inv[es->weapon_slot];
        console_put(0, row, it->ch, it->col, COL_PANEL_BG);
        console_printf(2, row, COL_WHITE, COL_PANEL_BG, "%-12.12s", it->name);
        console_printf(15, row, COL_GREEN, COL_PANEL_BG, "+%datk", es->weapon_bonus);
    }
    else
    {
        console_printf(0, row, COL_DARK_GREY, COL_PANEL_BG, "/  (no weapon)    ");
    }

    /* Armor slot */
    if (es->armor_slot >= 0 && es->armor_slot < es->inv_count)
    {
        const Item* it = &es->inv[es->armor_slot];
        console_put(24, row, it->ch, it->col, COL_PANEL_BG);
        console_printf(26, row, COL_WHITE, COL_PANEL_BG, "%-12.12s", it->name);
        console_printf(39, row, COL_GREEN, COL_PANEL_BG, "+%ddef", es->armor_bonus);
    }
    else
    {
        console_printf(24, row, COL_DARK_GREY, COL_PANEL_BG, "[  (no armor)     ");
    }

    /* Ring slots */
    if (es->ring_left_slot >= 0 && es->ring_left_slot < es->inv_count)
    {
        const Item* it = &es->inv[es->ring_left_slot];
        console_put(50, row, '=', it->col, COL_PANEL_BG);
        console_printf(52, row, COL_WHITE, COL_PANEL_BG, "%.14s", it->name);
    }
    else
    {
        console_printf(50, row, COL_DARK_GREY, COL_PANEL_BG, "=  (no ring L)    ");
    }

    row = MAP_H + 3;
    Item* ground = item_at((EntityState*)&g->es, g->es.player.x, g->es.player.y);
    if (ground && ground->kind != ITEM_NONE)
    {
        console_printf(0, row, COL_YELLOW, COL_PANEL_BG, " [A] pick up: ");
        console_put(14, row, ground->ch, ground->col, COL_PANEL_BG);
        console_printf(16, row, COL_WHITE, COL_PANEL_BG, "%s", ground->name);
    }
    else
    {
        console_print(0, row, " D-pad:move  A:pickup  B:inv  R:stairs",
                      COL_DARK_GREY, COL_PANEL_BG);
    }

    uint32_t wt_col = (es->carry_weight < es->max_carry * 3 / 4)
                          ? COL_GREY
                          : (es->carry_weight < es->max_carry)
                          ? COL_YELLOW
                          : COL_RED;
    console_printf(68, row, wt_col, COL_PANEL_BG, "Wt:%d/%d",
                   es->carry_weight, es->max_carry);

    console_hline(0, MAP_H + 4, CON_W, 0xC4, 0xFF3A3A5A, COL_PANEL_BG);

    static const uint32_t log_fade[] = {
        COL_WHITE, COL_LIGHT_GREY, COL_GREY, COL_DARK_GREY, COL_DARK_GREY,
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

static void render_target_overlay(const GameState* g)
{
    console_put(g->target_x, g->target_y, 'X', COL_BLACK, COL_YELLOW);

    for (int dy = -2; dy <= 2; dy++)
    {
        for (int dx = -2; dx <= 2; dx++)
        {
            if (dx == 0 && dy == 0) continue;
            int ax = g->target_x + dx, ay = g->target_y + dy;
            if (ax < 0 || ax >= MAP_W || ay < 0 || ay >= MAP_H) continue;
            if (!g->dungeon.explored[ay][ax]) continue;
            console_put(ax, ay, '~', COL_DARK_RED, 0xFF110000);
        }
    }

    const EntityState* es = &g->es;
    for (int i = 0; i < es->mob_count; i++)
    {
        const Entity* m = &es->mobs[i];
        if (!m->alive || !g->dungeon.visible[m->y][m->x]) continue;
        if (abs(m->x - g->target_x) > 2 || abs(m->y - g->target_y) > 2) continue;
        console_put(m->x, m->y, m->ch, m->col, 0xFF330000);
    }
    if (abs(es->player.x - g->target_x) <= 2 &&
        abs(es->player.y - g->target_y) <= 2)
    {
        console_put(es->player.x, es->player.y, '@', COL_WHITE, 0xFF330000);
    }

    console_put(g->target_x, g->target_y, 'X', COL_BLACK, COL_YELLOW);

    console_hline(0, MAP_H + 3, CON_W, ' ', COL_WHITE, COL_PANEL_BG);
    Entity* ce = entity_at((EntityState*)&g->es, g->target_x, g->target_y);
    if (ce && ce->kind != MOB_PLAYER)
    {
        console_printf(0, MAP_H + 3, COL_YELLOW, COL_PANEL_BG,
                       "  Target: %s  HP:%d/%d   A:fire  B:cancel",
                       ce->name, ce->hp, ce->hp_max);
    }
    else
    {
        console_printf(0, MAP_H + 3, COL_YELLOW, COL_PANEL_BG,
                       "  [TARGETING]   A:zap  B:cancel");
    }
}

#define INV_X 0
#define INV_W CON_W
#define INV_Y 0
#define INV_H CON_H

static const char* item_action_label(const EntityState* es, int slot)
{
    if (slot < 0 || slot >= es->inv_count) return "";
    const Item* it = &es->inv[slot];

    /* Equipped items show "unequip" */
    if (es->weapon_slot == slot) return "unequip";
    if (es->armor_slot == slot) return "unequip";
    if (es->ring_left_slot == slot || es->ring_right_slot == slot) return "remove";

    if (item_is_wand(it->kind))
    {
        static char wlbl[16];
        snprintf(wlbl, sizeof(wlbl), "zap (%dc)", it->value);
        return wlbl;
    }
    return it->action;
}

static void render_inventory(const GameState* g)
{
    const EntityState* es = &g->es;

    /* Full-screen background */
    for (int y = 0; y < CON_H; y++)
    {
        console_hline(0, y, CON_W, ' ', COL_WHITE, COL_PANEL_BG);
    }

    console_border(0, 0, CON_W, CON_H, COL_GOLD, COL_PANEL_BG);
    console_print(CON_W / 2 - 5, 0, " INVENTORY ", COL_GOLD, COL_PANEL_BG);

    uint32_t wt_col = (es->carry_weight < es->max_carry * 3 / 4)
                          ? COL_GREEN
                          : (es->carry_weight < es->max_carry)
                          ? COL_YELLOW
                          : COL_RED;
    console_printf(CON_W - 14, 0, wt_col, COL_PANEL_BG,
                   " %d/%d ", es->carry_weight, es->max_carry);

    /* Layout:
     *   Row 0         : border + title
     *   Rows 1..n     : items  (display_rows = CON_H - 5)
     *   Row CON_H-4   : separator
     *   Row CON_H-3   : description
     *   Row CON_H-2   : separator
     *   Row CON_H-1   : controls + border bottom
     */
    int display_rows = CON_H - 5;

    if (es->inv_count == 0)
    {
        console_print(2, 2, "(inventory is empty)", COL_DARK_GREY, COL_PANEL_BG);
    }
    else
    {
        int scroll_start = 0;
        if (g->inv_cursor >= display_rows)
        {
            scroll_start = g->inv_cursor - display_rows + 1;
        }

        /* Scroll indicators */
        if (scroll_start > 0)
        {
            console_print(CON_W / 2 - 4, 0, " ^ more ^ ", COL_DARK_GREY, COL_PANEL_BG);
        }
        int last_visible = scroll_start + display_rows - 1;
        if (last_visible < es->inv_count - 1)
        {
            console_print(CON_W / 2 - 4, CON_H - 4, " v more v ", COL_DARK_GREY, COL_PANEL_BG);
        }

        for (int i = scroll_start; i < es->inv_count && (i - scroll_start) < display_rows; i++)
        {
            const Item* it = &es->inv[i];
            int row = 1 + (i - scroll_start);
            int sel = (i == g->inv_cursor);
            int equip = (es->weapon_slot == i || es->armor_slot == i ||
                es->ring_left_slot == i || es->ring_right_slot == i);

            uint32_t rbg = sel ? 0xFF1A1600 : equip ? 0xFF071007 : COL_PANEL_BG;

            console_hline(1, row, CON_W - 2, ' ', COL_WHITE, rbg);
            console_put(1, row, sel ? '>' : ' ', COL_YELLOW, rbg);
            console_printf(3, row, sel ? COL_YELLOW : COL_GREY, rbg, "%c)", 'a' + i);
            console_put(6, row, it->ch, it->col, rbg);
            console_printf(8, row, sel ? COL_WHITE : COL_GREY, rbg, "%-23.23s", it->name);

            if (it->kind == ITEM_GOLD)
            {
                console_printf(33, row, COL_GOLD, rbg, "%d gp ", it->value);
            }
            else
            {
                console_printf(33, row, COL_DARK_GREY, rbg, "%2dkg", it->weight);
            }

            if (equip)
            {
                console_put(39, row, '*', COL_GREEN, rbg);
            }

            if (sel)
            {
                const char* lbl = item_action_label(es, i);
                int lx = CON_W - (int)strlen(lbl) - 2;
                console_print(lx, row, lbl, COL_CYAN, rbg);
            }
        }
    }

    int desc_row = CON_H - 4;
    console_hline(1, desc_row, CON_W - 2, 0xC4, 0xFF3A3A5A, COL_PANEL_BG);
    if (es->inv_count > 0 && g->inv_cursor < es->inv_count)
    {
        console_print(2, desc_row + 1, es->inv[g->inv_cursor].desc, COL_GREY, COL_PANEL_BG);
    }

    console_hline(1, CON_H - 2, CON_W - 2, 0xC4, 0xFF3A3A5A, COL_PANEL_BG);
    console_print(2, CON_H - 1, "Up/Dn: navigate   A: use/equip   X: drop   B: close",
                  COL_DARK_GREY, COL_PANEL_BG);
}


/*
 * 5-row block-font bitmaps for the letters in "DC-DELVE".
 * Each row is a bitmask; bit 4 = leftmost column (4 cols per letter).
 * Dash is 3 columns wide (stored in 4, rightmost unused).
 */
#define BF_W 4
static const uint8_t BF_D[5] = {0xE0, 0x90, 0x90, 0x90, 0xE0}; /* 1110 1001 1001 1001 1110 */
static const uint8_t BF_C[5] = {0x70, 0x80, 0x80, 0x80, 0x70}; /* 0111 1000 1000 1000 0111 */
static const uint8_t BF_DASH[5] = {0x00, 0x00, 0xE0, 0x00, 0x00}; /* dash = middle row only */
static const uint8_t BF_E[5] = {0xF0, 0x80, 0xE0, 0x80, 0xF0};
static const uint8_t BF_L[5] = {0x80, 0x80, 0x80, 0x80, 0xF0};
static const uint8_t BF_V[5] = {0x90, 0x90, 0x90, 0x60, 0x40};

static void draw_block_letter(int sx, int sy, const uint8_t* rows, int cols,
                              uint32_t fg, uint32_t bg)
{
    for (int r = 0; r < 5; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            int bit = (rows[r] >> (7 - c)) & 1;
            console_put(sx + c, sy + r,
                        bit ? 0xDB : ' ',
                        bit ? fg : bg, bg);
        }
    }
}

static void render_title(const GameState* g)
{
    uint32_t bg = 0xFF06060E;
    for (int y = 0; y < CON_H; y++)
    {
        console_hline(0, y, CON_W, ' ', COL_WHITE, bg);
    }

    /* Outer frame */
    console_border(0, 0, CON_W, CON_H, 0xFF2A2A55, bg);

    /* Shimmer line above title */
    for (int x = 2; x < CON_W - 2; x++)
    {
        console_put(x, 2, 0xCD, 0xFF1A1A44, bg);
    }

    /* Big block-font title  "DC-DELVE"
     * Widths: D=4, C=4, dash=3, D=4, E=4, L=4, V=4, E=4
     * Total cols: 4+1+4+1+3+1+4+1+4+1+4+1+4+1+4 = 38 (letters + spaces between)
     * Centre on 80: (80-38)/2 = 21 */
    {
        int ty = 4;
        uint32_t tfg = COL_GOLD;

        struct
        {
            const uint8_t* rows;
            int w;
        } letters[] = {
            {BF_D, 4}, {BF_C, 4}, {BF_DASH, 3},
            {BF_D, 4}, {BF_E, 4}, {BF_L, 4}, {BF_V, 4}, {BF_E, 4},
        };

        int n = (int)(sizeof(letters) / sizeof(letters[0]));
        int total = 0;

        for (int i = 0; i < n; i++)
        {
            total += letters[i].w + (i < n - 1 ? 1 : 0);
        }

        int tx = (CON_W - total) / 2;

        for (int i = 0; i < n; i++)
        {
            draw_block_letter(tx, ty, letters[i].rows, letters[i].w, tfg, bg);
            tx += letters[i].w + 1;
        }
    }

    /* Subtitle */
    {
        const char* sub = "A  T R A D I T I O N A L  R O G U E L I K E";
        int sx = (CON_W - (int)strlen(sub)) / 2;
        console_print(sx, 10, sub, COL_ORANGE, bg);
    }

    /* Separator */
    for (int x = 4; x < CON_W - 4; x++)
    {
        console_put(x, 12, 0xC4, 0xFF222244, bg);
    }

    /* Mini dungeon vignette */
    {
        int vx = 16, vy = 13, vw = 48, vh = 7;
        console_border(vx, vy, vw, vh, 0xFF3A3A6A, bg);

        uint32_t flr = 0xFF0F0E0B;
        for (int y = vy + 1; y < vy + vh - 1; y++)
        {
            console_hline(vx + 1, y, vw - 2, '.', 0xFF1E1C15, flr);
        }

        /* walls */
        console_hline(vx + 1, vy + 1, 10, '#', 0xFF4A5870, flr);
        console_hline(vx + 1, vy + 2, 3, '#', 0xFF4A5870, flr);
        console_hline(vx + 1, vy + 3, 3, '#', 0xFF4A5870, flr);
        console_hline(vx + 29, vy + 1, 18, '#', 0xFF4A5870, flr);

        /* monsters */
        console_put(vx + 18, vy + 2, 'D', COL_RED, flr);
        console_put(vx + 23, vy + 3, 'Z', COL_DARK_GREEN, flr);
        console_put(vx + 9, vy + 4, 'S', COL_GREEN, flr);
        console_put(vx + 32, vy + 2, 'T', COL_CYAN, flr);

        /* gold */
        console_put(vx + 14, vy + 3, '$', COL_GOLD, flr);

        /* stairs */
        console_put(vx + 38, vy + 4, '>', COL_YELLOW, flr);

        /* player */
        console_put(vx + 7, vy + 3, '@', COL_WHITE, 0xFF14100A);
    }

    /* Separator */
    for (int x = 4; x < CON_W - 4; x++)
    {
        console_put(x, 21, 0xC4, 0xFF222244, bg);
    }

    /* Description */
    {
        const char* d1 = "Descend 26 floors.";
        const char* d2 = "Collect gold. Fight monsters.";
        const char* d3 = "Find the Amulet of Yendor and escape.";
        console_print((CON_W - (int)strlen(d1)) / 2, 22, d1, COL_GREY, bg);
        console_print((CON_W - (int)strlen(d2)) / 2, 23, d2, COL_GREY, bg);
        console_print((CON_W - (int)strlen(d3)) / 2, 24, d3, COL_LIGHT_GREY, bg);
    }

    /* "Press START" — blink using frame counter */
    if ((g->frame / 25) % 2 == 0)
    {
        const char* ps = ">  Press  START  to  begin  <";
        int px = (CON_W - (int)strlen(ps)) / 2;
        console_print(px, 27, ps, COL_WHITE, bg);
    }

    /* Bottom credits + version */
    {
        const char* cred = "for Sega Dreamcast";
        console_print((CON_W - (int)strlen(cred)) / 2, CON_H - 2, cred,0xFF333366, bg);

        console_print(CON_W - (int)strlen(GAME_VERSION) - 2, CON_H - 2,GAME_VERSION, 0xFF333366, bg);
    }
}

static void render_pause(const GameState* g)
{
    /* Dim the map beneath */
    render_map(g);
    render_ui(g);

    int bw = 36, bh = 10;
    int bx = (CON_W - bw) / 2;
    int by = (MAP_H - bh) / 2;
    uint32_t pbg = 0xFF0A0A18;

    for (int y = by + 1; y < by + bh - 1; y++)
    {
        console_hline(bx + 1, y, bw - 2, ' ', COL_WHITE, pbg);
    }
    console_border(bx, by, bw, bh, COL_GOLD, pbg);
    console_print(bx + (bw - 7) / 2, by, " PAUSE ", COL_GOLD, pbg);

    static const char* items[] = {"Resume", "Controls", "Quit to Title"};
    for (int i = 0; i < 3; i++)
    {
        int row = by + 2 + i * 2;
        int sel = (g->pause_cursor == i);
        uint32_t fc = sel ? COL_WHITE : COL_GREY;
        uint32_t bc = sel ? 0xFF14120A : pbg;
        console_hline(bx + 2, row, bw - 4, ' ', fc, bc);
        console_put(bx + 2, row, sel ? '>' : ' ', COL_YELLOW, bc);
        console_print(bx + 4, row, items[i], fc, bc);
        if (sel) console_print(bx + bw - 4, row, " A ", COL_CYAN, bc);
    }

    console_print(bx + 2, by + bh - 2, "D-pad:nav  A:select  B:resume",
                  COL_DARK_GREY, pbg);
}

static void render_controls(const GameState* g)
{
    (void)g;
    uint32_t bg = 0xFF06060E;
    for (int y = 0; y < CON_H; y++)
    {
        console_hline(0, y, CON_W, ' ', COL_WHITE, bg);
    }
    console_border(0, 0, CON_W, CON_H, 0xFF2A2A55, bg);

    console_print((CON_W - 10) / 2, 1, " CONTROLS ", COL_GOLD, bg);

    /* Two-column layout */
    static const struct
    {
        const char* key;
        const char* desc;
    } ctrl[] = {
        {"D-pad", "Move / Attack"},
        {"A", "Pick up item"},
        {"B", "Open inventory"},
        {"R trigger", "Use stairs (>)"},
        {"START", "Pause menu"},
        {"", ""},
        {"--- Inventory ---", ""},
        {"D-pad Up/Down", "Navigate items"},
        {"A", "Use / Equip item"},
        {"X", "Drop item"},
        {"B", "Close inventory"},
        {"", ""},
        {"--- Targeting ---", ""},
        {"D-pad", "Move crosshair"},
        {"A", "Fire wand / scroll"},
        {"B", "Cancel targeting"},
        {"", ""},
        {"--- Status Effects ---", ""},
        {"Held / Frozen", "Cannot move; time passes"},
        {"Poisoned", "Strength drains over time"},
        {"Confused", "May move randomly"},
        {"Blind", "Cannot see the map"},
    };
    int n = (int)(sizeof(ctrl) / sizeof(ctrl[0]));
    for (int i = 0; i < n && i < 22; i++)
    {
        int row = 3 + i;
        if (ctrl[i].key[0] == '-')
        {
            console_print(4, row, ctrl[i].key, COL_ORANGE, bg);
        }
        else
        {
            console_printf(4, row, COL_YELLOW, bg, "%-18s", ctrl[i].key);
            console_print(23, row, ctrl[i].desc, COL_GREY, bg);
        }
    }

    console_print((CON_W - 26) / 2, CON_H - 2, "B / START : back to pause", COL_DARK_GREY, bg);
}

void game_render(const GameState* g)
{
    console_clear();

    if (g->screen == SCREEN_TITLE)
    {
        render_title(g);
        console_render();
        return;
    }

    if (g->screen == SCREEN_CONTROLS)
    {
        render_controls(g);
        console_render();
        return;
    }

    render_map(g);
    render_ui(g);

    if (g->screen == SCREEN_INVENTORY) render_inventory(g);
    if (g->screen == SCREEN_TARGET) render_target_overlay(g);
    if (g->screen == SCREEN_PAUSE) render_pause(g);

    if (g->screen == SCREEN_DEAD)
    {
        int bx = 21, bw = 38, by = 7, bh = 10;
        uint32_t ibg = 0xFF1A0000;
        for (int y = by + 1; y < by + bh - 1; y++)
        {
            console_hline(bx + 1, y, bw - 2, ' ', COL_RED, ibg);
        }
        console_border(bx, by, bw, bh, COL_RED, ibg);
        const char* t1 = "*  YOU HAVE DIED  *";
        console_print(bx + (bw - (int)strlen(t1)) / 2, by + 2, t1, COL_RED, ibg);
        char tmp[40];
        snprintf(tmp, sizeof(tmp), "Slain on floor %d  Gold: %d", g->depth, g->es.gold);
        console_print(bx + (bw - (int)strlen(tmp)) / 2, by + 4, tmp, 0xFF886666, ibg);
        const char* t3 = "START to begin anew";
        console_print(bx + (bw - (int)strlen(t3)) / 2, by + 7, t3, COL_DARK_GREY, ibg);
    }

    if (g->screen == SCREEN_VICTORY)
    {
        int bx = 14, bw = 52, by = 7, bh = 10;
        uint32_t ibg = 0xFF1A1400;
        for (int y = by + 1; y < by + bh - 1; y++)
        {
            console_hline(bx + 1, y, bw - 2, ' ', COL_YELLOW, ibg);
        }
        console_border(bx, by, bw, bh, COL_GOLD, ibg);
        const char* t1 = "* YOU ESCAPED WITH THE AMULET! *";
        console_print(bx + (bw - (int)strlen(t1)) / 2, by + 2, t1, COL_GOLD, ibg);
        const char* t2 = "Your legend will be remembered forever.";
        console_print(bx + (bw - (int)strlen(t2)) / 2, by + 4, t2, COL_WHITE, ibg);
        char tmp[52];
        snprintf(tmp, sizeof(tmp), "Level %d  Gold: %d", g->es.player_level, g->es.gold);
        console_print(bx + (bw - (int)strlen(tmp)) / 2, by + 6, tmp, COL_YELLOW, ibg);
        const char* t3 = "START to begin anew";
        console_print(bx + (bw - (int)strlen(t3)) / 2, by + 8, t3, COL_DARK_GREY, ibg);
    }

    console_render();
}

static void game_tick_hunger(GameState* g)
{
    EntityState* es = &g->es;
    int rate = es->ring_slow_digest ? 2 : 1;
    /* Tick every 'rate' calls (simplistic: skip tick if slow digest) */
    static int hunger_tick = 0;
    hunger_tick++;
    if (hunger_tick < rate) return;
    hunger_tick = 0;

    if (es->hunger > 0)
    {
        es->hunger--;
        if (es->hunger == 399) entity_log(es, "You are starting to feel hungry.");
        else if (es->hunger == 199) entity_log(es, "You are very hungry!");
        else if (es->hunger == 1) entity_log(es, "You are starving!");
    }
    else
    {
        es->player.hp--;
        if (es->player.hp % 5 == 0) entity_log(es, "Starving! You lose HP.");
    }
}

int game_player_move(GameState* g, int dx, int dy)
{
    if (g->screen != SCREEN_PLAY) return 0;
    EntityState* es = &g->es;

    /* Held/frozen: waste turn; main.c will call mobs_take_turn */
    if (es->held_turns > 0 || es->frozen_turns > 0)
    {
        game_tick_hunger(g);
        return 1;
    }

    int nx = es->player.x + dx;
    int ny = es->player.y + dy;

    Entity* target = entity_at(es, nx, ny);
    if (target && target != &es->player)
    {
        int dmg = entity_attack(es, &es->player, target);
        if (target->alive)
        {
            entity_log(es, "You hit the %s for %d!", target->name, dmg);
        }
        if (hit_sfx != SFXHND_INVALID) snd_sfx_play(hit_sfx, 255, 128);
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
    game_tick_hunger(g);

    if (es->player.hp <= 0)
    {
        es->player.alive = 0;
        g->screen = SCREEN_DEAD;
    }
    return 1;
}

void game_player_use_stairs(GameState* g)
{
    if (g->screen != SCREEN_PLAY) return;
    EntityState* es = &g->es;
    if (es->player.x != g->dungeon.stairs.x || es->player.y != g->dungeon.stairs.y)
    {
        entity_log(es, "No stairs here. Find > to descend.");
        return;
    }

    if (g->depth >= 26)
    {
        /* Need Amulet to win */
        int has_amulet = 0;
        for (int i = 0; i < es->inv_count; i++)
        {
            if (es->inv[i].kind == ITEM_AMULET_YENDOR)
            {
                has_amulet = 1;
                break;
            }
        }
        if (has_amulet)
        {
            g->screen = SCREEN_VICTORY;
            entity_log(es, "You escape with the Amulet of Yendor! You win!");
        }
        else
        {
            entity_log(es, "You must find the Amulet of Yendor first!");
        }
    }
    else
    {
        g->depth++;
        entity_log(es, "You descend to floor %d...", g->depth);
        game_new_level(g);
    }
}

void game_player_pickup(GameState* g)
{
    if (g->screen != SCREEN_PLAY) return;
    EntityState* es = &g->es;
    Item* it = item_at(es, es->player.x, es->player.y);
    if (!it)
    {
        entity_log(es, "Nothing here to pick up.");
        return;
    }

    /* Gold is picked up directly, not into inventory */
    if (it->kind == ITEM_GOLD)
    {
        es->gold += it->value;
        entity_log(es, "You pick up %d gold pieces. (Total: %d)", it->value, es->gold);
        it->kind = ITEM_NONE;
        return;
    }

    if (es->inv_count >= MAX_INV)
    {
        entity_log(es, "No room in your pack!");
        return;
    }
    if (es->carry_weight + it->weight > es->max_carry)
    {
        entity_log(es, "Too heavy! (%d/%d)", es->carry_weight, es->max_carry);
        return;
    }

    es->carry_weight += it->weight;
    es->inv[es->inv_count++] = *it;

    /* Auto-pickup amulet triggers win on ascent */
    if (it->kind == ITEM_AMULET_YENDOR)
    {
        entity_log(es, "You seize the Amulet of Yendor! Now escape!");
    }
    else
    {
        entity_log(es, "You pick up the %s.", it->name);
    }

    it->kind = ITEM_NONE;
}

void game_inventory_use(GameState* g)
{
    if (g->screen != SCREEN_INVENTORY)
    {
        return;
    }

    EntityState* es = &g->es;
    int slot = g->inv_cursor;

    if (slot < 0 || slot >= es->inv_count)
    {
        return;
    }

    Item* it = &es->inv[slot];
    ItemKind k = it->kind;

    /* ---- Weapons ---- */
    if (item_is_weapon(k))
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
            es->weapon_bonus = it->value + it->enchant;
            entity_log(es, "You wield the %s. (+%d ATK)", it->name, es->weapon_bonus);
        }
        goto end_use;
    }

    /* ---- Armor ---- */
    if (item_is_armor(k))
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
            {
                entity_log(es, "You remove the %s.", es->inv[es->armor_slot].name);
            }

            es->armor_slot = slot;
            es->armor_bonus = it->value + it->enchant;
            entity_log(es, "You put on the %s. (+%d DEF)", it->name, es->armor_bonus);
        }
        goto end_use;
    }

    /* ---- Rings ---- */
    if (item_is_ring(k))
    {
        if (es->ring_left_slot == slot || es->ring_right_slot == slot)
        {
            if (es->ring_left_slot == slot)
            {
                es->ring_left_slot = -1;
            }
            else
            {
                es->ring_right_slot = -1;
            }
            entity_recompute_rings(es);
            entity_log(es, "You remove the %s.", it->name);
        }
        else
        {
            if (es->ring_left_slot < 0)
            {
                es->ring_left_slot = slot;
            }
            else if (es->ring_right_slot < 0)
            {
                es->ring_right_slot = slot;
            }
            else
            {
                /* Replace left ring */
                entity_log(es, "You remove the %s.", es->inv[es->ring_left_slot].name);
                es->ring_left_slot = slot;
            }
            entity_recompute_rings(es);
            entity_log(es, "You put on the %s.", it->name);
        }
        goto end_use;
    }

    /* ---- Potions ---- */
    if (item_is_potion(k))
    {
        switch (k)
        {
        case ITEM_POT_HEALING:
            es->player.hp = es->player.hp + it->value;
            if (es->player.hp > es->player.hp_max) es->player.hp = es->player.hp_max;
            entity_log(es, "You drink a healing potion. Restored %d HP.", it->value);
            break;
        case ITEM_POT_EXTRA_HEALING:
            es->player.hp = es->player.hp + it->value;
            if (es->player.hp > es->player.hp_max) es->player.hp = es->player.hp_max;
            entity_log(es, "You feel much better! (+%d HP)", it->value);
            break;
        case ITEM_POT_GAIN_STR:
            es->player_str++;
            es->player_str_max++;
            es->player.atk++;
            entity_log(es, "You feel stronger! STR %d", es->player_str);
            break;
        case ITEM_POT_RESTORE_STR:
            es->player.atk += (es->player_str_max - es->player_str);
            es->player_str = es->player_str_max;
            es->poisoned = 0;
            entity_log(es, "You feel your strength return! STR %d", es->player_str);
            break;
        case ITEM_POT_HASTE_SELF:
            es->hasted_turns = it->value;
            entity_log(es, "You feel yourself speed up!");
            break;
        case ITEM_POT_CONFUSION:
            es->confused_turns = it->value;
            entity_log(es, "You feel confused!");
            break;
        case ITEM_POT_BLINDNESS:
            es->blind_turns = it->value;
            entity_log(es, "You are blinded!");
            break;
        case ITEM_POT_HALLUCINATION:
            es->halluc_turns = it->value;
            entity_log(es, "You see strange visions!");
            break;
        case ITEM_POT_POISON:
            if (!es->ring_sustain_str)
            {
                es->poisoned = 5;
                es->poison_timer = 0;
                entity_log(es, "The poison courses through you!");
            }
            else
            {
                entity_log(es, "The ring sustains your strength.");
            }
            break;
        case ITEM_POT_RAISE_LEVEL:
            entity_grant_xp(es, es->xp_to_next - es->player_xp);
            entity_log(es, "You feel more experienced!");
            break;
        case ITEM_POT_SEE_INVIS:
            es->blind_turns = 0; /* clears blindness too */
            es->ring_see_invis = 1; /* temporary */
            entity_log(es, "You can now see the invisible!");
            break;
        case ITEM_POT_MON_DETECT:
            {
                int n = 0;
                for (int i = 0; i < es->mob_count; i++)
                {
                    if (es->mobs[i].alive)
                    {
                        n++;
                    }
                }
                entity_log(es, "You sense %d monster%s on this floor.", n, n != 1 ? "s" : "");
                break;
            }
        case ITEM_POT_MAGIC_DETECT:
            entity_log(es, "You sense magic items nearby.");
            break;
        case ITEM_POT_LEVITATION:
            es->levitate_turns = it->value;
            entity_log(es, "You float into the air!");
            break;
        default:
            entity_log(es, "You drink the potion. Nothing obvious happens.");
            break;
        }
        consume_item(g, slot);
        goto end_use;
    }

    /* ---- Scrolls ---- */
    if (item_is_scroll(k))
    {
        switch (k)
        {
        case ITEM_SCR_TELEPORT:
            teleport_player(g);
            break;
        case ITEM_SCR_MAGIC_MAP:
            for (int y = 0; y < MAP_H; y++)
            {
                for (int x = 0; x < MAP_W; x++)
                {
                    g->dungeon.explored[y][x] = 1;
                }
            }

            entity_log(es, "The floor is revealed!");
            break;
        case ITEM_SCR_HOLD_MONSTER:
            wand_hold_all(g);
            break;
        case ITEM_SCR_SLEEP:
            {
                int n = 0;
                for (int i = 0; i < es->mob_count; i++)
                {
                    Entity* m = &es->mobs[i];

                    if (!m->alive || !g->dungeon.visible[m->y][m->x])
                    {
                        continue;
                    }

                    m->held = grng_range(5, 12);
                    n++;
                }
                entity_log(es, "%d monster%s fall asleep!", n, n != 1 ? "s" : "");
                break;
            }
        case ITEM_SCR_ENCH_WEAPON:
            if (es->weapon_slot >= 0)
            {
                es->inv[es->weapon_slot].enchant++;

                es->weapon_bonus = es->inv[es->weapon_slot].value + es->inv[es->weapon_slot].enchant;

                entity_log(es, "Your weapon glows! (+%d ATK)", es->weapon_bonus);
            }
            else
            {
                entity_log(es, "Nothing to enchant.");
            }
            break;
        case ITEM_SCR_ENCH_ARMOR:
            if (es->armor_slot >= 0)
            {
                es->inv[es->armor_slot].enchant++;

                es->armor_bonus = es->inv[es->armor_slot].value + es->inv[es->armor_slot].enchant;

                entity_log(es, "Your armor shines! (+%d DEF)", es->armor_bonus);
            }
            else
            {
                entity_log(es, "Nothing to enchant.");
            }
            break;
        case ITEM_SCR_PROTECT_ARMOR:
            if (es->armor_slot >= 0)
            {
                es->ring_maint_armor = 1; /* temporary protection */
                entity_log(es, "Your armor is protected from corrosion.");
            }
            else
            {
                entity_log(es, "Nothing to protect.");
            }
            break;
        case ITEM_SCR_SCARE_MONSTER:
            for (int i = 0; i < es->mob_count; i++)
            {
                Entity* m = &es->mobs[i];
                if (!m->alive)
                {
                    continue;
                }

                m->flags &= ~MOB_MEAN;
                m->held = grng_range(10, 20);
            }
            entity_log(es, "The monsters are terrified of you!");
            break;
        case ITEM_SCR_AGGRAVATE:
            for (int i = 0; i < es->mob_count; i++)
            {
                es->mobs[i].flags |= MOB_MEAN;
            }
            entity_log(es, "All monsters are enraged!");
            break;
        case ITEM_SCR_REMOVE_CURSE:
            for (int i = 0; i < es->inv_count; i++)
            {
                es->inv[i].cursed = 0;
            }

            entity_log(es, "You feel a burden lifted.");
            break;
        case ITEM_SCR_CREATE_MONSTER:
            {
                int placed = 0;
                for (int tries = 0; tries < 20 && !placed; tries++)
                {
                    int nx = es->player.x + grng_range(-3, 3);
                    int ny = es->player.y + grng_range(-3, 3);
                    if (dungeon_passable(&g->dungeon, nx, ny) && !entity_at(es, nx, ny))
                    {
                        mob_spawn(es, pick_mob_for_depth(g->depth), nx, ny);
                        placed = 1;
                    }
                }
                entity_log(es, "A monster appears!");
                break;
            }
        case ITEM_SCR_FOOD_DETECT:
            entity_log(es, "You sense food on this floor.");
            break;
        default:
            entity_log(es, "You read the scroll. Nothing obvious happens.");
            break;
        }
        consume_item(g, slot);
        goto end_use;
    }

    /* ---- Wands ---- */
    if (item_is_wand(k))
    {
        if (it->value <= 0)
        {
            entity_log(es, "The wand is out of charges!");
            goto end_use;
        }
        if (k == ITEM_WAND_LIGHT)
        {
            fov_compute(&g->dungeon, es->player.x, es->player.y, 20);
            /* Mark visible as explored */
            for (int y = 0; y < MAP_H; y++)
            {
                for (int x = 0; x < MAP_W; x++)
                {
                    if (g->dungeon.visible[y][x]) g->dungeon.explored[y][x] = 1;
                }
            }

            entity_log(es, "The dungeon is lit up!");
            it->value--;

            if (it->value <= 0)
            {
                entity_log(es, "The wand crumbles to dust.");
                consume_item(g, slot);
            }
            goto end_use;
        }
        /* Other wands enter targeting mode */
        g->target_x = es->player.x;
        g->target_y = es->player.y;
        g->target_item_slot = slot;
        g->screen = SCREEN_TARGET;
        return;
    }

    /* ---- Food ---- */
    if (k == ITEM_FOOD)
    {
        es->hunger += it->value;
        if (es->hunger > es->hunger_max) es->hunger = es->hunger_max;
        entity_log(es, "You eat the %s.", it->name);
        consume_item(g, slot);
        goto end_use;
    }

    /* ---- Gold in inventory (shouldn't normally happen) ---- */
    if (k == ITEM_GOLD)
    {
        entity_log(es, "You count %d gold pieces.", it->value);
        goto end_use;
    }

    /* ---- Amulet ---- */
    if (k == ITEM_AMULET_YENDOR)
    {
        entity_log(es, "The Amulet pulses with power. Find the stairs!");
        goto end_use;
    }

    entity_log(es, "Nothing happens.");

end_use:
    if (g->inv_cursor >= es->inv_count && g->inv_cursor > 0)
    {
        g->inv_cursor = es->inv_count - 1;
    }

    if (g->screen == SCREEN_INVENTORY)
    {
        mobs_take_turn(es, &g->dungeon);
        game_tick_hunger(g);
        if (es->player.hp <= 0)
        {
            es->player.alive = 0;
            g->screen = SCREEN_DEAD;
        }
    }
}

void game_open_inventory(GameState* g)
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

void game_inventory_cursor(GameState* g, int dir)
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

void game_inventory_close(GameState* g)
{
    g->screen = SCREEN_PLAY;
}

void game_inventory_drop(GameState* g)
{
    if (g->screen != SCREEN_INVENTORY)
    {
        return;
    }

    EntityState* es = &g->es;
    int slot = g->inv_cursor;

    if (slot < 0 || slot >= es->inv_count)
    {
        return;
    }

    if (es->weapon_slot == slot || es->armor_slot == slot ||
        es->ring_left_slot == slot || es->ring_right_slot == slot)
    {
        entity_log(es, "Unequip it before dropping.");
        return;
    }
    if (es->item_count >= MAX_ENTITIES)
    {
        entity_log(es, "No room on the ground!");
        return;
    }

    Item* it = &es->inv[slot];
    if (it->kind == ITEM_GOLD)
    {
        entity_log(es, "You drop %d gold.", it->value);
        item_spawn_gold(es, it->value, es->player.x, es->player.y);
        consume_item(g, slot);
        return;
    }

    es->items[es->item_count] = *it;
    es->items[es->item_count].x = es->player.x;
    es->items[es->item_count].y = es->player.y;
    es->item_count++;
    entity_log(es, "You drop the %s.", it->name);
    consume_item(g, slot);
}

void game_target_move(GameState* g, int dx, int dy)
{
    int nx = g->target_x + dx;
    int ny = g->target_y + dy;
    g->target_x = (nx < 0) ? 0 : (nx >= MAP_W) ? MAP_W - 1 : nx;
    g->target_y = (ny < 0) ? 0 : (ny >= MAP_H) ? MAP_H - 1 : ny;
}

void game_target_confirm(GameState* g)
{
    EntityState* es = &g->es;

    int slot = g->target_item_slot;

    if (slot < 0 || slot >= es->inv_count)
    {
        g->screen = SCREEN_PLAY;
        return;
    }

    Item* it = &es->inv[slot];
    zap_at_target(g, g->target_x, g->target_y, it->kind);

    it->value--;
    if (it->value <= 0)
    {
        entity_log(es, "The wand crumbles to dust.");
        consume_item(g, slot);
    }
    else
    {
        entity_log(es, "The wand has %d charge%s left.", it->value, it->value == 1 ? "" : "s");
    }

    mobs_take_turn(es, &g->dungeon);
    game_tick_hunger(g);

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

void game_target_cancel(GameState* g) { g->screen = SCREEN_PLAY; }

void game_open_pause(GameState* g)
{
    g->prev_screen = g->screen;
    g->screen = SCREEN_PAUSE;
    g->pause_cursor = 0;
}

void game_pause_cursor(GameState* g, int dir)
{
    if (g->screen != SCREEN_PAUSE)
    {
        return;
    }

    g->pause_cursor += dir;

    if (g->pause_cursor < 0)
    {
        g->pause_cursor = 2;
    }

    if (g->pause_cursor > 2)
    {
        g->pause_cursor = 0;
    }
}

void game_pause_confirm(GameState* g)
{
    if (g->screen != SCREEN_PAUSE)
    {
        return;
    }

    switch (g->pause_cursor)
    {
    case 0: /* Resume */
        g->screen = g->prev_screen;
        break;
    case 1: /* Controls */
        g->screen = SCREEN_CONTROLS;
        break;
    case 2: /* Quit to title */
        game_init(g, g->world_seed + g->frame);
        break;
    }
}

void game_pause_back(GameState* g)
{
    if (g->screen == SCREEN_CONTROLS)
    {
        g->screen = SCREEN_PAUSE;
    }
    else if (g->screen == SCREEN_PAUSE)
    {
        g->screen = g->prev_screen;
    }
}
