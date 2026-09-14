// --- map --- The paper map: a folded card in the off hand, drawn through the viewmodel's lens.
//
// See src/map.h for what this is and why. This file is the whole of it: the raise/lower, the card,
// every live mark on the card, and the compass strip at the top of the HUD.
//
// THREE THINGS ARE WORTH KNOWING BEFORE READING THE CODE.
//
// 1. The card is drawn like a weapon, not like a HUD element. It goes through
//    camera_view_proj_lens at the viewmodel's own field of view and into the viewmodel's slice of
//    the depth buffer, which is what keeps it in front of a wall you are standing against and what
//    makes it sway and bob with the same head the gun does. A map drawn in UI space would be a
//    menu; this one is an object you are holding, and the difference is the whole point.
//
// 2. Everything on the card is a QUAD IN THE CARD'S OWN PLANE, lifted a millimetre or two along
//    the card's normal per layer, never a texture that is rewritten per frame. The world pass has
//    no alpha blending (see gfx.c's pipeline table: pipe_world is opaque, and lit.frag discards
//    below half alpha), so every mark is an opaque colour and the layering is done with depth --
//    which is free, because the depth test was already going to run.
//
// 3. The basis. A model matrix in this engine must have its columns as a RIGHT-HANDED triple with
//    the third column pointing AT the camera, or the front faces are culled -- gfx_draw_sprite
//    says so in as many words and goes to some trouble to guarantee it. With the camera's own
//    (right, up, fwd) from camera_view_basis, `right x up` is -fwd, so (right, up, -fwd) is
//    exactly that triple and needs no flipping anywhere. The quad mesh is x in [-0.5, 0.5],
//    y in [0, 1], facing +Z, uv (0,0) at its top-left, so a card built on that basis takes the
//    printed map the right way round with no uv gymnastics either.
//
// NORTH IS UP, always, on the card and on the compass strip alike. The island's own axes (see
// assets/levels/island.txt) put island north at world -X and island east at world +Z, so the card
// runs screen-right along +Z and screen-down along +X. tools/island_terrain.py --map draws the
// print to match, and assets/textures/map_island.txt is the one place the world rectangle it
// covers is written down, so the two sides cannot drift apart.
#include "game.h"
#include "map.h"
#include "debug.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------- tuning

#define MAP_RAISE_RATE   11.0f    // exponential rate of the raise/lower ease; ~0.25 s door to door
#define MAP_W            0.300f   // card width in metres, at MAP_FWD from the eye
#define MAP_ASPECT       1.600f   // the print is 1024 x 640; the card matches it exactly
#define MAP_H            (MAP_W / MAP_ASPECT)
#define MAP_FWD          0.440f   // metres in front of the eye when fully up
#define MAP_RIGHT        0.018f
#define MAP_DOWN         0.012f
#define MAP_DROP         0.340f   // how far below the frame it sits when fully down
#define MAP_PITCH        21.0f    // degrees the top edge lies away from you, the way paper does
#define MAP_FOV          58.0f    // the same lens the gun is drawn through (weaponview.c VM_FOV_DEFAULT)
#define MAP_NEAR         0.02f
#define MAP_FAR          12.0f
#define MAP_DEPTH        0.12f    // the viewmodel's depth slice; the gun uses the same one
#define MAP_LIFT         0.0016f  // metres between overlay layers

#define MAP_DASH_PITCH   0.0080f  // card metres between dashes on the route line
#define MAP_DASH_LEN     0.0042f
#define MAP_DASH_CRAWL   0.028f   // card metres a second the dashes travel toward the objective
#define MAP_MAX_DASHES   80

// How much world the card shows. NOT the whole island: the Culvert mouth is thirteen metres from
// the boat, and thirteen metres of route drawn across three hundred and fifty of island is four
// pixels of dotted line -- which is to say, no line at all. So the window is sized to hold you and
// whatever you are walking to, with a margin, and never tighter than MAP_ZOOM_MIN or wider than the
// print itself. Walk to the far end of the island and the map pulls back to cover the walk; stand
// on the pier with the door in sight and it is a plan of the pier.
#define MAP_ZOOM_MIN    120.0f    // metres across the card, closest in
#define MAP_ZOOM_MARGIN  2.6f     // how much wider than you-to-objective the window is

// Ink. The print is paper, so everything drawn on it is a pen.
#define INK              v4(0.14f, 0.12f, 0.10f, 1)
#define INK_SOFT         v4(0.38f, 0.33f, 0.27f, 1)
#define PAPER_TINT       v4(0.70f, 0.66f, 0.57f, 1)
#define OBJ_RED          v4(0.80f, 0.16f, 0.12f, 1)
#define YOU_BLUE         v4(0.12f, 0.30f, 0.62f, 1)

