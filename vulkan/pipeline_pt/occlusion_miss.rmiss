// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require

#ifdef SANDBOX_PATH_TRACER
#include "../setup_iterative_pt.glsl"
#else
#include "setup_recursive_pt.glsl"
#endif

rayPayloadInEXT int occlusion_hit;

void main() {
    occlusion_hit = 0;
}
