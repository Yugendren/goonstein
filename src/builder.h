// Character builder: pick a rigged model, switch its parts on and off, borrow parts from the other
// rigged files onto the same skeleton, recolour its paint colours, choose the animation set, and
// save it as assets/characters/NAME.txt. The game shows the result live on the hero while the
// panel is open.
#pragma once
#include "widgets.h"
#include "charmodel.h"

#define BLD_MAX_FILES 48
#define BLD_MAX_PARTS 64
#define BLD_MAX_PAL   24

typedef struct Builder {
    char files[BLD_MAX_FILES][160], names[BLD_MAX_FILES][48]; int nfiles, file_sel;
    CharSpec spec; char name[40];
    const char *parts[BLD_MAX_PARTS]; int nparts;      // node names owned by the live model
    char part_files[BLD_MAX_FILES][160], part_names[BLD_MAX_FILES][48]; int npart_files, part_file_sel;   // OBJ / model files to attach
    int bone_sel, attach_sel;
    int borrow_file_sel;                               // which other rigged file we are picking parts from (-1 none)
    char borrow_parts[BLD_MAX_PARTS][48]; int nborrow_parts;   // its mesh node names, read straight from the file
    ModelColor pal[BLD_MAX_PAL]; int npal, pal_sel;
    int style, clip_sel, tab; float scroll;
    char msg[160]; float msg_t;
    bool name_focus;
} Builder;

enum { BLD_RELOAD = 1, BLD_RECOLOR = 2, BLD_HIDE = 4, BLD_PLAY_CLIP = 8, BLD_SAVE = 16, BLD_USE = 32, BLD_OPEN_SPRITE = 64, BLD_ATTACH = 128 };

void builder_init(Builder *b);                                        // finds rigged models
void builder_open(Builder *b, const CharSpec *current, const char *name);
void builder_model_loaded(Builder *b, const Model *m);               // refresh parts and palette after a (re)load
// Draw the panel and handle its input. Returns BLD_* flags telling the game what to apply.
int  builder_panel(Builder *b, Ui *ui, const Input *keys, float w, float h, const Model *m);
