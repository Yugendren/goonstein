#include "game.h"
#include <string.h>

void game_init(Game *g) {
    memset(g, 0, sizeof *g);
}

void game_tick(Game *g, const Input *in, double dt) {
    g->time += dt;
    g->tick++;
    g->px += in->move_x * 3.0f * (float)dt;
    g->py += in->move_y * 3.0f * (float)dt;
}

void game_render(Game *g, Platform *pf, float alpha) {
    (void)alpha;
    if (!pf->cmd || !pf->swapchain) return;

    // Placeholder: clear to a slow-breathing dark colour so we can see the loop is alive.
    float breathe = 0.5f + 0.5f * SDL_sinf((float)g->time * 0.8f);
    SDL_GPUColorTargetInfo target = {
        .texture = pf->swapchain,
        .clear_color = { 0.02f + 0.03f * breathe, 0.02f, 0.03f + 0.02f * breathe, 1.0f },
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(pf->cmd, &target, 1, NULL);
    SDL_EndGPURenderPass(pass);
}

void game_shutdown(Game *g) {
    (void)g;
}
