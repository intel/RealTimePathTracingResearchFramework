// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef MC_NEE_INTERFACE_HEADER
#define MC_NEE_INTERFACE_HEADER

#include "../defaults.glsl"
#include "../bsdfs/hit_point.glsl"
#include "../lights/light.h.glsl"

inline float nee_mis_heuristic(float n_f, float pdf_f, float n_g, float pdf_g) {
	float f = n_f * pdf_f;
	float g = n_g * pdf_g;
	return f / (f + g);
}

struct NEEQueryInfo {
    // todo: potentially material properties (LTC info?)
    int reserved;
};
struct NEEQueryPoint {
    vec3 point;
    vec3 normal;
    vec3 w_o;
    NEEQueryInfo info;
};

struct NEESampledArea {
    float approx_solid_angle;
    int type; // currently ignored, as only triangles
};
inline NEESampledArea no_nee_sampled_area() {
    NEESampledArea area;
    area.approx_solid_angle = 0.0f;
    area.type = -1;
    return area;
}

#ifdef MATERIAL_TYPE
inline NEEQueryPoint nee_query_point(const MATERIAL_TYPE mat, GLSL_in(InteractionPoint) hit, const vec3 w_o) {
    return NEEQueryPoint GLSL_construct(hit.p, hit.n, w_o, NEEQueryInfo GLSL_construct(0));
}
#endif

#ifdef SUN_LIGHT_GLSL
inline float eval_direct_sun_light_pdf(NEEQueryPoint query, vec3 w_i) {
    return scene_params.sun_radiance.w * eval_sun_light_pdf(query.point, query.normal, w_i, scene_params.sun_dir, scene_params.sun_cos_angle);
}
#endif

#ifdef TRI_LIGHTS_GLSL
inline float wpdf_direct_tri_light(float approx_solid_angle) {
    return (1.0f - scene_params.sun_radiance.w) * approx_tri_lights_pdf(approx_solid_angle);
}

inline float wpdf_direct_light(NEESampledArea light) {
    // if (light.type == LIGHT_TYPE_TRIANGLE) {
        return wpdf_direct_tri_light(light.approx_solid_angle);
    //}
}
#endif

#endif
