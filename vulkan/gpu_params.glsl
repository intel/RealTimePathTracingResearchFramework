// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef GPU_PARAMS_H_GLSL
#define GPU_PARAMS_H_GLSL

#define QUANTIZED_POSITIONS
#define QUANTIZED_NORMALS_AND_UVS
#define REQUIRE_UNROLLED_VERTICES
//#define PIXEL_FILTER_TENT_WINDOW 0.66f
//#define REPROJECTION_ACCUM_GBUFFER
#define PREMULTIPLIED_BASE_COLOR_ALPHA

#if !defined(ENABLE_CUDA) && !defined(ENABLE_RASTER)
// do not unroll instance geometry params
#define IMPLICIT_INSTANCE_PARAMS
#endif

#define ENABLE_AOV_BUFFERS
//#define ATOMIC_ACCUMULATE
//#define ATOMIC_ACCUMULATE_ADD
#define ANYHIT_FORCE_ALPHATEST
//#define ALPHA_RAY_RESTART

//#define PRE_EXPOSE_IMAGES

#ifndef RAY_EPSILON
#define RAY_EPSILON 0.000005f
#endif

#ifndef TIME_PERIOD
#define TIME_PERIOD 8192.0f
#endif

#define ACCUMULATION_FLAGS_ATOMIC 0x1
#define ACCUMULATION_FLAGS_AOVS 0x2
#define ACCUMULATION_FLAGS_SEPARATE_REFLECTIONS 0x4

#ifdef PIXEL_FILTER_TENT_WINDOW
#define SAMPLE_PIXEL_FILTER(urand) (PIXEL_FILTER_TENT_WINDOW * sample_tent(urand))
#else
#define SAMPLE_PIXEL_FILTER(urand) (urand - vec2(0.5f))
#endif

#define PUSH_CONSTANT_KERNEL_PARAMETERS \
    int num_rayqueries; \
    int accumulation_frame_offset; \
    int accumulation_batch_size; \
    int accumulation_flags; \

struct PushConstantKernelParams {
    PUSH_CONSTANT_KERNEL_PARAMETERS
};

#include "../librender/render_params.glsl.h"

#if defined(TRANSPORT_ROUGHENING) || defined(TRANSPORT_MIPMAPPING)
#define TRANSPORT_RELIABILITY
#endif

struct ViewParams {
    uint32_t frame_id;
    uint32_t frame_offset;
    uvec2 frame_dims;

    vec2 screen_jitter;
    vec2 _pad1;

    // pinhole camera currently used by RT
    vec3 cam_pos;
    float time;
    vec4 cam_du;
    vec4 cam_dv;
    vec4 cam_dir_top_left;

    // Previous frame data
    vec3 cam_pos_reference;
    float prev_time;
    vec4 cam_du_reference;
    vec4 cam_dv_reference;
    vec4 cam_dir_top_left_reference;

    mat4 VP;
    mat4 VP_reference;

    LightSamplingConfig light_sampling;
};

#define LOCAL_RENDER_PARAMETER_POOL \
    ViewParams view_params; \
    ViewParams ref_view_params; \

// constant buffer layout of local parameters
struct LocalParams {
    LOCAL_RENDER_PARAMETER_POOL
};

// layout of push constant parameters
struct PushConstantParams {
    PUSH_CONSTANT_KERNEL_PARAMETERS
    // may add more
};
// active set of push constant parameters (may be moved between here and local constants)
#define PUSH_CONSTANT_PARAMETERS \
    PUSH_CONSTANT_KERNEL_PARAMETERS \

// active set of local constant parameters (may be moved between here and push constants)
#define LOCAL_CONSTANT_PARAMETERS \
    LOCAL_RENDER_PARAMETER_POOL \

#include "../rendering/lights/sky_model_arhosek/sky_model.h.glsl"

struct LightSamplingSceneParams {
    int32_t light_count;
    int32_t optimized_bin_size;
    int32_t optimized_light_bin_count;
    int32_t _pad1;
};

struct SceneParams {
    SkyModelParams sky_params;
    vec3 sun_dir; float sun_cos_angle;
    vec4 sun_radiance;

    float normal_z_scale;
    uint32_t num_local_lights;
    int32_t _pad2;
    int32_t _pad3;

    LightSamplingSceneParams light_sampling;
};

#define GLOBAL_RENDER_PARAMETER_POOL \
    RenderParams render_params; \
    SceneParams scene_params; \

struct GlobalParams {
    GLOBAL_RENDER_PARAMETER_POOL
};
// active set of global constant parameters
#define GLOBAL_CONSTANT_PARAMETERS GLOBAL_RENDER_PARAMETER_POOL

// currently unused: fused local and global parameter layouts
struct RasterUniformParams {
    LocalParams local_params;
    GlobalParams global_params;
};
// active set of fused local and global parameters
#define RASTER_UNIFORM_CONSTANT_PARAMETERS \
    LOCAL_RENDER_PARAMETER_POOL \
    GLOBAL_RENDER_PARAMETER_POOL \


#define SCENE_BIND_POINT 0
#define VIEW_PARAMS_BIND_POINT 1
#define MATERIALS_BIND_POINT 2
#define LIGHTS_BIND_POINT 3
#define SCENE_PARAMS_BIND_POINT 4
#define RANDOM_NUMBERS_BIND_POINT 5
#define INSTANCES_BIND_POINT 6

#define FRAMEBUFFER_BIND_POINT 8
#define ACCUMBUFFER_BIND_POINT 9
#define ATOMIC_ACCUMBUFFER_BIND_POINT 10

#define AOV_ALBEDO_ROUGHNESS_BIND_POINT 11
#define AOV_NORMAL_DEPTH_BIND_POINT 12
#define AOV_MOTION_JITTER_BIND_POINT 13

#define RAYQUERIES_BIND_POINT 14
#define RAYRESULTS_BIND_POINT 15
#define RAYSTATS_BIND_POINT 16

#define HISTORY_BUFFER_BIND_POINT 17
#define HISTORY_FRAMEBUFFER_BIND_POINT 18
#define HISTORY_AOV_BUFFER_BIND_POINT 18 // note aliasing as not occuring simultaneously
#define HISTORY_AOV_BUFFER2_BIND_POINT 19

#define DENOISE_BUFFER_BIND_POINT 20

#define DEBUG_MODE_BUFFER 24

// First available slot that can be used by extensions.
#define EMPTY_BIND_POINT 25

#define QUERY_BIND_SET 0

#ifdef UNROLL_STANDARD_TEXTURES
#define STANDARD_TEXTURE_BIND_SET 1
#endif
#define TEXTURE_BIND_SET 2


#define BLAS_BIND_SET 5

#endif