// ---------------------------------------------------------------- state
//
// A file static, for the same reason weaponview.c keeps the first-person arms in one: this is a
// texture and a raise/lower spring belonging to whoever is sitting at this keyboard. It is never
// replicated, never saved, and never seen by anybody else.
static struct {
    bool    up;                     // the player has asked for it
    float   k;                      // 0..1, eased
    float   sway_x, sway_y;         // the card lagging behind the mouse
    double  crawl;                  // dash phase, card metres

    char    level[64];              // the level stem the print below belongs to
    bool    checked;                // we have decided what this level's map is
    Texture print; bool print_ok;   // the printed map, if this level has one
    float   x0, x1, z0, z1;         // the world rectangle it covers (from the sidecar)
    bool    schematic;              // no print, but no terrain either: draw the level's own blocks
    float   sx0, sx1, sz0, sz1;     // and the world rectangle they occupy
    bool    ever_opened;            // the "M map" nudge on the compass strip stops after the first
} s_map;

// ---------------------------------------------------------------- small geometry

// Columns straight from three axes and a position. weaponview.c has the same six lines for the
// same reason: a card, a hand and a dash are all placed against the picture's own axes rather than
// against a yaw, and nothing here is ever asked for an Euler angle.
static Mat4 basis_m(Vec3 pos, Vec3 x, Vec3 y, Vec3 z) {
    Mat4 m = m4_identity();
    m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
    m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
    m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
    m.m[12] = pos.x; m.m[13] = pos.y; m.m[14] = pos.z;
    return m;
}

// The card, as everything drawn on it needs to see it: where its middle is, its two in-plane axes
// (screen right and screen up, after the tilt), its normal, and the world-to-card scale.
typedef struct Card {
    Vec3  o, right, up, n;
    float wxc, wzc;     // the world point at the middle of the card
    float s;            // card metres per world metre
} Card;

// A quad lying in the card's plane. (sx, sy) is its centre in card metres from the middle of the
// card, (w, h) its size, rot its rotation in the plane (anticlockwise on screen), layer how many
// MAP_LIFTs proud of the paper it sits.
static void card_quad(Gfx *x, const Card *c, float sx, float sy, float w, float h, float rot,
                      Vec4 col, const Texture *tex, int layer) {
    float ca = cosf(rot), sa = sinf(rot);
    Vec3 ax = v3_add(v3_scale(c->right, ca), v3_scale(c->up, sa));
    Vec3 ay = v3_add(v3_scale(c->right, -sa), v3_scale(c->up, ca));
    Vec3 mid = v3_add(c->o, v3_add(v3_scale(c->right, sx),
                      v3_add(v3_scale(c->up, sy), v3_scale(c->n, MAP_LIFT * (float)layer))));
    Vec3 base = v3_sub(mid, v3_scale(ay, h * 0.5f));
    gfx_draw(x, &x->quad, tex ? tex : &x->white,
             basis_m(base, v3_scale(ax, w), v3_scale(ay, h), c->n), col, v4(1, 1, 0, 0));
}

// A line between two points of the card, as one rotated quad. Two dozen of these are the whole
// overlay vocabulary: a dash, a wall, a compass arm and the edge of an arena are all this.
static void card_line(Gfx *x, const Card *c, float ax, float ay, float bx, float by,
                      float thick, Vec4 col, int layer) {
    float dx = bx - ax, dy = by - ay;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f) return;
    card_quad(x, c, (ax + bx) * 0.5f, (ay + by) * 0.5f, len, thick, atan2f(dy, dx), col, NULL, layer);
}

static void card_rect(Gfx *x, const Card *c, float cx, float cy, float w, float h,
                      float thick, Vec4 col, int layer) {
    card_line(x, c, cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy - h * 0.5f, thick, col, layer);
    card_line(x, c, cx - w * 0.5f, cy + h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, thick, col, layer);
    card_line(x, c, cx - w * 0.5f, cy - h * 0.5f, cx - w * 0.5f, cy + h * 0.5f, thick, col, layer);
    card_line(x, c, cx + w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, thick, col, layer);
}

// World xz -> card metres. North (world -X) is up, east (world +Z) is right; see the banner.
static void to_card(const Card *c, float wx, float wz, float *sx, float *sy) {
    *sx = (wz - c->wzc) * c->s;
    *sy = -(wx - c->wxc) * c->s;
}

static bool on_card(float sx, float sy) {
    return fabsf(sx) < MAP_W * 0.5f - 0.004f && fabsf(sy) < MAP_H * 0.5f - 0.004f;
}

// ---------------------------------------------------------------- what this level's map is

