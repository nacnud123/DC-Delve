#pragma once
#include "console.h"
#include "dungeon.h"
#include <stdint.h>

#define MAX_ENTITIES 64
#define MAX_INV 26 /* a–z slots; actual limit is carry weight */
#define MAX_LOG 10

typedef enum {
    ITEM_NONE = 0,
    ITEM_POTION_HP,
    ITEM_SWORD,
    ITEM_SHIELD,
    ITEM_SCROLL_FIREBALL,
    ITEM_LEATHER_ARMOR,
    ITEM_CHAIN_MAIL,
    ITEM_PLATE_ARMOR,
    ITEM_WAND_FIREBALL,
} ItemKind;

typedef struct {
    ItemKind kind;
    int x, y;   /* world pos; -1,-1 if in inventory */
    int value;  /* hp restore / damage bonus / charges */
    int weight; /* encumbrance units */
    char name[24];
    char desc[48];
    char action[8]; /* "drink" "equip" "aim" */
    uint8_t ch;
    uint32_t col;
} Item;

typedef enum {
    MOB_NONE = 0,
    MOB_PLAYER,
    MOB_RAT,
    MOB_GOBLIN,
    MOB_ORC,
    MOB_TROLL,
} MobKind;

typedef struct {
    MobKind kind;
    int x, y;
    int hp, hp_max;
    int atk, def;
    int xp_reward;
    char name[20];
    uint8_t ch;
    uint32_t col;
    int alive;
} Entity;

typedef struct {
    Entity player;
    int player_xp, player_level, xp_to_next;
    Item inv[MAX_INV];
    int inv_count;
    int weapon_slot; /* index into inv, -1 = none */
    int shield_slot;
    int armor_slot;
    int weapon_bonus;
    int shield_bonus;
    int armor_bonus;
    int carry_weight;
    int max_carry;

    Entity mobs[MAX_ENTITIES];
    int mob_count;

    Item items[MAX_ENTITIES];
    int item_count;

    char log[MAX_LOG][80];
    int log_count;
} EntityState;

void entity_state_init(EntityState *es);
void entity_log(EntityState *es, const char *fmt, ...);
Entity *entity_at(EntityState *es, int x, int y);
Item *item_at(EntityState *es, int x, int y);

void mob_spawn(EntityState *es, MobKind kind, int x, int y);
void item_spawn(EntityState *es, ItemKind kind, int x, int y);

int entity_attack(EntityState *es, Entity *attacker, Entity *target);
void entity_grant_xp(EntityState *es, int xp);
void mobs_take_turn(EntityState *es, const Dungeon *d);
