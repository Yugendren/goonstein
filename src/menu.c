// The front door. See menu.h for what it is; this is how it behaves.
//
// Three rules run through the whole file:
//   * the menu never simulates anything. It reads the input, tells netgame to host, join or stop,
//     and draws. The world keeps ticking behind it, because a host that stops answering its
//     clients for the length of an Esc menu has dropped them.
//   * whatever is under the cursor is what a click hits, and whatever row is highlighted is what
//     Enter or the A button takes. Keyboard, mouse and pad all drive the same two variables.
//   * while a text field owns the keyboard, nothing else in the game sees a key: platform.c stops
//     handing them out (platform_text_input), so typing "wasd" into NAME cannot walk the player.
#include "game.h"
#include "menu.h"
#include "debug.h"
#include "net_sys.h"   // netsys_local_ips: the addresses a friend can type in
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MW ((float)INTERNAL_W)
#define MH ((float)INTERNAL_H)
#define MENU_PORT_DEFAULT 7777
#define MENU_JOIN_TIMEOUT 5.0    // seconds without an answer before the join gives up

// Main menu rows, and the Esc menu's.
enum { ROW_SOLO, ROW_HOST, ROW_JOIN, ROW_NAME, ROW_QUIT, ROW_MAIN_N };
enum { ROW_RESUME, ROW_INVITE, ROW_LEAVE, ROW_PAUSE_QUIT, ROW_PAUSE_N };

static const Vec4 C_TEXT = { 0.92f, 0.90f, 0.86f, 1 };
static const Vec4 C_DIM  = { 0.58f, 0.56f, 0.52f, 1 };
static const Vec4 C_HOT  = { 1.00f, 0.80f, 0.38f, 1 };
static const Vec4 C_BAD  = { 1.00f, 0.55f, 0.42f, 1 };

// ---------------------------------------------------------------- small pieces

// The one line under the rows. note() is bad news (red), note_good() is not (amber).
static void vnote(Menu *m, bool good, const char *fmt, va_list ap) {
    vsnprintf(m->note, sizeof m->note, fmt, ap);
    m->note_t = 8.0f; m->note_good = good;
}
static void note(Menu *m, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vnote(m, false, fmt, ap); va_end(ap);
}
static void note_good(Menu *m, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vnote(m, true, fmt, ap); va_end(ap);
}

static void field_set(MenuField *f, const char *s) {
    snprintf(f->buf, sizeof f->buf, "%s", s ? s : "");
    f->len = (int)strlen(f->buf);
}

// One tick of typing: the characters SDL handed us, then backspace (which repeats).
static void field_type(MenuField *f, const Input *in) {
    for (int i = 0; i < in->ntext && f->len < (int)sizeof f->buf - 1; i++) {
        char c = in->text[i];
        if (c >= 32 && c < 127) f->buf[f->len++] = c;
    }
    f->buf[f->len] = 0;
    if (in->key_down[SDL_SCANCODE_BACKSPACE] && f->len > 0) f->buf[--f->len] = 0;
}

// settings.txt, the same file main.c reads at startup: one `key value` line.
static bool settings_get(const char *key, char *out, size_t n) {
    char sp[640]; snprintf(sp, sizeof sp, "%s/settings.txt", HOLLOW_ASSET_DIR);
    size_t sz = 0; char *st = SDL_LoadFile(sp, &sz); if (!st) return false;
    bool found = false; char *cur = st;
    while (*cur && !found) {
        char *line = cur, *nl = strchr(cur, '\n');
        if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char k[32], v[128];
        if (sscanf(line, "%31s %127s", k, v) == 2 && !strcmp(k, key)) { snprintf(out, n, "%s", v); found = true; }
    }
    SDL_free(st);
    return found;
}

// ---------------------------------------------------------------- pages

static void page(Game *g, MenuPage p) {
    Menu *m = &g->menu;
    m->page = p; m->row = 0; m->t = 0;
    m->name.active = false; m->addr.active = false;
    if (p == MENU_JOIN) m->addr.active = true;   // the join page is one field: start typing straight away
}

// Everything a fresh single-player, host or client session needs before the world is handed back.
static void start_playing(Game *g, const char *how) {
    Menu *m = &g->menu;
    m->page = MENU_OFF; m->row = 0; m->t = 0;
    m->name.active = m->addr.active = false;
    platform_text_input(g->pf, false);
    if (g->state == GS_MENU) { g->state = GS_EXPLORE; g->state_t = 0; game_snap_camera(g); }
    SDL_Log("menu: playing (%s)", how);
    dbg_log("menu: playing (%s)", how);
}

