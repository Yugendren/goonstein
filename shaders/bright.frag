#version 450
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 3, binding = 0, std140) uniform P { vec4 params; };  // x = threshold, y = soft knee
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() {
    vec3 c = texture(tex, v_uv).rgb;
    float l = max(max(c.r, c.g), c.b);
    float k = params.y;
    float soft = clamp(l - params.x + k, 0.0, 2.0 * k);
    soft = soft * soft / (4.0 * k + 1e-4);
    float w = max(soft, l - params.x) / max(l, 1e-4);
    o_color = vec4(c * w, 1.0);
}
