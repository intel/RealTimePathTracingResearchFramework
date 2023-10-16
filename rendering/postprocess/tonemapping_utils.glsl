// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef TONEMAPPING_UTILS_GLSL
#define TONEMAPPING_UTILS_GLSL

#include "tonemapping.h"

vec3 neutral_tone_map(vec3 c) {
    const float luminance_level = max(max(c.x, c.y), max(c.z, 1.0f));
    c *= mix(0.1f * log2(luminance_level), 1.0f, 0.8f)
        / luminance_level;
    return c;
}

vec3 tonemap(int tone_mapping_mode, vec3 colorLinear)
{
    if (tone_mapping_mode == NO_TONE_MAPPING)
    {
        colorLinear = colorLinear;
    }
    else if (tone_mapping_mode == FAST_TONE_MAPPING)
    {
        colorLinear = colorLinear / (vec3(1.0 + colorLinear));
    }
    else if (tone_mapping_mode == NEUTRAL_TONE_MAPPING)
    {
        colorLinear = neutral_tone_map(colorLinear);
    }
    return colorLinear;
}

#endif // TONEMAPPING_UTILS_GLSL
