#version 450
/* The overlay (overlay.cpp): ImGui's triangles, in window pixels */

layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 UV;
layout(location = 2) in vec4 Color;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

layout(push_constant, std430) uniform Registers
{
    vec2 scale;
    vec2 translate;
} reg;

void main()
{
    vColor = Color;
    vUV = UV;
    gl_Position = vec4(Position * reg.scale + reg.translate, 0.0, 1.0);
}
