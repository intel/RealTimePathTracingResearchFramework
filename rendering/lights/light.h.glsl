// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef LIGHTS_H_GLSL
#define LIGHTS_H_GLSL

#define LIGHT_TYPE_POINT 0
#define LIGHT_TYPE_TRIANGLE 1
#define LIGHT_TYPE_QUAD 2

struct LightData
{
    GLM(vec3) positionWS GLCPP_DEFAULT(= GLM(vec3)(0.0f));
    float range GLCPP_DEFAULT(= 1);

    GLM(vec3) radiance GLCPP_DEFAULT(= GLM(vec3)(0.0f));
    float falloff GLCPP_DEFAULT(= 1);

    GLM(vec3) up GLCPP_DEFAULT(= GLM(vec3)(0.0f));
    float width GLCPP_DEFAULT(= 1);

    GLM(vec3) right GLCPP_DEFAULT(= GLM(vec3)(0.0f));
    float height GLCPP_DEFAULT(= 1);

    GLM(vec3) forward GLCPP_DEFAULT(= GLM(vec3)(0.0f));
    uint32_t type GLCPP_DEFAULT(= 0);
};

#endif // LIGHTS_H_GLSL