// Put the net game back to single player: sockets closed, the other three seats emptied, this
// process back in slot 0. Used by LEAVE, END HOST, a failed join and a host that said goodbye.
static void net_off(Game *g) {
    Menu *m = &g->menu;
    netgame_shutdown(g);
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        charmodel_destroy(&g->gfx, &g->player_models[i]);
        memset(&g->players[i], 0, sizeof g->players[i]);
        g->slot_tinted[i] = false;
    }
    memset(&g->net, 0, sizeof g->net);
    g->net.mode = NM_OFF; g->net.slots_max = NET_MAX_PLAYERS; g->net.local = 0; g->local = 0;
    g->net.slots[0].active = true; g->net.slots[0].tint = v4(1, 1, 1, 1);
    snprintf(g->net.name, sizeof g->net.name, "%s", m->name.buf);
    game_ensure_player_model(g, 0);
    game_spawn_player(g, 0);
    game_give_loadout(g, 0);
    game_snap_camera(g);
    m->nips = 0; m->joined = false;
}

static bool host_start(Game *g) {
    Menu *m = &g->menu;
    NetGame *n = &g->net;
    memset(n, 0, sizeof *n);
    n->mode = NM_HOST; n->port = m->port; n->slots_max = NET_MAX_PLAYERS; n->local = 0;
    snprintf(n->name, sizeof n->name, "%s", m->name.buf[0] ? m->name.buf : "host");
    if (!netgame_start(g)) { note(m, "could not listen on port %u: is another game already hosting?", m->port); net_off(g); return false; }
    g->no_scenes = true;   // multiplayer skips cutscenes, exactly as --host does
    m->nips = netsys_local_ips(m->ips, MENU_IPS);
    return true;
}

static void join_start(Game *g, const char *addr) {
    Menu *m = &g->menu;
    NetGame *n = &g->net;
    memset(n, 0, sizeof *n);
    n->mode = NM_CLIENT; n->slots_max = NET_MAX_PLAYERS; n->local = 0;
    snprintf(n->join_target, sizeof n->join_target, "%s", addr);
    snprintf(n->name, sizeof n->name, "%s", m->name.buf[0] ? m->name.buf : "player");
    if (!netgame_start(g)) { note(m, "cannot reach %s: check the address", addr); net_off(g); page(g, MENU_MAIN); m->row = ROW_JOIN; m->script[0] = 0; return; }
    g->no_scenes = true;
    page(g, MENU_CONNECTING);
    m->connect_t = 0;
}

// The address a joiner has to be given: every LAN address of this machine, with the port.
static void host_addr(const Menu *m, int i, char *out, size_t n) {
    snprintf(out, n, "%s:%u", i < m->nips ? m->ips[i] : "127.0.0.1", m->port);
}

// ---------------------------------------------------------------- input

// Gamepad edges the platform layer does not forward (it maps the pad to combat, not to menus).
enum { PAD_UP, PAD_DOWN, PAD_A, PAD_B };
static bool pad_edge(Game *g, int which) {
    static const SDL_GamepadButton BTN[4] = { SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                                              SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST };
    Menu *m = &g->menu;
    bool now = g->pf->gamepad && SDL_GetGamepadButton(g->pf->gamepad, BTN[which]);
    bool edge = now && !m->pad_prev[which];
    m->pad_prev[which] = now;
    return edge;
}

typedef struct MenuKeys { bool up, down, ok, back; } MenuKeys;

