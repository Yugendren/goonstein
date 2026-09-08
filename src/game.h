// Game state and the three entry points the main loop calls.
#pragma once

#include "platform.h"

typedef struct Game {
    double   time;       // simulated seconds
    unsigned tick;       // simulated ticks
    float    px, py;     // placeholder player position, until we have a world
} Game;

void game_init(Game *g);
void game_tick(Game *g, const Input *in, double dt);
void game_render(Game *g, Platform *pf, float alpha);
void game_shutdown(Game *g);
