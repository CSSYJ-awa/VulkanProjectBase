#version 450
// particle.frag —— 粒子点精灵（软圆 alpha + 可选纹理）
layout(location = 0) in vec4 vColor;
layout(location = 1) in float vFade;

layout(binding = 0, std140) uniform ParticleUBO {
    mat4  uView;
    mat4  uProj;
    float uSizeScale;
    float uTexEnabled;
    float _pad0;
    float _pad1;
} ubo;

layout(binding = 1) uniform sampler2D uTex;

layout(location = 0) out vec4 outColor;

void main()
{
    vec2 d = gl_PointCoord - vec2(0.5);
    float dist = length(d) * 2.0;
    if (dist > 1.0) discard;

    float a = 1.0 - dist;            // 软圆
    vec4 tex = texture(uTex, gl_PointCoord);
    float useTex = ubo.uTexEnabled > 0.5 ? 1.0 : 0.0;

    vec3 rgb = mix(vec3(1.0), tex.rgb, useTex);
    float alpha = mix(a, a * tex.a, useTex);

    outColor = vec4(vColor.rgb * rgb, vColor.a * alpha);
}
