#version 450
// skybox.vert —— 全屏天空背景三角形（无顶点缓冲）
layout(location = 0) out vec2 uv;

void main()
{
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
