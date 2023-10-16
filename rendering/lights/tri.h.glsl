// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef TRI_LIGHTS_H_GLSL
#define TRI_LIGHTS_H_GLSL

// Tri-shaped light source
struct TriLight {
    GLM(vec3) v0, v1, v2;
    GLM(vec3) radiance;
};

struct TriLightData {
#ifndef QUANTIZED_POSITIONS_TODO
    float v0_x, v0_y, v0_z;
    float v1_x, v1_y, v1_z;
    float v2_x, v2_y, v2_z;
#else
    uint64_t tri;
#endif
#ifndef QUANTIZED_EMITTERS
    float radiance_x, radiance_y, radiance_z;
#else
    uint32_t radiance;
#endif
};

#endif
