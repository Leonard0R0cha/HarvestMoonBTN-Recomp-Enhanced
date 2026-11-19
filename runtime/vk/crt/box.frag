#version 450
/* HMBTN: before a CRT filter, the picture at the console's size, each pixel the average of the
 * upscaled picture's pixels it covers (ratio of them each way): smooth 3D edges, as upscaled,
 * under the scanlines (one sample from the middle instead gave back the console's jagged ones) */

layout(push_constant, std430) uniform Registers
{
    vec2 ratio;     /* the picture's pixels per console pixel */
} reg;

layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D Source;

void main()
{
    ivec2 size = textureSize(Source, 0);
    vec2 o = floor(gl_FragCoord.xy) * reg.ratio;
    ivec2 a = ivec2(floor(o + 0.001)), b = ivec2(ceil(o + reg.ratio - 0.001));
    b = min(min(b, a + 16), size);  /* (16 x 16 at most: upscale 8 needs 8 x 8) */
    vec3 sum = vec3(0.0);
    for (int y = a.y; y < b.y; y++)
        for (int x = a.x; x < b.x; x++)
            sum += texelFetch(Source, ivec2(x, y), 0).rgb;
    FragColor = vec4(sum / float(max((b.x - a.x) * (b.y - a.y), 1)), 1.0);
}
