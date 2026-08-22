#version 450

// 调试可视化碎片着色器：直接输出每顶点颜色
layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(fragColor, 1.0);
}