// ".../levels/island.txt" -> "island". The level stem is the only key: a level with a printed map
// has one called map_<stem>.png and nothing has to be listed anywhere.
static void level_stem(const Game *g, char *out, size_t n) {
    const char *slash = strrchr(g->level_path, '/');
    SDL_strlcpy(out, slash ? slash + 1 : g->level_path, n);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

// The sidecar: `region X0 X1 Z0 Z1`, written by tools/island_terrain.py --map. Without it we do
// not know what rectangle of the world the image is a picture of, and guessing is how a map ends
// up putting you in the sea.
static bool read_region(const char *path, float *x0, float *x1, float *z0, float *z1) {
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return false;
    char buf[512];
    size_t n = SDL_ReadIO(io, buf, sizeof buf - 1);
    SDL_CloseIO(io);
    buf[n] = '\0';
    for (char *line = buf; line && *line; ) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (sscanf(line, " region %f %f %f %f", x0, x1, z0, z1) == 4) return true;
        line = nl ? nl + 1 : NULL;
    }
    return false;
}

static void map_forget(Game *g) {
    if (s_map.print_ok) gfx_texture_destroy(&g->gfx, &s_map.print);
    s_map.print_ok = false; s_map.schematic = false; s_map.checked = false; s_map.level[0] = '\0';
}

void map_unload(Game *g) {
    map_forget(g);
    s_map.up = false; s_map.k = 0;
}

// Decide, once per level, what the player is holding: a printed map, a schematic drawn from the
// level's own blocks, or nothing at all.
static void map_ensure(Game *g) {
    char stem[64];
    level_stem(g, stem, sizeof stem);
    if (s_map.checked && !strcmp(stem, s_map.level)) return;
    map_forget(g);
    SDL_strlcpy(s_map.level, stem, sizeof s_map.level);
    s_map.checked = true;

    char png[512], txt[512];
    snprintf(png, sizeof png, "%s/textures/map_%s.png", HOLLOW_ASSET_DIR, stem);
    snprintf(txt, sizeof txt, "%s/textures/map_%s.txt", HOLLOW_ASSET_DIR, stem);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(png, &info) && read_region(txt, &s_map.x0, &s_map.x1, &s_map.z0, &s_map.z1)) {
        s_map.print = gfx_texture_load(&g->gfx, png, 2048);
        s_map.print_ok = true;
        dbg_log("map: %s printed, %.0f x %.0f m of world", stem,
                (double)(s_map.z1 - s_map.z0), (double)(s_map.x1 - s_map.x0));
        return;
    }

    // No print. A level with no terrain under it is hand-built out of blocks, and the blocks ARE
    // the map: a room seen from above. A level WITH terrain and no print (the lantern test level)
    // gets nothing, and pressing M there says so rather than showing a blank sheet.
    if (g->level.terrain_file[0]) {
        dbg_log("map: no map of %s (terrain level with no assets/textures/map_%s.png)", stem, stem);
        return;
    }
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (int i = 0; i < g->level.nblocks; i++) {
        const Block *b = &g->level.blocks[i];
        if (!b->solid) continue;
        if (b->center.x - b->size.x * 0.5f < x0) x0 = b->center.x - b->size.x * 0.5f;
        if (b->center.x + b->size.x * 0.5f > x1) x1 = b->center.x + b->size.x * 0.5f;
        if (b->center.z - b->size.z * 0.5f < z0) z0 = b->center.z - b->size.z * 0.5f;
        if (b->center.z + b->size.z * 0.5f > z1) z1 = b->center.z + b->size.z * 0.5f;
    }
    if (x1 <= x0 || z1 <= z0) { dbg_log("map: no map of %s (nothing to draw)", stem); return; }
    s_map.schematic = true;
    s_map.sx0 = x0; s_map.sx1 = x1; s_map.sz0 = z0; s_map.sz1 = z1;
    dbg_log("map: %s drawn from its own blocks, %.0f x %.0f m", stem, (double)(z1 - z0), (double)(x1 - x0));
}

bool map_exists(const Game *g) { (void)g; return s_map.print_ok || s_map.schematic; }
float map_raised(const Game *g) { (void)g; return s_map.k; }

// ---------------------------------------------------------------- the tick

