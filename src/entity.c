#include "entity.h"
#include "dungeon.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int erng_state = 0xc0ffee42u;

static unsigned int erng(void)
{
    erng_state ^= erng_state << 13;
    erng_state ^= erng_state >> 17;
    erng_state ^= erng_state << 5;
    return erng_state;
}

static int erng_range(int lo, int hi)
{
    if (hi <= lo)
    {
        return lo;
    }
    return lo + (int)(erng() % (unsigned)(hi - lo + 1));
}

static int roll_dice(int n, int d)
{
    int total = 0;
    for (int i = 0; i < n; i++)
    {
        total += erng_range(1, d);
    }
    return total;
}

typedef struct
{
    MobKind kind;
    int level;
    int hp_n, hp_d;
    int atk;
    int def; /* 10 - AC */
    int xp;
    uint32_t flags;
    int satt;
    char name[20];
    uint8_t ch;
    uint32_t col;
} MobTemplate;

static const MobTemplate mob_tmpl[] = {
    {MOB_AQUATOR, 5, 5, 8, 0, 8, 20, MOB_MEAN, SATT_RUST, "aquator", 'A', COL_CYAN},
    {MOB_BAT, 1, 1, 8, 2, 7, 1, MOB_FLY, SATT_NONE, "bat", 'B', COL_DARK_GREY},
    {MOB_CENTAUR, 4, 4, 8, 4, 6, 17, 0, SATT_NONE, "centaur", 'C', COL_BROWN},
    {MOB_DRAGON, 10, 10, 8, 17, 11, 5000, MOB_MEAN, SATT_NONE, "dragon", 'D', COL_RED},
    {MOB_EMU, 1, 1, 8, 2, 3, 2, MOB_MEAN, SATT_NONE, "emu", 'E', COL_BROWN},
    {MOB_FLYTRAP, 8, 8, 8, 0, 7, 80, MOB_MEAN, SATT_HOLD, "venus flytrap", 'F', COL_GREEN},
    {MOB_GRIFFIN, 13, 13, 8, 8, 8, 2000, MOB_MEAN | MOB_FLY | MOB_REGEN, SATT_NONE, "griffin", 'G', COL_GOLD},
    {MOB_HOBGOBLIN, 1, 1, 8, 5, 5, 3, MOB_MEAN, SATT_NONE, "hobgoblin", 'H', COL_DARK_GREEN},
    {MOB_ICE_MONSTER, 1, 1, 8, 0, 1, 5, 0, SATT_FREEZE, "ice monster", 'I', COL_CYAN},
    {MOB_JABBERWOCK, 15, 15, 8, 13, 4, 3000, 0, SATT_NONE, "jabberwock", 'J', COL_ORANGE},
    {MOB_KESTREL, 1, 1, 8, 3, 3, 1, MOB_MEAN | MOB_FLY, SATT_NONE, "kestrel", 'K', COL_BROWN},
    {MOB_LEPRECHAUN, 3, 3, 8, 1, 2, 10, 0, SATT_STEAL_GOLD, "leprechaun", 'L', COL_GREEN},
    {MOB_MEDUSA, 8, 8, 8, 8, 8, 200, MOB_MEAN, SATT_NONE, "medusa", 'M', COL_MAGENTA},
    {MOB_NYMPH, 3, 3, 8, 0, 1, 37, 0, SATT_STEAL_ITEM, "nymph", 'N', COL_LIGHT_GREY},
    {MOB_ORC, 1, 1, 8, 5, 4, 5, MOB_GREED, SATT_NONE, "orc", 'O', COL_DARK_GREEN},
    {MOB_PHANTOM, 8, 8, 8, 10, 7, 120, MOB_INVIS, SATT_NONE, "phantom", 'P', COL_DARK_GREY},
    {MOB_QUAGGA, 3, 3, 8, 3, 7, 15, MOB_MEAN, SATT_NONE, "quagga", 'Q', COL_BROWN},
    {MOB_RATTLESNAKE, 2, 2, 8, 4, 7, 9, MOB_MEAN, SATT_POISON, "rattlesnake", 'R', COL_YELLOW},
    {MOB_SNAKE, 1, 1, 8, 2, 5, 2, MOB_MEAN, SATT_NONE, "snake", 'S', COL_GREEN},
    {MOB_TROLL, 6, 6, 8, 5, 6, 120, MOB_MEAN | MOB_REGEN, SATT_NONE, "troll", 'T', COL_CYAN},
    {MOB_UNICORN, 7, 7, 8, 5, 12, 190, MOB_MEAN, SATT_NONE, "black unicorn", 'U', COL_WHITE},
    {MOB_VAMPIRE, 8, 8, 8, 6, 9, 350, MOB_MEAN | MOB_REGEN, SATT_DRAIN_LVL, "vampire", 'V', COL_RED},
    {MOB_WRAITH, 5, 5, 8, 4, 6, 55, 0, SATT_NONE, "wraith", 'W', COL_DARK_GREY},
    {MOB_XEROC, 7, 7, 8, 10, 3, 100, 0, SATT_NONE, "xeroc", 'X', COL_YELLOW},
    {MOB_YETI, 4, 4, 8, 4, 4, 50, 0, SATT_NONE, "yeti", 'Y', COL_WHITE},
    {MOB_ZOMBIE, 2, 2, 8, 5, 2, 6, MOB_MEAN, SATT_NONE, "zombie", 'Z', COL_DARK_GREEN},
};
#define N_MOB_TMPL ((int)(sizeof(mob_tmpl) / sizeof(mob_tmpl[0])))

