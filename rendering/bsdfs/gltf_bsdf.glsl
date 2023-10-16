// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef GLTF_BSDF_GLSL
#define GLTF_BSDF_GLSL

#include "../defaults.glsl"
#include "../util.glsl"

#ifdef MEGAKERNEL_MATERIALS
#define GLTF_SUPPORT_TRANSMISSION
#define GLTF_SUPPORT_TRANSMISSION_ROUGHNESS
#endif

struct GLTFMaterial {
    vec3 base_color;
    float metallic;

    float specular;
    float roughness;
    float ior;
#ifdef GLTF_SUPPORT_TINT
    float specular_tint;
#endif
#ifdef GLTF_SUPPORT_TRANSMISSION
#ifdef GLTF_SUPPORT_TRANSMISSION_ROUGHNESS
    float transmission_roughness;
#endif
    
    float specular_transmission;
    vec3 transmission_color;
#endif

    uint32_t flags;
};

#ifdef ENABLE_MATERIAL_DECODE
inline void load_material(GLSL_inout(GLTFMaterial) mat, GLSL_in(MATERIAL_PARAMS) p, GLSL_in(HitPoint) hit, GLSL_inout(float) alpha) {
#ifdef GLTF_SUPPORT_TINT
    mat.specular_tint = textured_scalar_param(p.specular_tint, hit);
#endif
#ifdef GLTF_SUPPORT_TRANSMISSION
    mat.specular_transmission = textured_scalar_param(p.specular_transmission, hit);
    if (mat.specular_transmission > 0.0f) {
        if (!(mat.ior > 1.0f)) {
            alpha *= 1.0f - mat.specular_transmission;
            mat.specular_transmission = 0.0f;
        }
        else {
            //mat.transmission_color = vec3(textured_color_param(vec4(p.transmission_color, 1.0f), hit));
            mat.transmission_color = mat.base_color;
#ifdef GLTF_SUPPORT_TRANSMISSION_ROUGHNESS
            mat.transmission_roughness = mat.roughness;
            mat.roughness = sqrt(textured_scalar_param(p.clearcoat_gloss, hit));
#endif
        }
    }
    else
        mat.transmission_color = vec3(0.0f);
#endif
    mat.flags = p.flags;
}

inline void apply_roughening(GLSL_inout(GLTFMaterial) mat, float roughening) {
    float minRoughness = sqrt(roughening);
    mat.roughness = max(mat.roughness, minRoughness);
#ifdef GLTF_SUPPORT_TRANSMISSION_ROUGHNESS
    mat.transmission_roughness = max(mat.roughness, minRoughness);
#endif
}
#endif

// https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation
// 
// Formal material definitions:
// 

// material = mix(dielectric_brdf, metal_brdf, metallic)

// metal_brdf =
//   conductor_fresnel(
//     f0 = baseColor,
//     bsdf = specular_brdf(
//       α = roughness ^ 2))

// dielectric_brdf =
//  fresnel_mix(
//    ior = 1.5,
//    base = diffuse_brdf(
//      color = baseColor),
//    layer = specular_brdf(
//      α = roughness ^ 2))

// MicrofacetBRDF = GD / (4|N⋅L||N⋅V|)

// Trowbridge-Reitz/GGX microfacet distribution:
// D = α^2 χ^+(N⋅H) / ( π * ( (N⋅H)^2 (α^2 − 1) + 1)^2 )
// G =
//   2|N⋅L| χ^+(H⋅L) / ( |N⋅L| + sqrt( α^2 + (1−α^2) (N⋅L)^2 ) )
// * 2|N⋅V| χ^+(H⋅V) / ( |N⋅V| + sqrt( α^2 + (1−α^2) (N⋅V)^2 ) )
// V = G / (4 |N⋅L| |N⋅V|)
// MicrofacetBRDF = VD
// V =
//   χ^+(H⋅L) / ( |N⋅L| + sqrt( α^2 + (1−α^2) (N⋅L)^2 ) )
// * χ^+(H⋅V) / ( |N⋅V| + sqrt( α^2 + (1−α^2) (N⋅V)^2 ) )

// specular_brdf(α) = MicrofacetBRDF
// diffuse_brdf(color) = (1/pi) * color

