#version 450
// particle.vert —— 粒子点精灵顶点
layout(location = 0) in vec3 inPos;
layout(location = 1) in float inSize;
layout(location = 2) in vec4 inColor;

layout(binding = 0, std140) uniform ParticleUBO {
    mat4  uView;
    mat4  uProj;
    float uSizeScale;
    float uTexEnabled;
    float _pad0;
    float _pad1;
} ubo;

layout(location = 0) out vec4 vColor;
layout(location = 1) out float vFade;

void main()
{
    vec4 clip = ubo.uProj * ubo.uView * vec4(inPos, 1.0);
    gl_Position = clip;
    // 点大小（像素）：世界尺寸 × 视口高/2 × 透视 w（w = -viewZ）
    gl_PointSize = clamp(inSize * ubo.uSizeScale * clip.w, 1.0, 300.0);
    vColor = inColor;
    vFade  = 1.0;
}
