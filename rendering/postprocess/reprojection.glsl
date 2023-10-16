// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "../defaults.glsl"
#include "reprojection.h"
#include "../util.glsl"

// options:
// #define REPROJECTION_ACCUM_GBUFFER

// #define REPROJECTION_MOTION_JITTER_BUFFER
// #define REPROJECTION_ACCUM_HISTORY
// #define REPROJECTION_NORMAL_DEPTH_HISTORY
// #define REPROJECTION_AOV2_HISTORY
// #define REPROJECTION_ACCUM_TARGET

#define REPROJECTION_ACCUM_BOUNDARY_SEARCH
//#define REPROJECTION_ACCUM_CONFLICT_RESOLUTION
#define REPROJECTION_ACCUM_BILATERAL
//#define REPROJECTION_ACCUM_BILATERAL_TEST
#define REPROJECTION_ACCUM_BILATERAL_PROJECTION
#define REPROJECTION_ACCUM_FIT_GEOMETRY_DISTRIBUTION
//#define REPROJECTION_ACCUM_BACKGROUND
//#define REPROJECTION_TEST_BILATERAL_ACCUM_GUESS

#if defined(REPROJECTION_ACCUM_STOCHASTIC_SELECT) // || defined(REPROJECTION_ACCUM_STOCHASTIC)
#include "pointsets/lcg_rng.glsl"
#endif

inline vec4 reproject_and_accumulate_test_tonemap(vec4 color) {
    //return color / (color + 1.0f);
    return log2(1.0f + color);
}
inline vec4 reproject_and_accumulate_test_untonemap(vec4 color) {
    //return color / (1.0f - color);
    return exp2(color) - 1.0f;
}
inline vec4 reproject_and_accumulate_test_untonemap_delta(vec4 color, vec4 ldr_delta) {
    vec4 offset_color_ldr = reproject_and_accumulate_test_tonemap(color) + ldr_delta;
    return reproject_and_accumulate_test_untonemap(offset_color_ldr) - color;
}

