// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef SUN_LIGHT_GLSL
#define SUN_LIGHT_GLSL

#include "../util.glsl"

inline vec3 sample_sun_dir(vec3 sun_dir, float cos_radius, vec2 sampl) {
    float phi = 2.0f * M_PI * sampl.x;
    float cosTheta = mix(1.0f, cos_radius, sampl.y);
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));

    return ortho_frame(sun_dir) * vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

inline float sample_sun_dir_pdf(vec3 sun_dir, float cos_radius, vec3 sampled) {
    float solid_angle = 2.0f * M_PI * (1.0f - cos_radius);
    return 1.0f / solid_angle;
}

#endif