static MenuKeys menu_keys(Game *g, const Input *in, float dt) {
    Menu *m = &g->menu;
    MenuKeys k = { 0 };
    k.up   = in->key_down[SDL_SCANCODE_UP] || pad_edge(g, PAD_UP);
    k.down = in->key_down[SDL_SCANCODE_DOWN] || pad_edge(g, PAD_DOWN);
    k.ok   = in->key_down[SDL_SCANCODE_RETURN] || in->key_down[SDL_SCANCODE_KP_ENTER] || pad_edge(g, PAD_A);
    k.back = in->key_down[SDL_SCANCODE_ESCAPE] || pad_edge(g, PAD_B);
    // W/S and the left stick move one row per flick, not one per tick.
    m->stick_cool = fmaxf(0, m->stick_cool - dt);
    if (!m->name.active && !m->addr.active) {
        float v = in->move_y;
        if (in->key_down[SDL_SCANCODE_W]) v = -1;
        if (in->key_down[SDL_SCANCODE_S]) v = 1;
        if (fabsf(v) > 0.6f && m->stick_cool <= 0) { m->stick_cool = 0.22f; if (v < 0) k.up = true; else k.down = true; }
        if (fabsf(v) < 0.3f) m->stick_cool = 0;
        if (in->interact) k.ok = true;   // E, and the pad's A through the normal path
    }
    return k;
}

// Rows are laid out by the drawing code as well; this is the one place that says where they are.
static float row_y(int i)  { return 336.0f + (float)i * 58.0f; }
static float row_top(int i){ return row_y(i) - 8.0f; }
#define ROW_H 50.0f
#define ROW_W 560.0f

