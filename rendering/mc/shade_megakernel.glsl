// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef MC_MEGAKERNEL_SHADE_HEADER
#define MC_MEGAKERNEL_SHADE_HEADER

#include "language.glsl"

#include "mc/shading_interface.glsl"
#include "shade_base_material.glsl"


inline int shade_megakernel(GLSL_inout(ShadingSampleState) state
    , GLSL_inout(vec3) illum, GLSL_inout(vec3) path_throughput
    , int material_id, BaseMaterial params, HitPoint lookup_point
    , NEESampledArea nee_area
    , vec3 w_o, GLSL_inout(InteractionPoint) interaction
    , GLSL_inout(RANDOM_STATE) rng, GLSL_inout(vec3) w_i, GLSL_inout(ShadingQueryAux) aux
    , bool active_mask) {
    bool is_neural = false;
    int neural_result;
#ifdef shade_neural
    is_neural = active_mask && (params.flags & BASE_MATERIAL_NEURAL) != 0;
    {
        neural_result = shade_neural(state
            , illum, path_throughput
            , material_id, params, lookup_point
            , nee_area
            , w_o, interaction
            , rng, w_i, aux, is_neural);
    }
#endif
    if (is_neural)
        return neural_result;

    if (!active_mask)
        return SHADING_RESULT_TERMINATE;
    return shade_base_material(state
        , illum, path_throughput
        , material_id, params, lookup_point
        , nee_area
        , w_o, interaction
        , rng, w_i, aux);
}

#ifdef CUSTOM_MATERIAL_ALPHA

inline float get_material_alpha(int material_id, GLSL_in(MATERIAL_PARAMS) p, GLSL_in(HitPoint) hit)
{
#ifdef get_neural_alpha
    if ((p.flags & BASE_MATERIAL_NEURAL) != 0)
        return get_neural_alpha(material_id, p, hit);
#endif
    return get_base_material_alpha(material_id, p, hit);
}

#endif

#endif
