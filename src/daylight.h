// Time of day: turns an hour of the clock into a level Look. A level with `daytime H`
// runs its authored look through daylight_apply once per load (and whenever the hour
// changes), so one level file can be dawn, noon, golden hour or night.
#pragma once
#include "level.h"

// Copy of *base with the sun, ambient, sky, fog colour, stars and lift set for `hour`
// (0..24, wrapped). Everything else -- fog density and shape, toon, grade, pixel -- is
// kept from *base, as is the sun's compass direction: the level author's yaw is taken
// as the sun's bearing at noon and swept east to west around it.
Look daylight_apply(const Look *base, float hour);
