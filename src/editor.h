// In-engine pixel sprite editor. Opens with `hollow --edit NAME [--size N]`.
// Mouse paints, keys switch tools; Ctrl+S saves straight into the game's sprite format.
#pragma once
#include "platform.h"
#include "gfx.h"
#include "pixio.h"

#define ED_UNDO 48

typedef enum EdTool { TOOL_PENCIL, TOOL_ERASER, TOOL_FILL, TOOL_PICK, TOOL_LINE,
                       TOOL_RECT, TOOL_ELLIPSE, TOOL_SELECT, TOOL_LIGHTEN, TOOL_DARKEN } EdTool;

typedef struct EdSnapshot { int anim, dir, frame; PixFrame px; bool valid; } EdSnapshot;

typedef struct Btn { float x, y, w, h; } Btn;

// Responsive layout, rebuilt whenever the window size (or the document) changes. Drawing and
// hit-testing both read it, so a click always lands on the thing that was drawn there.
typedef struct EdLayout {
    float w, h;                                          // window size this was built for, in points
    float title_y, help_y, msg_y, foot_y;                // text rows
    float canvas_x, canvas_y, canvas_w, canvas_h;        // area the pixel canvas is centred in
    float panel_x, panel_w, panel_top, panel_bot;        // right-hand panel
    float anim_x, anim_y, anim_w, anim_h; int anim_cols, anim_max;
    float tool_x, tool_y, tool_sz, tool_pitch; int tool_cols;
    Btn   size_minus, size_plus; float size_val_x;       // brush-size stepper
    float pal_x, pal_y, pal_sz, pal_pitch; int pal_cols;
    float ramp_x, ramp_y, ramp_w, ramp_h, ramp_pitch;
    float dir_x, dir_y, dir_w, dir_h, dir_pitch;
    float frames_label_y, frames_x, frames_y, frames_sz, frames_pitch; int frames_cols, frames_max;
    float act_x, act_y, act_w, act_h, act_pitch_y; int act_cols;
    float prev_label_y, prev_x, prev_y; int prev_zoom; bool prev_on, prev_thumbs;
} EdLayout;

typedef struct Editor {
    PixDoc doc;
    int anim, dir, frame;
    EdTool tool; int pal; uint32_t color;
    bool onion, mirror_x, playing, grid;
    float play_t;
    int zoom, cx, cy;                 // canvas placement
    bool painting; int last_x, last_y; bool stroke_open;
    int line_x0, line_y0; bool line_armed;
    int brush_size;                                   // 1..4, pencil / eraser / shade tools
    bool shade_visited[PIX_MAX_SIZE * PIX_MAX_SIZE];   // per-stroke, so lighten/darken don't repeat a pixel
    // rectangle / ellipse drag
    bool shape_drag, shape_filled; int shape_x0, shape_y0, shape_x1, shape_y1;
    // rectangle select / move
    bool sel_on, sel_creating, sel_moving;
    int sel_x0, sel_y0, sel_x1, sel_y1;                // normalised (x0<=x1, y0<=y1)
    int sel_start_x, sel_start_y;                      // press point while creating a selection
    int sel_move_press_x, sel_move_press_y, sel_move_dx, sel_move_dy;
    float ants_t;                                      // marching-ants animation clock
    // right mouse: drag erases, click (<=8px movement) picks the colour under the cursor
    bool rmb_prev, rmb_moved; float rmb_press_mx, rmb_press_my;
    bool rstroke_open; int rlast_x, rlast_y;
    bool lmb_prev;                                     // for detecting the release edge
    EdSnapshot undo[ED_UNDO]; int undo_n; EdSnapshot redo[ED_UNDO]; int redo_n;
    char msg[160]; float msg_t;
    bool dirty;
    int hover_x, hover_y;             // pixel under the mouse, -1 if none
    int new_anim_pick;                // index into the preset anim-name list
    int def_fw, def_fh;
    float win_w, win_h;               // tool-window size in points (editor_set_size)
    EdLayout lay;
} Editor;

bool editor_init(Editor *e, const char *name, int frame_size);
void editor_shutdown(Editor *e);
// Window size in UI points; call before tick/draw. The layout flows to fit (see layout in editor.c).
void editor_set_size(Editor *e, float w, float h);
void editor_tick(Editor *e, const Input *in, float mx, float my, float dt);
void editor_draw(Editor *e, Gfx *g);