// Mouse: hovering a row highlights it, clicking it takes it. A cursor that is not moving does not
// steal the highlight from the keyboard - which also keeps the scripted menu test honest.
static bool mouse_rows(Game *g, const Input *in, int n, float y0, bool *clicked) {
    Menu *m = &g->menu;
    float mx, my; platform_mouse_ui(g->pf, INTERNAL_W, INTERNAL_H, &mx, &my);
    bool moved = fabsf(mx - m->mx_prev) + fabsf(my - m->my_prev) > 1.0f;
    m->mx_prev = mx; m->my_prev = my;
    *clicked = false;
    for (int i = 0; i < n; i++) {
        float y = y0 + (float)i * 58.0f - 8.0f;
        if (mx < MW * 0.5f - ROW_W * 0.5f || mx > MW * 0.5f + ROW_W * 0.5f || my < y || my > y + ROW_H) continue;
        if (moved) m->row = i;
        if (in->click) { m->row = i; *clicked = true; }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- the pages, one tick each

static void tick_main(Game *g, const Input *in, MenuKeys k) {
    Menu *m = &g->menu;
    if (m->name.active) {   // NAME is edited in place: the row stays where it is
        field_type(&m->name, in);
        if (k.ok || k.back) {
            m->name.active = false;
            if (!m->name.buf[0]) field_set(&m->name, "goon");
            game_settings_set(g, "name", m->name.buf);
            snprintf(g->net.name, sizeof g->net.name, "%s", m->name.buf);
            note_good(m, "you are %s", m->name.buf);
        }
        return;
    }
    bool clicked = false;
    mouse_rows(g, in, ROW_MAIN_N, row_y(0), &clicked);
    if (k.up)   m->row = (m->row + ROW_MAIN_N - 1) % ROW_MAIN_N;
    if (k.down) m->row = (m->row + 1) % ROW_MAIN_N;
    if (!k.ok && !clicked) return;
    switch (m->row) {
    case ROW_SOLO: start_playing(g, "solo"); break;
    case ROW_HOST: if (host_start(g)) start_playing(g, "hosting"); break;
    case ROW_JOIN: page(g, MENU_JOIN); break;
    case ROW_NAME: m->name.active = true; break;
    case ROW_QUIT: g->pf->want_quit = true; break;
    default: break;
    }
}

static void tick_join(Game *g, const Input *in, MenuKeys k) {
    Menu *m = &g->menu;
    field_type(&m->addr, in);
    if (k.back) { page(g, MENU_MAIN); m->row = ROW_JOIN; return; }
    bool clicked = false; float mx, my; platform_mouse_ui(g->pf, INTERNAL_W, INTERNAL_H, &mx, &my);
    if (in->click && my > 430 && my < 490 && mx > MW * 0.5f - 120 && mx < MW * 0.5f + 120) clicked = true;   // the CONNECT button
    if (!k.ok && !clicked) return;
    if (!m->addr.buf[0]) { note(m, "type the host's address, like 192.168.1.20:7777"); return; }
    if (!strchr(m->addr.buf, ':')) {   // a bare address means the usual port
        char with_port[MENU_FIELD_MAX + 8]; snprintf(with_port, sizeof with_port, "%s:%u", m->addr.buf, m->port);
        field_set(&m->addr, with_port);
    }
    join_start(g, m->addr.buf);
}

static void tick_connecting(Game *g, MenuKeys k, float dt) {
    Menu *m = &g->menu;
    m->connect_t += dt;
    if (g->net.connected) {   // an address that worked is the one the join page offers next time
        game_settings_set(g, "last_join", g->net.join_target);
        m->note_t = 0; start_playing(g, "joined"); return;
    }
    if (k.back) { note(m, "join cancelled"); net_off(g); page(g, MENU_MAIN); m->row = ROW_JOIN; return; }
    if (g->net.rejected) { char t[128]; snprintf(t, sizeof t, "%s", g->net.join_target); net_off(g); page(g, MENU_MAIN); m->row = ROW_JOIN; m->script[0] = 0; note(m, "%s is full: four players already", t); return; }
    if (m->connect_t > MENU_JOIN_TIMEOUT) {
        char t[128]; snprintf(t, sizeof t, "%s", g->net.join_target);
        net_off(g); page(g, MENU_MAIN); m->row = ROW_JOIN; m->script[0] = 0;   // a scripted test has its answer
        note(m, "no answer from %s after %.0f s", t, MENU_JOIN_TIMEOUT);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "menu: no answer from %s", t);
    }
}

static void tick_pause(Game *g, const Input *in, MenuKeys k) {
    Menu *m = &g->menu;
    bool clicked = false;
    mouse_rows(g, in, ROW_PAUSE_N, row_y(0), &clicked);
    if (k.up)   m->row = (m->row + ROW_PAUSE_N - 1) % ROW_PAUSE_N;
    if (k.down) m->row = (m->row + 1) % ROW_PAUSE_N;
    if (k.back) { m->page = MENU_OFF; return; }
    if (!k.ok && !clicked) return;
    switch (m->row) {
    case ROW_RESUME: m->page = MENU_OFF; break;
    case ROW_INVITE: page(g, MENU_INVITE); break;
    case ROW_LEAVE:
        if (g->net.mode != NM_OFF) net_off(g);
        g->state = GS_MENU; g->state_t = 0;
        page(g, MENU_MAIN);
        break;
    case ROW_PAUSE_QUIT: g->pf->want_quit = true; break;
    default: break;
    }
}

// ---------------------------------------------------------------- scripted menu test

// --menu-test host[:PORT] | --menu-test join:HOST:PORT (or HOLLOW_MENU_TEST=...). Drives the rows
// the way a player would, so the headless check exercises the real path and not a private shortcut.
static void test_tick(Game *g, float dt) {
    Menu *m = &g->menu;
    if (!m->script[0]) return;
    if (m->page == MENU_CONNECTING) return;   // waiting for the host to answer is the join page's business
    m->step_t += dt;
    if (m->step_t < 0.35f) return;
    m->step_t = 0;
    if (++m->step > 10) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "menu-test: gave up after %d steps", m->step); m->script[0] = 0; return; }
    // "host[:PORT]", "join:HOST:PORT", "solo", "pause" (solo, then the Esc menu) and
    // "invite[:PORT]" (host, then INVITE INFO): one script per page a screenshot wants.
    if (!strncmp(m->script, "name:", 5)) {   // "name:NAME": edit the field and keep it
        const char *v = m->script + 5;
        if (m->page != MENU_MAIN) return;
        if (!m->name.active) { m->row = ROW_NAME; m->name.active = true; SDL_Log("menu-test: NAME"); return; }
        if (strcmp(m->name.buf, v) != 0) {
            Input typed; memset(&typed, 0, sizeof typed);   // one tick's worth of typing, as SDL would deliver it
            m->name.len = 0; m->name.buf[0] = 0;
            snprintf(typed.text, sizeof typed.text, "%s", v); typed.ntext = (int)strlen(typed.text);
            field_type(&m->name, &typed);
            SDL_Log("menu-test: typed %s", m->name.buf);
            return;
        }
        Input none; memset(&none, 0, sizeof none);
        MenuKeys enter; memset(&enter, 0, sizeof enter); enter.ok = true;
        tick_main(g, &none, enter);   // ENTER keeps it: settings.txt and the net name, the real path
        m->script[0] = 0;
        return;
    }
    bool join = !strncmp(m->script, "join", 4);
    bool solo = !strncmp(m->script, "solo", 4) || !strncmp(m->script, "pause", 5);
    if (m->page == MENU_MAIN) {
        int want = join ? ROW_JOIN : solo ? ROW_SOLO : ROW_HOST;
        const char *name = join ? "JOIN GAME" : solo ? "PLAY SOLO" : "HOST GAME";
        if (m->row != want) { m->row = want; SDL_Log("menu-test: row %s", name); return; }
        SDL_Log("menu-test: choose %s", name);
        if (join) { page(g, MENU_JOIN); return; }
        if (solo) { start_playing(g, "solo"); return; }
        { const char *p = strchr(m->script, ':'); if (p && atoi(p + 1) > 0) m->port = (uint16_t)atoi(p + 1); }   // host:PORT for a test that needs a free one
        if (host_start(g)) start_playing(g, "hosting");
        return;
    }
    if (m->page == MENU_PAUSE) { SDL_Log("menu-test: INVITE INFO"); page(g, MENU_INVITE); m->script[0] = 0; return; }
    if (m->page == MENU_JOIN) {
        const char *a = strchr(m->script, ':');
        if (!a) { m->script[0] = 0; SDL_Log("menu-test: join page"); return; }   // "join" on its own: stop here
        a = a + 1;
        if (strcmp(m->addr.buf, a) != 0) { field_set(&m->addr, a); SDL_Log("menu-test: typed %s", a); return; }
        SDL_Log("menu-test: connect to %s", a);
        join_start(g, a);
        return;
    }
    if (m->page == MENU_OFF) {
        bool pause = !strncmp(m->script, "pause", 5), invite = !strncmp(m->script, "invite", 6);
        if (pause || invite) {   // carry on into the Esc menu so a headless run can photograph it
            page(g, MENU_PAUSE); SDL_Log("menu-test: Esc menu");
            if (pause) m->script[0] = 0; else m->row = ROW_INVITE;
            return;
        }
        m->script[0] = 0; SDL_Log("menu-test: done");
    }
}

