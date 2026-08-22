#version 450

// 3D 碎片着色器（v1.0.2）：
//   逐像素 N·L 漫反射 + 半球环境光 + 三平面映射材质 + 距离雾
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in float fragViewDepth;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushColor
{
    vec4 color;
} pc;

layout(binding = 0) uniform UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 lightDir;    // 世界空间光传播方向
    float ambient;    // 环境光强度
    vec3 lightColor;  // 光颜色 × 强度
    float _pad;
    vec4 fogColor;    // xyz=雾色  w=雾开关
    vec4 fogParams;   // x=fogStart y=fogEnd z=texScale w=texMix
    vec4 ambientUp;   // 半球环境光·上
    vec4 ambientDown; // 半球环境光·下
} ubo;

layout(binding = 1) uniform sampler2D uTex;   // 材质纹理（三平面映射）

void main()
{
    vec3 N = normalize(fragNormal);
    // lightDir 是光传播方向，取反得到"指向光源"的方向做漫反射
    vec3 L = normalize(-ubo.lightDir);
    float ndl = max(dot(N, L), 0.0);

    // 半球环境光：按法线朝上程度在 ambientDown/ambientUp 间插值
    float upFactor = N.y * 0.5 + 0.5;
    vec3 ambient = mix(ubo.ambientDown.rgb, ubo.ambientUp.rgb, upFactor) * ubo.ambient;

    vec3 lighting = ambient + ubo.lightColor * ndl;
    vec3 base = fragColor * lighting * pc.color.rgb;

    // 三平面映射材质（无需 UV）：按法线主轴权重采样 3 个投影面
    float texScale = ubo.fogParams.z;
    if (texScale > 0.001)
    {
        vec3 n = abs(N);
        vec3 cX = texture(uTex, fragPos.yz * texScale).rgb;
        vec3 cY = texture(uTex, fragPos.xz * texScale).rgb;
        vec3 cZ = texture(uTex, fragPos.xy * texScale).rgb;
        n = normalize(pow(n, vec3(4.0)));   // 平滑权重，减少接缝
        vec3 tex = cX * n.x + cY * n.y + cZ * n.z;
        // 纹理与顶点色按 texMix 混合（光照保持不变）
        base = mix(base, tex * lighting, ubo.fogParams.w);
    }

    // 距离雾：view 深度线性过渡到雾色
    if (ubo.fogColor.w > 0.001)
    {
        float d = -fragViewDepth;   // 深度为正
        float fog = smoothstep(ubo.fogParams.x, ubo.fogParams.y, d);
        base = mix(base, ubo.fogColor.rgb, clamp(fog, 0.0, 1.0));
    }

    outColor = vec4(base, 1.0);
}
