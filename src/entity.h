#pragma once
#include "console.h"
#include "dungeon.h"
#include <stdint.h>

#define MAX_ENTITIES 64
#define MAX_INV 26
#define MAX_LOG 10

#define MOB_MEAN   (1u<<0)
#define MOB_FLY    (1u<<1)
#define MOB_REGEN  (1u<<2)
#define MOB_INVIS  (1u<<3)
#define MOB_GREED  (1u<<4)

#define SATT_NONE       0
#define SATT_RUST       1
#define SATT_HOLD       2
#define SATT_FREEZE     3
#define SATT_STEAL_GOLD 4
#define SATT_STEAL_ITEM 5
#define SATT_POISON     6
#define SATT_DRAIN_LVL  7

typedef enum
{
    ITEM_NONE = 0,
    ITEM_MACE, ITEM_LONGSWORD, ITEM_SHORTBOW, ITEM_ARROW, ITEM_DAGGER,
    ITEM_TWO_HANDED_SWORD, ITEM_DART, ITEM_SHURIKEN, ITEM_SPEAR,
    ITEM_ARMOR_LEATHER, ITEM_ARMOR_RING_MAIL, ITEM_ARMOR_STUDDED,
    ITEM_ARMOR_SCALE_MAIL, ITEM_ARMOR_CHAIN_MAIL, ITEM_ARMOR_SPLINT_MAIL,
    ITEM_ARMOR_BANDED_MAIL, ITEM_ARMOR_PLATE_MAIL,
    ITEM_POT_CONFUSION, ITEM_POT_HALLUCINATION, ITEM_POT_POISON,
    ITEM_POT_GAIN_STR, ITEM_POT_SEE_INVIS, ITEM_POT_HEALING,
    ITEM_POT_MON_DETECT, ITEM_POT_MAGIC_DETECT, ITEM_POT_RAISE_LEVEL,
    ITEM_POT_EXTRA_HEALING, ITEM_POT_HASTE_SELF, ITEM_POT_RESTORE_STR,
    ITEM_POT_BLINDNESS, ITEM_POT_LEVITATION,
    ITEM_SCR_MON_CONFUSION, ITEM_SCR_MAGIC_MAP, ITEM_SCR_HOLD_MONSTER,
    ITEM_SCR_SLEEP, ITEM_SCR_ENCH_ARMOR, ITEM_SCR_ID_POTION,
    ITEM_SCR_ID_SCROLL, ITEM_SCR_ID_WEAPON, ITEM_SCR_ID_ARMOR,
    ITEM_SCR_ID_RING, ITEM_SCR_SCARE_MONSTER, ITEM_SCR_FOOD_DETECT,
    ITEM_SCR_TELEPORT, ITEM_SCR_ENCH_WEAPON, ITEM_SCR_CREATE_MONSTER,
    ITEM_SCR_REMOVE_CURSE, ITEM_SCR_AGGRAVATE, ITEM_SCR_PROTECT_ARMOR,
    ITEM_RING_PROTECTION, ITEM_RING_ADD_STR, ITEM_RING_SUSTAIN_STR,
    ITEM_RING_SEARCHING, ITEM_RING_SEE_INVIS, ITEM_RING_ADORNMENT,
    ITEM_RING_AGGRAVATE, ITEM_RING_DEXTERITY, ITEM_RING_INCREASE_DMG,
    ITEM_RING_REGENERATION, ITEM_RING_SLOW_DIGEST, ITEM_RING_TELEPORTATION,
    ITEM_RING_STEALTH, ITEM_RING_MAINT_ARMOR,
    ITEM_WAND_LIGHT, ITEM_WAND_INVIS, ITEM_WAND_LIGHTNING, ITEM_WAND_FIRE,
    ITEM_WAND_COLD, ITEM_WAND_POLYMORPH, ITEM_WAND_MAGIC_MISSILE,
    ITEM_WAND_HASTE_MONSTER, ITEM_WAND_SLOW_MONSTER, ITEM_WAND_DRAIN_LIFE,
    ITEM_WAND_NOTHING, ITEM_WAND_TELEPORT_AWAY, ITEM_WAND_TELEPORT_TO,
    ITEM_WAND_CANCELLATION,
    ITEM_AMULET_YENDOR, ITEM_FOOD, ITEM_GOLD,
} ItemKind;

