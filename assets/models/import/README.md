# Drop models here

Any `.obj` (with its `.mtl`), `.glb` or `.gltf` placed in this folder (or anywhere under `assets/models/`) shows up in the
environment editor's palette under a category named after its folder, with no other steps.
Scanned or downloaded props go here; rigged characters go in `assets/models/kaykit/` (or any folder
you point the character builder at) and are listed by the builder instead.

Keep the licence of every file you add in `ASSETS.md`. Only CC0, OFL, MIT or your own work.

OBJ from CAD: export with materials so each body keeps its colour (Kd in the .mtl). Files in
millimetres are scaled to metres automatically. `cad_lamp_post.obj` is a generated example.
