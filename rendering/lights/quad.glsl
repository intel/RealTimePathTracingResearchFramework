// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef QUAD_LIGHTS_GLSL
#define QUAD_LIGHTS_GLSL

#include "../language.glsl"

#include "quad.h.glsl"

vec3 sample_quad_light_position(const QuadLight light, vec2 samples)
{
    return samples.x * light.v_x * light.width + samples.y * light.v_y * light.height +
           light.position;
}

/* Compute the PDF of sampling the sampled point p light with the ray specified by orig and
 * dir, assuming the light is not occluded
 */
float quad_light_pdf(const QuadLight light,
                     const vec3 p,
                     const vec3 orig,
                     const vec3 dir)
{
    float surface_area = light.width * light.height;
    vec3 to_pt = p - orig;
    float dist_sqr = dot(to_pt, to_pt);
    float n_dot_w = dot(light.normal, -dir);
    if (n_dot_w < EPSILON) {
        return 0.f;
    }
    return dist_sqr / (n_dot_w * surface_area);
}

bool quad_intersect(const QuadLight light,
                    const vec3 orig,
                    const vec3 dir,
                    GLSL_out(float) t,
                    GLSL_out(vec3) light_pos)
{
    float denom = dot(dir, light.normal);
    if (denom != 0.f) {
        t = dot(light.position - orig, light.normal) / denom;
        if (t < 0.f) {
            return false;
        }

        // It's a finite plane so now see if the hit point is actually inside the plane
        light_pos = orig + dir * t;
        vec3 hit_v = light_pos - light.position;
        if (abs(dot(hit_v, light.v_x)) < light.width &&
            abs(dot(hit_v, light.v_y)) < light.height) {
            return true;
        }
    }
    return false;
}

#endif

