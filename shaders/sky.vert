#version 450
layout(location = 0) out vec2 v_ndc;
void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_ndc = uv * 2.0 - 1.0;
    gl_Position = vec4(v_ndc.x, -v_ndc.y, 1.0, 1.0);   // far plane
}
