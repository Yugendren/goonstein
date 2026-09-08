// Immediate-mode widgets for the tool window (debugger, level editor, sprite editor panels).
// Every call both draws and handles input for this frame. Coordinates are in the window's UI pixels.
#pragma once
#include "gfx.h"
#include <stdbool.h>

typedef struct UiInput { float mx, my; bool down, pressed, released; float wheel; } UiInput;

typedef struct Ui {
    Gfx *g; UiInput in;
    int hot, active;          // widget ids under the mouse / being dragged
    int next_id;
    float scroll[8];          // scroll offsets for up to 8 lists
} Ui;

void ui_begin(Ui *ui, Gfx *g, UiInput in);   // resets ids; call once per frame before any widget
void ui_end(Ui *ui);

void  ui_label(Ui *ui, float x, float y, const char *text, Vec4 color);
void  ui_header(Ui *ui, float x, float y, const char *text);
bool  ui_button(Ui *ui, float x, float y, float w, float h, const char *text);          // true on click
bool  ui_toggle(Ui *ui, float x, float y, float w, float h, const char *text, bool *v); // true when changed
// Horizontal slider with label and value readout; returns true while the value changes.
bool  ui_slider(Ui *ui, float x, float y, float w, const char *label, float *v, float lo, float hi);
// Three sliders stacked for an rgb colour, with a swatch. Height used: 3 * 22.
bool  ui_color(Ui *ui, float x, float y, float w, const char *label, Vec3 *c, float hi);
// Scrollable list of strings in a box; returns true when the selection changes. list_id 0..7.
bool  ui_list(Ui *ui, int list_id, float x, float y, float w, float h, const char **items, int n, int *selected);
// Small number stepper: [-] value [+]; returns true when changed.
bool  ui_stepper(Ui *ui, float x, float y, const char *label, float *v, float step, float lo, float hi);
