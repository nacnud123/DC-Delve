#pragma once
#include "console.h"
#include "dungeon.h"
#include "entity.h"
#include "fov.h"

typedef enum
{
    SCREEN_TITLE,
    SCREEN_PLAY,
    SCREEN_INVENTORY,
    SCREEN_DEAD,
    SCREEN_VICTORY,
    SCREEN_TARGET, /* free-aim targeting mode */
    SCREEN_PAUSE,
    SCREEN_CONTROLS,
} GameScreen;

typedef struct
{
    Dungeon dungeon;
    EntityState es;
    GameScreen screen;
    GameScreen prev_screen; /* screen to return to from pause/controls */
    int depth;
    unsigned int world_seed;
    int inv_cursor;
    int target_x, target_y;
    int target_item_slot;
    int pause_cursor; /* 0=Resume 1=Controls 2=Quit */
    unsigned int frame; /* frame counter for animations */
} GameState;

void game_init(GameState* g, unsigned int entropy);
void game_start(GameState* g);
void game_new_level(GameState* g);
void game_render(const GameState* g);

int game_player_move(GameState* g, int dx, int dy);
void game_player_use_stairs(GameState* g);
void game_player_pickup(GameState* g);

void game_open_inventory(GameState* g);
void game_inventory_cursor(GameState* g, int dir);
void game_inventory_use(GameState* g);
void game_inventory_close(GameState* g);
void game_inventory_drop(GameState* g);

void game_target_move(GameState* g, int dx, int dy);
void game_target_confirm(GameState* g);
void game_target_cancel(GameState* g);

void game_open_pause(GameState* g);
void game_pause_cursor(GameState* g, int dir);
void game_pause_confirm(GameState* g);
void game_pause_back(GameState* g);

#define FOV_RADIUS 8
#define GAME_VERSION "v0.2"
