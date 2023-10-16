// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "../defaults.glsl"
#include "../lights/tri.glsl"
#include "../pathspace.h"
#include "../bsdfs/hit_point.glsl"

// #define BINNED_LIGHTS_BIN_MAX_SIZE
// #define BINNED_LIGHTS_BIN_SIZE
// #define SCENE_GET_BINNED_LIGHTS_BIN_COUNT()
// #define SCENE_GET_LIGHT_SOURCE_COUNT()
// #define SCENE_GET_LIGHT_SOURCE()

#ifdef PROFILER_CLOCK
uint64_t light_sampling_cycles = 0;
#endif

inline vec3 sample_tri_lights(const vec3 hit_p, const vec3 hit_n
    , vec2 dir_sample, vec2 sel_sample
    , GLSL_out(vec3) light_dir, GLSL_out(float) light_dist
    , GLSL_out(float) pdf, GLSL_out(float) mis_wpdf)
{
    int num_lights = SCENE_GET_LIGHT_SOURCE_COUNT();

#ifdef PROFILER_CLOCK
    uint64_t start_lights_profiler = PROFILER_CLOCK();
#endif

#if defined(BINNED_LIGHTS_BIN_MAX_SIZE) && BINNED_LIGHTS_BIN_MAX_SIZE > 1
    int num_bins = SCENE_GET_BINNED_LIGHTS_BIN_COUNT();
    sel_sample.x *= float(num_bins);
    int bin_id = int(uint(sel_sample.x));
    bin_id = min(bin_id, num_bins - 1);
    float sel_p = 1.0f / float(num_bins);
    sel_sample.x -= float(bin_id);

    float light_contributions[BINNED_LIGHTS_BIN_MAX_SIZE];
    float total_contrib = 0.0f;
    const float MIN_IRRADIANCE = 6.2e-4f * 0.001f; // avoid divisions by zero

    int bin_end = min(BINNED_LIGHTS_BIN_SIZE * (bin_id+1), num_lights);
    UNROLL_FOR (int i = 0; i < BINNED_LIGHTS_BIN_MAX_SIZE; ++i) {
        int light_id = BINNED_LIGHTS_BIN_SIZE * bin_id + i;
        if (!(light_id < bin_end)) break;
        TriLight light = SCENE_GET_LIGHT_SOURCE(light_id);
        light.v0 -= hit_p;
        light.v1 -= hit_p;
        light.v2 -= hit_p;
        bool front_facing = is_tri_facing_forward(light.v0, light.v1, light.v2);
        float contrib = luminance(light.radiance);
        if ((dot(light.v0, hit_n) > 0.0f || dot(light.v1, hit_n) > 0.0f || dot(light.v2, hit_n) > 0.0f) && front_facing) {
            // todo: backfacing test once we know which way is back
            light.v0 = normalize(light.v0);
            light.v1 = normalize(light.v1);
            light.v2 = normalize(light.v2);
            contrib *= approx_triangle_solid_angle(light.v0, light.v1, light.v2);
        }
        else
            contrib = 0.0f;
        contrib += MIN_IRRADIANCE;
        light_contributions[i] = contrib;
        total_contrib += contrib;
    }

    float p = 0.0f;
    float t = 0.0f;
    int light_id;
    UNROLL_FOR (int i = 0; i < BINNED_LIGHTS_BIN_MAX_SIZE; ++i) {
        light_id = BINNED_LIGHTS_BIN_SIZE * bin_id + i;
        if (!(light_id < bin_end)) break;
        p = light_contributions[i] / total_contrib;
        t += p;
        if (sel_sample.y < t)
            break;
    }
    sel_p *= p;
    sel_sample.y = (sel_sample.y - t) / p + 1.0f;
#else
    int light_id = int(uint(sel_sample.x * num_lights));
    light_id = min(light_id, num_lights - 1);
    float sel_p = 1.0f / float(num_lights);
#endif
    TriLight light = SCENE_GET_LIGHT_SOURCE(light_id);

#define SOLID_ANGLE_SAMPLING
#ifdef SOLID_ANGLE_SAMPLING
    vec3 d0 = normalize(light.v0 - hit_p);
    vec3 d1 = normalize(light.v1 - hit_p);
    vec3 d2 = normalize(light.v2 - hit_p);
    vec3 tri_parameters;
    float polygon_solid_angle = triangle_solid_angle(d0, d1, d2, tri_parameters);
    light_dir = sample_solid_angle_polygon(d0, d1, d2, polygon_solid_angle, tri_parameters, dir_sample);
    pdf = 1.0f / polygon_solid_angle;

    vec3 e0 = light.v1 - light.v0;
    vec3 e1 = light.v2 - light.v0;
    vec3 e_n = cross(e0, e1);
    light_dist = dot(light.v0 - hit_p, e_n) / dot(light_dir, e_n);
    //vec3 light_pos = hit_p + light_dir * light_dist;
    mis_wpdf = 2.0f * light_dist * light_dist / abs(dot(light_dir, e_n));
#else
    vec3 light_pos = sample_tri_light_position(light, dir_sample);
    light_dir = light_pos - hit_p;
    light_dist = length(light_dir);
    light_dir /= light_dist;

    vec3 e0 = light.v1 - light.v0;
    vec3 e1 = light.v2 - light.v0;
    vec3 e_n = cross(e0, e1);
    mis_wpdf = 2.0f * light_dist * light_dist / abs(dot(light_dir, e_n));

    pdf = tri_light_pdf(light, light_pos, hit_p, light_dir);
#endif

#ifdef PROFILER_CLOCK
    light_sampling_cycles += PROFILER_CLOCK() - start_lights_profiler;
#endif

    pdf *= sel_p;
#if defined(BINNED_LIGHTS_BIN_MAX_SIZE) && BINNED_LIGHTS_BIN_MAX_SIZE > 1
    mis_wpdf /= float(num_bins);
#else
    mis_wpdf /= float(num_lights);
#endif
    return 1.0f * light.radiance / pdf;
}

inline float approx_tri_lights_pdf(float approx_solid_angle) {
#if defined(BINNED_LIGHTS_BIN_MAX_SIZE) && BINNED_LIGHTS_BIN_MAX_SIZE > 1
    int num_bins = SCENE_GET_BINNED_LIGHTS_BIN_COUNT();
    return 1.0f / (float(num_bins) * approx_solid_angle);
#else
    int num_lights = SCENE_GET_LIGHT_SOURCE_COUNT();
    return 1.0f / (float(num_lights) * approx_solid_angle);
#endif
}
