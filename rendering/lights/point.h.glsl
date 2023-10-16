// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef POINT_LIGHTS_H_GLSL
#define POINT_LIGHTS_H_GLSL

struct PointLight
{
    GLM(vec3) positionWS;
    float range;
    GLM(vec3) radiance;
    float falloff;
};

#endif
