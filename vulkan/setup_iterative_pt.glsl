// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef SETUP_ITERATIVE_PATHTRACER_GLSL
#define SETUP_ITERATIVE_PATHTRACER_GLSL

#include "language.glsl"

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_shader_atomic_float : require

#define PRIMARY_RAY 0
#define OCCLUSION_RAY 1

struct RayPayload {
    vec3 normal;
    float dist;
    vec3 geo_normal;
    uint material_id;
    vec3 tangent;
    float bitangent_l;
    vec2 uv;
#if NEED_MESH_ID_FOR_VISUALIZATION
    uint parameterized_mesh_id;
#endif
#if RBO_lod_visualize == LOD_VISUALIZE_TS_INVOCATIONS
    uint ts_invocation_counter;
#endif
#if (RBO_debug_mode == DEBUG_MODE_ANY_HIT_COUNT_FULL_PATH || RBO_debug_mode == DEBUG_MODE_ANY_HIT_COUNT_PRIMARY_VISIBILITY)
    float any_hit_count;
#endif
};

#include "setup_pixel_assignment.glsl"

#endif