void map_tick(Game *g, const Input *in, float dt) {
    map_ensure(g);

    // HOLLOW_MAP=1 holds it up for the whole run. A capture harness has no hand to press M with,
    // and every other viewmodel knob in this game (HOLLOW_GRIP, HOLLOW_VM_REST) exists for exactly
    // the same reason: a frame you cannot photograph is a frame nobody reviews.
    static int forced = -1;
    if (forced < 0) { const char *e = SDL_getenv("HOLLOW_MAP"); forced = (e && e[0] != '0') ? 1 : 0; }
    if (forced) {
        s_map.up = map_exists(g) && g->state == GS_EXPLORE && !g->warp.phase;
        s_map.k = damp(s_map.k, s_map.up ? 1.0f : 0.0f, MAP_RAISE_RATE, dt);
        return;
    }

    // Anything that takes the world away takes the map with it: a cutscene, a fade through a door,
    // being flat on your back. It comes back up only if you ask again.
    if (g->state != GS_EXPLORE || g->warp.phase || weapons_is_down(g, g->local)) s_map.up = false;
    else if (in->map_toggle) {
        if (!map_exists(g)) {
            snprintf(g->msg, sizeof g->msg, "no map of this place");
            g->msg_t = 2.0f;
        } else {
            s_map.up = !s_map.up;
            if (s_map.up) s_map.ever_opened = true;
        }
    }
    s_map.k = damp(s_map.k, s_map.up ? 1.0f : 0.0f, MAP_RAISE_RATE, dt);
    if (s_map.k < 0.001f) s_map.k = 0;
}

// ---------------------------------------------------------------- the marks

// The dotted line along a route, with the dashes crawling toward the objective. It is drawn from
// the level's own `route` (assets/levels/README.md) and nothing else: this is not a path-finder
// and it never was -- the level already knows which way round a person goes, and the map's job is
// to show you the answer somebody wrote down. With no route there are no dashes, only the marker.
static void draw_route(Gfx *x, const Card *c, const Route *r) {
    if (!r || r->n < 2) return;
    // Walk the polyline in card metres, dropping a dash every MAP_DASH_PITCH. The crawl moves the
    // first dash's offset along, so as it grows every dash travels toward the last point.
    float phase = (float)fmod(s_map.crawl, (double)MAP_DASH_PITCH);
    float travelled = 0.0f, next = phase;
    int drawn = 0;
    for (int i = 0; i + 1 < r->n && drawn < MAP_MAX_DASHES; i++) {
        float ax, ay, bx, by;
        to_card(c, r->x[i], r->z[i], &ax, &ay);
        to_card(c, r->x[i + 1], r->z[i + 1], &bx, &by);
        float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-6f) continue;
        float ux = dx / len, uy = dy / len, rot = atan2f(dy, dx);
        while (next < travelled + len && drawn < MAP_MAX_DASHES) {
            float t = next - travelled;
            float px = ax + ux * t, py = ay + uy * t;
            if (on_card(px, py)) { card_quad(x, c, px, py, MAP_DASH_LEN, 0.0022f, rot, INK_SOFT, NULL, 2); drawn++; }
            next += MAP_DASH_PITCH;
        }
        travelled += len;
    }
}

// A ring, as eight short chords. Cheaper than it looks and the only round thing on the card.
static void card_ring(Gfx *x, const Card *c, float cx, float cy, float rad, float thick, Vec4 col, int layer) {
    const int SEG = 8;
    float prevx = cx + rad, prevy = cy;
    for (int i = 1; i <= SEG; i++) {
        float a = (float)i / SEG * 2.0f * PI;
        float px = cx + cosf(a) * rad, py = cy + sinf(a) * rad;
        card_line(x, c, prevx, prevy, px, py, thick, col, layer);
        prevx = px; prevy = py;
    }
}

// The objective: a pulsing ring with a cross in it, in the one colour on the card that is not ink.
static void draw_objective_mark(Gfx *x, const Card *c, float sx, float sy, float t) {
    float pulse = 0.0070f + 0.0022f * sinf(t * 4.0f);
    card_ring(x, c, sx, sy, pulse, 0.0016f, OBJ_RED, 5);
    card_line(x, c, sx - 0.0042f, sy, sx + 0.0042f, sy, 0.0016f, OBJ_RED, 5);
    card_line(x, c, sx, sy - 0.0042f, sx, sy + 0.0042f, 0.0016f, OBJ_RED, 5);
}

// You, and which way you are facing. Card angles run anticlockwise from screen right; a world yaw
// of psi faces (sin psi, cos psi) in xz, which is (cos psi, -sin psi) on a north-up card, so the
// rotation the arrow wants is simply -psi.
static void draw_you(Gfx *x, const Card *c, float sx, float sy, float yaw) {
    float rot = -yaw;
    float fx = cosf(rot), fy = sinf(rot);
    card_ring(x, c, sx, sy, 0.0058f, 0.0013f, INK, 6);
    card_quad(x, c, sx, sy, 0.0048f, 0.0048f, rot, YOU_BLUE, NULL, 7);
    card_quad(x, c, sx + fx * 0.0068f, sy + fy * 0.0068f, 0.0058f, 0.0024f, rot, YOU_BLUE, NULL, 7);
}

