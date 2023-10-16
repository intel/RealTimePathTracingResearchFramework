// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require

#include "setup_iterative_pt.glsl"
#include "gpu_params.glsl"
#include "defaults.glsl"


layout(binding = SCENE_PARAMS_BIND_POINT, set = 0, std140) uniform GPBuf {
    GLOBAL_CONSTANT_PARAMETERS
};

layout(location = PRIMARY_RAY) rayPayloadInEXT RayPayload payload;

#include "lights/sky_model_arhosek/sky_model.glsl"

void main() {
    payload.dist = -1;

#ifndef TRIVIAL_BACKGROUND_MISS
    vec3 dir = gl_WorldRayDirectionEXT;
    float ocean_coeff = 1.0f;
    if (dir.y <= 0.0f) {
        dir.y = -dir.y;
        ocean_coeff = 0.7 * pow(max(1.0 - abs(dir.y), 0.0), 5);
    }

    payload.normal = max( skymodel_radiance(scene_params.sky_params, scene_params.sun_dir, dir), vec3(0.0f) ) * ocean_coeff;
    if (dot(dir, scene_params.sun_dir) >= scene_params.sun_cos_angle)
        payload.geo_normal = vec3(scene_params.sun_radiance) * ocean_coeff;
    else
        payload.geo_normal = vec3(0.0f);
#else
#endif
}
