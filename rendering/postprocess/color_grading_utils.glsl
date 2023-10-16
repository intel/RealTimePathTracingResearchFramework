// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef COLOR_GRADING_UTILS_GLSL
#define COLOR_GRADING_UTILS_GLSL

const mat3x3 LIN_2_LMS_MAT = mat3x3(
    3.90405e-1, 5.49941e-1, 8.92632e-3,
    7.08416e-2, 9.63172e-1, 1.35775e-3,
    2.31082e-2, 1.28021e-1, 9.36245e-1
);

const mat3x3 LMS_2_LIN_MAT = mat3(
    2.85847e+0, -1.62879e+0, -2.48910e-2,
    -2.10182e-1,  1.15820e+0,  3.24281e-4,
    -4.18120e-2, -1.18169e-1,  1.06867e+0
);

vec3 linear_to_lms(vec3 x)
{
    return x * LIN_2_LMS_MAT;
}

vec3 lms_to_linear(vec3 x)
{
    return x * LMS_2_LIN_MAT;
}

vec3 color_balance(vec3 linear_color)
{
    // Move to lms space
    vec3 lms_color = linear_to_lms(linear_color);

    // Apply the color balance
    lms_color *= color_balance_operator.xyz;

    // Move back to linear space
    return lms_to_linear(lms_color);
}

#endif // COLOR_GRADING_UTILS_GLSL
