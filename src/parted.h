// Part editor: the simplest CAD, inside the game. Stack boxes, cylinders, spheres, wedges and
// your OBJ exports on a workbench next to the hero, move them with the mouse in the game window,
// size, turn and colour them in the tool window, and save the result as assets/models/own/NAME.part,
// which then works as a prop (environment editor) or a character part (builder).
#pragma once
#include "widgets.h"
#include "part.h"

#define PE_MAX_FILES 48
#define PE_UNDO 12

typedef struct PartEd {
    PartDoc doc; char name[40]; int sel; bool dirty, snap;
    PartDoc undo[PE_UNDO]; int undo_n;
    char obj_files[PE_MAX_FILES][160], obj_names[PE_MAX_FILES][48]; int nobj;      // imports to insert
    char part_files[PE_MAX_FILES][160], part_names[PE_MAX_FILES][48]; int nparts;  // saved parts to open
    float scroll; char msg[160]; float msg_t; bool name_focus;
    bool dragging; Vec3 drag_off; float drag_y;
} PartEd;

enum { PE_REBUILD = 1, PE_SAVE = 2 };

void parted_init(PartEd *e);
void parted_init_files(PartEd *e);   // rescan the folders (after a save)
void parted_open(PartEd *e);
int  parted_panel(PartEd *e, Ui *ui, const Input *keys, float w, float h);
// Mouse in the game window: `hit` is where the cursor meets the bench plane (bench-relative).
// Left-drag moves the selected shape; a click on empty bench deselects. Returns PE_* flags.
int  parted_world(PartEd *e, const Input *in, Vec3 hit, bool hit_ok);
void parted_push_undo(PartEd *e);