typedef struct
{
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
    {ITEM_MACE, 5, 20, "mace", "+5 ATK. Solid bashing weapon.", "equip", '/', COL_GREY},
    {ITEM_LONGSWORD, 8, 25, "long sword", "+8 ATK. Reliable blade.", "equip", '/', COL_WHITE},
    {ITEM_SHORTBOW, 1, 15, "short bow", "+1 ATK. Launcher for arrows.", "equip", ')', COL_BROWN},
    {ITEM_ARROW, 4, 1, "arrow", "2d3 hurl dmg. Fires from bow.", "hurl", '|', COL_BROWN},
    {ITEM_DAGGER, 4, 8, "dagger", "+4 ATK. Light and throwable.", "equip", '/', COL_YELLOW},
    {ITEM_TWO_HANDED_SWORD, 10, 35, "two-handed sword", "Heavy. +10 ATK.", "equip", '/', COL_LIGHT_GREY},
    {ITEM_DART, 1, 1, "dart", "1d3 hurl dmg. Comes in stacks.", "hurl", '|', COL_GREY},
    {ITEM_SHURIKEN, 2, 2, "shuriken", "2d4 hurl dmg. Comes in stacks.", "hurl", '*', COL_GREY},
    {ITEM_SPEAR, 5, 18, "spear", "+5 ATK. 1d6 hurl dmg.", "equip", '/', COL_BROWN},
    {ITEM_ARMOR_LEATHER, 2, 15, "leather armor", "AC 8. Light and flexible.", "equip", '[', COL_BROWN},
    {ITEM_ARMOR_RING_MAIL, 3, 25, "ring mail", "AC 7. Rings of metal.", "equip", '[', COL_GREY},
    {ITEM_ARMOR_STUDDED, 3, 20, "studded leather", "AC 7. Leather with studs.", "equip", '[', COL_BROWN},
    {ITEM_ARMOR_SCALE_MAIL, 4, 35, "scale mail", "AC 6. Overlapping scales.", "equip", '[', COL_DARK_GREEN},
    {ITEM_ARMOR_CHAIN_MAIL, 5, 40, "chain mail", "AC 5. Interlocked rings.", "equip", '[', COL_GREY},
    {ITEM_ARMOR_SPLINT_MAIL, 6, 50, "splint mail", "AC 4. Vertical metal strips.", "equip", '[', COL_LIGHT_GREY},
    {ITEM_ARMOR_BANDED_MAIL, 6, 45, "banded mail", "AC 4. Horizontal metal bands.", "equip", '[', COL_LIGHT_GREY},
    {ITEM_ARMOR_PLATE_MAIL, 7, 60, "plate mail", "AC 3. Near-impenetrable steel.", "equip", '[', COL_WHITE},
    {ITEM_POT_CONFUSION,      20, 1, "Confusion potion",      "Causes confusion for a time.",          "drink", '!', COL_DARK_BLUE  },
    {ITEM_POT_HALLUCINATION,  30, 1, "Hallucination potion",  "Wild visions distort reality.",         "drink", '!', COL_MAGENTA    },
    {ITEM_POT_POISON,          3, 1, "Poison potion",         "Drains your strength over time.",       "drink", '!', COL_DARK_GREEN  },
    {ITEM_POT_GAIN_STR,        1, 1, "Gain Strength potion",  "Increases your strength by 1.",         "drink", '!', COL_GREEN       },
    {ITEM_POT_SEE_INVIS,      20, 1, "See Invisible potion",  "Reveals invisible creatures.",          "drink", '!', COL_CYAN        },
    {ITEM_POT_HEALING,        30, 1, "Healing potion",        "Restores 30 HP.",                       "drink", '!', COL_RED         },
    {ITEM_POT_MON_DETECT,      1, 1, "Monster Detect potion", "Reveals all nearby monsters.",          "drink", '!', COL_YELLOW      },
    {ITEM_POT_MAGIC_DETECT,    1, 1, "Magic Detect potion",   "Reveals magic items on this floor.",    "drink", '!', COL_GOLD        },
    {ITEM_POT_RAISE_LEVEL,     1, 1, "Raise Level potion",    "Grants one experience level.",          "drink", '!', COL_GOLD        },
    {ITEM_POT_EXTRA_HEALING,  60, 1, "Extra Healing potion",  "Restores 60 HP.",                       "drink", '!', COL_RED         },
    {ITEM_POT_HASTE_SELF,     10, 1, "Haste Self potion",     "Move twice per turn briefly.",          "drink", '!', COL_CYAN        },
    {ITEM_POT_RESTORE_STR,     1, 1, "Restore Str. potion",   "Fully restores lost strength.",         "drink", '!', COL_GREEN       },
    {ITEM_POT_BLINDNESS,      25, 1, "Blindness potion",      "Cannot see the dungeon for a time.",    "drink", '!', COL_DARK_GREY   },
    {ITEM_POT_LEVITATION,     20, 1, "Levitation potion",     "Float over floor hazards briefly.",     "drink", '!', COL_CYAN        },
    {ITEM_SCR_MON_CONFUSION,   1, 1, "Mon. Confusion scroll", "Next monster you hit gets confused.",   "read",  '?', COL_WHITE       },
    {ITEM_SCR_MAGIC_MAP,       1, 1, "Magic Map scroll",      "Reveals the entire floor map.",         "read",  '?', COL_GOLD        },
    {ITEM_SCR_HOLD_MONSTER,    1, 1, "Hold Monster scroll",   "Freezes all nearby monsters.",          "read",  '?', COL_CYAN        },
    {ITEM_SCR_SLEEP,           1, 1, "Sleep scroll",          "Puts nearby monsters to sleep.",        "read",  '?', COL_BLUE        },
    {ITEM_SCR_ENCH_ARMOR,      1, 1, "Enchant Armor scroll",  "Improves your equipped armor by +1.",   "read",  '?', COL_GREEN       },
    {ITEM_SCR_ID_POTION,       1, 1, "Identify Potion scroll","Identifies an unknown potion.",         "read",  '?', COL_WHITE       },
    {ITEM_SCR_ID_SCROLL,       1, 1, "Identify Scroll scroll","Identifies an unknown scroll.",         "read",  '?', COL_WHITE       },
    {ITEM_SCR_ID_WEAPON,       1, 1, "Identify Weapon scroll","Identifies an unknown weapon.",         "read",  '?', COL_WHITE       },
    {ITEM_SCR_ID_ARMOR,        1, 1, "Identify Armor scroll", "Identifies unknown armor.",             "read",  '?', COL_WHITE       },
    {ITEM_SCR_ID_RING,         1, 1, "Identify Ring scroll",  "Identifies a ring, wand, or staff.",   "read",  '?', COL_WHITE       },
    {ITEM_SCR_SCARE_MONSTER,   1, 1, "Scare Monster scroll",  "Monsters flee from you briefly.",       "read",  '?', COL_YELLOW      },
    {ITEM_SCR_FOOD_DETECT,     1, 1, "Food Detection scroll", "Reveals food on this floor.",           "read",  '?', COL_BROWN       },
    {ITEM_SCR_TELEPORT,        1, 1, "Teleport scroll",       "Teleports you to a random location.",   "read",  '?', COL_MAGENTA     },
    {ITEM_SCR_ENCH_WEAPON,     1, 1, "Enchant Weapon scroll", "Improves your equipped weapon by +1.",  "read",  '?', COL_GREEN       },
    {ITEM_SCR_CREATE_MONSTER,  1, 1, "Create Monster scroll", "Summons a monster nearby.",             "read",  '?', COL_RED         },
    {ITEM_SCR_REMOVE_CURSE,    1, 1, "Remove Curse scroll",   "Removes curse from all held items.",    "read",  '?', COL_CYAN        },
    {ITEM_SCR_AGGRAVATE,       1, 1, "Aggravate scroll",      "Angers all monsters on the floor.",     "read",  '?', COL_ORANGE      },
    {ITEM_SCR_PROTECT_ARMOR,   1, 1, "Protect Armor scroll",  "Protects your armor from corrosion.",   "read",  '?', COL_BLUE        },
    {ITEM_RING_PROTECTION,     1, 2, "Protection ring",       "Adds +1 to your armor class.",          "wear",  '=', COL_GOLD        },
    {ITEM_RING_ADD_STR,        1, 2, "Add Strength ring",     "Adds +1 to your attack.",               "wear",  '=', COL_GREEN       },
    {ITEM_RING_SUSTAIN_STR,    1, 2, "Sustain Strength ring", "Your strength cannot be drained.",      "wear",  '=', COL_GREEN       },
    {ITEM_RING_SEARCHING,      1, 2, "Searching ring",        "Auto-searches each turn.",              "wear",  '=', COL_YELLOW      },
    {ITEM_RING_SEE_INVIS,      1, 2, "See Invisible ring",    "Reveals invisible creatures.",          "wear",  '=', COL_CYAN        },
    {ITEM_RING_ADORNMENT,      0, 2, "Adornment ring",        "A pretty ring. No effect.",             "wear",  '=', COL_MAGENTA     },
    {ITEM_RING_AGGRAVATE,      1, 2, "Aggravate ring",        "Angers monsters. Probably cursed.",     "wear",  '=', COL_RED         },
    {ITEM_RING_DEXTERITY,      1, 2, "Dexterity ring",        "Adds +1 to your defense.",              "wear",  '=', COL_CYAN        },
    {ITEM_RING_INCREASE_DMG,   1, 2, "Increase Damage ring",  "Adds +1 to weapon damage.",             "wear",  '=', COL_ORANGE      },
    {ITEM_RING_REGENERATION,   1, 2, "Regeneration ring",     "Slowly regenerates your HP.",           "wear",  '=', COL_GREEN       },
    {ITEM_RING_SLOW_DIGEST,    1, 2, "Slow Digestion ring",   "Hunger decreases more slowly.",         "wear",  '=', COL_BROWN       },
    {ITEM_RING_TELEPORTATION,  1, 2, "Teleportation ring",    "Randomly teleports you. Cursed?",       "wear",  '=', COL_MAGENTA     },
    {ITEM_RING_STEALTH,        1, 2, "Stealth ring",          "Monsters notice you less easily.",      "wear",  '=', COL_DARK_GREY   },
    {ITEM_RING_MAINT_ARMOR,    1, 2, "Maintain Armor ring",   "Armor resists corrosion.",              "wear",  '=', COL_GREY        },
    {ITEM_WAND_LIGHT,          5, 8, "Wand of Light",         "Illuminates a large area. 5 chg.",      "zap",   '|', COL_YELLOW      },
    {ITEM_WAND_INVIS,          5, 8, "Wand of Invisibility",  "Makes target invisible. 5 chg.",        "zap",   '|', COL_DARK_GREY   },
    {ITEM_WAND_LIGHTNING,      5, 8, "Wand of Lightning",     "Fires a lightning bolt. 5 chg.",        "zap",   '|', COL_CYAN        },
    {ITEM_WAND_FIRE,           5, 8, "Wand of Fire",          "Fires a bolt of fire. 5 chg.",          "zap",   '|', COL_ORANGE      },
    {ITEM_WAND_COLD,           5, 8, "Wand of Cold",          "Fires a bolt of cold. 5 chg.",          "zap",   '|', COL_BLUE        },
    {ITEM_WAND_POLYMORPH,      4, 8, "Wand of Polymorph",     "Transforms a monster. 4 chg.",          "zap",   '|', COL_MAGENTA     },
    {ITEM_WAND_MAGIC_MISSILE,  6, 8, "Wand of Magic Missile", "4d4 magic bolt. 6 chg.",                "zap",   '|', COL_WHITE       },
    {ITEM_WAND_HASTE_MONSTER,  3, 8, "Wand: Haste Monster",   "Speeds up a monster. 3 chg.",           "zap",   '|', COL_RED         },
    {ITEM_WAND_SLOW_MONSTER,   5, 8, "Wand: Slow Monster",    "Slows a monster. 5 chg.",               "zap",   '|', COL_BLUE        },
    {ITEM_WAND_DRAIN_LIFE,     5, 8, "Wand of Drain Life",    "Halves a monster's HP. 5 chg.",         "zap",   '|', COL_DARK_GREEN  },
    {ITEM_WAND_NOTHING,        3, 8, "Wand of Nothing",       "Does absolutely nothing. 3 chg.",       "zap",   '|', COL_DARK_GREY   },
    {ITEM_WAND_TELEPORT_AWAY,  5, 8, "Wand: Teleport Away",   "Teleports target away. 5 chg.",         "zap",   '|', COL_MAGENTA     },
    {ITEM_WAND_TELEPORT_TO,    5, 8, "Wand: Teleport To",     "Pulls target toward you. 5 chg.",       "zap",   '|', COL_MAGENTA     },
    {ITEM_WAND_CANCELLATION,   4, 8, "Wand of Cancellation",  "Cancels monster abilities. 4 chg.",     "zap",   '|', COL_GREY        },
    {ITEM_AMULET_YENDOR, 0, 2, "Amulet of Yendor", "The legendary amulet. Escape!", "take", '"', COL_GOLD},
    {ITEM_FOOD, 400, 3, "rations", "Trail rations. Fills your belly.", "eat", '%', COL_BROWN},
    {ITEM_GOLD, 0, 0, "gold pieces", "Shiny coins.", "-", '$', COL_GOLD},
};
#define N_ITEM_TMPL ((int)(sizeof(item_tmpl) / sizeof(item_tmpl[0])))

