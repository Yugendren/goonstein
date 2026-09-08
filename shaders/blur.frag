#version 450
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 3, binding = 0, std140) uniform P { vec4 dir; };  // xy = step in uv
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() {
    const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(tex, v_uv).rgb * w[0];
    for (int i = 1; i < 5; i++) {
        c += texture(tex, v_uv + dir.xy * float(i)).rgb * w[i];
        c += texture(tex, v_uv - dir.xy * float(i)).rgb * w[i];
    }
    o_color = vec4(c, 1.0);
}
