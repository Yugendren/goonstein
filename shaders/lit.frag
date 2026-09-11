#version 450
// Stylised lighting: banded sun, hemisphere ambient, point lights, rim, emissive, height fog.
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 2, binding = 1) uniform sampler2D shadow_map;   // sun depth, orthographic
layout(set = 3, binding = 0, std140) uniform Frame {
    vec4 cam_pos;
    vec4 sun_dir;          // xyz = direction the light travels, w = intensity
    vec4 sun_color;
    vec4 sky_ambient;      // rgb
    vec4 ground_ambient;   // rgb
    vec4 fog_color;        // rgb, a = density per metre
    vec4 fog_height;       // x = base height, y = falloff, z = sun scatter, w = start distance
    vec4 toon;             // x = band softness, y = shadow floor, z = rim power, w = time in seconds
    vec4 lights_pos[16];   // xyz, w = radius
    vec4 lights_color[16]; // rgb premultiplied by intensity
    ivec4 counts;          // x = light count
    mat4 sun_vp;           // world -> sun clip
    vec4 shadow;           // x = 1/map size, y = bias, z = strength (0 = off), w = fade distance from the map edge
};
layout(set = 3, binding = 1, std140) uniform Material {
    vec4 tint;             // rgb multiply, a = alpha
    vec4 emissive;         // rgb added, a = unlit amount (sprites keep their own colours)
    vec4 rim;              // rgb, a = strength
    vec4 water;            // x = 1 shades this surface as sea; yz = the coast texture's world origin in xz, w = 1 / its span
    vec4 flatc;   // rgb = the bound texture's mean colour, a = how far to blend toward it ("flat" is a reserved GLSL keyword)
};
layout(location = 0) in vec3 v_wpos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_color;
layout(location = 0) out vec4 o_color;

float band(float x, float edge, float soft) { return smoothstep(edge - soft, edge + soft, x); }