// A compass rose in the card's bottom-left corner: four arms, four short diagonals, a ring, and a
// needle that turns with your head. North is up on this card and always will be, so the rose is
// decoration and the needle is the part that tells you something.
static void draw_rose(Gfx *x, const Card *c, float yaw) {
    float cx = -MAP_W * 0.5f + 0.028f, cy = MAP_H * 0.5f - 0.026f;   // top-left: the hands own the bottom corners
    float r = 0.017f;
    card_ring(x, c, cx, cy, r, 0.0011f, INK_SOFT, 3);
    for (int i = 0; i < 4; i++) {
        float a = (float)i * (PI * 0.5f);
        card_line(x, c, cx, cy, cx + cosf(a) * r, cy + sinf(a) * r, 0.0013f, INK, 3);
        float d = a + PI * 0.25f;
        card_line(x, c, cx, cy, cx + cosf(d) * r * 0.5f, cy + sinf(d) * r * 0.5f, 0.0009f, INK_SOFT, 3);
    }
    card_quad(x, c, cx, cy + r + 0.0035f, 0.0045f, 0.0045f, PI * 0.25f, INK, NULL, 4);   // north
    float rot = -yaw;
    card_line(x, c, cx - cosf(rot) * r * 0.45f, cy - sinf(rot) * r * 0.45f,
                    cx + cosf(rot) * r * 0.80f, cy + sinf(rot) * r * 0.80f, 0.0018f, OBJ_RED, 5);
}

// The arena, seen from above, for a level that has no printed map: its own solid blocks, the ones
// tall enough to be walls and floors rather than kerbs, filled in flat. The exit is whatever
// `door:` trigger it has, which in the cave is the way back up the tunnel.
static void draw_schematic(Gfx *x, const Card *c, const Game *g) {
    const Level *lv = &g->level;
    for (int i = 0; i < lv->nblocks; i++) {
        const Block *b = &lv->blocks[i];
        if (!b->solid || b->size.y < 0.6f) continue;
        float sx, sy;
        to_card(c, b->center.x, b->center.z, &sx, &sy);
        if (!on_card(sx, sy)) continue;
        bool wall = b->size.y > 3.0f;
        card_quad(x, c, sx, sy, b->size.z * c->s, b->size.x * c->s, 0,
                  wall ? INK : INK_SOFT, NULL, wall ? 2 : 1);
    }
    if (lv->arena_max.x > lv->arena_min.x) {
        float ax, ay, bx, by;
        to_card(c, lv->arena_min.x, lv->arena_min.z, &ax, &ay);
        to_card(c, lv->arena_max.x, lv->arena_max.z, &bx, &by);
        card_rect(x, c, (ax + bx) * 0.5f, (ay + by) * 0.5f, fabsf(bx - ax), fabsf(by - ay), 0.0030f, INK, 3);
    }
    for (int i = 0; i < lv->ntriggers; i++) {
        if (strncmp(lv->triggers[i].name, "door:", 5)) continue;
        Vec3 mid = v3_scale(v3_add(lv->triggers[i].vmin, lv->triggers[i].vmax), 0.5f);
        float sx, sy;
        to_card(c, mid.x, mid.z, &sx, &sy);
        if (!on_card(sx, sy)) continue;
        card_ring(x, c, sx, sy, 0.0070f, 0.0016f, OBJ_RED, 4);
    }
}

// The hands holding it. Not the first-person arms model -- that one is hung off the gun in
// weaponview.c and goes down with it -- but two fists at the bottom corners, thumbs over the front
// of the paper and the rest of the hand behind it, which is how anybody holds a map.
static void draw_hands(Gfx *x, const Card *c, Vec4 skin, Vec4 sleeve) {
    for (int s = -1; s <= 1; s += 2) {
        float side = (float)s;
        Vec3 corner = v3_add(c->o, v3_add(v3_scale(c->right, side * (MAP_W * 0.5f - 0.014f)),
                                          v3_scale(c->up, -MAP_H * 0.5f + 0.008f)));
        // the hand itself, mostly behind the paper and below its bottom corner
        Vec3 palm = v3_add(corner, v3_add(v3_scale(c->up, -0.030f), v3_scale(c->n, -0.016f)));
        gfx_draw(x, &x->cube, &x->white,
                 basis_m(palm, v3_scale(c->right, 0.047f), v3_scale(c->up, 0.058f), v3_scale(c->n, 0.036f)),
                 skin, v4(1, 1, 0, 0));
        // a thumb laid across the front of the paper, and two knuckles beside it: that overlap is
        // the whole illusion. Without something in FRONT of the card it is a poster, not an object.
        Vec3 thumb = v3_add(corner, v3_add(v3_scale(c->right, side * -0.020f),
                            v3_add(v3_scale(c->up, 0.024f), v3_scale(c->n, 0.013f))));
        gfx_draw(x, &x->cube, &x->white,
                 basis_m(thumb, v3_scale(c->right, 0.038f), v3_scale(c->up, 0.014f), v3_scale(c->n, 0.013f)),
                 skin, v4(1, 1, 0, 0));
        for (int f = 0; f < 3; f++) {
            Vec3 at = v3_add(corner, v3_add(v3_scale(c->right, side * (0.001f - (float)f * 0.015f)),
                             v3_add(v3_scale(c->up, 0.008f - (float)f * 0.004f), v3_scale(c->n, 0.011f))));
            gfx_draw(x, &x->cube, &x->white,
                     basis_m(at, v3_scale(c->right, 0.011f), v3_scale(c->up, 0.028f), v3_scale(c->n, 0.012f)),
                     skin, v4(1, 1, 0, 0));
        }
        // and a sleeve running down out of the frame
        Vec3 cuff = v3_add(palm, v3_add(v3_scale(c->up, -0.062f), v3_scale(c->n, -0.030f)));
        gfx_draw(x, &x->cube, &x->white,
                 basis_m(cuff, v3_scale(c->right, 0.070f), v3_scale(c->up, 0.090f), v3_scale(c->n, 0.060f)),
                 sleeve, v4(1, 1, 0, 0));
    }
}

