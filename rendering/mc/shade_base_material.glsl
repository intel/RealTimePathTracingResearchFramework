// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef MC_BASE_SHADE_HEADER
#define MC_BASE_SHADE_HEADER

#include "../language.glsl"

#include "shading_interface.glsl"

#include "../rt/material_textures.glsl"
#include "nee.glsl"

inline int shade_base_material(GLSL_inout(ShadingSampleState) state
    , GLSL_inout(vec3) illum, GLSL_inout(vec3) path_throughput
    , int material_id, BaseMaterial params, HitPoint lookup_point
    , NEESampledArea nee_area
    , vec3 w_o, InteractionPoint interaction
    , GLSL_inout(RANDOM_STATE) rng, GLSL_inout(vec3) w_i, GLSL_inout(ShadingQueryAux) aux) {
    MATERIAL_TYPE mat;
    EmitterParams emit;
    float material_alpha;
    material_alpha = unpack_material(mat, emit
        , material_id, params
        , lookup_point);
    vec3 scatter_throughput = path_throughput; // now handled by any-hit testing: * material_alpha;

#ifdef ENABLE_AOV_BUFFERS
    if (state.bounce == 0 && (accumulation_flags & ACCUMULATION_FLAGS_AOVS) != 0)
        store_material_aovs(path_throughput * mat.base_color, mat.roughness, mat.ior);
#endif

    // direct emitter hit
    if (state.output_channel == 0 && emit.radiance != vec3(0.0f))
    {
        float light_pdf = wpdf_direct_light(nee_area);
        float w = nee_mis_heuristic(1.f, state.prev_bounce_pdf, 1.f, light_pdf);
        illum += w * scatter_throughput * emit.radiance;
    }

    // AOVs
    if (state.output_channel != 0) {
        float reliability = pow(0.25f, float(state.bounce));
        if (state.output_channel == 1)
            illum += scatter_throughput * mat.base_color * reliability;
        else if (state.output_channel == 2) {
            vec3 pathspace_normal = interaction.n;
            illum += pathspace_normal * reliability;
        }
        else if (state.output_channel == 3) {
            illum += interaction.p * reliability;
        }
    }

    // don't waste time on the last bounce if cut afterwards
    if (state.bounce+1 >= render_params.max_path_depth)
        return SHADING_RESULT_TERMINATE;

    if (state.output_channel == 0) {
        // first two dimensions light position selection, last light selection (sky/direct)
        vec4 nee_rng_sample = vec4(RANDOM_FLOAT2(rng, DIM_POSITION_X), RANDOM_FLOAT2(rng, DIM_LIGHT_SEL_1));
        NEEQueryAux nee_aux;
        nee_aux.mis_pdf = 0.0f;
        illum += scatter_throughput * sample_direct_light(mat, interaction, w_o, nee_rng_sample.xy, nee_rng_sample.zw, nee_aux);
    }
    RANDOM_SHIFT_DIM(rng, DIM_LIGHT_END);

    if (render_params.glossy_only_mode != 0 && !(mat.roughness < GLOSSY_MODE_ROUGHNESS_THRESHOLD && mat.ior != 1.0f))
        return SHADING_RESULT_TERMINATE;

    // now handled by any-hit testing
    //if (material_alpha < 1.0f && (0.0f >= material_alpha || RANDOM_FLOAT1(rng, DIM_FREE_PATH) >= material_alpha)) {
    //    // transparent
    //    RANDOM_SHIFT_DIM(rng, DIM_VERTEX_END);
    //}
    //else
    {
        vec2 bsdfLobeSample = RANDOM_FLOAT2(rng, DIM_LOBE);
        vec2 bsdfDirSample = RANDOM_FLOAT2(rng, DIM_DIRECTION_X);

        vec3 bsdf = sample_bsdf(mat, interaction, w_o, w_i, aux.sampling_pdf, aux.mis_pdf, bsdfDirSample, bsdfLobeSample, rng);
        RANDOM_SHIFT_DIM(rng, DIM_VERTEX_END);
        // Must increment bounce before returning an error or alpha will be
        // accumulated incorrectly.
        ++state.bounce;
        if (aux.mis_pdf == 0.f || bsdf == vec3(0.f) || !(dot(w_i, interaction.n) * dot(w_i, interaction.gn) > 0.0f)) {
            return SHADING_RESULT_TERMINATE;
        }

        path_throughput *= bsdf;

        state.prev_bounce_pdf = aux.mis_pdf;
        return SHADING_RESULT_BOUNCE;
    }
    return SHADING_RESULT_TRANSPARENT;
}

#endif
