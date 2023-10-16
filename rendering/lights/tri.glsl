// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef TRI_LIGHTS_GLSL
#define TRI_LIGHTS_GLSL

#include "../language.glsl"
#include "../util.glsl"

#include "tri.h.glsl"

inline TriLight decode_tri_light(const TriLightData data) {
    TriLight light;
    light.v0 = GLM(vec3)(data.v0_x, data.v0_y, data.v0_z);
    light.v1 = GLM(vec3)(data.v1_x, data.v1_y, data.v1_z);
    light.v2 = GLM(vec3)(data.v2_x, data.v2_y, data.v2_z);
    light.radiance = GLM(vec3)(data.radiance_x, data.radiance_y, data.radiance_z);
    return light;
}

inline bool is_tri_facing_forward(vec3 v0, vec3 v1, vec3 v2) {
    return dot(cross(v0, v1), v2) < 0.0f;
}

inline vec3 sample_tri_light_position(const TriLight light, vec2 samples)
{
    float su0 = sqrt(samples.x);
    vec2 uv = vec2(1.0f - su0, samples.y * su0);
    return light.v0 + (light.v1 - light.v0) * uv.x + (light.v2 - light.v0) * uv.y;
}

inline float tri_light_pdf(const TriLight light,
                     const vec3 p,
                     const vec3 orig,
                     const vec3 dir)
{
    vec3 n = -cross(light.v1 - light.v0, light.v2 - light.v0);
    float nl = length(n);
    n /= nl;
    float surface_area = 0.5f * nl;
    vec3 to_pt = p - orig;
    float dist_sqr = dot(to_pt, to_pt);
    float n_dot_w = abs(dot(n, -dir)); // todo: no abs
    if (n_dot_w < EPSILON)
        return 0.f;
    return dist_sqr / (n_dot_w * surface_area);
}

// "BRDF Importance Sampling for Polygonal Lights"
// Copyright (C) 2021, Christoph Peters, Karlsruhe Institute of Technology
// Three-clause BSD license
// https://github.com/MomentsInGraphics/vulkan_renderer/blob/main/src/shaders/polygon_sampling.glsl

/*! A piecewise polynomial approximation to positive_atan(y). The maximal
	absolute error is 1.16e-05f. At least on Turing GPUs, it is faster but also
	significantly less accurate. The proper atan has at most 2 ulps of error
	there.*/
inline float fast_positive_atan(float y) {
	float rx;
	float ry;
	float rz;
	rx = (abs(y) > 1.0f) ? (1.0f / abs(y)) : abs(y);
	ry = rx * rx;
	rz = fma(ry, 0.02083509974181652f, -0.08513300120830536f);
	rz = fma(ry, rz, 0.18014100193977356f);
	rz = fma(ry, rz, -0.3302994966506958f);
	ry = fma(ry, rz, 0.9998660087585449f);
	rz = fma(-2.0f * ry, rx, float(0.5f * M_PI));
	rz = (abs(y) > 1.0f) ? rz : 0.0f;
	rx = fma(rx, ry, rz);
	return (y < 0.0f) ? (M_PI - rx) : rx;
}
/*! Returns an angle between 0 and M_PI such that tan(angle) == tangent. In
	other words, it is a version of atan() that is offset to be non-negative.
	Note that it may be switched to an approximate mode by the
	USE_BIASED_PROJECTED_SOLID_ANGLE_SAMPLING flag.*/
inline float positive_atan(float tangent) {
	return fast_positive_atan(tangent);
}

