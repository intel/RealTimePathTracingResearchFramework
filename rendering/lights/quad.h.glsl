// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef QUAD_LIGHTS_H_GLSL
#define QUAD_LIGHTS_H_GLSL

// Quad-shaped light source
struct QuadLight {
    GLM(vec3) emission; float _pad1;
    GLM(vec3) position; float _pad2;
    GLM(vec3) normal;   float _pad3;
    // x and y vectors spanning the quad, with
    // the half-width and height
    GLM(vec3) v_x;
    float width;
    GLM(vec3) v_y;
    float height;
};

#endif