// ---------------------------------------------------------------- entry points

void menu_init(Game *g) {
    Menu *m = &g->menu;
    if (m->booted) return;
    m->booted = true;
    char v[128];
    field_set(&m->name, settings_get("name", v, sizeof v) ? v : "goon");
    field_set(&m->addr, settings_get("last_join", v, sizeof v) ? v : "127.0.0.1:7777");
    m->port = (uint16_t)(settings_get("port", v, sizeof v) ? atoi(v) : MENU_PORT_DEFAULT);
    if (!m->port) m->port = MENU_PORT_DEFAULT;
    if (!g->net.name[0]) snprintf(g->net.name, sizeof g->net.name, "%s", m->name.buf);
    if (SDL_getenv("HOLLOW_MENU_TEST")) menu_set_test(g, SDL_getenv("HOLLOW_MENU_TEST"));
    if (g->net.mode == NM_HOST) m->nips = netsys_local_ips(m->ips, MENU_IPS);   // --host from the command line still shows its address
}

void menu_open_main(Game *g) {
    menu_init(g);
    g->state = GS_MENU; g->state_t = 0;
    page(g, MENU_MAIN);
    dbg_log("menu: main menu over %s", g->level.path);
}

void menu_set_test(Game *g, const char *script) {
    Menu *m = &g->menu;
    snprintf(m->script, sizeof m->script, "%s", script ? script : "");
    m->step = 0; m->step_t = 0;
}

