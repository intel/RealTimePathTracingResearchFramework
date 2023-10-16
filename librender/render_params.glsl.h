// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef RENDER_PARAMS_H_GLSL
#define RENDER_PARAMS_H_GLSL

// compile-time features
#define USE_MIPMAPPING
#define UNROLL_STANDARD_TEXTURES
#define TRANSPORT_MIPMAPPING
#define TRANSPORT_ROUGHENING
#define TRANSPORT_RELIABILITY
#define TRANSPORT_NORMALFRAME

// compile-time config
#define MAX_PATH_DEPTH 9
#define DEFAULT_RR_PATH_DEPTH 2
#define BINNED_LIGHTS_BIN_MAX_SIZE 16
#define GLOSSY_MODE_ROUGHNESS_THRESHOLD 0.1f

// run-time config
#ifndef GLM
    #include <glm/glm.hpp>
    #define GLM(type) glm::type
#endif
#ifndef GLCPP_DEFAULT
    #define GLCPP_DEFAULT(...) __VA_ARGS__
#endif

#include "../rendering/postprocess/reprojection.h"
#include "../rendering/mc/light_sampling.h"


#define RNG_VARIANT_UNIFORM 0
#define RNG_VARIANT_BN 1
#define RNG_VARIANT_SOBOL 2
#define RNG_VARIANT_Z_SBL 3
// note: order has to match numbers!
#define RNG_VARIANT_NAMES \
    "UNIFORM", \
    "BN", \
    "SOBOL", \
    "Z_SBL"

#define OUTPUT_CHANNEL_COLOR 0
#define OUTPUT_CHANNEL_ALBEDO_ROUGHNESS 1
#define OUTPUT_CHANNEL_NORMAL_DEPTH 2
#define OUTPUT_CHANNEL_MOTION_JITTER 3
// note: order has to match numbers!
#define OUTPUT_CHANNEL_NAMES \
    "COLOR", \
    "ALBEDO_ROUGHNESS", \
    "NORMAL_DEPTH", \
    "MOTION_JITTER"

#define RBO_rng_variant_DEFAULT RNG_VARIANT_UNIFORM
#define RBO_rng_variant_NAMES_PREFIX "RNG_VARIANT_"
#define RBO_rng_variant_NAMES RNG_VARIANT_NAMES

#define RBO_render_upscale_factor_DEFAULT 1
#define RBO_rebuild_triangle_budget_DEFAULT 500000

#define DEBUG_MODE_OFF 0
#define DEBUG_MODE_ANY_HIT_COUNT_FULL_PATH 1
#define DEBUG_MODE_ANY_HIT_COUNT_PRIMARY_VISIBILITY 2
#define DEBUG_MODE_BOUNCE_COUNT 3

#define RBO_debug_mode_DEFAULT DEBUG_MODE_OFF
#define RBO_debug_mode_NAMES_PREFIX "DEBUG_MODE_"
#define RBO_debug_mode_NAMES "OFF", "ANY_HIT_COUNT_FULL_PATH", "ANY_HIT_COUNT_PRIMARY_VISIBILITY", "BOUNCE_COUNT"

#define RBO_enum_t int
struct RenderBackendOptions {
    // options passed to render extensions and GPU compilers
#define RENDER_BACKEND_OPTIONS(declare) \
    declare(RBO_enum_t, rng_variant, RBO_rng_variant_DEFAULT, \
        RBO_STAGES_INTEGRATOR) \
    declare(RBO_enum_t, light_sampling_variant, RBO_light_sampling_variant_DEFAULT, \
        RBO_STAGES_INTEGRATOR) \
    declare(int, light_sampling_bucket_count, RBO_light_sampling_bucket_count_DEFAULT, \
        RBO_STAGES_INTEGRATOR) \
    declare(bool, unroll_bounces, false, \
        GPU_PROGRAM_FEATURE_MEGAKERNEL) \
    \
    declare(int, render_upscale_factor, RBO_render_upscale_factor_DEFAULT, \
        RBO_STAGES_CPU_ONLY) \
    declare(bool, enable_rayqueries, false, \
        RBO_STAGES_INTEGRATOR) \
    \
    declare(bool, force_bvh_rebuild, false, \
        RBO_STAGES_CPU_ONLY) \
    declare(int, rebuild_triangle_budget, RBO_rebuild_triangle_budget_DEFAULT, \
        RBO_STAGES_CPU_ONLY) \
    \
    declare(bool, enable_taa, false, \
        RBO_STAGES_CPU_ONLY) \
    declare(bool, enable_raytraced_dof, true, \
        RBO_STAGES_CPU_ONLY) \
    \
    RENDER_BACKEND_OPTIONS_EXTENDED(declare)

#define RENDER_BACKEND_OPTIONS_EXTENDED(declare)

