#version 450

/*
   Hyllian's CRT Shader
   with cgwg's magenta/green dotmask
   ported to GLSL/SLANG by DariusG & hunterk

   Copyright (C) 2011-2015 Hyllian - sergiogdb@gmail.com

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
*/

/* HMBTN: crt/shaders/hyllian/crt-hyllian-fast.slang from github.com/libretro/slang-shaders, one
 * stage: SourceSize (the console's picture: its lines), OutputSize and the parameters (crt_params.h)
 * come as push constants, the vertex shader's half-texel shift is done here. */

layout(push_constant, std430) uniform Registers
{
    vec4 SourceSize;
    vec4 OutputSize;
    float MASK_INTENSITY;
    float InputGamma;
    float OutputGamma;
    float BRIGHTBOOST;
    float SCANLINES;
    float SHARPER;
} params;

#define global params
#define MASK_INTENSITY params.MASK_INTENSITY
#define InputGamma params.InputGamma
#define OutputGamma params.OutputGamma
#define BRIGHTBOOST params.BRIGHTBOOST
#define SCANLINES params.SCANLINES
#define SHARPER params.SHARPER

#define SourceSize global.SourceSize
#define COMPAT_TEXTURE texture
#define GAMMA_IN(color)     pow(color, vec3(InputGamma, InputGamma, InputGamma))
#define GAMMA_OUT(color)    pow(color, vec3(1.0 / OutputGamma, 1.0 / OutputGamma, 1.0 / OutputGamma))

layout(location = 0) in vec2 vTexCoord0;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D Source;

void main()
{
   vec2 ps = SourceSize.zw;
   vec2 vTexCoord = vTexCoord0 + ps * vec2(-0.49999, 0.0);
   vec2 dx = vec2(ps.x, 0.0);
   vec2 dy = vec2(0.0, ps.y);

   vec2 tc = (floor(vTexCoord.xy * SourceSize.xy) + vec2(0.49999, 0.49999)) / SourceSize.xy;

   vec2 fp = fract(vTexCoord.xy * SourceSize.xy);

   vec3 c10 = COMPAT_TEXTURE(Source, tc -       dx).xyz;
   vec3 c11 = COMPAT_TEXTURE(Source, tc           ).xyz;
   vec3 c12 = COMPAT_TEXTURE(Source, tc +       dx).xyz;
   vec3 c13 = COMPAT_TEXTURE(Source, tc + 2.0 * dx).xyz;

   vec4 lobes = vec4(fp.x*fp.x*fp.x, fp.x*fp.x, fp.x, 1.0);

   vec4 InvX = vec4(0.0);

   if (SHARPER == 0.0) {
// Horizontal cubic filter - "Catrom"
    InvX.x = dot(vec4( -0.5, 1.0, -0.5, 0.0), lobes);
    InvX.y = dot(vec4(  1.5,-2.5,  0.0, 1.0), lobes);
    InvX.z = dot(vec4( -1.5, 2.0,  0.5, 0.0), lobes);
    InvX.w = dot(vec4(  0.5,-0.5,  0.0, 0.0), lobes);
   }
   else if (SHARPER == 1.0) {
// Swith to "Hermite" - Sharper, smoothed bilinear
    InvX.x = dot(vec4(  0.0, 0.0,  0.0, 0.0), lobes);
    InvX.y = dot(vec4(  2.0,-3.0,  0.0, 1.0), lobes);
    InvX.z = dot(vec4( -2.0, 3.0,  0.0, 0.0), lobes);
    InvX.w = dot(vec4(  0.0, 0.0,  0.0, 0.0), lobes);
   }

    vec3 color = InvX.x*c10.xyz;
         color+= InvX.y*c11.xyz;
         color+= InvX.z*c12.xyz;
         color+= InvX.w*c13.xyz;

    color = GAMMA_IN(max(color, vec3(0.0)));    /* HMBTN: the cubic filter can undershoot; pow(<0) is undefined */

    float pos1 = 1.5-SCANLINES - abs(fp.y - 0.5);
    float d1 = max(0.0, min(1.0, pos1));
    float d = d1*d1*(3.0+BRIGHTBOOST - (2.0*d1));

    color = color*d;

// dotmask
    float mod_factor = vTexCoord.x * global.OutputSize.x;
    vec4 dotMaskWeights = mix(
                                 vec4(1.0, 1.0-MASK_INTENSITY, 1.0, 1.),
                                 vec4(1.0-MASK_INTENSITY, 1.0, 1.0-MASK_INTENSITY, 1.),
                                 floor(mod(mod_factor, 2.0))
                                  );
    color *=vec3(dotMaskWeights.x,dotMaskWeights.y,dotMaskWeights.z);

    color  = GAMMA_OUT(color);
    FragColor = vec4(color.r, color.g, color.b, 1.0);
}
