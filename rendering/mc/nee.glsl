// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef MC_NEE_HEADER
#define MC_NEE_HEADER

// #define DISABLE_AREA_LIGHT_SAMPLING

#include "../defaults.glsl"
#include "light_sampling.h"

#if RBO_light_sampling_variant == LIGHT_SAMPLING_VARIANT_NONE
#define DISABLE_AREA_LIGHT_SAMPLING
#endif

#include "lights_sun.glsl"
#ifndef DISABLE_AREA_LIGHT_SAMPLING
#include "lights_linear.glsl"
#endif
#include "../bsdfs/hit_point.glsl"
#include "../util.glsl"
#include "nee_interface.glsl"

inline bool raytrace_test_visibility(const vec3 from, const vec3 dir, float dist);

struct NEEQueryAux {
    vec3 light_dir;
    float light_dist;
    float mis_pdf;
};

inline vec3 sample_direct_light(const MATERIAL_TYPE mat, GLSL_in(InteractionPoint) hit, const vec3 w_o
    , vec2 dir_sample, vec2 sel_sample
    , GLSL_inout(NEEQueryAux) aux_info) {
    vec3 illum = vec3(0.0f);
    vec3 light_dir;
    float light_dist = 2.e16f;
    float light_pdf = 0.0f;
    float mis_pdf = aux_info.mis_pdf;

    // sun light
#ifndef DISABLE_AREA_LIGHT_SAMPLING
    if (sel_sample.x <= scene_params.sun_radiance.w) { // note: <= necessary to default to sun in all cases when no other lights
        sel_sample.x /= scene_params.sun_radiance.w;
#else
    {
#endif
        illum += sample_sun_light(hit.p, hit.n, scene_params.sun_dir, scene_params.sun_cos_angle, dir_sample, sel_sample, light_dir, light_pdf)
            * (vec3(scene_params.sun_radiance) / scene_params.sun_radiance.w);
#ifndef DISABLE_AREA_LIGHT_SAMPLING
        light_pdf *= scene_params.sun_radiance.w;
#endif
        // always use proper light PDF for sky light
        mis_pdf = light_pdf;
    }
#ifndef DISABLE_AREA_LIGHT_SAMPLING
    // tri light
    else {
        sel_sample.x = (sel_sample.x - scene_params.sun_radiance.w) / (1.0f - scene_params.sun_radiance.w);
        
        float tri_mis_wpdf = 0.0f;
        illum += sample_tri_lights(hit.p, hit.n, dir_sample, sel_sample, light_dir, light_dist, light_pdf, tri_mis_wpdf)
            / (1.0f - scene_params.sun_radiance.w);
        light_pdf *= 1.0f - scene_params.sun_radiance.w;

        // allow overriding MIS pdf
        if (mis_pdf == 0.0f)
            mis_pdf = tri_mis_wpdf * (1.0f - scene_params.sun_radiance.w);
    }
#endif

    // strict normals
    if (light_pdf > 0.0f && dot(light_dir, hit.gn) * dot(light_dir, hit.n) > 0.0f) {
        bool visibility = raytrace_test_visibility(hit.p, light_dir, light_dist);

        float bsdf_pdf = eval_bsdf_wpdf(mat, hit, w_o, light_dir);
        if (bsdf_pdf >= 0.0f && visibility) {
            vec3 bsdf = eval_bsdf(mat, hit, w_o, light_dir);
            float w = nee_mis_heuristic(1.f, mis_pdf, 1.f, bsdf_pdf);
            illum *= w * abs(dot(light_dir, hit.n)) * bsdf;
            aux_info.light_dir = light_dir;
            aux_info.light_dist = light_dist;
            aux_info.mis_pdf = mis_pdf;
            return illum;
        }
    }

    aux_info.mis_pdf = 0.0f;
    return vec3(0.0f);
}

#endif
