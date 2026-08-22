#version 450
// postfx.frag —— 后处理特效（单 pass 7 模式 + 色调映射）
layout(binding = 0) uniform sampler2D uScene;
layout(binding = 1, std140) uniform PostFxUBO {
    int   uMode;           // 0=none 1=grayscale 2=invert 3=blur 4=edge 5=sharpen 6=bloom
    float uIntensity;      // 特效强度
    float uExposure;       // 色调映射曝光（<=0 关闭）
    float uBloomThreshold; // 泛光阈值
    float uTexelW;
    float uTexelH;
    float _pad0;
    float _pad1;
} ubo;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

vec3 sampleAt(vec2 off) { return texture(uScene, uv + off).rgb; }

// 9-tap 高斯权重采样
vec3 blur9()
{
    vec2 t = vec2(ubo.uTexelW, ubo.uTexelH);
    vec3 acc = vec3(0.0);
    acc += sampleAt(vec2(-t.x, -t.y)) * 0.0625;
    acc += sampleAt(vec2( 0.0, -t.y)) * 0.125;
    acc += sampleAt(vec2( t.x, -t.y)) * 0.0625;
    acc += sampleAt(vec2(-t.x,  0.0)) * 0.125;
    acc += sampleAt(vec2( 0.0,  0.0)) * 0.25;
    acc += sampleAt(vec2( t.x,  0.0)) * 0.125;
    acc += sampleAt(vec2(-t.x,  t.y)) * 0.0625;
    acc += sampleAt(vec2( 0.0,  t.y)) * 0.125;
    acc += sampleAt(vec2( t.x,  t.y)) * 0.0625;
    return acc;
}

void main()
{
    vec3 c = texture(uScene, uv).rgb;

    if (ubo.uMode == 1)                 // 灰度
    {
        float g = dot(c, vec3(0.299, 0.587, 0.114));
        c = mix(c, vec3(g), ubo.uIntensity);
    }
    else if (ubo.uMode == 2)            // 反色
    {
        c = mix(c, 1.0 - c, ubo.uIntensity);
    }
    else if (ubo.uMode == 3)            // 高斯模糊
    {
        c = mix(c, blur9(), ubo.uIntensity);
    }
    else if (ubo.uMode == 4)            // 边缘检测（拉普拉斯近似）
    {
        vec2 t = vec2(ubo.uTexelW, ubo.uTexelH);
        vec3 e;
        e  = sampleAt(vec2(-t.x, -t.y)) * -1.0;
        e += sampleAt(vec2( 0.0, -t.y)) * -1.0;
        e += sampleAt(vec2( t.x, -t.y)) * -1.0;
        e += sampleAt(vec2(-t.x,  0.0)) * -1.0;
        e += sampleAt(vec2( 0.0,  0.0)) *  8.0;
        e += sampleAt(vec2( t.x,  0.0)) * -1.0;
        e += sampleAt(vec2(-t.x,  t.y)) * -1.0;
        e += sampleAt(vec2( 0.0,  t.y)) * -1.0;
        e += sampleAt(vec2( t.x,  t.y)) * -1.0;
        c = mix(c, clamp(e, 0.0, 1.0), ubo.uIntensity);
    }
    else if (ubo.uMode == 5)            // 锐化
    {
        c = mix(c, c + (c - blur9()) * 2.0, ubo.uIntensity);
    }
    else if (ubo.uMode == 6)            // 近似泛光（阈值高亮 + 邻域加权 + 叠加）
    {
        vec2 t = vec2(ubo.uTexelW, ubo.uTexelH);
        vec3 glow = vec3(0.0);
        float total = 0.0;
        for (int i = -3; i <= 3; ++i)
        {
            for (int j = -3; j <= 3; ++j)
            {
                float dist2 = float(i * i + j * j) + 1.0;
                float wgt = 1.0 / (dist2 * 1.7);
                glow += max(texture(uScene, uv + vec2(float(i) * t.x, float(j) * t.y)).rgb
                            - vec3(ubo.uBloomThreshold), vec3(0.0)) * wgt;
                total += wgt;
            }
        }
        glow = clamp(glow / max(total, 1e-5), 0.0, 1.0);
        c = mix(c, c + glow, ubo.uIntensity);
    }

    // 色调映射（指数曝光近似）
    if (ubo.uExposure > 0.0)
        c = vec3(1.0) - exp(-c * ubo.uExposure);

    outColor = vec4(c, 1.0);
}
