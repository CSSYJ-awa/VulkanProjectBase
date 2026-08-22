#version 450

// 调试可视化顶点着色器：世界坐标 + 每顶点颜色（LINE_LIST）
layout(binding = 0) uniform VP
{
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main()
{
    gl_Position = ubo.proj * ubo.view * vec4(inPos, 1.0);
    fragColor = inColor;
}