static inline int item_is_weapon(ItemKind k)
{
    return k >= ITEM_MACE && k <= ITEM_SPEAR;
}

static inline int item_is_armor(ItemKind k)
{
    return k >= ITEM_ARMOR_LEATHER && k <= ITEM_ARMOR_PLATE_MAIL;
}

static inline int item_is_potion(ItemKind k)
{
    return k >= ITEM_POT_CONFUSION && k <= ITEM_POT_LEVITATION;
}

static inline int item_is_scroll(ItemKind k)
{
    return k >= ITEM_SCR_MON_CONFUSION && k <= ITEM_SCR_PROTECT_ARMOR;
}

static inline int item_is_ring(ItemKind k)
{
    return k >= ITEM_RING_PROTECTION && k <= ITEM_RING_MAINT_ARMOR;
}

static inline int item_is_wand(ItemKind k)
{
    return k >= ITEM_WAND_LIGHT && k <= ITEM_WAND_CANCELLATION;
}

typedef struct
{
    ItemKind kind;
    int x, y;
    int value; /* HP restored / damage / charges / gold amount */
    int weight;
    int enchant;
    int cursed;
    char name[24];
    char desc[48];
    char action[8];
    uint8_t ch;
    uint32_t col;
} Item;

typedef enum
{
    MOB_NONE = 0,
    MOB_PLAYER,
    MOB_AQUATOR, MOB_BAT, MOB_CENTAUR, MOB_DRAGON, MOB_EMU, MOB_FLYTRAP,
    MOB_GRIFFIN, MOB_HOBGOBLIN, MOB_ICE_MONSTER, MOB_JABBERWOCK, MOB_KESTREL,
    MOB_LEPRECHAUN, MOB_MEDUSA, MOB_NYMPH, MOB_ORC, MOB_PHANTOM, MOB_QUAGGA,
    MOB_RATTLESNAKE, MOB_SNAKE, MOB_TROLL, MOB_UNICORN, MOB_VAMPIRE,
    MOB_WRAITH, MOB_XEROC, MOB_YETI, MOB_ZOMBIE,
} MobKind;

typedef struct
{
    MobKind kind;
    int x, y;
    int hp, hp_max;
    int atk, def;
    int xp_reward;
    int level; /* earliest dungeon depth this mob appears */
    uint32_t flags;
    int satt;
    int held; /* turns remaining paralysed */
    char name[20];
    uint8_t ch;
    uint32_t col;
    int alive;
} Entity;

typedef struct
{
    Entity player;
    int player_xp, player_level, xp_to_next;
    int player_str, player_str_max;
    int gold;

    Item inv[MAX_INV];
    int inv_count;
    int weapon_slot; /* -1 = none */
    int armor_slot;
    int ring_left_slot;
    int ring_right_slot;
    int weapon_bonus;
    int armor_bonus;
    int ring_def_bonus;
    int ring_atk_bonus;
    int ring_see_invis;
    int ring_slow_digest;
    int ring_regen;
    int ring_stealth;
    int ring_sustain_str;
    int ring_maint_armor;
    int ring_teleport;

    int carry_weight, max_carry;
    int hunger, hunger_max;

    int confused_turns;
    int blind_turns;
    int halluc_turns;
    int hasted_turns;
    int held_turns;
    int frozen_turns;
    int levitate_turns;
    int poisoned;
    int poison_timer;

    Entity mobs[MAX_ENTITIES];
    int mob_count;

    Item items[MAX_ENTITIES];
    int item_count;

    char log[MAX_LOG][80];
    int log_count;
} EntityState;

void entity_state_init(EntityState* es);
void entity_log(EntityState* es, const char* fmt, ...);
Entity* entity_at(EntityState* es, int x, int y);
Item* item_at(EntityState* es, int x, int y);

void mob_spawn(EntityState* es, MobKind kind, int x, int y);
void item_spawn(EntityState* es, ItemKind kind, int x, int y);
void item_spawn_gold(EntityState* es, int amount, int x, int y);

int entity_attack(EntityState* es, Entity* attacker, Entity* target);
void entity_grant_xp(EntityState* es, int xp);
void mobs_take_turn(EntityState* es, const Dungeon* d);
void mobs_regen(EntityState* es);
void entity_tick_status(EntityState* es);
void entity_recompute_rings(EntityState* es);
