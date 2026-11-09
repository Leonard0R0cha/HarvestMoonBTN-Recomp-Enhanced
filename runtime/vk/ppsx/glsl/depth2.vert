#version 450
// HMBTN: the depth test's second pass (renderer.cpp render_ordered): where the first one passed
// (stencil 1), the depth a later primitive is tested against. Covers the same pixels as the
// first pass (same position, invariant).
layout(location = 0) in vec4 Position;
layout(location = 1) in float Other;
layout(location = 0) noperspective out float vOther;
invariant gl_Position;

const vec2 FB_SIZE = vec2(1024.0, 512.0);

void main()
{
   gl_Position = vec4(Position.xy / FB_SIZE * 2.0 - 1.0, Position.z, 1.0) * Position.w;
   vOther = Other;
}
