#version 450
// instanced.vert —— 实例化渲染（大量同模型）
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in mat4 inModel;     // per-instance
layout(location = 7) in vec4 inInstColor; // per-instance

layout(binding = 0, std140) uniform InstanceUBO {
    mat4  uView;
    mat4  uProj;
    vec3  uLightDir;
    float uAmbient;
    vec3  uLightColor;
    float _pad;
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec3 vWorldPos;

void main()
{
    vec4 world = inModel * vec4(inPos, 1.0);
    gl_Position = ubo.uProj * ubo.uView * world;
    vNormal   = mat3(transpose(inverse(inModel))) * inNormal;
    vColor    = inColor * inInstColor.rgb;
    vWorldPos = world.xyz;
}
