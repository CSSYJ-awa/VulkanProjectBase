#version 450
// shadow.vert —— 阴影深度生成（光空间投影）
layout(binding = 0, std140) uniform LightVP {
    mat4 uLightVP;
} ubo;

layout(location = 0) in vec3 inPos;

layout(push_constant) uniform Model {
    mat4 uModel;
} pc;

void main()
{
    gl_Position = ubo.uLightVP * pc.uModel * vec4(inPos, 1.0);
}