// ---------------------------------------------------------------- the card

void map_draw_viewmodel(Game *g) {
    Gfx *x = &g->gfx;
    if (x->in_shadow || g->cam.mode != CAM_FIRST) return;
    if (s_map.k <= 0.001f || !map_exists(g)) return;

    float dt = fmaxf(game_frame_dt(g), 0.0f);
    s_map.crawl += (double)(MAP_DASH_CRAWL * dt);

    Vec3 eye, right, up, fwd;
    camera_view_basis(&g->cam, &eye, &right, &up, &fwd);

    // The mouse sway, measured the way weapons_draw measures the gun's: how far the view turned
    // since the last frame, lagged. A map held perfectly still while the head moves is a decal.
    static float s_yaw = 0, s_pitch = 0;
    float dyaw = angle_wrap(g->cam.yaw - s_yaw), dpitch = g->cam.pitch - s_pitch;
    s_yaw = g->cam.yaw; s_pitch = g->cam.pitch;
    float rate = 1.0f - expf(-16.0f * dt);
    s_map.sway_x = lerpf(s_map.sway_x, clampf(-dyaw * 0.9f, -0.045f, 0.045f), rate);
    s_map.sway_y = lerpf(s_map.sway_y, clampf(dpitch * 0.7f, -0.035f, 0.035f), rate);

    float ease = smoothstep(s_map.k);
    float ph = camera_bob_phase(&g->cam), gain = camera_bob_gain(&g->cam);

    Vec3 pos = v3_add(eye, v3_add(v3_scale(fwd, MAP_FWD),
                      v3_add(v3_scale(right, MAP_RIGHT + s_map.sway_x),
                             v3_scale(up, -MAP_DOWN - MAP_DROP * (1.0f - ease) + s_map.sway_y))));
    pos = v3_add(pos, v3_scale(right, sinf(ph) * 0.016f * gain));         // one sway a stride
    pos = v3_add(pos, v3_scale(up, sinf(ph * 2.0f) * 0.010f * gain));     // one dip a footfall

    // Tilt: the top edge lies away from you, and a good deal more while it is still coming up, so
    // the card unfolds into the frame rather than sliding into it.
    float pitch = (MAP_PITCH + 34.0f * (1.0f - ease)) * DEG2RAD;
    float roll = (s_map.sway_x * 34.0f + sinf(ph) * 0.9f * gain) * DEG2RAD;
    Card c;
    {
        Vec3 n0 = v3_scale(fwd, -1.0f);                                  // out of the paper, at the eye
        Vec3 u1 = v3_sub(v3_scale(up, cosf(pitch)), v3_scale(n0, sinf(pitch)));
        Vec3 n1 = v3_add(v3_scale(n0, cosf(pitch)), v3_scale(up, sinf(pitch)));
        float cr = cosf(roll), sr = sinf(roll);
        c.right = v3_add(v3_scale(right, cr), v3_scale(u1, sr));
        c.up    = v3_sub(v3_scale(u1, cr), v3_scale(right, sr));
        c.n     = n1;
    }
    c.o = pos;

    // Where the world sits on the paper. The print's rectangle has the card's own aspect, so one
    // scale serves both axes; a schematic is fitted to whatever shape the room happens to be.
    Objective ob = game_objective(g);
    Vec3 me = PLAYER(g).c.pos;
    Vec4 uv = v4(1, 1, 0, 0);
    if (s_map.print_ok) {
        // The window: wide enough to hold you and the objective with room to breathe, never
        // tighter than MAP_ZOOM_MIN, never wider than the print. See MAP_ZOOM_MIN above for why
        // this is not simply the whole island every time.
        float span_z = s_map.z1 - s_map.z0, span_x = s_map.x1 - s_map.x0;
        float want = MAP_ZOOM_MIN;
        if (ob.has_pos) {
            float dz = fabsf(ob.pos.z - me.z), dx = fabsf(ob.pos.x - me.x) * MAP_ASPECT;
            want = fmaxf(want, fmaxf(dz, dx) * MAP_ZOOM_MARGIN);
        }
        float win_z = clampf(want, MAP_ZOOM_MIN, span_z);
        float win_x = win_z / MAP_ASPECT;
        float mz = ob.has_pos ? (me.z + ob.pos.z) * 0.5f : me.z;
        float mx = ob.has_pos ? (me.x + ob.pos.x) * 0.5f : me.x;
        c.wzc = clampf(mz, s_map.z0 + win_z * 0.5f, s_map.z1 - win_z * 0.5f);
        c.wxc = clampf(mx, s_map.x0 + win_x * 0.5f, s_map.x1 - win_x * 0.5f);
        c.s = MAP_W / win_z;
        // and the piece of the print that window is: scale then offset, exactly as world.vert
        // applies uv_xform, so the card shows that rectangle of the image and nothing else.
        uv = v4(win_z / span_z, win_x / span_x,
                (c.wzc - win_z * 0.5f - s_map.z0) / span_z,
                (c.wxc - win_x * 0.5f - s_map.x0) / span_x);
    } else {
        c.wxc = (s_map.sx0 + s_map.sx1) * 0.5f;
        c.wzc = (s_map.sz0 + s_map.sz1) * 0.5f;
        c.s = fminf(MAP_W * 0.84f / fmaxf(s_map.sz1 - s_map.sz0, 1.0f),
                    MAP_H * 0.84f / fmaxf(s_map.sx1 - s_map.sx0, 1.0f));
    }

    // ---- everything below goes through the viewmodel's lens and its depth slice ----
    Mat4 vp = camera_view_proj_lens(&g->cam, (float)INTERNAL_W / (float)INTERNAL_H, MAP_FOV, MAP_NEAR, MAP_FAR);
    gfx_set_view_proj(x, vp);
    gfx_depth_range(x, 0.0f, MAP_DEPTH);
    Material mat = material_default();
    mat.unlit = 0.80f;                       // paper you are holding is lit by the level, but not much
    mat.emissive = v3(0.03f, 0.028f, 0.022f);
    gfx_set_material(x, &mat);

    // the card itself: the print (the window of it worked out above), or the game's own paper
    // with the room drawn on it for a level that has no print
    gfx_draw(x, &x->quad, s_map.print_ok ? &s_map.print : (x->paper.tex ? &x->paper : &x->white),
             basis_m(v3_sub(c.o, v3_scale(c.up, MAP_H * 0.5f)),
                     v3_scale(c.right, MAP_W), v3_scale(c.up, MAP_H), c.n), PAPER_TINT, uv);
    if (!s_map.print_ok) draw_schematic(x, &c, g);

    float sx, sy;

    if (ob.route[0]) draw_route(x, &c, level_route(&g->level, ob.route));

    // the boat, which is where the run started and where it has to end
    for (int i = 0; i < g->level.nprops; i++) {
        if (strcmp(g->level.props[i].name, "boat")) continue;
        to_card(&c, g->level.props[i].pos.x, g->level.props[i].pos.z, &sx, &sy);
        if (!on_card(sx, sy)) break;
        card_quad(x, &c, sx, sy, 0.0090f, 0.0036f, 0.5f, INK, NULL, 3);
        card_quad(x, &c, sx, sy + 0.0032f, 0.0020f, 0.0062f, 0.5f, INK, NULL, 3);
        break;
    }

    // the other goons, in their own slot colours
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (i == g->local || !g->net.slots[i].active) continue;
        to_card(&c, g->players[i].c.pos.x, g->players[i].c.pos.z, &sx, &sy);
        if (!on_card(sx, sy)) continue;
        Vec4 t = g->net.slots[i].tint; t.w = 1.0f;
        card_ring(x, &c, sx, sy, 0.0050f, 0.0012f, INK, 4);
        card_quad(x, &c, sx, sy, 0.0062f, 0.0062f, PI * 0.25f, t, NULL, 5);
    }

    if (ob.has_pos) {
        to_card(&c, ob.pos.x, ob.pos.z, &sx, &sy);
        if (on_card(sx, sy)) draw_objective_mark(x, &c, sx, sy, (float)g->time);
    }

    to_card(&c, me.x, me.z, &sx, &sy);
    if (on_card(sx, sy)) draw_you(x, &c, sx, sy, PLAYER(g).c.yaw);

    draw_rose(x, &c, g->cam.yaw);

    Vec4 tint = g->net.slots[g->local].tint;
    draw_hands(x, &c, v4(0.55f, 0.38f, 0.28f, 1),
               v4(0.30f * tint.x, 0.20f * tint.y, 0.17f * tint.z, 1));

    gfx_set_material(x, NULL);
    gfx_depth_range(x, 0.0f, 1.0f);
    gfx_reset_view_proj(x);
}

