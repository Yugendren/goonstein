// The front door: a main menu at launch, an Esc menu in the game, and everything four people need
// to play together without a command line.
//
// Nothing here simulates anything. The menu reads the input, draws over whatever the game is
// already rendering, and asks netgame to host, join or stop. The world keeps running behind it:
// a co-op game cannot pause, and the host must keep answering its clients while the Esc menu is up.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "platform.h"

struct Game;

// Which page is up. MENU_OFF is "playing"; the game state is GS_MENU only for the main menu, so
// the Esc menu can appear over the overworld, a fight or a scene without disturbing it.
// MENU_SETTINGS is one page reached from both the main menu and the Esc menu (DESIGN.md asks for
// a minimal UI, so there are no sub-pages under it): every row is changed in place with left and
// right, applied live, and written back to assets/settings.txt.
typedef enum MenuPage {
    MENU_OFF, MENU_MAIN, MENU_JOIN, MENU_CONNECTING, MENU_PAUSE, MENU_INVITE, MENU_SETTINGS
} MenuPage;

#define MENU_FIELD_MAX 64
#define MENU_IPS       6

// One editable line of text. `active` means the keyboard belongs to it: while it does, typed keys
// reach the field and nothing else (see platform_text_input).
typedef struct MenuField { char buf[MENU_FIELD_MAX]; int len; bool active; } MenuField;

typedef struct Menu {
    MenuPage page;
    int      row;                  // the highlighted row of the current page
    float    t;                    // seconds on this page: fades and the caret blink
    float    orbit;                // the main menu's slow turn around the pier, in radians
    MenuField name, addr;          // NAME, and the JOIN address (IP:PORT)
    double   connect_t;            // seconds spent waiting for the host to answer
    char     note[96]; float note_t; bool note_good;   // the one line under the rows, and whether it is bad news
    uint16_t port;                 // settings `port`, 7777 unless it says otherwise
    char     ips[MENU_IPS][16]; int nips;   // this machine's LAN addresses, read once when hosting
    bool     pad_prev[6];          // gamepad edges: up, down, A, B, left, right
    float    mx_prev, my_prev;     // last cursor position: a still mouse does not steal the highlight
    float    stick_cool;           // stick repeat delay, so one flick moves one row
    char     script[160];          // --menu-test / HOLLOW_MENU_TEST: "host", "join:HOST:PORT", or a
                                   // "settings:volume=40,vsync=off" list, which is the long one
    MenuPage set_from;             // which page SETTINGS was opened from: ESC goes back to that one
    int      saved_row;            // the settings row whose value was last written to settings.txt
    float    saved_t;              // and how long its "(saved)" flash has left
    int      step; float step_t;   // where that script has got to
    bool     joined;               // this process was accepted by a host at least once
    bool     booted;               // menu_init has run
} Menu;

// Read settings.txt (name, port, last_join) and seat the name in the net game. Safe to call twice.
void menu_init(struct Game *g);
// Launch with no --host/--join/--level/--start/--bot: show the main menu over the island.
void menu_open_main(struct Game *g);
// Drive the menu for one tick. Returns true while a menu owns the input, which is the caller's cue
// to feed the rest of the tick an empty Input: keys typed at a menu must never move the player.
bool menu_tick(struct Game *g, const Input *in, float dt);
// True while a menu page is up. The HUD skips itself then: it would only show through the menu.
bool menu_up(const struct Game *g);
// The menu itself, drawn over the finished frame in the game window's UI pixels.
void menu_draw(struct Game *g);
// Run a scripted menu: "host" hosts and starts, "join:HOST:PORT" types the address and connects,
// "settings" opens the settings page and "settings:volume=40,vsync=off" then works the arrows.
void menu_set_test(struct Game *g, const char *script);
