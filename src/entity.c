#include "entity.h"
#include "dungeon.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    MobKind kind;
    int hp, atk, def, xp;
    char name[20];
    uint8_t ch;
    uint32_t col;
} MobTemplate;

static const MobTemplate mob_tmpl[] = {
    {MOB_RAT, 4, 2, 0, 5, "rat", 'r', COL_BROWN},
    {MOB_GOBLIN, 8, 4, 1, 15, "goblin", 'g', COL_GREEN},
    {MOB_ORC, 16, 6, 2, 30, "orc", 'o', COL_DARK_GREEN},
    {MOB_TROLL, 30, 10, 4, 80, "troll", 'T', COL_CYAN},
};
#define N_TMPL (sizeof(mob_tmpl) / sizeof(mob_tmpl[0]))

typedef struct {
    ItemKind kind;
    int value;
    int weight;
    char name[24];
    char desc[48];
    char action[8];
    uint8_t ch;
    uint32_t col;
} ItemTemplate;

static const ItemTemplate item_tmpl[] = {
    {ITEM_POTION_HP, 20, 5, "healing potion", "Restores 20 HP when consumed.", "drink", '!', COL_RED},
    {ITEM_SWORD, 4, 15, "sword", "A sharp blade. Grants +4 ATK.", "equip", '/', COL_WHITE},
    {ITEM_SHIELD, 2, 20, "shield", "A sturdy shield. Grants +2 DEF.", "equip", ']', COL_GREY},
    {ITEM_SCROLL_FIREBALL, 12, 2, "fireball scroll", "Deals 12 dmg in 2-tile radius. Aim it.", "aim", '?', COL_ORANGE},
    {ITEM_LEATHER_ARMOR, 2, 15, "leather armor", "Light armour. Grants +2 DEF.", "equip", '[', COL_BROWN},
    {ITEM_CHAIN_MAIL, 4, 30, "chain mail", "Medium armour. Grants +4 DEF.", "equip", '[', COL_GREY},
    {ITEM_PLATE_ARMOR, 6, 50, "plate armor", "Heavy armour. Grants +6 DEF.", "equip", '[', COL_WHITE},
    {ITEM_WAND_FIREBALL, 5, 8, "wand of fire", "12 dmg in 2-tile radius. 5 charges.", "aim", '|', COL_ORANGE},
};
#define N_ITEM_TMPL (sizeof(item_tmpl) / sizeof(item_tmpl[0]))

void entity_state_init(EntityState *es)
{
    memset(es, 0, sizeof(EntityState));
    es->player.kind = MOB_PLAYER;
    es->player.hp = 30;
    es->player.hp_max = 30;
    es->player.atk = 5;
    es->player.def = 1;
    es->player.alive = 1;
    strcpy(es->player.name, "you");
    es->player.ch = '@';
    es->player.col = COL_WHITE;

    es->player_level = 1;
    es->player_xp = 0;
    es->xp_to_next = 30;
    es->weapon_slot = -1;
    es->shield_slot = -1;
    es->armor_slot = -1;
    es->weapon_bonus = 0;
    es->shield_bonus = 0;
    es->armor_bonus = 0;
    es->carry_weight = 0;
    es->max_carry = 100;
}

void entity_log(EntityState *es, const char *fmt, ...)
{
    if (es->log_count < MAX_LOG)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(es->log[es->log_count++], 80, fmt, ap);
        va_end(ap);
    }
    else
    {
        memmove(es->log[0], es->log[1], sizeof(es->log[0]) * (MAX_LOG - 1));
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(es->log[MAX_LOG - 1], 80, fmt, ap);
        va_end(ap);
    }
}

Entity *entity_at(EntityState *es, int x, int y)
{
    if (es->player.x == x && es->player.y == y && es->player.alive)
    {
        return &es->player;
    }

    for (int i = 0; i < es->mob_count; i++)
    {
        if (es->mobs[i].alive && es->mobs[i].x == x && es->mobs[i].y == y)
        {
            return &es->mobs[i];
        }
    }

    return NULL;
}

Item *item_at(EntityState *es, int x, int y)
{
    for (int i = 0; i < es->item_count; i++)
    {
        if (es->items[i].kind != ITEM_NONE && es->items[i].x == x && es->items[i].y == y)
        {
            return &es->items[i];
        }
    }

    return NULL;
}

