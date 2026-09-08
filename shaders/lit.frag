#version 450
// Stylised lighting: banded sun, hemisphere ambient, point lights, rim, emissive, height fog.
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 3, binding = 0, std140) uniform Frame {
    vec4 cam_pos;
    vec4 sun_dir;          // xyz = direction the light travels, w = intensity
    vec4 sun_color;
    vec4 sky_ambient;      // rgb
    vec4 ground_ambient;   // rgb
    vec4 fog_color;        // rgb, a = density per metre
    vec4 fog_height;       // x = base height, y = falloff, z = sun scatter, w = start distance
    vec4 toon;             // x = band softness, y = shadow floor, z = rim power, w = unused
    vec4 lights_pos[16];   // xyz, w = radius
    vec4 lights_color[16]; // rgb premultiplied by intensity
    ivec4 counts;          // x = light count
};
layout(set = 3, binding = 1, std140) uniform Material {
    vec4 tint;             // rgb multiply, a = alpha
    vec4 emissive;         // rgb added, a = unlit amount (sprites keep their own colours)
    vec4 rim;              // rgb, a = strength
};
layout(location = 0) in vec3 v_wpos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_color;
layout(location = 0) out vec4 o_color;

float band(float x, float edge, float soft) { return smoothstep(edge - soft, edge + soft, x); }

void main() {
    vec4 t = texture(tex, v_uv) * v_color * tint;
    if (t.a < 0.5) discard;
    t.rgb = pow(t.rgb, vec3(2.2));   // albedo is sRGB; light in linear
    vec3 n = normalize(v_normal);
    vec3 view = cam_pos.xyz - v_wpos;
    float dist = length(view);
    vec3 v = view / max(dist, 1e-4);

    // Sun: two soft bands, never fully black
    float ndl = dot(n, -sun_dir.xyz);
    float lit = toon.y + (1.0 - toon.y) * (0.65 * band(ndl, 0.05, toon.x) + 0.35 * band(ndl, 0.5, toon.x));
    vec3 light = sun_color.rgb * sun_dir.w * lit;
    // Hemisphere ambient
    light += mix(ground_ambient.rgb, sky_ambient.rgb, n.y * 0.5 + 0.5);
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

    // Fog: distance plus height, tinted toward the sun when looking into it
    float fd = max(dist - fog_height.w, 0.0);
    float f = 1.0 - exp(-fd * fog_color.a);
    float h = exp(-max(v_wpos.y - fog_height.x, 0.0) * fog_height.y);
    f = clamp(f * h, 0.0, 1.0);
    float scatter = pow(clamp(dot(-v, sun_dir.xyz), 0.0, 1.0), 6.0) * fog_height.z;
    vec3 fc = fog_color.rgb + sun_color.rgb * scatter;
    o_color = vec4(mix(c, fc, f), t.a);
}
