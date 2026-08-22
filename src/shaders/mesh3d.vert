#version 450

// 3D 顶点着色器：MVP 变换 + 法线/世界位置/视图深度输出
layout(binding = 0) uniform UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 lightDir;    // 世界空间光传播方向（碎片着色器内取反做漫反射）
    float ambient;    // 环境光强度
    vec3 lightColor;  // 光颜色 × 强度
    float _pad;
    vec4 fogColor;    // xyz=雾色  w=雾开关
    vec4 fogParams;   // x=fogStart y=fogEnd z=texScale w=texMix
    vec4 ambientUp;
    vec4 ambientDown;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out float fragViewDepth;   // 雾计算（view 空间深度，负值）

void main()
{
    // 法线变换用 inverse(transpose(model))，兼容非等比缩放；
    // 模型顶点数很少，逐顶点计算代价可忽略。
    mat3 normalMat = mat3(transpose(inverse(ubo.model)));
    fragNormal = normalMat * inNormal;
    fragPos = vec3(ubo.model * vec4(inPosition, 1.0));
    vec4 viewPos = ubo.view * vec4(fragPos, 1.0);
    fragViewDepth = viewPos.z;
    gl_Position = ubo.proj * viewPos;
    fragColor = inColor;
}