bool menu_tick(Game *g, const Input *in, float dt) {
    Menu *m = &g->menu;
    menu_init(g);
    m->t += dt;
    if (m->note_t > 0) m->note_t -= dt;

    // The host said goodbye: back to the front door. Only ever after a join that worked, so a
    // client still knocking at the door (--join from the command line) is left alone.
    if (g->net.connected) m->joined = true;
    if (m->joined && g->net.mode == NM_CLIENT && !g->net.connected) {
        net_off(g); g->state = GS_MENU; page(g, MENU_MAIN);
        note(m, "the host ended the game");
    }
    if (g->state == GS_MENU) {
        // A slow turn over the pier, so the front door is the island and not a black rectangle.
        // The eye stays above the ground: the dock and the boat are exactly what it orbits.
        m->orbit += dt * 0.05f;
        Vec3 pivot = v3_add(g->level.spawn, v3(0, 1.6f, 0));
        Vec3 eye = v3_add(pivot, v3(sinf(m->orbit) * 26.0f, 13.0f, cosf(m->orbit) * 26.0f));
        if (g->terrain.present) eye.y = fmaxf(eye.y, terrain_height(&g->terrain, eye.x, eye.z) + 4.0f);
        camera_set_scene(&g->cam, eye, pivot, 42.0f, true);
        if (m->page == MENU_OFF) page(g, MENU_MAIN);   // nothing else can be showing in this state
    }
    // Esc opens the in-game menu. The tools keep Esc for themselves while one of them is open.
    if (m->page == MENU_OFF && g->state != GS_MENU && g->tool_mode == 0) {
        MenuKeys k = menu_keys(g, in, dt);
        if (k.back) { page(g, MENU_PAUSE); }
    } else {
        MenuKeys k = menu_keys(g, in, dt);
        switch (m->page) {
        case MENU_MAIN:       tick_main(g, in, k); break;
        case MENU_JOIN:       tick_join(g, in, k); break;
        case MENU_CONNECTING: tick_connecting(g, k, dt); break;
        case MENU_PAUSE:      tick_pause(g, in, k); break;
        case MENU_INVITE:     if (k.back || k.ok) { page(g, MENU_PAUSE); m->row = ROW_INVITE; } break;
        case MENU_OFF:        break;
        }
    }
    test_tick(g, dt);
    platform_text_input(g->pf, m->name.active || m->addr.active);
    return m->page != MENU_OFF;
}

// ---------------------------------------------------------------- drawing

static void text_mid(Gfx *x, float cx, float y, int px, Vec4 c, const char *s) {
    gfx_ui_text_px(x, cx - gfx_ui_text_px_width(x, px, s) * 0.5f, y, px, c, s);
}

static void draw_field(Gfx *x, float fx, float fy, float fw, const MenuField *f, float t, bool active, const char *empty) {
    gfx_ui_rect(x, fx, fy, fw, 44, v4(0.06f, 0.06f, 0.08f, 0.85f));
    Vec4 edge = active ? C_HOT : v4(0.35f, 0.34f, 0.32f, 1);
    gfx_ui_rect(x, fx, fy, fw, 2, edge); gfx_ui_rect(x, fx, fy + 42, fw, 2, edge);
    gfx_ui_rect(x, fx, fy, 2, 44, edge); gfx_ui_rect(x, fx + fw - 2, fy, 2, 44, edge);
    const char *s = f->buf[0] ? f->buf : empty;
    gfx_ui_text_px(x, fx + 12, fy + 6, 30, f->buf[0] ? C_TEXT : C_DIM, s);
    if (active && fmodf(t, 1.0f) < 0.55f)
        gfx_ui_rect(x, fx + 14 + gfx_ui_text_px_width(x, 30, f->buf), fy + 8, 2, 28, C_HOT);
}

static void draw_row(Gfx *x, int i, const char *label, bool sel) {
    float y = row_top(i), cx = MW * 0.5f;
    if (sel) {
        gfx_ui_rect(x, cx - ROW_W * 0.5f, y, ROW_W, ROW_H, v4(1.0f, 0.80f, 0.38f, 0.13f));
        gfx_ui_rect(x, cx - ROW_W * 0.5f, y, 4, ROW_H, C_HOT);
    }
    text_mid(x, cx, y + 4, 38, sel ? C_HOT : C_TEXT, label);
}

// Who is here: the slot colour, the name and what the wire costs.
static void draw_players(Game *g, float px_, float py, bool big) {
    Gfx *x = &g->gfx;
    const NetGame *n = &g->net;
    int size = big ? 26 : 22;
    float line = big ? 34.0f : 28.0f;
    gfx_ui_text_px(x, px_, py, size, C_DIM, n->mode == NM_OFF ? "playing solo" : "players");
    py += line;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!n->slots[i].active) continue;
        Vec4 tint = n->slots[i].tint; tint.w = 1;
        gfx_ui_rect(x, px_, py + 6, 14, 14, tint);
        char s[96];
        const char *who = n->slots[i].name[0] ? n->slots[i].name : "player";
        float rtt = 0;
        if (n->mode == NM_HOST && n->slots[i].has_peer) rtt = n->slots[i].peer.rtt;
        else if (n->mode == NM_CLIENT && i == n->local) rtt = n->server.rtt;
        if (rtt > 0) snprintf(s, sizeof s, "%s%s   %.0f ms", who, i == g->local ? " (you)" : "", (double)(rtt * 1000.0f));
        else snprintf(s, sizeof s, "%s%s", who, i == g->local ? " (you)" : "");
        gfx_ui_text_px(x, px_ + 24, py, size, i == g->local ? C_TEXT : C_DIM, s);
        py += line;
    }
}