inline vec4 reproject_and_accumulate(vec4 accum_color, ivec2 fb_pixel, ivec2 fb_dims
    , float min_sample_weight
    , int sample_base_index, int sample_batch_size
    , float exposure, float focus_distance)
{
    vec2 motions[3][3];
    vec2 edge_motions[3][3];
    UNROLL_FOR (ivec2 n = ivec2(-1); n.y <= 1; ++n.y)
        UNROLL_FOR (n.x = -1; n.x <= 1; ++n.x) {
            motions[n.x+1][n.y+1] = imageLoad(REPROJECTION_MOTION_JITTER_BUFFER, fb_pixel + n).xy;
            edge_motions[n.x+1][n.y+1] = motions[n.x+1][n.y+1];
        }
#ifdef REPROJECTION_ACCUM_BOUNDARY_SEARCH
    DYNAMIC_FOR (ivec2 o = ivec2(-2); o.y <= 2; ++o.y)
        DYNAMIC_FOR (o.x = -2; o.x <= 2; ++o.x) {
            vec2 m = imageLoad(REPROJECTION_MOTION_JITTER_BUFFER, fb_pixel + o).xy;
            float ml = dot(m, m);
            UNROLL_FOR (ivec2 n = ivec2(-1); n.y <= 1; ++n.y)
                UNROLL_FOR (n.x = -1; n.x <= 1; ++n.x) {
                    if (all(lessThanEqual(abs(o-n), ivec2(1)))) {
                        vec2 cm = edge_motions[n.x+1][n.y+1];
                        float cml = dot(cm, cm);
                        if (ml > cml)
                            edge_motions[n.x+1][n.y+1] = m;
                    }
                }
        }
    UNROLL_FOR (ivec2 n = ivec2(-1); n.y <= 1; ++n.y)
        UNROLL_FOR (n.x = -1; n.x <= 1; ++n.x) {
            vec2 starting_point = (vec2(fb_pixel + n) + vec2(0.5f)) / vec2(fb_dims);
            vec2 reconstruction_point = starting_point + 0.5f * motions[n.x+1][n.y+1];

            vec2 anchor_point = ivec2(starting_point + 0.5f * edge_motions[n.x+1][n.y+1]);
            // clip actual motion target to a edge motion anchor box
            vec2 anchor_min = floor(anchor_point) - vec2(0.5f);
            vec2 anchor_max = floor(anchor_point) + vec2(1.5f);
            reconstruction_point = clamp(reconstruction_point, anchor_min, anchor_max);
            motions[n.x+1][n.y+1] = 2.0f * (reconstruction_point - starting_point);
        }
#endif
    vec2 starting_point = (vec2(fb_pixel) + vec2(0.5f)) / vec2(fb_dims);
    vec2 reconstruction_point = starting_point + 0.5f * motions[1][1];
    vec2 motion_px = vec2(fb_dims) * 0.5f * motions[1][1];
    float motion_rate = max(abs(motion_px.x), abs(motion_px.y)) / min_sample_weight;
    motion_rate *= 0.5f; // aggressiveness

    vec4 history_color = vec4(0.0f);
    vec4 test_result = vec4(1.0f, 0.0f, 1.0f, -1.0f);
#ifdef REPROJECTION_ACCUM_GBUFFER
    vec4 history_aov_depth_normal = vec4(0.0f);
    vec4 history_aov2 = vec4(0.0f);
#endif
    float new_sample_weight = 1.0f;
    float old_sample_weight = 0.0f;
    if (reconstruction_point.x >= 0.0f && reconstruction_point.y >= 0.0f &&
        reconstruction_point.x < 1.0f && reconstruction_point.y < 1.0f) {
#ifdef REPROJECTION_ACCUM_CONFLICT_RESOLUTION
        vec2 reconstruction_bias = vec2(0.0f);
        // analysis given here: http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
        float bias_offset = float(uint(sample_base_index) & uint(1024-1)); // repeats after 1024 samples
        reconstruction_bias.y = fract(bias_offset * 0.754877666f);
        reconstruction_bias.x = fract(bias_offset * 0.569840291f);
        vec2 reconstruction_rnd_bias = reconstruction_bias;
        reconstruction_rnd_bias.y = -0.5f + fract(float(fb_pixel.x & 127) * 0.61803398875f + reconstruction_bias.y);
        reconstruction_rnd_bias.x = -0.5f + fract(float(fb_pixel.y & 127) * 0.61803398875f + reconstruction_bias.x);
        //reconstruction_rnd_bias.y = fract(starting_point.x + reconstruction_bias.x) - 0.5f;
        //reconstruction_rnd_bias.x = fract(starting_point.y + reconstruction_bias.y) - 0.5f;
        ivec2 reconstruction_pixel = ivec2(reconstruction_point * vec2(fb_dims) + reconstruction_rnd_bias);
        int priority_mask = int(sample_base_index & 0x3);
        int my_priority = (fb_pixel.x & 1) + 2 * (fb_pixel.y & 1);
        my_priority ^= (fb_pixel.x & 2);
        my_priority ^= priority_mask;
        UNROLL_FOR (ivec2 n = ivec2(-1); n.y <= 1; ++n.y)
            UNROLL_FOR (n.x = -1; n.x <= 1; ++n.x) {
                if (n == ivec2(0))
                    continue;
                ivec2 npx = fb_pixel + n;
                vec2 sp = (vec2(npx) + vec2(0.5f)) / vec2(fb_dims);
                vec2 rp = sp + 0.5f * motions[n.x+1][n.y+1];
                vec2 nrrb = reconstruction_bias;
                nrrb.y = -0.5f + fract(float(npx.x & 127) * 0.61803398875f + reconstruction_bias.y);
                nrrb.x = -0.5f + fract(float(npx.y & 127) * 0.61803398875f + reconstruction_bias.x);
                //nrrb.y = fract(sp.x + reconstruction_bias.x) - 0.5f;
                //nrrb.x = fract(sp.y + reconstruction_bias.y) - 0.5f;
                ivec2 rpx = ivec2(rp * vec2(fb_dims) + nrrb);
                int n_priority = (npx.x & 1) + 2 * (npx.y & 1);
                n_priority ^= (npx.x & 2);
                n_priority ^= priority_mask;
                if (reconstruction_pixel == rpx && my_priority < n_priority)
                    my_priority = -1;
            }
        history_color = texelFetch(REPROJECTION_ACCUM_HISTORY, reconstruction_pixel, 0);
        old_sample_weight = 1.0f - history_color.a;
        if (my_priority < 0) {
            vec4 interp_color = textureLod(REPROJECTION_ACCUM_HISTORY, reconstruction_point, 0.0f);
            vec3 r = vec3(history_color) / vec3(interp_color);
            old_sample_weight = min(min(min(r.x, r.y), r.z), old_sample_weight);
            old_sample_weight = min(3.0f - 2.0f * pow(max(max(r.x, r.y), r.z), 2), old_sample_weight);
            old_sample_weight = 0.0f;
        }
#else
        history_color = textureLod(REPROJECTION_ACCUM_HISTORY, reconstruction_point, 0.0f);
        old_sample_weight = 1.0f - history_color.a;
#endif
        if (old_sample_weight > 0.0f)
            new_sample_weight = old_sample_weight / (1.0f + old_sample_weight * float(sample_batch_size));
#ifdef REPROJECTION_ACCUM_GBUFFER
        history_aov_depth_normal = textureLod(REPROJECTION_NORMAL_DEPTH_HISTORY, reconstruction_point, 0.0f);
        history_aov2 = textureLod(REPROJECTION_AOV2_HISTORY, reconstruction_point, 0.0f);
#endif
    }
    new_sample_weight = max(new_sample_weight, min_sample_weight);

    // non-accumulation object types (e.g. water)
    if (accum_color.a > 1.0f)
        new_sample_weight = 0.95f;

    // normal/depth-based history invalidation
    vec4 current_normal_depth = imageLoad(REPROJECTION_ACCUM_NORMAL_DEPTH_TARGET, fb_pixel);
#ifdef REPROJECTION_ACCUM_BILATERAL
    if (new_sample_weight < 1.0f) {
        //vec2 anchor_point = starting_point + 0.5f * edge_motions[1][1];
        ivec2 reconstruction_pixel = ivec2(reconstruction_point * fb_dims);
#ifdef REPROJECTION_ACCUM_FIT_GEOMETRY_DISTRIBUTION
        vec3 avg_normal = vec3(0.0f);
        float avg_depth = 0.0f, sq_depth = 0.0f;
        float min_depth = 2.e32f; float max_depth = 0.0f;
        for (ivec2 o = ivec2(-1); o.y <= 1; ++o.y) {
            for (o.x = -1; o.x <= 1; ++o.x) {
                vec4 recons_normal_depth;
#ifdef REPROJECTION_ACCUM_BACKGROUND
                if (o == ivec2(0))
                    recons_normal_depth = texelFetch(REPROJECTION_NORMAL_DEPTH_HISTORY, reconstruction_pixel + o, 0);
                else
#endif
                recons_normal_depth = imageLoad(REPROJECTION_ACCUM_NORMAL_DEPTH_TARGET, fb_pixel + o);
                avg_normal += recons_normal_depth.xyz;
                float rel_depth = recons_normal_depth.w / current_normal_depth.w;
                avg_depth += rel_depth;
                sq_depth += rel_depth * rel_depth;
                min_depth = min(min_depth, rel_depth);
                max_depth = max(max_depth, rel_depth);
            }
        }
        avg_normal /= 9.0f;
        avg_depth /= 9.0f;
        sq_depth /= 9.0f;
        float normal_sigma = max(1.0f - length(avg_normal), 0.0f);
        float depth_sigma = sqrt(max(sq_depth - avg_depth * avg_depth, 0.0f));
#ifdef REPROJECTION_ACCUM_BACKGROUND
//        if (min_depth > 1.0f || max_depth < 1.0f)
//            depth_sigma = max(max(max_depth - 1.0f, 1.0f - min_depth), depth_sigma);
        if (min_depth > 1.0f)
            depth_sigma += (min_depth - 1.0f) * max(1.0f - 0.05f * motion_rate, 0.0f);
        if (max_depth < 1.0f)
            depth_sigma += (1.0f - max_depth) * max(1.0f - 0.05f * motion_rate, 0.0f);
#endif
        //depth_sigma = mix(max(max_depth - 1.0f, 1.0f - min_depth), depth_sigma, min(90.0f * depth_sigma / (min_depth - max_depth), 1.0f) );
#else
        float normal_sigma = 0.001f;
        float depth_sigma = 0.001f;
#endif

        //vec4 accum_mean = log(1.0f + accum_color);
        vec4 accum_mean = accum_color;
        vec4 accum_sq = accum_color * accum_color;
        for (ivec2 o = ivec2(-1); o.y <= 1; ++o.y) {
            for (o.x = -1; o.x <= 1; ++o.x) {
                if (o == vec2(0.0f)) continue;
                vec4 a = imageLoad(accum_buffer, fb_pixel + o);
                //accum_mean += log2(1.0f + a);
                accum_mean += a;
                accum_sq += a * a;
            }
        }
        accum_mean /= 9.0f;
        accum_sq /= 9.0f;
        vec4 accum_sigma = sqrt(max(accum_sq - accum_mean * accum_mean, 0.0f));
        //accum_mean = exp2(accum_mean) - 1.0f;

        float bilateral_weight = 0.0f;
        vec4 bilateral_history = vec4(0.0f);
        vec4 bilateral_history_sq = vec4(0.0f);
        float mix_weight = 0.0f;
        float max_weight = 0.0f;
        vec4 mix_history_color = vec4(0.0f);
    #ifdef REPROJECTION_ACCUM_GBUFFER
        vec4 mix_history_normal_depth = vec4(0.0f);
        vec4 mix_history_aov2 = vec4(0.0f);
        float mix_aov_weight = 0.0f;
    #endif
        float old_sample_weight = 0.0f;
        for (ivec2 o = ivec2(-1); o.y <= 1; ++o.y) {
            for (o.x = -1; o.x <= 1; ++o.x) {
                vec4 neighbor_history_color = texelFetch(REPROJECTION_ACCUM_HISTORY, reconstruction_pixel + o, 0);
                float neighbor_old_sample_weight = 1.0f - neighbor_history_color.a;

                vec4 recons_normal_depth = texelFetch(REPROJECTION_NORMAL_DEPTH_HISTORY, reconstruction_pixel + o, 0);
                float angle = dot(recons_normal_depth.xyz, current_normal_depth.xyz);
                float rcp_depth_delta = abs(recons_normal_depth.w / current_normal_depth.w - 1.0f);
                float weight = smoothstep(-0.66f, 1.0f, angle + normal_sigma)
                    * min(max(0.0f, 1.0f - min(10.0f, 1.0f / depth_sigma) * rcp_depth_delta), 1.0f)
                    ;
#ifdef REPROJECTION_ACCUM_GBUFFER
                float aovWeight = smoothstep(-0.66f, 1.0f, angle + 2.0f * normal_sigma)
                    * min(max(0.0f, 1.0f - min(10.0f, 1.0f / depth_sigma) * rcp_depth_delta + step(current_normal_depth.w, recons_normal_depth.w) * 1.5f), 1.0f)
                    ;
                aovWeight = max(aovWeight, weight);
#endif
                max_weight = max(max_weight, weight);
                vec4 neighbor_test_color = reproject_and_accumulate_test_tonemap(neighbor_history_color);
                bilateral_history += weight * neighbor_test_color;
                bilateral_history_sq += weight * neighbor_test_color * neighbor_test_color;
                bilateral_weight += weight;

                float filterWeight = exp(-3.0f * length2(vec2(reconstruction_pixel + o) + vec2(0.5f) - reconstruction_point * fb_dims));
                weight *= filterWeight;

    #ifdef REPROJECTION_ACCUM_GBUFFER
                vec4 recons_aov2 = texelFetch(REPROJECTION_AOV2_HISTORY, reconstruction_pixel + o, 0);
    #endif
                if (neighbor_old_sample_weight > 0.0f) {
                    mix_weight += weight;
                    mix_history_color += weight * neighbor_history_color;
                    old_sample_weight += weight * weight * neighbor_old_sample_weight;
                }

    #ifdef REPROJECTION_ACCUM_GBUFFER
                aovWeight *= filterWeight;
                mix_aov_weight += aovWeight;
                mix_history_normal_depth += aovWeight * recons_normal_depth;
                mix_history_aov2 += aovWeight * recons_aov2;
    #endif
            }
        }

        if (mix_weight > 0.0f) {
            bilateral_history /= bilateral_weight;
            bilateral_history_sq /= bilateral_weight;
            vec4 sigma_ldr = sqrt(max(bilateral_history_sq - bilateral_history * bilateral_history, 0.0f));

            mix_history_color /= mix_weight;
            old_sample_weight /= mix_weight * mix_weight;

    #ifdef REPROJECTION_ACCUM_GBUFFER
            mix_history_normal_depth /= totalAOVWeight;
            mix_history_aov2 /= totalAOVWeight;
            history_aov_depth_normal = mix_history_normal_depth;
            history_aov2 = mix_history_aov2;
    #endif

#ifdef REPROJECTION_ACCUM_BILATERAL_TEST
            {
                float motion_weight = min(motion_rate, 1.0f);
                //motion_weight = 1.0f;

                vec4 offset_low = reproject_and_accumulate_test_untonemap_delta(mix_history_color, -3.f * sigma_ldr);
                vec4 offset_high = -offset_low; // reproject_and_accumulate_test_untonemap_delta(mix_history_color, 3.0f * sigma_ldr);
                vec4 center = mix(mix_history_color, accum_mean, 0.0f);
                vec3 history_box_min = vec3( max(motion_weight * (min(mix_history_color, center) + offset_low), accum_mean - 3.0f * accum_sigma) );
                vec3 history_box_max = vec3( max(max(mix_history_color, center) + offset_high, (1.0f - motion_weight) * (accum_mean + 3.0f * accum_sigma)) );

                vec3 p0 = vec3(accum_color);
                history_box_min = min(history_box_min, p0);
                history_box_max = max(history_box_max, p0);
                vec3 d = vec3(history_color) - p0;

                vec3 d_back = mix(vec3(history_box_max), vec3(history_box_min), lessThan(d, vec3(0.0f))) - p0;
                d_back /= d;
                float t0 = max(min(min(d_back.x, d_back.y), d_back.z), 0.0f);
                new_sample_weight = max(new_sample_weight, min(1.0f - t0, 1.0f));
                //test_result = max(vec4(t0, t0, t0, 1.0f), 0.0f); // vec4(t0, 1.0f);
            }
#elif defined(REPROJECTION_ACCUM_BILATERAL_PROJECTION)
            vec3 line = vec3(history_color - accum_color);
            float t = dot(vec3(mix_history_color - accum_color), line) / dot(line, line);
            new_sample_weight = max(new_sample_weight, 1.0f - max(t, 0.0f));
    #ifdef REPROJECTION_TEST_BILATERAL_ACCUM_GUESS
            test_result = vec4(vec3(mix_history_color), old_sample_weight);
    #endif
#else
            history_color = mix_history_color;
            new_sample_weight = old_sample_weight / (1.0f + old_sample_weight * float(sample_batch_size));
#endif
        }
        else {
            //vec3 r1 = vec3(accum_mean) / vec3(history_color);
            //vec3 r2 = vec3(history_color) / vec3(accum_mean);
            //new_sample_weight = max(1.0f - min(min(r1.x, r1.y), r1.z), new_sample_weight);
            //new_sample_weight = max(1.0f - min(min(r2.x, r2.y), r2.z), new_sample_weight);
            new_sample_weight = 1.0f;
        }

        //new_sample_weight = max(new_sample_weight, 1.0f - max_weight);
    }
    new_sample_weight = max(new_sample_weight, min_sample_weight);
#endif

    history_color = history_color + (accum_color - history_color) * new_sample_weight;
    history_color.a = 1.0f - new_sample_weight;

    accum_color.xyz = mix(accum_color.xyz, history_color.xyz, 1.0f);
    imageStore(REPROJECTION_ACCUM_TARGET, fb_pixel, vec4(accum_color.xyz, history_color.w));

#ifdef REPROJECTION_ACCUM_GBUFFER
    vec4 accum_normal_depth = history_aov_depth_normal + (current_normal_depth - history_aov_depth_normal) * new_sample_weight;
    imageStore(REPROJECTION_ACCUM_NORMAL_DEPTH_TARGET, fb_pixel, accum_normal_depth);

    vec4 accum_aov2 = imageLoad(REPROJECTION_ACCUM_AOV2_TARGET, fb_pixel);
    accum_aov2 = history_aov2 + (accum_aov2 - history_aov2) * new_sample_weight;
    imageStore(REPROJECTION_ACCUM_AOV2_TARGET, fb_pixel, accum_aov2);
#endif

#ifdef REPROJECTION_TEST_BILATERAL_ACCUM_GUESS
    // want: new_sample_weight = min(new_sample_weight, test_result.w)
    // downweight new_sample_weight by test_result.w / new_sample_weight, if smaller then
    if (test_result.w >= 0.0f)
        accum_color.xyz = mix(vec3(test_result), vec3(accum_color), min(test_result.w / new_sample_weight, 1.0f));
#else
    if (test_result.w >= 0.0f)
        return test_result;
#endif

    return accum_color;
}