static void recompute_ring_bonuses(EntityState* es)
{
    es->ring_def_bonus = 0;
    es->ring_atk_bonus = 0;
    es->ring_see_invis = 0;
    es->ring_slow_digest = 0;
    es->ring_regen = 0;
    es->ring_stealth = 0;
    es->ring_sustain_str = 0;
    es->ring_maint_armor = 0;
    es->ring_teleport = 0;

    int slots[2] = {es->ring_left_slot, es->ring_right_slot};

    for (int s = 0; s < 2; s++)
    {
        int idx = slots[s];
        if (idx < 0 || idx >= es->inv_count)
        {
            continue;
        }
        switch (es->inv[idx].kind)
        {
        case ITEM_RING_PROTECTION: es->ring_def_bonus++;
            break;
        case ITEM_RING_DEXTERITY: es->ring_def_bonus++;
            break;
        case ITEM_RING_ADD_STR: es->ring_atk_bonus++;
            break;
        case ITEM_RING_INCREASE_DMG: es->ring_atk_bonus++;
            break;
        case ITEM_RING_SEE_INVIS: es->ring_see_invis = 1;
            break;
        case ITEM_RING_SLOW_DIGEST: es->ring_slow_digest = 1;
            break;
        case ITEM_RING_REGENERATION: es->ring_regen = 1;
            break;
        case ITEM_RING_STEALTH: es->ring_stealth = 1;
            break;
        case ITEM_RING_SUSTAIN_STR: es->ring_sustain_str = 1;
            break;
        case ITEM_RING_MAINT_ARMOR: es->ring_maint_armor = 1;
            break;
        case ITEM_RING_TELEPORTATION: es->ring_teleport = 1;
            break;
        default: break;
        }
    }
}

