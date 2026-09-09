#include "daylight.h"
#include <math.h>

// A day in five keyframes. Colours are in the same ranges the hand-authored levels use
// (see assets/levels/glade.txt); the table wraps, so 20.5 -> 0.0 crosses midnight.
typedef struct DayKey {
    float hour;
    float sun_intensity; Vec3 sun_color;
    Vec3  sky_ambient, ground_ambient;
    Vec3  sky_zenith, sky_horizon, sky_ground;
    float sun_glow, stars;
    Vec3  lift;
} DayKey;

static const DayKey KEYS[] = {
    // night: deep blue sky, cool dim moonlight, full stars
    { 0.0f,  0.15f, {0.55f, 0.66f, 0.95f},
      {0.10f, 0.13f, 0.24f}, {0.030f, 0.032f, 0.045f},
      {0.010f, 0.018f, 0.055f}, {0.05f, 0.07f, 0.15f}, {0.010f, 0.010f, 0.020f},
      0.25f, 1.0f, {0.010f, 0.012f, 0.035f} },
    // pre-dawn: still night overhead, the first warmth creeping into the horizon
    { 4.0f,  0.15f, {0.62f, 0.66f, 0.90f},
      {0.14f, 0.15f, 0.26f}, {0.038f, 0.038f, 0.048f},
      {0.030f, 0.045f, 0.130f}, {0.26f, 0.18f, 0.22f}, {0.016f, 0.015f, 0.026f},
      0.40f, 0.75f, {0.012f, 0.012f, 0.034f} },
    // dawn: orange-pink horizon under a still-blue zenith, warm low sun
    { 6.5f,  0.65f, {1.00f, 0.72f, 0.48f},
      {0.30f, 0.28f, 0.34f}, {0.060f, 0.052f, 0.050f},
      {0.100f, 0.160f, 0.380f}, {0.95f, 0.55f, 0.38f}, {0.060f, 0.050f, 0.055f},
      0.90f, 0.15f, {0.012f, 0.008f, 0.020f} },
    // noon: white sun, light blue sky, clean shadows
    { 13.0f, 1.10f, {1.00f, 0.97f, 0.92f},
      {0.36f, 0.44f, 0.62f}, {0.100f, 0.100f, 0.090f},
      {0.160f, 0.340f, 0.680f}, {0.66f, 0.78f, 0.92f}, {0.150f, 0.170f, 0.190f},
      0.45f, 0.0f, {0.005f, 0.005f, 0.010f} },
    // golden hour: warm orange sun, purple-pink horizon
    { 18.5f, 0.85f, {1.00f, 0.62f, 0.34f},
      {0.32f, 0.26f, 0.30f}, {0.070f, 0.060f, 0.060f},
      {0.100f, 0.130f, 0.360f}, {0.92f, 0.42f, 0.40f}, {0.070f, 0.050f, 0.070f},
      1.00f, 0.10f, {0.012f, 0.008f, 0.024f} },
    // dusk: the last of the light going out, stars coming up
    { 20.5f, 0.30f, {0.85f, 0.55f, 0.55f},
      {0.18f, 0.17f, 0.30f}, {0.050f, 0.042f, 0.050f},
      {0.040f, 0.050f, 0.160f}, {0.35f, 0.20f, 0.30f}, {0.030f, 0.030f, 0.040f},
      0.55f, 0.55f, {0.012f, 0.010f, 0.030f} },
    // night falling: nearly the night key, so 20.5 -> 22 is the fade and the small hours are flat
    { 22.0f, 0.18f, {0.62f, 0.66f, 0.90f},
      {0.12f, 0.14f, 0.26f}, {0.035f, 0.036f, 0.048f},
      {0.014f, 0.022f, 0.070f}, {0.09f, 0.09f, 0.17f}, {0.014f, 0.014f, 0.024f},
      0.30f, 0.90f, {0.010f, 0.012f, 0.034f} },
};
#define NKEYS ((int)(sizeof KEYS / sizeof KEYS[0]))

// The night key light: dim, cool, from high up.
#define MOON_INTENSITY 0.15f
static const Vec3 MOON_COLOR = { 0.55f, 0.66f, 0.95f };

static float wrap24(float h) { h = fmodf(h, 24.0f); return h < 0 ? h + 24.0f : h; }