    // for gpu program features, see #include "rendering/gpu_programs.h"
    // shader stages that options may apply to
    #define RBO_STAGES_HOST_PIPELINE 0x0   // not affecting GPU program sources, reuse same binaries
    #define RBO_STAGES_CPU_ONLY 0x80000000 // not affecting pipeline layouts, reuse same pipelines
    #define RBO_STAGES_ALL 0x7fff0000
    // options applying to renderers
    #define RBO_STAGES_INTEGRATOR 0x010000
    #define RBO_STAGES_RASTERIZED 0x020000
    #define RBO_STAGES_RAYTRACED 0x040000
    // options applying to various processing stages
    #define RBO_STAGES_PROCESSING 0x1000000

#define RENDER_BACKEND_OPTIONS_DECLARE(type, name, default, flags) type name GLCPP_DEFAULT(= default);
    RENDER_BACKEND_OPTIONS(RENDER_BACKEND_OPTIONS_DECLARE)
#undef RENDER_BACKEND_OPTIONS_DECLARE
};

#define RENDER_BACKEND_OPTION(option) RBO_##option

struct LightSamplingConfig {
    float light_mis_angle GLCPP_DEFAULT(= 0.0f);
    int bin_size GLCPP_DEFAULT(= 16);
    float min_perceived_receiver_dist GLCPP_DEFAULT(= 15.0f);
    float min_radiance GLCPP_DEFAULT(= 0.0f);
};

struct RenderParams {
    int batch_spp GLCPP_DEFAULT(= 1);
    int max_path_depth GLCPP_DEFAULT(= MAX_PATH_DEPTH);
    int rr_path_depth GLCPP_DEFAULT(= DEFAULT_RR_PATH_DEPTH);
    int glossy_only_mode GLCPP_DEFAULT(= 0);

    float aperture_radius GLCPP_DEFAULT(= 0);
    float focus_distance GLCPP_DEFAULT(= 2.5f);
    float pixel_radius GLCPP_DEFAULT(= 1.0f);
    float variance_radius GLCPP_DEFAULT(= 4.0f);

    int output_channel GLCPP_DEFAULT(= 0);
    int output_moment GLCPP_DEFAULT(= 0);
    float exposure GLCPP_DEFAULT(= 0.0);
    int early_tone_mapping_mode GLCPP_DEFAULT(= -1);

    int reprojection_mode GLCPP_DEFAULT(= REPROJECTION_MODE_NONE);
    int spp_accumulation_window GLCPP_DEFAULT(= 8);
    int enable_raster_taa GLCPP_DEFAULT(= 0);
    int render_upscale_factor GLCPP_DEFAULT(= 1);
    
    float focal_length GLCPP_DEFAULT(= 35.0);
    int _pad3;
    int _pad4;
    int _pad5;
};

struct SceneConfig {
    float bump_scale GLCPP_DEFAULT(= 1);
    GLM(vec3) sun_dir GLCPP_DEFAULT(= GLM(vec3)(0.0f, 1.0f, 0.0f));
    float turbidity GLCPP_DEFAULT(= 3.0f);
    GLM(vec3) albedo GLCPP_DEFAULT(= GLM(vec3)(0.2f));
};

// cross-backend data exchange
struct RenderRayQuery {
    GLM(vec3) origin;
    int mode_or_data;
    GLM(vec3) dir;
    float t_max;
};

#define DEFAULT_RAY_QUERY_BUDGET (512*512)

#endif
