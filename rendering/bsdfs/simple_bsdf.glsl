// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef SIMPLE_BSDF_GLSL
#define SIMPLE_BSDF_GLSL

#include "../defaults.glsl"
#include "../util.glsl"

#ifdef MEGAKERNEL_MATERIALS
#define GLTF_SUPPORT_TRANSMISSION
#endif

#ifndef SIMPLIFIED_MATERIAL
#define SIMPLIFIED_MATERIAL
#endif

struct SimpleMaterial {
    vec3 base_color;
#ifdef GLTF_SUPPORT_TRANSMISSION
    float specular_transmission;
#endif
    // constants for shading system
    float roughness;
    float ior;

    uint32_t flags;
};

#ifdef ENABLE_MATERIAL_DECODE
inline void load_material(GLSL_inout(SimpleMaterial) mat, GLSL_in(MATERIAL_PARAMS) p, GLSL_in(HitPoint) hit, GLSL_inout(float) alpha) {
#ifdef GLTF_SUPPORT_TRANSMISSION
    mat.specular_transmission = textured_scalar_param(p.specular_transmission, hit);
#endif
    mat.roughness = 1.0f;
    mat.ior = 1.0f;
    mat.flags = p.flags;
}

inline void apply_roughening(GLSL_inout(SimpleMaterial) mat, float roughening) {
}
#endif

inline vec3 simple_bsdf(const SimpleMaterial mat, const vec3 n,
    const vec3 w_o, const vec3 w_i)
{
    float i_dot_n = dot(n, w_i);
    float o_dot_n = dot(n, w_o);

    vec3 diffuse = mat.base_color * float(M_1_PI);

    if (i_dot_n * o_dot_n < 0.0f) {
//#ifndef GLTF_SUPPORT_TRANSMISSION
        return vec3(0.0f);
//#else
    }

    return diffuse;
}

inline vec3 sample_sphere(vec2 rnd) {
    float phi = 2.0f * M_PI * rnd.x;
    float cos_theta = rnd.y * 2.0f - 1.0f;
    float sin_theta = sqrt(1.0f - cos_theta * cos_theta);
    return vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
}

inline float simple_pdf(const SimpleMaterial mat, const vec3 n,
    const vec3 w_o, const vec3 w_i)
{
    float i_dot_n = dot(n, w_i);
    float o_dot_n = dot(n, w_o);

    float pdf = M_1_PI * abs(i_dot_n);

    if (i_dot_n * o_dot_n < 0.0f) {
//#ifndef GLTF_SUPPORT_TRANSMISSION
        return 0.0f;
//#else
    }

    return pdf;
}

inline vec3 sample_simple_brdf(const SimpleMaterial mat, const vec3 n, const vec3 w_o,
    GLSL_out(vec3) w_i, GLSL_out(float) pdf, GLSL_out(float) mis_pdf,
    vec2 rng_sample)
{
    w_i = normalize( n + sample_sphere(rng_sample) );
    float i_dot_n = dot(n, w_i);
    pdf = mis_pdf = M_1_PI * abs(i_dot_n);

    return mat.base_color;
}

#ifndef NO_MATERIAL_REGISTRATION

#define MATERIAL_TYPE SimpleMaterial
#define eval_bsdf(mat, hit, w_o, w_i) \
    simple_bsdf(mat, hit.n, w_o, w_i)
#define eval_bsdf_wpdf(mat, hit, w_o, w_i) \
    simple_pdf(mat, hit.n, w_o, w_i)
#define sample_bsdf(mat, hit, w_o, w_i_out, sample_pdf_out, mis_wpdf_out, rn2_dir, rn2_lobe, rng) \
    sample_simple_brdf(mat, hit.n, w_o, w_i_out, sample_pdf_out, mis_wpdf_out, rn2_dir)

#endif

#endif
