#version 450
// HMBTN: the depth test's second pass: the smaller of a quad's two planes of 1/w here (gpu.c
// tests against the farther of a polygon's two planes), divided by the margin (1.002)
layout(location = 0) noperspective in float vOther;
layout(constant_id = 6) const float INV_MARGIN = 1.0;

void main()
{
   gl_FragDepth = min(gl_FragCoord.z, vOther) * INV_MARGIN;
}
