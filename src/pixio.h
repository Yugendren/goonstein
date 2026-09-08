// Pixel sprite documents for the in-engine editor: frames in memory, PNG sheets and sprite
// definition files on disk, in the same format the game loads (see sprite.h: "dircol" sheets,
// directions across columns in the order down, up, left, right; frames down the rows).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PIX_MAX_SIZE   64
#define PIX_MAX_FRAMES 16
#define PIX_MAX_ANIMS  16
#define PIX_MAX_CONTACT 8

typedef struct PixFrame { uint32_t px[PIX_MAX_SIZE * PIX_MAX_SIZE]; } PixFrame;   // RGBA8 little-endian (0xAABBGGRR); row stride is always PIX_MAX_SIZE

typedef struct PixAnim {
    char name[32];
    int  fw, fh;                 // frame size in pixels
    int  ndirs;                  // 1 or 4
    int  nframes;
    float fps; bool loop;
    int  contact[PIX_MAX_CONTACT]; int ncontact;
    PixFrame *frames;            // ndirs * PIX_MAX_FRAMES, index dir * PIX_MAX_FRAMES + frame; heap
} PixAnim;

typedef struct PixDoc {
    char name[32];               // character name; files are assets/sprites/own/NAME.txt and NAME_ANIM.png
    float size;                  // world height in metres of one frame
    PixAnim anims[PIX_MAX_ANIMS]; int nanims;
} PixDoc;

void pix_doc_init(PixDoc *d, const char *name);
void pix_doc_free(PixDoc *d);
// Add an animation with empty transparent frames. Returns its index or -1.
int  pix_anim_add(PixDoc *d, const char *name, int fw, int fh, int ndirs, int nframes, float fps, bool loop);
void pix_anim_remove(PixDoc *d, int idx);
PixFrame *pix_frame(PixAnim *a, int dir, int frame);
bool pix_anim_insert_frame(PixAnim *a, int at, bool duplicate_previous);   // in every direction
bool pix_anim_delete_frame(PixAnim *a, int at);

// Save: writes assets/sprites/own/NAME_<anim>.png for every anim and assets/sprites/own/NAME.txt,
// plus assets/characters/NAME.txt binding every anim by name. `asset_dir` is HOLLOW_ASSET_DIR.
bool pix_doc_save(const PixDoc *d, const char *asset_dir);
// Load a previously saved document by name (parses the .txt and decodes the sheets). False if missing.
bool pix_doc_load(PixDoc *d, const char *asset_dir, const char *name);
// Import an arbitrary sheet PNG (e.g. from a pack) as an anim: cols x rows grid of fw x fh frames,
// with directions across columns when dircol is true. Returns anim index or -1.
int  pix_anim_import_sheet(PixDoc *d, const char *png_path, const char *anim_name, int fw, int fh, bool dircol, float fps, bool loop);
