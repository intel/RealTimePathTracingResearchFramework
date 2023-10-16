// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef RT_FOOTPRINT_GLSL
#define RT_FOOTPRINT_GLSL

#include "../defaults.glsl"
#include "../util.glsl"

mat2 dpdxy_to_footprint(vec3 ray_dir, vec3 dpdx, vec3 dpdy) {
    vec3 t, b;
    ortho_basis(t, b, ray_dir);
    mat2 F = transpose(mat2x3(t, b)) * mat2x3(dpdx, dpdy);
    return F * transpose(F);
}

uvec2 encode_footprint(mat2 F, float scale) {
    return uvec2(packHalf2x16(vec2(F[0][0], F[1][1])), packHalf2x16(vec2(F[0][1], scale)));
}

mat2 decode_footprint(uvec2 e, GLSL_out(float) scale) {
    vec2 ab = unpackHalf2x16(e.x);
    vec2 cs = unpackHalf2x16(e.y);
    scale = cs.y;
    return mat2(ab.x, cs.x, cs.x, ab.y);
}

mat2 transform_footprint(vec3 dst_ray_dir, mat3 T, vec3 src_ray_dir, mat2 F) {
    vec3 t, b;
    ortho_basis(t, b, src_ray_dir);
    mat2x3 T2 = T * mat2x3(t, b);
    ortho_basis(t, b, dst_ray_dir);
    mat2 T3 = transpose(mat2x3(t, b)) * T2;
    return T3 * F * transpose(T3);
}

// todo: account for off-center surface reflections
mat2 reflect_footprint(vec3 dst_ray_dir, vec3 src_ray_dir, mat2 F) {
    vec3 n = normalize(dst_ray_dir - src_ray_dir);
    mat3 R = mat3(1.0f) - 2.0f * outerProduct(n, n);
    return transform_footprint(dst_ray_dir, R, src_ray_dir, F);
}

void footprint_to_dpdxy(GLSL_out(vec3) dpdx, GLSL_out(vec3) dpdy, vec3 ray_dir, mat2 F) {
    // float A = 1.0f;
    float B = F[0][0] + F[1][1]; // * -1
    float C = F[0][0] * F[1][1] - F[0][1] * F[1][0];
    float D = sqrt(B * B * 0.25f - C);
    vec2 ev = vec2(0.5f * B - D, 0.5f * B + D);
    mat2 X;
    if (abs(F[0][1]) > 3.0e-39f) {
        X[0] = vec2(F[1][0], ev.x - F[0][0]);
        X[1] = vec2(ev.y - F[1][1], F[0][1]);
    } else
        X = mat2(1.0f);
    vec3 t, b;
    ortho_basis(t, b, ray_dir);
    dpdx = mat2x3(t, b) * normalize(X[0]) * sqrt(ev.x);
    dpdy = mat2x3(t, b) * normalize(X[1]) * sqrt(ev.y);
}

float roughness_to_reliability_change(float det_footprint, float roughness) {
    return M_PI * 4.0f * ((roughness * roughness) * (roughness * roughness))
        / det_footprint;
}

float next_reliability(float reliability, float reliability_change) {
    //return 1.0f / (1.0f / reliability + reliability_change);
    return reliability / (1.0f + reliability * reliability_change);
}

float reliability_roughening(float det_footprint, float reliability, float roughness) {
    float matAlpha = roughness * roughness;
    // + camera_footprint: do not include camera footprint for now, roughness was specified for direct visibility after all
    float indirectFootprint = max(1.0f / reliability - 1.0f, 0.0f);
    float transportSpreadNrm = indirectFootprint * det_footprint;
    float roughenedAlpha = sqrt(min(max(matAlpha * matAlpha, 0.25f * M_1_PI * transportSpreadNrm), 1.0f));
    return sqrt(roughenedAlpha);
}

#endif // RT_FOOTPRINT_GLSL