void mob_spawn(EntityState *es, MobKind kind, int x, int y)
{
    if (es->mob_count >= MAX_ENTITIES)
    {
        return;
    }

    for (int i = 0; i < (int)N_TMPL; i++)
    {
        if (mob_tmpl[i].kind == kind)
        {
            Entity *m = &es->mobs[es->mob_count++];
            m->kind = kind;
            m->x = x;
            m->y = y;
            m->hp = m->hp_max = mob_tmpl[i].hp;
            m->atk = mob_tmpl[i].atk;
            m->def = mob_tmpl[i].def;
            m->xp_reward = mob_tmpl[i].xp;
            strcpy(m->name, mob_tmpl[i].name);
            m->ch = mob_tmpl[i].ch;
            m->col = mob_tmpl[i].col;
            m->alive = 1;
            return;
        }
    }
}

void item_spawn(EntityState *es, ItemKind kind, int x, int y)
{
    if (es->item_count >= MAX_ENTITIES)
    {
        return;
    }

    for (int i = 0; i < (int)N_ITEM_TMPL; i++)
    {
        if (item_tmpl[i].kind == kind)
        {
            Item *it = &es->items[es->item_count++];
            it->kind = kind;
            it->x = x;
            it->y = y;
            it->value = item_tmpl[i].value;
            it->weight = item_tmpl[i].weight;
            strcpy(it->name, item_tmpl[i].name);
            strcpy(it->desc, item_tmpl[i].desc);
            strcpy(it->action, item_tmpl[i].action);
            it->ch = item_tmpl[i].ch;
            it->col = item_tmpl[i].col;
            return;
        }
    }
}

static int rng_combat(int range)
{
    static unsigned int s = 0xdeadbeef;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (int)(s % (unsigned)range);
}

void entity_grant_xp(EntityState *es, int xp)
{
    es->player_xp += xp;
    while (es->player_xp >= es->xp_to_next)
    {
        es->player_level++;
        es->xp_to_next = es->xp_to_next * 3 / 2;
        es->player.hp_max += 5;
        es->player.hp = es->player.hp_max;
        es->player.atk++;

        entity_log(es, "You reached level %d!", es->player_level);
    }
}

int entity_attack(EntityState *es, Entity *attacker, Entity *target)
{
    int dmg = attacker->atk - target->def + rng_combat(5) - 2;
    if (attacker == &es->player)
    {
        dmg += es->weapon_bonus;
    }

    if (target == &es->player)
    {
        dmg -= (es->shield_bonus + es->armor_bonus);
    }

    if (dmg < 1)
    {
        dmg = 1;
    }

    target->hp -= dmg;
    if (target->hp <= 0)
    {
        target->alive = 0;
        if (target->kind != MOB_PLAYER)
        {
            entity_log(es, "You kill the %s! (+%d xp)", target->name, target->xp_reward);
            entity_grant_xp(es, target->xp_reward);
        }
    }
    return dmg;
}

static int mob_can_see_player(const Dungeon *d, const Entity *mob, const Entity *player)
{
    int dx = player->x - mob->x;
    int dy = player->y - mob->y;
    if (dx * dx + dy * dy > 100)
    {
        return 0; /* 10-tile range */
    }

    return d->visible[mob->y][mob->x];
}

static int sign(int v)
{
    return v > 0 ? 1 : v < 0 ? -1
                             : 0;
}

void mobs_take_turn(EntityState *es, const Dungeon *d)
{
    for (int i = 0; i < es->mob_count; i++)
    {
        Entity *m = &es->mobs[i];
        if (!m->alive)
        {
            continue;
        }

        if (!mob_can_see_player(d, m, &es->player))
        {
            continue;
        }

        int dx = es->player.x - m->x;
        int dy = es->player.y - m->y;

        int mx = 0, my = 0;
        if (abs(dx) >= abs(dy))
        {
            mx = sign(dx);
        }
        else
        {
            my = sign(dy);
        }

        int tx = m->x + mx, ty = m->y + my;

        if (tx == es->player.x && ty == es->player.y)
        {
            int dmg = entity_attack(es, m, &es->player);
            entity_log(es, "The %s hits you for %d hp!", m->name, dmg);
        }
        else if (dungeon_passable(d, tx, ty) && !entity_at(es, tx, ty))
        {
            m->x = tx;
            m->y = ty;
        }
        else
        {
            int ox = my, oy = mx;
            if (dungeon_passable(d, m->x + ox, m->y + oy) && !entity_at(es, m->x + ox, m->y + oy))
            {
                m->x += ox;
                m->y += oy;
            }
        }
    }
}
