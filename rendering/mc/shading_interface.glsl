// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef MC_MATERIAL_SHADE_HEADER
#define MC_MATERIAL_SHADE_HEADER

#define SHADING_RESULT_TERMINATE -1
#define SHADING_RESULT_NULL 0
#define SHADING_RESULT_BOUNCE 1
#define SHADING_RESULT_TRANSPARENT 2

#include "../bsdfs/hit_point.glsl"
#include "nee_interface.glsl"

struct ShadingSampleState {
    int bounce;
    int output_channel;
    float prev_bounce_pdf;
};
ShadingSampleState init_shading_sample_state() {
    return ShadingSampleState(0, 0, 2.e16f);
}

struct ShadingQueryAux {
    float sampling_pdf;
    float mis_pdf;
};

inline int perform_shading(GLSL_inout(ShadingSampleState) state
    , GLSL_inout(vec3) illum, GLSL_inout(vec3) path_throughput
    , int material_id, BaseMaterial params, HitPoint lookup_point
    , NEESampledArea nee_area
    , vec3 wo, InteractionPoint interaction
    , GLSL_inout(RANDOM_STATE) rng, GLSL_inout(vec3) wi, GLSL_inout(ShadingQueryAux) aux);

#endif