inline float half_triangle_solid_angle_tan(vec3 v0, vec3 v1, vec3 v2, GLSL_out(vec3) triangle_parameters) {
    // Prepare a Householder transform that maps vertex 0 onto (+/-1, 0, 0). We
	// only store the yz-components of that Householder vector and a factor of
	// 2.0f / sqrt(abs(polygon.vertex_dirs[0].x) + 1.0f) is pulled in there to
	// save on multiplications later. This approach is necessary to avoid
	// numerical instabilities in determinant computation below.
	float householder_sign = (v0.x > 0.0f) ? -1.0f : 1.0f;
	vec2 householder_yz = vec2(v0.y, v0.z) * (1.0f / (abs(v0.x) + 1.0f));
    // Compute solid angles
    float dot_0_1 = dot(v0, v1);
    float dot_0_2 = dot(v1, v2);
    float dot_1_2 = dot(v0, v2);
    // Compute the bottom right minor of vertices after application of the
    // Householder transform
    float dot_householder_0 = fma(-householder_sign, v1.x, dot_0_1);
    float dot_householder_2 = fma(-householder_sign, v2.x, dot_1_2);
    mat2 bottom_right_minor = mat2(
        fma2(vec2(-dot_householder_0), householder_yz, vec2(v1.y, v1.z)),
        fma2(vec2(-dot_householder_2), householder_yz, vec2(v2.y, v2.z)));
    // The absolute value of the determinant of vertices equals the 2x2
    // determinant because the Householder transform turns the first column
    // into (+/-1, 0, 0)
    float simplex_volume = abs(determinant(bottom_right_minor));
    // Compute the solid angle of the triangle using a formula proposed by:
    // A. Van Oosterom and J. Strackee, 1983, The Solid Angle of a
    // Plane Triangle, IEEE Transactions on Biomedical Engineering 30:2
    // https://doi.org/10.1109/TBME.1983.325207
    float dot_0_2_plus_1_2 = dot_0_2 + dot_1_2;
    float one_plus_dot_0_1 = 1.0f + dot_0_1;
    float tangent = simplex_volume / (one_plus_dot_0_1 + dot_0_2_plus_1_2);
    triangle_parameters = vec3(simplex_volume, dot_0_2_plus_1_2, one_plus_dot_0_1);
    return tangent;
}

inline float triangle_solid_angle(vec3 v0, vec3 v1, vec3 v2, GLSL_out(vec3) triangle_parameters) {
    float tangent = half_triangle_solid_angle_tan(v0, v1, v2, triangle_parameters);
    return 2.0f * positive_atan(tangent);
}

inline float approx_triangle_solid_angle(vec3 v0, vec3 v1, vec3 v2) {
    vec3 triangle_parameters;
    float tangent = half_triangle_solid_angle_tan(v0, v1, v2, triangle_parameters);
    return 2.0f * positive_atan(tangent);
}

/*! Given the output of prepare_solid_angle_polygon_sampling(), this function
	maps the given random numbers in the range from 0 to 1 to a normalized
	direction vector providing a sample of the solid angle of the polygon in
	the original space (used for arguments of
	prepare_solid_angle_polygon_sampling()). Samples are distributed in
	proportion to solid angle assuming uniform inputs.*/
inline vec3 sample_solid_angle_polygon(vec3 v0, vec3 v1, vec3 v2, float polygon_solid_angle, vec3 solid_angle_parameters, vec2 random_numbers) {
	// Decide which triangle needs to be sampled
	float target_solid_angle = polygon_solid_angle * random_numbers[0];
	float subtriangle_solid_angle = target_solid_angle;
    vec3 parameters = solid_angle_parameters;
	vec3 vertices[3] = { v1, v0, v2 };
	// Construct a new vertex 2 on the arc between vertices 0 and 2 such that
	// the resulting triangle has solid angle subtriangle_solid_angle
	vec2 cos_sin = vec2(cos(0.5f * subtriangle_solid_angle), sin(0.5f * subtriangle_solid_angle));
	vec3 offset = vertices[0] * (parameters[0] * cos_sin.x - parameters[1] * cos_sin.y) + vertices[2] * (parameters[2] * cos_sin.y);
	vec3 new_vertex_2 = fma3(2.0f * vec3(dot(vertices[0], offset) / dot(offset, offset)), offset, -vertices[0]);
	// Now sample the line between vertex 1 and the newly created vertex 2
	float s2 = dot(vertices[1], new_vertex_2);
	float s = mix_fma(1.0f, s2, random_numbers[1]);
	float denominator = fma(-s2, s2, 1.0f);
	float t_normed = sqrt(fma(-s, s, 1.0f) / denominator);
	// s2 may exceed one due to rounding error. random_numbers[1] is the
	// limit of t_normed for s2 -> 1.
	t_normed = (denominator > 0.0f) ? t_normed : random_numbers[1];
	return fma(-t_normed, s2, s) * vertices[1] + t_normed * new_vertex_2;
}

#endif