// function conductor_fresnel(f0, bsdf) {
//  return bsdf * (f0 + (1 - f0) * (1 - abs(VdotH))^5)
// }
// function fresnel_mix(ior, base, layer) {
//   f0 = ((1-ior)/(1+ior))^2
//   fr = f0 + (1 - f0)*(1 - abs(VdotH))^5
//   return mix(base, layer, fr)
// }

// Implementation notes:
// metal_brdf = specular_brdf(roughness^2) * (baseColor.rgb + (1 - baseColor.rgb) * (1 - abs(VdotH))^5)
// dielectric_brdf = mix(diffuse_brdf(baseColor.rgb), specular_brdf(roughness^2), 0.04 + (1 - 0.04) * (1 - abs(VdotH))^5)

// Optimized with values shared for both models:
// const black = 0

// c_diff = lerp(baseColor.rgb, black, metallic)
// f0 = lerp(0.04, baseColor.rgb, metallic)
// α = roughness^2

// F = f0 + (1 - f0) * (1 - abs(VdotH))^5

// f_diffuse = (1 - F) * (1 / π) * c_diff
// f_specular = F * D(α) * G(α) / (4 * abs(VdotN) * abs(LdotN))

// material = f_diffuse + f_specular

// https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md
// 
// Transmission extension:
// 

// dielectric_brdf =
//   fresnel_mix(
//     ior = 1.5,
//     base = mix(
//       diffuse_brdf(baseColor),
//       specular_btdf(α = roughness^2) * baseColor,
//       transmission),
//     layer = specular_brdf(α = roughness^2))

// Trowbridge-Reitz/GGX microfacet distribution:
// D = α^2 χ^+(N⋅H) / ( π * ( (N⋅H)^2 (α^2 − 1) + 1)^2 )
// G =
//   2|N⋅L| χ^+(H⋅L / N⋅L) / ( |N⋅L| + sqrt( α^2 + (1−α^2) (N⋅L)^2 ) )
// * 2|N⋅V| χ^+(H⋅V / N⋅V) / ( |N⋅V| + sqrt( α^2 + (1−α^2) (N⋅V)^2 ) )
// V = G / (4 |N⋅L| |N⋅V|)
// MicrofacetBTDF = VD
// V =
//   χ^+(H⋅L / N⋅L) / ( |N⋅L| + sqrt( α^2 + (1−α^2) (N⋅L)^2 ) )
// * χ^+(H⋅V / N⋅V) / ( |N⋅V| + sqrt( α^2 + (1−α^2) (N⋅V)^2 ) )

// specular_btdf(α) = MicrofacetBTDF


// https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_ior/README.md
//
// IOR extension
//
// const dielectricSpecular = ((ior - 1)/(ior + 1))^2

// F = f0 + (1 - f0) * (1 - abs(VdotH))^5
inline float schlick_weight(float local_cos_theta) {
    return pow(clamp(1.f - local_cos_theta, 0.f, 1.f), 5);
}

// Complete Fresnel Dielectric computation, for transmission at ior near 1
// they mention having issues with the Schlick approximation.
// eta_i: material on incident side's ior
// eta_t: material on transmitted side's ior
inline float fresnel_dielectric(float cos_theta_i, float eta_i, float eta_t) {
    float g = pow2(eta_t) / pow2(eta_i) - 1.f + pow2(cos_theta_i);
    if (g < 0.f) {
        return 1.f;
    }
    return 0.5f * pow2(g - cos_theta_i) / pow2(g + cos_theta_i)
        * (1.f + pow2(cos_theta_i * (g + cos_theta_i) - 1.f) / pow2(cos_theta_i * (g - cos_theta_i) + 1.f));
}

// D_GTR2: Generalized Trowbridge-Reitz with gamma=2 (Burley notes eq. 8)
// * matches GLTF2:
// * does not perform microfacet visibility check
// D = α^2 χ^+(N⋅H) / ( π * ( (N⋅H)^2 (α^2 − 1) + 1)^2 )
inline float gtr_2(float cos_theta_h, float alpha) {
    float alpha_sqr = alpha * alpha;
    return M_1_PI * alpha_sqr
        / pow2(1.f + (alpha_sqr - 1.f) * cos_theta_h * cos_theta_h);
}

