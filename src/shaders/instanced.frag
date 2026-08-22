#version 450
// instanced.frag —— 实例化渲染光照
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec3 vWorldPos;

layout(binding = 0, std140) uniform InstanceUBO {
    mat4  uView;
    mat4  uProj;
    vec3  uLightDir;
    float uAmbient;
    vec3  uLightColor;
    float _pad;
} ubo;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-ubo.uLightDir);
    float ndl = max(dot(N, L), 0.0);
    vec3 lighting = vec3(ubo.uAmbient) + ubo.uLightColor * ndl;
    outColor = vec4(vColor * lighting, 1.0);
}
