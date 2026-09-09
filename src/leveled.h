// Environment editor: place kit pieces, lights and emitters in the live world, tune the look,
// save back to the level file. Runs on the game window (fly camera, ghost placement) with its
// controls in the tool window.
#pragma once
#include "level.h"
#include "camera.h"
#include "platform.h"
#include "gfx.h"
#include "props.h"
#include "widgets.h"
#include "terrain.h"

#define KIT_MAX 160
#define ED_UNDO_LEVELS 8

typedef struct KitPiece {
    char category[16], name[32], file[128];
    float scale, collide; Vec3 glow;
    bool has_light; Vec3 light_color; float light_radius, light_intensity, light_flicker;
} KitPiece;

typedef enum EdTool2 { LT_SELECT, LT_PIECE, LT_LIGHT, LT_EMITTER } EdTool2;

typedef struct LevelEd {
    bool open; int tab;
    KitPiece kit[KIT_MAX]; int nkit;
    char categories[10][16]; int ncat; int cat;
    int list_sel; int piece;            // selection in the category list, and the kit index it maps to
    EdTool2 tool;
    float ghost_yaw, ghost_scale; bool ghost_collide, snap;
    Vec3 ghost_pos; bool ghost_valid;
    int sel_prop, sel_light, sel_emitter; bool dragging; Vec3 drag_offset;
    Vec3 cam_pos; float cam_yaw, cam_pitch; float cam_speed;
    Level *undo[ED_UNDO_LEVELS]; int undo_n;   // heap snapshots
    struct TerrainSnap *tundo[ED_UNDO_LEVELS];  // matching terrain heights/colours (NULL when none)
    Terrain *tr;                                // terrain being edited while open (set by leveled_tick)
    bool dirty; char msg[160]; float msg_t;
    Vec3 light_color; float light_radius, light_intensity;   // defaults for the light tool
    // terrain sculpting
    int tbrush;                          // TerrainBrush, or 5 = scatter, 6 = clear props
    float tradius, tstrength; Vec3 tpaint; int tpaint_sel;
    float snow_h, rock_slope; int scatter_cat; float scatter_density; float scatter_accum;
    bool sculpting;
    float pscroll[3], pcontent[3];       // panel scroll offset and content height per tab
    bool wheel_in_list;
} LevelEd;

bool leveled_init(LevelEd *e, const char *kit_path);
void leveled_shutdown(LevelEd *e);
void leveled_open(LevelEd *e, const Level *lv, const Camera *cam);   // enter editing from the current camera
// Game-window side: fly camera, ghost, picking, placement. mx/my are the game window's UI mouse.
void leveled_tick(LevelEd *e, Level *lv, Terrain *tr, Camera *cam, const Input *in, float mx, float my, float dt, Gfx *g, PropCache *pc);
void leveled_draw_world(LevelEd *e, const Level *lv, Gfx *g, PropCache *pc);
// Tool-window side: the control panel (draws with widgets into the current UI target).
void leveled_panel(LevelEd *e, Level *lv, Terrain *tr, Ui *ui, float w, float h);
bool leveled_save(LevelEd *e, Level *lv, Terrain *tr);
