// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "../defaults.glsl"
#include "../lights/sun.glsl"
#include "../bsdfs/hit_point.glsl"

inline vec3 sample_sun_light(const vec3 hit_point, const vec3 hit_normal
    , vec3 sun_dir, float sun_cap_cos
    , vec2 dir_sample, vec2 sel_sample
    , GLSL_out(vec3) light_dir, GLSL_out(float) pdf)
{
    light_dir = sample_sun_dir(sun_dir, sun_cap_cos, dir_sample);
    pdf = sample_sun_dir_pdf(sun_dir, sun_cap_cos, light_dir);
    return vec3(1.0f) / pdf;
}

inline float eval_sun_light_pdf(const vec3 hit_point, const vec3 hit_normal, const vec3 w_i
    , vec3 sun_dir, float sun_cap_cos)
{
    return sample_sun_dir_pdf(sun_dir, sun_cap_cos, w_i);
}
