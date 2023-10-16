// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef HIT_POINT_H_GLSL
#define HIT_POINT_H_GLSL

struct HitPoint {
    vec3 p; // for volumetric textures, object space
    vec2 uv;
    mat2 duvdxy;
    vec3 d; // for volumetric textures, ray dir in object space
};

struct InteractionPoint {
    vec3 p;
    vec3 gn;
    vec3 n;
    vec3 v_x, v_y;
    int primitiveId;
    int instanceId;
};

#endif