static void draw_main(Game *g) {
    Gfx *x = &g->gfx;
    Menu *m = &g->menu;
    gfx_ui_rect(x, 0, 0, MW, MH, v4(0.02f, 0.02f, 0.04f, 0.58f));
    gfx_ui_rect(x, 0, 96, MW, 4, v4(1.0f, 0.80f, 0.38f, 0.25f));
    text_mid(x, MW * 0.5f, 108, 92, C_TEXT, "GOONSTEIN ISLAND");
    text_mid(x, MW * 0.5f, 214, 24, C_DIM, "four idiots, one boat, no qualifications");

    char namerow[128]; snprintf(namerow, sizeof namerow, "NAME   %s", m->name.buf[0] ? m->name.buf : "goon");
    const char *rows[ROW_MAIN_N] = { "PLAY SOLO", "HOST GAME", "JOIN GAME", namerow, "QUIT" };
    for (int i = 0; i < ROW_MAIN_N; i++) draw_row(x, i, rows[i], m->row == i);
    if (m->name.active) {
        draw_field(x, MW * 0.5f - 150, row_top(ROW_NAME) + 2, 300, &m->name, m->t, true, "your name");
        text_mid(x, MW * 0.5f, row_top(ROW_NAME) + 52, 22, C_DIM, "type a name, ENTER keeps it");
    }
    if (m->note_t > 0) text_mid(x, MW * 0.5f, MH - 96, 26, m->note_good ? C_HOT : C_BAD, m->note);
    text_mid(x, MW * 0.5f, MH - 56, 24, C_DIM, "arrows or mouse to choose   ENTER to take it");
}

static void draw_join(Game *g) {
    Gfx *x = &g->gfx;
    Menu *m = &g->menu;
    gfx_ui_rect(x, 0, 0, MW, MH, v4(0.02f, 0.02f, 0.04f, 0.66f));
    text_mid(x, MW * 0.5f, 150, 64, C_TEXT, "JOIN GAME");
    text_mid(x, MW * 0.5f, 262, 26, C_DIM, "the address your friend read out");
    draw_field(x, MW * 0.5f - 230, 310, 460, &m->addr, m->t, true, "192.168.1.20:7777");
    gfx_ui_rect(x, MW * 0.5f - 120, 430, 240, 54, v4(1.0f, 0.80f, 0.38f, 0.16f));
    text_mid(x, MW * 0.5f, 440, 34, C_HOT, "CONNECT");
    if (m->note_t > 0) text_mid(x, MW * 0.5f, 520, 26, m->note_good ? C_HOT : C_BAD, m->note);
    text_mid(x, MW * 0.5f, MH - 56, 24, C_DIM, "ENTER connects   ESC goes back");
}

static void draw_connecting(Game *g) {
    Gfx *x = &g->gfx;
    Menu *m = &g->menu;
    gfx_ui_rect(x, 0, 0, MW, MH, v4(0.02f, 0.02f, 0.04f, 0.72f));
    char s[192];
    int dots = ((int)(m->t * 2.5f)) % 4;
    snprintf(s, sizeof s, "connecting to %s%.*s", g->net.join_target, dots, "...");
    text_mid(x, MW * 0.5f, 340, 44, C_TEXT, s);
    snprintf(s, sizeof s, "%.0f s", MENU_JOIN_TIMEOUT - m->connect_t);
    text_mid(x, MW * 0.5f, 400, 26, C_DIM, s);
    text_mid(x, MW * 0.5f, MH - 56, 24, C_DIM, "ESC gives up");
}

static void draw_pause(Game *g) {
    Gfx *x = &g->gfx;
    Menu *m = &g->menu;
    gfx_ui_rect(x, 0, 0, MW, MH, v4(0.02f, 0.02f, 0.04f, 0.55f));
    text_mid(x, MW * 0.5f, 200, 64, C_TEXT, "PAUSED");
    const char *leave = g->net.mode == NM_HOST ? "END HOST" : g->net.mode == NM_CLIENT ? "LEAVE GAME" : "MAIN MENU";
    const char *rows[ROW_PAUSE_N] = { "RESUME", "INVITE INFO", leave, "QUIT" };
    for (int i = 0; i < ROW_PAUSE_N; i++) draw_row(x, i, rows[i], m->row == i);
    if (g->net.mode == NM_HOST) text_mid(x, MW * 0.5f, MH - 96, 24, C_DIM, "the island keeps running while this is up: you are still hosting");
    text_mid(x, MW * 0.5f, MH - 56, 24, C_DIM, "ESC resumes");
}

