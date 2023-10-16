// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require

#include "setup_recursive_pt.glsl"
#include "../gpu_params.glsl"
#include "defaults.glsl"

layout(binding = SCENE_PARAMS_BIND_POINT, set = 0, std140) uniform GPBuf {
    GLOBAL_CONSTANT_PARAMETERS
};

#ifdef TAIL_RECURSIVE
#define RENDERER_NUM_RAYQUERIES num_rayqueries
#endif

rayPayloadInEXT RayPayload payload;

#if defined(TAIL_RECURSIVE) || defined(ENABLE_AOV_BUFFERS)
#include "accumulate.glsl"
#endif

#include "mc/lights_sun.glsl"
#include "mc/nee_interface.glsl"

#include "lights/sky_model_arhosek/sky_model.glsl"

void main() {
    vec3 ray_origin = gl_WorldRayOriginEXT;
    vec3 ray_dir = gl_WorldRayDirectionEXT;

    vec3 atmosphere_illum;
    vec3 sun_illum;
#ifndef TRIVIAL_BACKGROUND_MISS
    vec3 dir = gl_WorldRayDirectionEXT;
    float ocean_coeff = 1.0f;
    if (dir.y <= 0.0f) {
        dir.y = -dir.y;
        ocean_coeff = 0.7 * pow(max(1.0 - abs(dir.y), 0.0), 5);
    }

    atmosphere_illum = max( skymodel_radiance(scene_params.sky_params, scene_params.sun_dir, dir), vec3(0.0f) ) * ocean_coeff;
    if (dot(dir, scene_params.sun_dir) >= scene_params.sun_cos_angle)
        sun_illum = vec3(scene_params.sun_radiance) * ocean_coeff;
    else
        sun_illum = vec3(0.0f);
#else
#endif

    vec3 path_throughput = get_throughput(payload).xyz;
    float prev_bsdf_pdf = payload.prev_bsdf_pdf;
    vec3 illum = vec3(0.0f);

    illum += path_throughput * abs(atmosphere_illum);
    {
        NEEQueryPoint query;
        query.point = ray_origin;
        query.normal = vec3(0.0f); // todo: query points need to be serializable, not currently using prev_n;
        query.w_o = vec3(0.0f); // todo: query points need to be serializable, not currently using prev_wo;
        query.info = NEEQueryInfo(0);
#ifndef PT_DISABLE_NEE
        float light_pdf = eval_direct_sun_light_pdf(query, ray_dir);
        float w = nee_mis_heuristic(1.f, prev_bsdf_pdf, 1.f, light_pdf);
#else
        float w = 1.0f;
#endif
        illum += w * path_throughput * abs(sun_illum);
    }

    payload.illum.xyz += illum;


    int bounce = get_bounce(payload);
#ifdef ENABLE_AOV_BUFFERS
    if (bounce == 0 && (accumulation_flags & ACCUMULATION_FLAGS_AOVS) != 0) {
        store_material_aovs(vec3(0.0f), 1.0f, 1.0f);
        store_geometry_aovs(-ray_dir, ray_origin + ray_dir * 1000000.0f, vec3(0.0f));
    }
#endif

#ifdef TAIL_RECURSIVE
    accumulate(~0u, vec4(payload.illum, bounce > 0 ? 1.0f : 0.0f), ~0u);
#elif defined(NON_RECURSIVE)
    set_bounce_and_reliability(payload, MAX_PATH_DEPTH, 1.0f);
#endif
}
