#version 450
/* The CRT filters' vertex shader: a quad (corners -1..1) over the viewport, vTexCoord 0..1 */

layout(location = 0) in vec2 Position;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = vec4(Position, 0.0, 1.0);
    vTexCoord = 0.5 * Position + 0.5;
}