// Blend the two keyframes bracketing `hour`, smoothly and wrapping past midnight.
static DayKey key_at(float hour) {
    int i = NKEYS - 1;
    for (int k = 0; k < NKEYS; k++) if (hour >= KEYS[k].hour) i = k;
    int j = (i + 1) % NKEYS;
    float h0 = KEYS[i].hour, h1 = KEYS[j].hour;
    if (h1 <= h0) h1 += 24.0f;
    float h = hour < h0 ? hour + 24.0f : hour;
    float t = smoothstep((h - h0) / (h1 - h0));

    const DayKey *a = &KEYS[i], *b = &KEYS[j];
    DayKey r = *a;
    r.hour           = hour;
    r.sun_intensity  = lerpf(a->sun_intensity, b->sun_intensity, t);
    r.sun_color      = v3_lerp(a->sun_color, b->sun_color, t);
    r.sky_ambient    = v3_lerp(a->sky_ambient, b->sky_ambient, t);
    r.ground_ambient = v3_lerp(a->ground_ambient, b->ground_ambient, t);
    r.sky_zenith     = v3_lerp(a->sky_zenith, b->sky_zenith, t);
    r.sky_horizon    = v3_lerp(a->sky_horizon, b->sky_horizon, t);
    r.sky_ground     = v3_lerp(a->sky_ground, b->sky_ground, t);
    r.sun_glow       = lerpf(a->sun_glow, b->sun_glow, t);
    r.stars          = lerpf(a->stars, b->stars, t);
    r.lift           = v3_lerp(a->lift, b->lift, t);
    return r;
}

// Sun elevation in radians: 0 at 6h, 62 deg at 13h, 0 again at 20h, and a shallow
// negative arc through the night so the light keeps moving while it is down.
static float sun_elevation(float hour) {
    if (hour >= 6.0f && hour <= 20.0f) return 62.0f * DEG2RAD * sinf(PI * (hour - 6.0f) / 14.0f);
    float h = hour < 6.0f ? hour + 24.0f : hour;      // 20 .. 30
    return -18.0f * DEG2RAD * sinf(PI * (h - 20.0f) / 10.0f);
}

// Light direction (the direction the light travels, as in the `sun` level line) for an
// elevation above the horizon and a compass yaw.
static Vec3 dir_from(float yaw, float elev) {
    return v3(sinf(yaw) * cosf(elev), -sinf(elev), cosf(yaw) * cosf(elev));
}

Look daylight_apply(const Look *base, float hour) {
    Look lk = *base;
    hour = wrap24(hour);
    DayKey k = key_at(hour);

    // The author's sun direction is the bearing at noon; the sun swings 180 degrees
    // around it between sunrise and sunset.
    float noon_yaw = atan2f(base->sun_dir.x, base->sun_dir.z);
    float hd = hour < 1.0f ? hour + 24.0f : hour;     // keep the sweep continuous by day
    float yaw = noon_yaw + PI * (hd - 13.0f) / 14.0f;
    float elev = sun_elevation(hour);

    // Around the horizon the sun hands over to a high, cool moon so night stays readable.
    float day = smoothstep((elev + 6.0f * DEG2RAD) / (12.0f * DEG2RAD));
    Vec3 sun = dir_from(yaw, fmaxf(elev, 2.0f * DEG2RAD));
    Vec3 moon = dir_from(noon_yaw + PI, 60.0f * DEG2RAD);
    lk.sun_dir = v3_norm(v3_lerp(moon, sun, day));

    // ... and the key light fades to the moon's dim cool one with it, whatever the table says.
    lk.sun_intensity  = lerpf(MOON_INTENSITY, k.sun_intensity, day);
    lk.sun_color      = v3_lerp(MOON_COLOR, k.sun_color, day);
    lk.sky_ambient    = k.sky_ambient;
    lk.ground_ambient = k.ground_ambient;
    lk.sky_zenith     = k.sky_zenith;
    lk.sky_horizon    = k.sky_horizon;
    lk.sky_ground     = k.sky_ground;
    lk.sun_glow       = k.sun_glow;
    lk.stars          = k.stars;
    lk.lift           = k.lift;
    // Fog takes its colour from the horizon; its density and shape stay authored.
    lk.fog_color      = v3_scale(k.sky_horizon, 0.45f);
    return lk;
}
