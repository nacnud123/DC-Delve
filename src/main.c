#include "console.h"
#include "game.h"
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/sound/sound.h>
#include <kos.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

int main(void)
{
    pvr_init_defaults();
    snd_init();
    console_init();

    GameState game;
    game_init(&game);

    uint32_t prev_buttons = 0;
    uint8_t prev_rtrig = 0;
    int move_repeat = 0;
    uint32_t held_dir = 0;
    int running = 1;

    while (running)
    {
        maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);

        uint32_t b = 0;
        uint8_t rtrig = 0;

        if (dev)
        {
            cont_state_t *st = (cont_state_t *)maple_dev_status(dev);
            if (st)
            {
                b = st->buttons;
                rtrig = (uint8_t)st->rtrig;
            }
        }

        uint32_t pressed = b & ~prev_buttons;
        int r_pressed = (rtrig > 64) && !(prev_rtrig > 64);
        prev_buttons = b;
        prev_rtrig = rtrig;


        uint32_t dir = b & (CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT);
        if (dir != held_dir)
        {
            held_dir = dir;
            move_repeat = 0;
        }
        else if (held_dir)
        {
            move_repeat++;
        }
        int do_dir = dir && (move_repeat == 0 || move_repeat > 12);

        if (game.screen == SCREEN_PLAY)
        {
            if (do_dir)
            {
                int dx = 0, dy = 0;
                if (b & CONT_DPAD_LEFT)
                {
                    dx = -1;
                }

                if (b & CONT_DPAD_RIGHT)
                {
                    dx = 1;
                }

                if (b & CONT_DPAD_UP)
                {
                    dy = -1;
                }

                if (b & CONT_DPAD_DOWN)
                {
                    dy = 1;
                }

                int acted = game_player_move(&game, dx, dy);
                if (acted && game.screen == SCREEN_PLAY)
                {
                    mobs_take_turn(&game.es, &game.dungeon);
                    if (game.es.player.hp <= 0)
                    {
                        game.es.player.alive = 0;
                        game.screen = SCREEN_DEAD;
                    }
                }
            }
            if (pressed & CONT_A)
            {
                game_player_pickup(&game);
            }
            if (pressed & CONT_B)
            {
                game_open_inventory(&game);
            }
            if (r_pressed)
            {
                game_player_use_stairs(&game);
            }
        }
        else if (game.screen == SCREEN_INVENTORY)
        {
            if (pressed & CONT_DPAD_UP)
            {
                game_inventory_cursor(&game, -1);
            }

            if (pressed & CONT_DPAD_DOWN)
            {
                game_inventory_cursor(&game, 1);
            }

            if (pressed & CONT_A)
            {
                game_inventory_use(&game);
            }

            if (pressed & CONT_X)
            {
                game_inventory_drop(&game);
            }

            if (pressed & CONT_B)
            {
                game_inventory_close(&game);
            }
        }
        else if (game.screen == SCREEN_TARGET)
        {
            if (do_dir)
            {
                int dx = 0, dy = 0;
                if (b & CONT_DPAD_LEFT)
                {
                    dx = -1;
                }

                if (b & CONT_DPAD_RIGHT)
                {
                    dx = 1;
                }

                if (b & CONT_DPAD_UP)
                {
                    dy = -1;
                }

                if (b & CONT_DPAD_DOWN)
                {
                    dy = 1;
                }
                game_target_move(&game, dx, dy);
            }
            if (pressed & CONT_A)
            {
                game_target_confirm(&game);
            }

            if (pressed & CONT_B)
            {
                game_target_cancel(&game);
            }
        }

        if (pressed & CONT_START)
        {
            if (game.screen == SCREEN_DEAD || game.screen == SCREEN_VICTORY)
            {
                game_init(&game);
            }
            else
            {
                running = 0;
            }
        }

        game_render(&game);
    }

    console_shutdown();
    pvr_shutdown();
    return 0;
}