// ---------------------------------------------------------------- the compass strip
//
// The map is optional and this is what makes it optional: a bearing tape and a distance, always
// on, costing eight ticks and two lines of text. Island north is world -X and island east is
// world +Z (assets/levels/island.txt says so at the top), so the tape is labelled by the island's
// own compass rather than by the world axes, and it agrees with the card.

#define STRIP_HALF_W   206.0f
#define STRIP_HALF_FOV (72.0f * DEG2RAD)

void map_draw_hud(Game *g) {
    if (g->state != GS_EXPLORE || g->cam.mode == CAM_SCENE) return;
    Objective ob = game_objective(g);
    if (!ob.has_pos) return;
    Gfx *x = &g->gfx;
    const float cx = INTERNAL_W * 0.5f, top = 8.0f, h = 19.0f;

    gfx_ui_rect(x, cx - STRIP_HALF_W, top, STRIP_HALF_W * 2, h, v4(0.04f, 0.04f, 0.05f, 0.34f));
    gfx_ui_rect(x, cx - STRIP_HALF_W, top + h - 1, STRIP_HALF_W * 2, 1, v4(0.55f, 0.52f, 0.46f, 0.45f));

    // The eight points of the island's compass, at their world bearings.
    static const struct { const char *name; float bearing; } POINTS[8] = {
        { "N",  -PI * 0.5f  }, { "NE", -PI * 0.25f }, { "E",  0.0f       }, { "SE", PI * 0.25f },
        { "S",   PI * 0.5f  }, { "SW",  PI * 0.75f }, { "W",  PI        }, { "NW", -PI * 0.75f },
    };
    for (int i = 0; i < 8; i++) {
        float rel = angle_wrap(POINTS[i].bearing - g->cam.yaw);
        if (fabsf(rel) > STRIP_HALF_FOV) continue;
        float px = cx + rel / STRIP_HALF_FOV * STRIP_HALF_W;
        bool cardinal = (i % 2) == 0;
        gfx_ui_rect(x, px, top + (cardinal ? 2 : 6), 1, cardinal ? 6 : 4, v4(0.8f, 0.78f, 0.72f, 0.7f));
        if (cardinal) {
            float w = gfx_ui_text_width(1.0f, POINTS[i].name);
            gfx_ui_text(x, px - w * 0.5f, top + 9, 1.0f, v4(0.86f, 0.84f, 0.78f, 0.85f), POINTS[i].name);
        }
    }

    // The objective's own mark, clamped to the ends of the tape with an arrow when it is behind
    // you: a compass that silently stops pointing the moment you turn round is worse than none.
    Vec3 d = v3_sub(ob.pos, PLAYER(g).c.pos); d.y = 0;
    float dist = v3_len(d);
    float rel = angle_wrap(atan2f(d.x, d.z) - g->cam.yaw);
    float px = cx + clampf(rel, -STRIP_HALF_FOV, STRIP_HALF_FOV) / STRIP_HALF_FOV * STRIP_HALF_W;
    Vec4 mark = v4(0.95f, 0.36f, 0.24f, 1);
    gfx_ui_rect(x, px - 1, top + 1, 3, h - 3, mark);
    if (fabsf(rel) > STRIP_HALF_FOV) {
        float s = rel > 0 ? 1.0f : -1.0f;
        for (int i = 0; i < 4; i++) gfx_ui_rect(x, px + s * (4 + i * 2), top + 3 + i, 2, h - 7 - i * 2, mark);
    }

    char line[96];
    snprintf(line, sizeof line, "%s   %d m", ob.text, (int)(dist + 0.5f));
    float w = gfx_ui_text_width(1.1f, line);
    gfx_ui_text(x, cx - w * 0.5f, top + h + 3, 1.1f, v4(0.92f, 0.88f, 0.78f, 0.92f), line);
    if (map_exists(g) && !s_map.ever_opened)
        gfx_ui_text(x, cx + STRIP_HALF_W + 8, top + 6, 1.0f, v4(0.72f, 0.70f, 0.64f, 0.75f), "M  map");
}
