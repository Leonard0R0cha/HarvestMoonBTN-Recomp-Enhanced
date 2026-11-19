/* The CRT filters' settings (runtime/vk/crt): each shader's "#pragma parameter" lines from
 * libretro's slang-shaders (default, minimum, maximum, step), in the order of its push constants.
 * ini keys: [crt] hyllian_<name>, lottes_<name>; the overlay (F7) has a slider for each. */
#pragma once

#define CRT_PARAMS 16       /* per shader, at most */

typedef struct { const char *name, *label; float def, min, max, step; } CrtParam;

static const CrtParam crt_params[2][CRT_PARAMS] = {
    {   /* crt-hyllian (scanlines) */
        { "MASK_INTENSITY", "Mask intensity", 0.5f, 0.0f, 1.0f, 0.1f },
        { "InputGamma", "Input gamma", 2.4f, 0.0f, 5.0f, 0.1f },
        { "OutputGamma", "Output gamma", 2.2f, 0.0f, 5.0f, 0.1f },
        { "BRIGHTBOOST", "Bright boost", 1.5f, 0.0f, 2.0f, 0.1f },
        { "SCANLINES", "Scanline strength", 0.72f, 0.0f, 1.0f, 0.02f },
        { "SHARPER", "Sharper", 0.0f, 0.0f, 1.0f, 1.0f },
    },
    {   /* crt-lottes (CRT) */
        { "hardScan", "Scanline hardness", -8.0f, -20.0f, 0.0f, 1.0f },
        { "hardPix", "Pixel hardness", -3.0f, -20.0f, 0.0f, 1.0f },
        { "warpX", "Curvature X", 0.031f, 0.0f, 0.125f, 0.01f },
        { "warpY", "Curvature Y", 0.041f, 0.0f, 0.125f, 0.01f },
        { "maskDark", "Mask dark", 0.5f, 0.0f, 2.0f, 0.1f },
        { "maskLight", "Mask light", 1.5f, 0.0f, 2.0f, 0.1f },
        { "scaleInLinearGamma", "Linear gamma", 1.0f, 0.0f, 1.0f, 1.0f },
        { "shadowMask", "Mask type", 3.0f, 0.0f, 4.0f, 1.0f },
        { "brightBoost", "Brightness boost", 1.0f, 0.0f, 2.0f, 0.05f },
        { "hardBloomPix", "Bloom X softness", -1.5f, -2.0f, -0.5f, 0.1f },
        { "hardBloomScan", "Bloom Y softness", -2.0f, -4.0f, -1.0f, 0.1f },
        { "bloomAmount", "Bloom amount", 0.15f, 0.0f, 1.0f, 0.05f },
        { "shape", "Filter kernel shape", 2.0f, 0.0f, 10.0f, 0.05f },
    },
};
static const char *const crt_shader_key[2] = { "hyllian_", "lottes_" };

static inline int crt_param_count(int shader)
{
    int n = 0;
    while (n < CRT_PARAMS && crt_params[shader][n].name) n++;
    return n;
}
