// In-engine pixel sprite editor. Opens with `hollow --edit NAME [--size N]`.
// Mouse paints, keys switch tools; Ctrl+S saves straight into the game's sprite format.
#pragma once
#include "platform.h"
#include "gfx.h"
#include "pixio.h"

#define ED_UNDO 48

typedef enum EdTool { TOOL_PENCIL, TOOL_ERASER, TOOL_FILL, TOOL_PICK, TOOL_LINE } EdTool;

typedef struct EdSnapshot { int anim, dir, frame; PixFrame px; bool valid; } EdSnapshot;

typedef struct Editor {
    PixDoc doc;
    int anim, dir, frame;
    EdTool tool; int pal; uint32_t color;
    bool onion, mirror_x, playing, grid;
    float play_t;
    int zoom, cx, cy;                 // canvas placement
    bool painting; int last_x, last_y; bool stroke_open;
    int line_x0, line_y0; bool line_armed;
    EdSnapshot undo[ED_UNDO]; int undo_n; EdSnapshot redo[ED_UNDO]; int redo_n;
    char msg[160]; float msg_t;
    bool dirty;
    int hover_x, hover_y;             // pixel under the mouse, -1 if none
    int new_anim_pick;                // index into the preset anim-name list
    int def_fw, def_fh;
} Editor;

bool editor_init(Editor *e, const char *name, int frame_size);
void editor_shutdown(Editor *e);
void editor_tick(Editor *e, const Input *in, float mx, float my, float dt);
void editor_draw(Editor *e, Gfx *g);
