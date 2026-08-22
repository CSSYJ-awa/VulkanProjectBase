#version 450
// postfx.vert —— 全屏后处理三角形（无顶点缓冲，由顶点 ID 生成）
layout(location = 0) out vec2 uv;

void main()
{
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
