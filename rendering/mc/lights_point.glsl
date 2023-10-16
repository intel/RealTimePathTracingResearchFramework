// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef LIGHTS_POINT_GLSL
#define LIGHTS_POINT_GLSL

#include "../defaults.glsl"
#include "../lights/light.h.glsl"

float point_light_attenuation(in LightData lightData, float distance)
{
    // Light attenuation
    float s = distance / lightData.range;
    float s2 = s*s;
    float p = (1- s2);
    return distance <= lightData.range ? p * p /(1 + lightData.falloff * s2) : 0.0;
}

vec3 eval_point_light(in LightData lightData, vec3 hit_point, vec3 hit_normal)
{
    // Light vector
    vec3 L = (lightData.positionWS - hit_point);
    float distance = length(L);
    L /= max(distance, 1e-6);

    // Compute the distance attenuation
    float distance_att = point_light_attenuation(lightData, distance);

    // Evaluate the clamped NdotL
    float clampNdotL = clamp(dot(L, hit_normal), 0, 1);

    // TODO: Lambert lighting, need to be replaced by the disney_eval
    return M_1_PI * clampNdotL * lightData.radiance * distance_att;
}

#endif // LIGHTS_POINT_GLSL