inline float smith_visibility_den1(float n_dot_o, float alpha_sq) {
    return abs(n_dot_o) + sqrt(alpha_sq + (1.0f - alpha_sq) * n_dot_o * n_dot_o);
}
// V =
//   χ^+(H⋅L) / ( |N⋅L| + sqrt( α^2 + (1−α^2) (N⋅L)^2 ) )
// * χ^+(H⋅V) / ( |N⋅V| + sqrt( α^2 + (1−α^2) (N⋅V)^2 ) )
// note: does not perform microfacet/inout sign checks!
inline float smith_visibility_ggx(float n_dot_o, float n_dot_i, float alpha_g) {
    float a = alpha_g * alpha_g;
    float den_shad = smith_visibility_den1(n_dot_i, a);
    float den_mask = smith_visibility_den1(n_dot_o, a);
    return 1.f / (den_shad * den_mask);
}

// converts a 2D random number to a sample on a cylindrical pipe in [(-1, -1, 0), (1, 1, 1)]
// such samples can easily be converted into spheres, hemispheres, and visible normals
inline vec3 to_pipe_sample(const vec2 U) {
    float phi = 2.0f * M_PI * U.x;
    float x = cos(phi);
    float y = sin(phi);
    float h = U.y;
    return vec3(x, y, h);
}

// uniform point sample on the unit sphere
inline vec3 sample_sphere(vec3 UP) {
    float cos_theta = UP.z * 2.0f - 1.0f;
    float sin_theta = sqrt(max(1.0f - cos_theta * cos_theta, 0.0f));
    return vec3(sin_theta * UP.x, sin_theta * UP.y, cos_theta);
}

// Dupuy and Benyoub, Sampling Visible GGX Normals with Spherical Caps
// https://arxiv.org/abs/2306.05044
// Output Ne: normal sampled with PDF D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z
inline vec3 sample_gtr_2_vndf(const vec3 w_o_local, vec2 alpha, const vec3 UP) {
    // warp to the hemisphere configuration
    vec3 wiStd = normalize(vec3(alpha.x * w_o_local.x, alpha.y * w_o_local.y, w_o_local.z));
    // sample a spherical cap in (-wiStd.z, 1]
    float z = fma((1.0f - UP.z), (1.0f + wiStd.z), -wiStd.z);
    float sinTheta = sqrt(clamp(1.0f - z * z, 0.0f, 1.0f));
    float x = sinTheta * UP.x;
    float y = sinTheta * UP.y;
    // compute half-vector
    vec3 wmStd = vec3(x, y, z) + wiStd;
    
    // warp back to the ellipsoid configuration
    vec3 wm = vec3(wmStd.x * alpha.x, wmStd.y * alpha.y, max(0.0f, wmStd.z));
    float wmL = length(wm);

    // return final normal
    return wm / wmL;
}

// does not check visibility!
inline float gtr_2_vndf_pdf(float n_dot_o, float cos_theta_h, float alpha) {
    return gtr_2(cos_theta_h, alpha)
        // note: (2 * n_dot_o) / n_dot_o * h_dot_o / (4 * h_dot_o) = 0.5
        * (0.5f / smith_visibility_den1(n_dot_o, alpha * alpha));
}

inline vec3 gltf_diffuse_basecolor(const GLTFMaterial mat) {
    return (1.0f - mat.metallic) * mat.base_color;
}

inline vec3 gltf_specular_basecolor(const GLTFMaterial mat, float ior) {
    // const dielectricSpecular = ((ior - 1)/(ior + 1))^2
    vec3 dielectric_base = vec3( pow2((ior - 1.0f) / (ior + 1.0f)) );
    // f0 = lerp(0.04, baseColor.rgb, metallic)
#ifdef GLTF_SUPPORT_TINT
    if (mat.specular_tint > 0.0f) {
        dielectric_base *= mix(vec3(1.0f), mat.base_color / luminance(mat.base_color), mat.specular_tint);
    }
#endif
    return mix(dielectric_base, mat.base_color, mat.metallic);
}

inline float gltf_specular_alpha(const GLTFMaterial mat) {
    return max(mat.roughness * mat.roughness, 0.002f);
}
#ifdef GLTF_SUPPORT_TRANSMISSION_ROUGHNESS
inline float gltf_transmission_alpha(const GLTFMaterial mat) {
    return max(mat.transmission_roughness * mat.transmission_roughness, 0.002f);
}
#endif