static void draw_invite(Game *g) {
    Gfx *x = &g->gfx;
    Menu *m = &g->menu;
    gfx_ui_rect(x, 0, 0, MW, MH, v4(0.02f, 0.02f, 0.04f, 0.72f));
    text_mid(x, MW * 0.5f, 140, 56, C_TEXT, "INVITE");
    float y = 250;
    if (g->net.mode == NM_HOST) {
        text_mid(x, MW * 0.5f, y, 26, C_DIM, "read one of these to a friend; they type it into JOIN GAME"); y += 46;
        if (m->nips == 0) { text_mid(x, MW * 0.5f, y, 30, C_BAD, "no network address found: are you on wifi?"); y += 46; }
        for (int i = 0; i < m->nips; i++) { char s[64]; host_addr(m, i, s, sizeof s); text_mid(x, MW * 0.5f, y, 44, C_HOT, s); y += 56; }
    } else if (g->net.mode == NM_CLIENT) {
        text_mid(x, MW * 0.5f, y, 26, C_DIM, "you joined"); y += 46;
        text_mid(x, MW * 0.5f, y, 44, C_HOT, g->net.join_target); y += 56;
    } else {
        text_mid(x, MW * 0.5f, y, 30, C_DIM, "you are playing solo"); y += 46;
        text_mid(x, MW * 0.5f, y, 26, C_DIM, "leave to the main menu and HOST GAME to play with friends"); y += 46;
    }
    draw_players(g, MW * 0.5f - 180, y + 20, true);
    text_mid(x, MW * 0.5f, MH - 56, 24, C_DIM, "ESC goes back");
}

// Hosting, playing: the address sits in the corner so it can be read out without stopping.
static void draw_host_corner(Game *g) {
    Gfx *x = &g->gfx;
    Menu *m = &g->menu;
    if (g->net.mode != NM_HOST) return;
    char s[64]; host_addr(m, 0, s, sizeof s);
    char line[96]; snprintf(line, sizeof line, "hosting  %s", s);
    float w = gfx_ui_text_px_width(x, 24, line);
    gfx_ui_rect(x, MW - w - 26, 12, w + 18, 32, v4(0, 0, 0, 0.35f));
    gfx_ui_text_px(x, MW - w - 18, 14, 24, v4(0.85f, 0.82f, 0.6f, 0.9f), line);
    if (m->nips > 1) {
        char more[64]; snprintf(more, sizeof more, "+%d more in the Esc menu", m->nips - 1);
        float w2 = gfx_ui_text_px_width(x, 20, more);
        gfx_ui_text_px(x, MW - w2 - 18, 46, 20, v4(0.7f, 0.68f, 0.6f, 0.7f), more);
    }
}

bool menu_up(const Game *g) { return g->menu.page != MENU_OFF; }

void menu_draw(Game *g) {
    Gfx *x = &g->gfx;
    Menu *m = &g->menu;
    switch (m->page) {
    case MENU_MAIN:       draw_main(g); break;
    case MENU_JOIN:       draw_join(g); break;
    case MENU_CONNECTING: draw_connecting(g); break;
    case MENU_PAUSE:      draw_pause(g); break;
    case MENU_INVITE:     draw_invite(g); break;
    case MENU_OFF:
        draw_host_corner(g);
        // Tab held: who is here, their ping and their colour.
        if (g->pf->input.key_held[SDL_SCANCODE_TAB] || SDL_getenv("HOLLOW_TAB")) {   // env: headless capture
            int n = 0; for (int i = 0; i < NET_MAX_PLAYERS; i++) if (g->net.slots[i].active) n++;
            gfx_ui_rect(x, MW * 0.5f - 220, 90, 440, 52.0f + 34.0f * (float)n, v4(0.02f, 0.02f, 0.04f, 0.78f));
            draw_players(g, MW * 0.5f - 196, 104, true);
        }
        break;
    }
}