void main() {
    // At flatc.a 1 the texture contributes nothing but its silhouette and its average colour,
    // which is the Roblox/Peak reading of a material -- everything a solid, and the tint and
    // vertex colour still telling one thing from another.
    // Half a mip level toward sharper. Textures have mips now (see gfx_texture_create), and without
    // this the ground detail -- tiled at a few repeats per metre and multiplied over the biome
    // colour -- averages to its own mean by the middle distance and the beach goes flat cream. That
    // is correct mip behaviour and it is not the look this game had. The bias lives here rather
    // than on the sampler because Metal's sampler has no LOD bias at all; a fetch bias is the one
    // spelling all three backends understand.
    vec4 s = texture(tex, v_uv, -0.5);
    // The ground (material.water == 2, set by terrain_draw). A detail map at half a metre repeats
    // nine hundred times across this island and the eye finds the period long before the fog does.
    // Multiplying in a second, much slower sample of the SAME map -- normalised by its own mean,
    // which push_material already hands us as flatc.rgb -- kills the period without a second
    // texture, a second sampler or a second pass: one extra fetch, on the one surface that fills
    // the bottom of every frame. The offset keeps the two samples from lining up at the origin.
    if (water.x > 1.5) {
        // +2 mip levels on the slow sample. It is a low-frequency term by construction, so the
        // detail is worth nothing, and a sixteenth-size fetch stays in cache instead of thrashing
        // it: the full-resolution version cost 0.7 ms on a summit view where the ground fills the
        // frame, which is most of what this whole pass saved.
        vec3 broad = texture(tex, v_uv * 0.137 + vec2(0.37, 0.61), 2.0).rgb / max(flatc.rgb, vec3(0.04));
        broad = vec3(1.0) + (broad - vec3(1.0)) * 1.9;   // the map's own contrast is too polite to read at sixteen metres a tile
        s.rgb *= mix(vec3(1.0), broad, 0.75);
    }
    // Alpha-tested cut-out, which is what makes a leaf card a leaf. Opacity is the texture's alpha
    // times the material's, and deliberately NOT the vertex colour's: that channel now carries the
    // wind weight the vertex shaders read (see world.vert). It was 1 on every mesh in the game, so
    // nothing changes colour or opacity by this; it only frees the channel.
    vec4 t = vec4(mix(s.rgb, flatc.rgb, flatc.a) * v_color.rgb * tint.rgb, s.a * tint.a);
    if (t.a < 0.5) discard;
    t.rgb = pow(t.rgb, vec3(2.2));   // albedo is sRGB; light in linear
    vec3 n = normalize(v_normal);
    vec3 view = cam_pos.xyz - v_wpos;
    float dist = length(view);
    vec3 v = view / max(dist, 1e-4);

    // The sea. Two crossing ripple trains whose normal is their analytic gradient: four sines and
    // no vertex work. The coast comes from the bound texture, looked up by world position: red is
    // how deep the water is (0 at the waterline, 1 past the shelf) and green is the foam mask along
    // that waterline. The sea's own colour is the material tint.
    //
    // The ripples are read three ways, and each is deliberately weak: the sky reflection barely
    // follows them (a fresnel taken from the full normal swings between sky and deep water on every
    // crest, and a near-level view then comes out in hard stripes), the wave height lifts the colour
    // a little, and the foam sits where the coast texture says the water is shallow. There is no
    // specular highlight at all: with a high sun the half vector sits near the water's own normal,
    // so any exponent smears the highlight along whole crests instead of glittering.
    float sea_shallow = 0.0, sea_foam = 0.0, sea_swell = 0.0;
    bool sea = water.x > 0.5 && water.x < 1.5;   // 1 = the sea, 2 = the ground (above)
    if (sea) {
        float wt = toon.w;
        vec2 p = v_wpos.xz;
        vec2 d1 = normalize(vec2(0.86, 0.51)), d2 = normalize(vec2(-0.42, 0.91));
        // Each train's phase is bent by the other one, or the crests come out as infinite parallel
        // lines and the sea looks like corrugated iron.
        float a1 = dot(p, d1) * 0.55 + wt * 1.10 + sin(dot(p, d2) * 0.21 - wt * 0.35) * 2.0;
        float a2 = dot(p, d2) * 1.30 - wt * 1.70 + sin(dot(p, d1) * 0.37 + wt * 0.50) * 1.6;
        vec2 cuv = clamp((v_wpos.xz - water.yz) * water.w, 0.0, 1.0);
        vec4 coast = texture(tex, cuv);
        sea_shallow = 1.0 - coast.r;
        // Waves flatten with depth and with distance: at 100 m a ripple is smaller than a pixel and
        // keeping it only buys shimmer.
        float calm = mix(1.0, 0.30, sea_shallow) * clamp(1.0 - (dist - 20.0) / 90.0, 0.12, 1.0);
        vec2 slope = (d1 * cos(a1) * 0.045 + d2 * cos(a2) * 0.028) * calm;
        n = normalize(vec3(-slope.x, 1.0, -slope.y));
        sea_foam = coast.g * smoothstep(0.15, 0.55, 0.5 + 0.5 * sin(a2 * 0.8 + wt * 0.9));
        // The swell needs to show under a low sun too, where a tilted normal changes almost
        // nothing: the wave height itself lifts and drops the colour a little.
        sea_swell = (0.5 * sin(a1) + 0.5 * sin(a2)) * calm;
        t = vec4(pow(tint.rgb, vec3(2.2)) * mix(1.0, 2.3, sea_shallow * sea_shallow), 1.0);   // shallow water shows the sand under it
    }

    // Sun: two soft bands, never fully black
    float ndl = dot(n, -sun_dir.xyz);
    // The sea is shaded smoothly: run ripples through the toon bands and every wave becomes a hard
    // edged stripe.
    float lit = sea ? toon.y + (1.0 - toon.y) * clamp(ndl * 0.5 + 0.5, 0.0, 1.0)
                    : toon.y + (1.0 - toon.y) * (0.65 * band(ndl, 0.05, toon.x) + 0.35 * band(ndl, 0.5, toon.x));
    // Sun shadow: 3x3 tap compare in the map, hard-edged to match the bands, fading at the map's rim
    float sh = 1.0;
    if (shadow.z > 0.0) {
        vec4 sc = sun_vp * vec4(v_wpos + n * shadow.y * 2.0, 1.0);
        vec3 sp = sc.xyz / sc.w;
        vec2 suv = sp.xy * 0.5 + 0.5; suv.y = 1.0 - suv.y;
        float edge = min(min(suv.x, 1.0 - suv.x), min(suv.y, 1.0 - suv.y));
        if (edge > 0.0 && sp.z < 1.0) {
            float bias = shadow.y * (1.5 - clamp(ndl, 0.0, 1.0));
            float occ = 0.0;
            for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
                float d = texture(shadow_map, suv + vec2(i, j) * shadow.x).r;
                occ += (sp.z - bias > d) ? 1.0 : 0.0;
            }
            occ /= 9.0;
            float fade = clamp(edge / shadow.w, 0.0, 1.0);
            sh = 1.0 - occ * shadow.z * fade;
        }
    }
    lit = mix(toon.y, lit, sh);   // shadowed surfaces fall to the shadow floor
    vec3 light = sun_color.rgb * sun_dir.w * lit;
    // Hemisphere ambient, a little lower in shadow so shade reads even under a bright sky
    light += mix(ground_ambient.rgb, sky_ambient.rgb, n.y * 0.5 + 0.5) * (0.4 + 0.6 * sh);
    // Point lights: smooth falloff, half-lambert so the back of things still catch colour
    for (int i = 0; i < counts.x; i++) {
        vec3 d = lights_pos[i].xyz - v_wpos;
        float ld = length(d);
        float att = clamp(1.0 - ld / lights_pos[i].w, 0.0, 1.0);
        att = att * att;
        float nl = dot(n, d / max(ld, 1e-4)) * 0.5 + 0.5;
        light += lights_color[i].rgb * att * nl;
    }
    vec3 lit_c = t.rgb * light + emissive.rgb;
    vec3 flat_c = t.rgb * (1.1 + emissive.rgb) * (0.75 + 0.25 * clamp(dot(sky_ambient.rgb + sun_color.rgb * sun_dir.w, vec3(0.33)), 0.0, 1.0));
    vec3 c = mix(lit_c, flat_c, emissive.a);
    // Rim: brightest where the surface turns away from the camera and faces the sun a little
    float fres = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), toon.z);
    c += rim.rgb * rim.a * fres * (0.4 + 0.6 * clamp(ndl + 0.5, 0.0, 1.0));

    if (sea) {
        // Water is mostly the sky, more of it the flatter you look across it. The fresnel is taken
        // from a normal only part of the way to the ripples: from the full one, a near-level
        // view swings between sky and deep water on every crest and the sea comes out in stripes.
        float f = pow(1.0 - clamp(dot(normalize(mix(vec3(0.0, 1.0, 0.0), n, 0.08)), v), 0.0, 1.0), 3.0);
        c = mix(c, fog_color.rgb + sky_ambient.rgb * 0.5, (0.12 + 0.50 * f) * (1.0 - sea_shallow * 0.7));
        c *= 1.0 + sea_swell * 0.09;
        c += vec3(0.75, 0.85, 0.90) * sea_foam * 0.30;
    }

    // Fog: distance plus height, tinted toward the sun when looking into it
    float fd = max(dist - fog_height.w, 0.0);
    float f = 1.0 - exp(-fd * fog_color.a);
    float h = exp(-max(v_wpos.y - fog_height.x, 0.0) * fog_height.y);
    f = clamp(f * h, 0.0, 1.0);
    float scatter = pow(clamp(dot(-v, sun_dir.xyz), 0.0, 1.0), 6.0) * fog_height.z;
    vec3 fc = fog_color.rgb + sun_color.rgb * scatter;
    o_color = vec4(mix(c, fc, f), t.a);
}