inline float gltf_schlick_weight(float local_o_dot_h, float ior) {
    float f_weight = schlick_weight(local_o_dot_h);
    // fix up reflectance towards critical angle, if necessary
    if (ior < 1.0f) {
        float cos_critical = sqrt(1.0f - ior * ior);
        f_weight = mix(f_weight, 1.0f, min((1.0f - local_o_dot_h) / (1.0f - cos_critical), 1.0f));
    }
    return f_weight;
}

inline vec3 gltf_bsdf(const GLTFMaterial mat, const vec3 n,
    const vec3 w_o, const vec3 w_i, const vec3 v_x, const vec3 v_y)
{
    float i_dot_n = dot(n, w_i);
    float o_dot_n = dot(n, w_o);
    float ior = o_dot_n < 0.0f ? 1.0f / mat.ior : mat.ior;

    vec3 w_h;
    if (i_dot_n * o_dot_n < 0.0f) {
#ifndef GLTF_SUPPORT_TRANSMISSION
        return vec3(0.0f);
#else
        if (!(mat.specular_transmission > 0.f))
            return vec3(0.0f);
        if ((mat.flags & BASE_MATERIAL_ONESIDED) != 0)
            w_h = -ior * w_i - w_o;
        else
            w_h = reflect(w_i, n) + w_o;
        // w_h is on the side of the thinner medium (exterior)
        if (!(dot(w_h, n) > 0.0f))
            return vec3(0.0f);
#endif
    }
    else
        w_h = w_i + w_o;
    
    w_h = normalize(w_h);
    float o_dot_h = dot(w_o, w_h), i_dot_h = dot(w_i, w_h);

    vec3 diffuse = gltf_diffuse_basecolor(mat) * float(M_1_PI);
    vec3 specular = vec3(0.0f);
    if (mat.ior > 1.0f) { // otherwise assert: mat.specular_transmission == 0
        vec3 f0 = gltf_specular_basecolor(mat, mat.ior);

        float specular_alpha = gltf_specular_alpha(mat);
#ifdef GLTF_SUPPORT_TRANSMISSION_ROUGHNESS
        if (i_dot_n * o_dot_n < 0.0f)
            specular_alpha = gltf_transmission_alpha(mat);
#endif
        float specular_refl = gtr_2(dot(n, w_h), specular_alpha);
        specular_refl *= smith_visibility_ggx(o_dot_n, i_dot_n, specular_alpha);
        
        float f_weight = gltf_schlick_weight(abs(o_dot_h), ior);
        vec3 F = mix(f0, vec3(1.0f), f_weight);

#ifdef GLTF_SUPPORT_TRANSMISSION
        if (i_dot_n * o_dot_n < 0.0f) {
            diffuse = vec3(0.0f);
            specular = specular_refl * (1.f - mat.metallic) * mat.specular_transmission * mat.transmission_color * (vec3(1.0f) - F);
            // transmission angle compression
            if ((mat.flags & BASE_MATERIAL_ONESIDED) != 0) {
                float angle_compression = 2.0f * o_dot_h / (i_dot_h * ior + o_dot_h);
                specular *= angle_compression * angle_compression;
            }
        } else {
            diffuse *= (1.0f - mat.specular_transmission);
#else
        { // with or without transmission:
#endif
            diffuse *= (vec3(1.0f) - F);
            specular = specular_refl * F;
        }
    }
    
    return diffuse + specular;
}

#ifdef GLTF_SUPPORT_TRANSMISSION
    #define GLTF_COMPONENT_COUNT 3
#else
    #define GLTF_COMPONENT_COUNT 2
#endif
struct GLTFComponentSampler {
    float weights[GLTF_COMPONENT_COUNT];
};
inline GLTFComponentSampler gltf_component_sampler(const GLTFMaterial mat, float ior, vec4 o_dot_h, vec4 visibility, vec3 w_o) {
    GLTFComponentSampler components;
    float specular_base_lum = luminance(gltf_specular_basecolor(mat, mat.ior));
    // note: these should be caught by common subexpression elimination for MIS PDF!
    float F0 = mix(specular_base_lum, 1.0f, gltf_schlick_weight(o_dot_h.x, 1.0f));
    float F1 = mix(specular_base_lum, 1.0f, gltf_schlick_weight(o_dot_h.y, 1.0f));
    float F2 = mix(specular_base_lum, 1.0f, gltf_schlick_weight(o_dot_h.z, ior));

    components.weights[0] = (1.0f - F0) * visibility.x * (1.0f - mat.metallic) * luminance(gltf_diffuse_basecolor(mat));
    components.weights[1] = F1 * visibility.y;
#ifdef GLTF_SUPPORT_TRANSMISSION
    components.weights[0] *= (1.0f - mat.specular_transmission);
    components.weights[2] = (1.0f - F2) * visibility.z * (1.0f - mat.metallic) * mat.specular_transmission;
#endif

    float weight_sum = 0.0f;
    for (int i = 0; i < GLTF_COMPONENT_COUNT; ++i)
        weight_sum += components.weights[i];
    if (weight_sum > 0.0f) {
        for (int i = 0; i < GLTF_COMPONENT_COUNT; ++i)
            components.weights[i] /= weight_sum;
    }
    else
        components.weights[0] = 1.0f;
    return components;
}
inline int glft_sample_reuse_component(GLTFComponentSampler components, GLSL_inout(float) rnd, GLSL_out(float) component_probability) {
    int component = 0;
    float next_layer_p_base = 0.0f, layer_p_base = 0.0f;
    for (int i = 0; i < GLTF_COMPONENT_COUNT; ++i) {
        float layer_p = components.weights[i];
        if (layer_p > 0.0f && rnd >= next_layer_p_base) {
            component = i;
            component_probability = layer_p;
            layer_p_base = next_layer_p_base;
        }
        next_layer_p_base += layer_p;
    }
    rnd = min(1.0f, (rnd - layer_p_base) / component_probability);
    return component;
}

// note: this PDF does not actually match the sampling PDF, since not all sampled
// information can be reconstructed precisely. the layer sampling probabilities
// may therefore deviate from those used during sampling.
inline float gltf_wpdf(const GLTFMaterial mat, const vec3 n,
    const vec3 w_o, const vec3 w_i, const vec3 v_x, const vec3 v_y)
{
    float i_dot_n = dot(n, w_i);
    float o_dot_n = dot(n, w_o);
    float ior = o_dot_n < 0.0f ? 1.0f / mat.ior : mat.ior;

    float pdf = M_1_PI * abs(i_dot_n);

    if (mat.ior > 1.0f) { // otherwise assert: mat.specular_transmission == 0
        vec3 w_h;
        if (i_dot_n * o_dot_n < 0.0f) {
#ifndef GLTF_SUPPORT_TRANSMISSION
            return 0.0f;
#else
            if (!(mat.specular_transmission > 0.f))
                return 0.0f;
            if ((mat.flags & BASE_MATERIAL_ONESIDED) != 0)
                w_h = -ior * w_i - w_o;
            else
                w_h = reflect(w_i, n) + w_o;
            // w_h is on the side of the thinner medium (exterior)
            if (!(dot(w_h, n) > 0.0f))
                return 0.0f;
#endif
        }
        else
            w_h = w_i + w_o;
        
        w_h = normalize(w_h);
        float o_dot_h = dot(w_o, w_h), i_dot_h = dot(w_i, w_h);
        float cos_theta_h = dot(w_h, n);

        vec4 visibility = vec4(0.0f);
        visibility.x = 1.0f;

        float specular_alpha = gltf_specular_alpha(mat);
        // note: i_dot_n may deviate from actual sampling PDF computations on refraction/reflection mismatch
        visibility.y = 2.0f * abs(i_dot_n) / smith_visibility_den1(i_dot_n, specular_alpha * specular_alpha);
#ifdef GLTF_SUPPORT_TRANSMISSION
        visibility.z = visibility.y;
#ifdef GLTF_SUPPORT_TRANSMISSION_ROUGHNESS
        float transmission_alpha = specular_alpha;
        if (mat.specular_transmission > 0.f) {
            transmission_alpha = gltf_transmission_alpha(mat);
            // note: i_dot_n may deviate from actual sampling PDF computations on refraction/reflection mismatch
            visibility.z = 2.0f * abs(i_dot_n) / smith_visibility_den1(i_dot_n, transmission_alpha * transmission_alpha);
        }
#endif
#endif
        // note: the layer probabilities produced here are only an approximation, as we cannot reconstruct all
        // of the original o_dot_h produced for different layers in the actual sampling
        GLTFComponentSampler components = gltf_component_sampler(mat, ior, vec4(abs(o_dot_h)), visibility, w_o);

#ifdef GLTF_SUPPORT_TRANSMISSION_ROUGHNESS
        // note: sampling of component does not respect transmission roughness
        if (i_dot_n * o_dot_n < 0.0f)
            specular_alpha = transmission_alpha;
#endif

        float specular = gtr_2_vndf_pdf(o_dot_n, cos_theta_h, specular_alpha);

#ifdef GLTF_SUPPORT_TRANSMISSION
        if (i_dot_n * o_dot_n < 0.0f) {
            // transmission angle compression
            if ((mat.flags & BASE_MATERIAL_ONESIDED) != 0) {
                float angle_compression = 2.0f * o_dot_h / (i_dot_h * ior + o_dot_h);
                specular *= angle_compression * angle_compression;
            }
            pdf = specular * components.weights[2]; // todo: angle compression?!
        }
        else
#endif
        {
            pdf *= components.weights[0]; // diffuse
            pdf += specular * components.weights[1];
        }
    }
    
    return pdf;
}

inline vec3 sample_gltf_brdf(const GLTFMaterial mat, const vec3 n, const vec3 w_o,
    GLSL_out(vec3) w_i, GLSL_out(float) pdf, GLSL_out(float) mis_wpdf,
    vec2 rng_sample, vec2 fresnel_sample, const vec3 v_x, const vec3 v_y)
{
    vec3 w_o_local = transpose(mat3(v_x, v_y, n)) * w_o;

    float o_dot_n = w_o_local.z;
#ifdef GLTF_SUPPORT_TRANSMISSION
    float ior = o_dot_n < 0.0f ? 1.0f / mat.ior : mat.ior;
    if (o_dot_n < 0.0f)
        w_o_local.z = -w_o_local.z;
#else
    float ior = mat.ior;
    if (o_dot_n < 0.0f) {
        pdf = 0.0f;
        return vec3(0.0f);
    }
#endif

    vec3 UP = to_pipe_sample(rng_sample);
    vec3 w_i_diffuse = normalize( n + sample_sphere(UP) );
#ifdef GLTF_SUPPORT_TRANSMISSION
    if (o_dot_n < 0.0f)
        w_i_diffuse = -w_i_diffuse;
#endif

    float specular_alpha = gltf_specular_alpha(mat);

    int component = 0;
    float component_selection_pdf = 0.0f;
    GLTFComponentSampler components;
    vec3 w_h_specular_local;
#ifdef GLTF_SUPPORT_TRANSMISSION
    vec3 w_h_transmission_local;
#endif
    if (mat.ior > 1.0f) {
        vec4 o_dot_h_all = vec4(0.0f);
        vec4 visibility_all = vec4(0.0f);
        // diffuse component
        o_dot_h_all.x = cos_half_angle( dot(w_o, w_i_diffuse) );
        visibility_all.x = 1.0f;
        // specular component
        w_h_specular_local = sample_gtr_2_vndf(w_o_local, vec2(specular_alpha), UP);
        // assert: w_h_specular_local should always be visible
        o_dot_h_all.y = dot(w_o_local, w_h_specular_local);
        float spec_i_dot_n_local = reflect(-w_o_local, w_h_specular_local).z;
        visibility_all.y = spec_i_dot_n_local > 0.0f
            ? 2.0f * spec_i_dot_n_local / smith_visibility_den1(spec_i_dot_n_local, specular_alpha * specular_alpha)
            : 0.0f;
#ifdef GLTF_SUPPORT_TRANSMISSION
        float transmission_alpha = specular_alpha;
        w_h_transmission_local = w_h_specular_local;
        o_dot_h_all.z = o_dot_h_all.y;
        float trans_i_dot_n_local = spec_i_dot_n_local;
        if (mat.specular_transmission > 0.f) {
#ifdef GLTF_SUPPORT_TRANSMISSION_ROUGHNESS
            transmission_alpha = gltf_transmission_alpha(mat);
            w_h_transmission_local = sample_gtr_2_vndf(w_o_local, vec2(transmission_alpha), UP);
            o_dot_h_all.z = dot(w_o_local, w_h_transmission_local);
#endif
            if ((mat.flags & BASE_MATERIAL_ONESIDED) != 0)
                trans_i_dot_n_local = -refract(-w_o_local, w_h_transmission_local, 1.0f / ior).z;
            else
                trans_i_dot_n_local = reflect(-w_o_local, w_h_transmission_local).z;
            visibility_all.z = trans_i_dot_n_local > 0.0f
                ? 2.0f * trans_i_dot_n_local / smith_visibility_den1(trans_i_dot_n_local, transmission_alpha * transmission_alpha)
                : 0.0f;
        }
#endif

        components = gltf_component_sampler(mat, ior, o_dot_h_all, visibility_all, w_o);
        component = glft_sample_reuse_component(components, fresnel_sample.x, component_selection_pdf);
    }

    float cos_theta_h;
    float i_dot_h;
    float o_dot_h;

    if (component == 0) {
        w_i = w_i_diffuse;
        vec3 w_h = normalize(w_i + w_o);
        cos_theta_h = dot(n, w_h);
        i_dot_h = o_dot_h = dot(w_o, w_h);
    } else {
#ifdef GLTF_SUPPORT_TRANSMISSION_ROUGHNESS
        if (component == 2) {
            specular_alpha = gltf_transmission_alpha(mat);
            w_h_specular_local = w_h_transmission_local;
        }
#endif
        vec3 w_h = w_h_specular_local;
#ifdef GLTF_SUPPORT_TRANSMISSION
        if (o_dot_n < 0.0f)
            w_h.z = -w_h.z; // flip into original frame if necessary
#endif
        cos_theta_h = w_h.z; // negative below hemisphere (angle measured from thinner side)
        w_h = mat3(v_x, v_y, n) * w_h; // same sign as w_o
        i_dot_h = o_dot_h = dot(w_o, w_h);
        // assert: w_h_local should always be visible
#ifdef GLTF_SUPPORT_TRANSMISSION
        if (component != 1) {
            if ((mat.flags & BASE_MATERIAL_ONESIDED) != 0) {
                w_i = refract(-w_o, w_h, 1.0f / ior);
                i_dot_h = dot(w_i, w_h);
            }
            else
                w_i = reflect(reflect(-w_o, w_h), n);
        } else
#endif
        {
            w_i = reflect(-w_o, w_h);
        }
    }
    float i_dot_n = dot(n, w_i);
#ifdef GLTF_SUPPORT_TRANSMISSION
    if ((i_dot_n * o_dot_n > 0.0f) != (component != 2)) {
#else
    if (!(i_dot_n > 0.0f)) {
#endif
        pdf = 0.0f;
        return vec3(0.0f);
    }

    float diffuse = M_1_PI * abs(i_dot_n);
    pdf = diffuse;

    if (mat.ior > 1.0f) {
        pdf *= components.weights[0]; // diffuse

        float specular = gtr_2_vndf_pdf(o_dot_n, cos_theta_h, specular_alpha); // todo: numerical precision, directly evaluate specular contribution :/
#ifdef GLTF_SUPPORT_TRANSMISSION
        if (i_dot_n * o_dot_n < 0.0f) {
            // transmission angle compression
            if ((mat.flags & BASE_MATERIAL_ONESIDED) != 0) {
                float angle_compression = 2.0f * o_dot_h / (i_dot_h * ior + o_dot_h);
                specular *= angle_compression * angle_compression;
            }
            pdf = specular * components.weights[2];
        }
        else
#endif
            pdf += specular * components.weights[1];
    }
    if (!(pdf > 0.0f))
        return vec3(0.0f);

    vec3 result = gltf_bsdf(mat, n, w_o, w_i, v_x, v_y);
    mis_wpdf = gltf_wpdf(mat, n, w_o, w_i, v_x, v_y);
    return result * abs(i_dot_n) / pdf;
}

#ifndef NO_MATERIAL_REGISTRATION

#define MATERIAL_TYPE GLTFMaterial
#define eval_bsdf(mat, hit, w_o, w_i) \
    gltf_bsdf(mat, hit.n, w_o, w_i, hit.v_x, hit.v_y)
#define eval_bsdf_wpdf(mat, hit, w_o, w_i) \
    gltf_wpdf(mat, hit.n, w_o, w_i, hit.v_x, hit.v_y)
#define sample_bsdf(mat, hit, w_o, w_i_out, sample_pdf_out, mis_wpdf_out, rn2_dir, rn2_lobe, rng) \
    sample_gltf_brdf(mat, hit.n, w_o, w_i_out, sample_pdf_out, mis_wpdf_out, rn2_dir, rn2_lobe, hit.v_x, hit.v_y)

#endif

#endif
