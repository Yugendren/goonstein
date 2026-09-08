#version 450
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 3, binding = 0, std140) uniform Frag { vec4 fog_color; };
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in float v_fog;
layout(location = 0) out vec4 o_color;
void main() {
    vec4 t = texture(tex, v_uv) * v_color;
    if (t.a < 0.5) discard;
    o_color = vec4(mix(t.rgb, fog_color.rgb, v_fog), t.a);
}
