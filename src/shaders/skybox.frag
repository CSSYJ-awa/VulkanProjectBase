#version 450
// skybox.frag —— 程序化天空：渐变 + 太阳光斑
layout(binding = 0, std140) uniform SkyUBO {
    mat4 uViewRotInv;   // 相机旋转逆
    vec4 uTopColor;
    vec4 uBottomColor;
    vec4 uSunColor;
    vec4 uParams;       // x=sunYaw y=sunPitch z=sunIntensity w=horizonSpread
    vec4 uScreen;       // x=aspect
} ubo;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main()
{
    vec2 ndc = uv * 2.0 - 1.0;
    // 屏幕方向 → 视图方向 → 世界方向（旋转逆）
    vec3 dirView = normalize(vec3(ndc.x * ubo.uScreen.x, ndc.y, 1.0));
    vec3 dir = normalize(mat3(ubo.uViewRotInv) * dirView);

    // 垂直渐变
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(ubo.uBottomColor.rgb, ubo.uTopColor.rgb,
                   pow(t, max(ubo.uParams.w, 0.01)));

    // 太阳光斑
    vec3 sunDir = vec3(
        cos(ubo.uParams.y) * cos(ubo.uParams.x),
        sin(ubo.uParams.y),
        cos(ubo.uParams.y) * sin(ubo.uParams.x));
    float s = pow(max(dot(dir, sunDir), 0.0), 96.0) * ubo.uParams.z;
    sky += ubo.uSunColor.rgb * s;

    // 地平线暖光带（太阳下方）
    float horizon = exp(-abs(dir.y) / max(ubo.uParams.w, 0.01)) * 0.35;
    sky += ubo.uSunColor.rgb * horizon * ubo.uParams.z * 0.5;

    outColor = vec4(sky, 1.0);
}
