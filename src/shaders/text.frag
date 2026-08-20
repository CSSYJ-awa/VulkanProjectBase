#version 450

layout(binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform TextColor
{
    vec4 color;
} pc;

void main()
{
    float a = texture(texSampler, fragTexCoord).r;
    outColor = vec4(pc.color.rgb, pc.color.a * a);
}