void entity_state_init(EntityState* es)
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
    es->player_str = 16;
    es->player_str_max = 16;
    es->gold = 0;
    es->weapon_slot = -1;
    es->armor_slot = -1;
    es->ring_left_slot = -1;
    es->ring_right_slot = -1;
    es->carry_weight = 0;
    es->max_carry = 120;
    es->hunger = 1000;
    es->hunger_max = 1000;
}

void entity_log(EntityState* es, const char* fmt, ...)
{
    va_list ap;

    if (es->log_count < MAX_LOG)
    {
        va_start(ap, fmt);
        vsnprintf(es->log[es->log_count++], 80, fmt, ap);
        va_end(ap);
    }
    else
    {
        memmove(es->log[0], es->log[1], sizeof(es->log[0]) * (MAX_LOG - 1));
        va_start(ap, fmt);
        vsnprintf(es->log[MAX_LOG - 1], 80, fmt, ap);
        va_end(ap);
    }
}

Entity* entity_at(EntityState* es, int x, int y)
{
    if (es->player.alive && es->player.x == x && es->player.y == y)
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

Item* item_at(EntityState* es, int x, int y)
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

void mob_spawn(EntityState* es, MobKind kind, int x, int y)
{
    if (es->mob_count >= MAX_ENTITIES)
    {
        return;
    }

    for (int i = 0; i < N_MOB_TMPL; i++)
    {
        if (mob_tmpl[i].kind != kind)
        {
            continue;
        }

        Entity* m = &es->mobs[es->mob_count++];
        m->kind = kind;
        m->x = x;
        m->y = y;
        m->hp = roll_dice(mob_tmpl[i].hp_n, mob_tmpl[i].hp_d);
        m->hp_max = m->hp;
        m->atk = mob_tmpl[i].atk;
        m->def = mob_tmpl[i].def;
        m->xp_reward = mob_tmpl[i].xp;
        m->level = mob_tmpl[i].level;
        m->flags = mob_tmpl[i].flags;
        m->satt = mob_tmpl[i].satt;
        m->held = 0;
        strcpy(m->name, mob_tmpl[i].name);
        m->ch = mob_tmpl[i].ch;
        m->col = mob_tmpl[i].col;
        m->alive = 1;
        return;
    }
}

void item_spawn(EntityState* es, ItemKind kind, int x, int y)
{
    if (es->item_count >= MAX_ENTITIES)
    {
        return;
    }

    for (int i = 0; i < N_ITEM_TMPL; i++)
    {
        if (item_tmpl[i].kind != kind)
        {
            continue;
        }

        Item* it = &es->items[es->item_count++];
        it->kind = kind;
        it->x = x;
        it->y = y;
        it->value = item_tmpl[i].value;
        it->weight = item_tmpl[i].weight;
        it->enchant = 0;
        it->cursed = 0;
        strcpy(it->name, item_tmpl[i].name);
        strcpy(it->desc, item_tmpl[i].desc);
        strcpy(it->action, item_tmpl[i].action);
        it->ch = item_tmpl[i].ch;
        it->col = item_tmpl[i].col;
        return;
    }
}

void item_spawn_gold(EntityState* es, int amount, int x, int y)
{
    if (es->item_count >= MAX_ENTITIES)
    {
        return;
    }

    Item* it = &es->items[es->item_count++];
    it->kind = ITEM_GOLD;
    it->x = x;
    it->y = y;
    it->value = amount;
    it->weight = 0;
    it->enchant = 0;
    it->cursed = 0;
    strcpy(it->name, "gold pieces");
    strcpy(it->desc, "Shiny coins.");
    strcpy(it->action, "take");
    it->ch = '$';
    it->col = COL_GOLD;
}

void entity_grant_xp(EntityState* es, int xp)
{
    es->player_xp += xp;

    while (es->player_xp >= es->xp_to_next)
    {
        es->player_level++;
        es->xp_to_next = es->xp_to_next * 3 / 2;
        es->player.hp_max += 6;
        es->player.hp = es->player.hp_max;
        es->player.atk++;
        entity_log(es, "Welcome to level %d!", es->player_level);
    }
}

static int rng_combat(int range)
{
    erng_state ^= erng_state << 13;
    erng_state ^= erng_state >> 17;
    erng_state ^= erng_state << 5;
    return (int)(erng_state % (unsigned)range);
}

int entity_attack(EntityState* es, Entity* attacker, Entity* target)
{
    int base = attacker->atk;
    if (attacker == &es->player)
    {
        base += es->weapon_bonus + es->ring_atk_bonus;
    }

    int def = target->def;
    if (target == &es->player)
    {
        def += es->armor_bonus + es->ring_def_bonus;
    }

    int dmg = base - def + rng_combat(5) - 2;
    if (dmg < 1)
    {
        dmg = 1;
    }

    target->hp -= dmg;

    if (attacker != &es->player && target == &es->player && target->hp > 0)
    {
        switch (attacker->satt)
        {
        case SATT_RUST:
            if (es->armor_slot >= 0 && !es->ring_maint_armor)
            {
                es->armor_bonus--;
                if (es->armor_bonus < 0)
                {
                    es->armor_bonus = 0;
                }
                entity_log(es, "The aquator rusts your armor!");
            }
            break;

        case SATT_HOLD:
            if (es->held_turns == 0)
            {
                es->held_turns = erng_range(3, 7);
                entity_log(es, "The flytrap holds you fast!");
            }
            break;

        case SATT_FREEZE:
            if (es->frozen_turns == 0)
            {
                es->frozen_turns = erng_range(2, 5);
                entity_log(es, "You are frozen solid!");
            }
            break;

        case SATT_STEAL_GOLD:
            if (es->gold > 0)
            {
                int stolen = es->gold / 2 + 1;
                es->gold -= stolen;
                if (es->gold < 0)
                {
                    es->gold = 0;
                }
                entity_log(es, "The leprechaun steals %d gold!", stolen);
            }
            break;

        case SATT_STEAL_ITEM:
            if (es->inv_count > 0)
            {
                int slot = erng_range(0, es->inv_count - 1);
                if (es->weapon_slot != slot && es->armor_slot != slot && es->ring_left_slot != slot && es->
                    ring_right_slot != slot)
                {
                    entity_log(es, "The nymph steals your %s!", es->inv[slot].name);
                    memmove(&es->inv[slot], &es->inv[slot + 1], sizeof(Item) * (es->inv_count - slot - 1));
                    es->inv_count--;
                }
            }
            break;

        case SATT_POISON:
            if (!es->ring_sustain_str && es->poisoned == 0)
            {
                es->poisoned = 5;
                es->poison_timer = 0;
                entity_log(es, "The rattlesnake poisons you!");
            }
            break;

        case SATT_DRAIN_LVL:
            if (es->player_level > 1)
            {
                es->player_level--;
                es->player.hp_max -= 6;
                if (es->player.hp > es->player.hp_max)
                {
                    es->player.hp = es->player.hp_max;
                }
                entity_log(es, "The vampire drains your life force!");
            }
            break;

        default:
            break;
        }
    }

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

static int mob_can_see_player(const Dungeon* d, const Entity* mob, const Entity* player, int stealth)
{
    int dx = player->x - mob->x;
    int dy = player->y - mob->y;
    /* stealth ring halves detection range (100 -> 36 tiles squared) */
    int range = stealth ? 36 : 100;
    if (dx * dx + dy * dy > range)
    {
        return 0;
    }
    return d->visible[mob->y][mob->x];
}

static int sign(int v)
{
    return v > 0 ? 1 : v < 0 ? -1 : 0;
}

void mobs_regen(EntityState* es)
{
    for (int i = 0; i < es->mob_count; i++)
    {
        Entity* m = &es->mobs[i];
        if (!m->alive)
        {
            continue;
        }
        if ((m->flags & MOB_REGEN) && m->hp < m->hp_max)
        {
            m->hp++;
        }
    }
}

void entity_tick_status(EntityState* es)
{
    if (es->confused_turns > 0)
    {
        es->confused_turns--;
    }

    if (es->blind_turns > 0)
    {
        es->blind_turns--;
    }

    if (es->halluc_turns > 0)
    {
        es->halluc_turns--;
    }

    if (es->hasted_turns > 0)
    {
        es->hasted_turns--;
    }

    if (es->held_turns > 0)
    {
        es->held_turns--;
    }

    if (es->frozen_turns > 0)
    {
        es->frozen_turns--;
    }

    if (es->levitate_turns > 0)
    {
        es->levitate_turns--;
    }

    if (es->poisoned)
    {
        es->poison_timer++;
        if (es->poison_timer >= 10)
        {
            es->poison_timer = 0;
            es->poisoned--;
            if (!es->ring_sustain_str && es->player_str > 1)
            {
                es->player_str--;
                es->player.atk--;
                if (es->player.atk < 1)
                {
                    es->player.atk = 1;
                }
                entity_log(es, "The poison weakens you. STR %d", es->player_str);
            }
            if (es->poisoned == 0)
            {
                entity_log(es, "The poison has worn off.");
            }
        }
    }

    if (es->ring_regen && es->player.hp < es->player.hp_max)
    {
        es->player.hp++;
    }

    if (es->ring_teleport && (erng() % 100) < 2)
    {
        es->player.x = erng_range(1, MAP_W - 2);
        es->player.y = erng_range(1, MAP_H - 2);
        entity_log(es, "Your ring teleports you!");
    }
}

void entity_recompute_rings(EntityState* es)
{
    recompute_ring_bonuses(es);
}

void mobs_take_turn(EntityState* es, const Dungeon* d)
{
    mobs_regen(es);
    entity_tick_status(es);

    for (int i = 0; i < es->mob_count; i++)
    {
        Entity* m = &es->mobs[i];
        if (!m->alive)
        {
            continue;
        }

        if (m->held > 0)
        {
            m->held--;
            continue;
        }

        if (!mob_can_see_player(d, m, &es->player, es->ring_stealth) && !(m->flags & MOB_MEAN))
        {
            continue;
        }

        if (m->flags & MOB_MEAN)
        {
            int dx = es->player.x - m->x;
            int dy = es->player.y - m->y;
            if (dx * dx + dy * dy > 225)
            {
                continue;
            }
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

        int tx = m->x + mx;
        int ty = m->y + my;

        if (tx == es->player.x && ty == es->player.y)
        {
            int dmg = entity_attack(es, m, &es->player);
            entity_log(es, "The %s hits you for %d!", m->name, dmg);
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
