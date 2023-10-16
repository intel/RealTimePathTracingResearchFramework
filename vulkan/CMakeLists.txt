# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT


include(${CMAKE_CURRENT_LIST_DIR}/cmake/glslcli.cmake)

set(GLSL_COMPILE_DEFNS "")
if (REPORT_RAY_STATS)
	set(GLSL_COMPILE_DEFNS ${GLSL_COMPILE_DEFNS} "REPORT_RAY_STATS=1")
endif()

glslc_command_line(GPU_PROGRAM_DEFAULT_CMDLINE GLSLC
    COMPILE_OPTIONS -O
    INCLUDE_DIRECTORIES
        ${PROJECT_SOURCE_DIR}/rendering
    COMPILE_DEFINITIONS
        ${GLSL_COMPILE_DEFNS}
    )

include(${CMAKE_CURRENT_LIST_DIR}/cmake/glslang-alternatives.cmake OPTIONAL)

add_gpu_program(PT_PIPELINE RAYTRACING "btd")
add_integrator(PT_TAIL_RECURSIVE "tail-recursive" COMPILE_DEFINITIONS TAIL_RECURSIVE RECURSION_DEPTH=MAX_PATH_DEPTH INHERITS "PT_PIPELINE")
add_integrator(PT_BTD_SHADE "shallow shade-on-hit" COMPILE_DEFINITIONS NON_RECURSIVE RECURSION_DEPTH=2 INHERITS "PT_PIPELINE")
add_integrator(PT_STACK_RECURSIVE "stack-recursive" COMPILE_DEFINITIONS STACK_RECURSIVE RECURSION_DEPTH=MAX_PATH_DEPTH INHERITS "PT_PIPELINE")

# default async RT hit material
add_gpu_program(DEFAULT_HIT MODULE "hit" FUNCTION "hit")
add_gpu_sources(DEFAULT_HIT pipeline_pt/any_hit.rahit pipeline_pt/hit.rchit)

add_gpu_sources(PT_PIPELINE (raygen: pipeline_pt/perspective.rgen) pipeline_pt/miss.rmiss pipeline_pt/occlusion_miss.rmiss
    PROGRAMS *DEFAULT_HIT)

# additional materials in async RT pipeline path tracers
add_gpu_program(TRANSMISSION_HIT MODULE "transmission" FUNCTION "hit_extended")
add_gpu_sources(TRANSMISSION_HIT pipeline_pt/hit.rchit
    COMPILE_DEFINITIONS GLTF_SUPPORT_TRANSMISSION)
add_gpu_program(THIN_TRANSMISSION_HIT MODULE "thin_transmission" FUNCTION "hit_extended_thin")
add_gpu_sources(THIN_TRANSMISSION_HIT pipeline_pt/any_hit.rahit  pipeline_pt/hit.rchit
    COMPILE_DEFINITIONS GLTF_SUPPORT_TRANSMISSION GLTF_SUPPORT_TRANSMISSION_ROUGHNESS)

add_gpu_sources(PT_PIPELINE

    (simplified_hit: *DEFAULT_HIT -DSIMPLIFIED_SHADER)
PROGRAMS
    *TRANSMISSION_HIT
    *THIN_TRANSMISSION_HIT
FEATURE_FLAGS
    EXTENDED_HIT)

add_integrator(PT_MEGAKERNEL "megakernel" INTEGRATOR_TYPE COMPUTE)
add_integrator(PT_RTP_MEGAKERNEL "debug megakernel (RT pipeline)")
add_gpu_sources(PT_MEGAKERNEL pt_megakernel.comp
    FEATURE_FLAGS MEGAKERNEL
    COMPILE_DEFINITIONS WORKGROUP_SIZE_X=32 WORKGROUP_SIZE_Y=16 DYNAMIC_LOOP_BOUNCES)
add_gpu_sources(PT_RTP_MEGAKERNEL (raygen: pt_megakernel.rgen) miss.rmiss (pipeline_pt/any_hit.rahit hit.rchit) pipeline_pt/occlusion_miss.rmiss
    COMPILE_DEFINITIONS USE_RT_PIPELINE SANDBOX_PATH_TRACER TRIVIAL_BACKGROUND_MISS DYNAMIC_LOOP_BOUNCES)

# bad, naive, original default renderer
add_integrator(PT "naive (independent)") # PRECOMPILE_OPTIONS rng_variant=RNG_VARIANT_UNIFORM|RNG_VARIANT_SOBOL
add_gpu_sources(PT raygen.rgen miss.rmiss (pipeline_pt/any_hit.rahit hit.rchit) pipeline_pt/occlusion_miss.rmiss
    COMPILE_DEFINITIONS SANDBOX_PATH_TRACER)

# helper GPU programs
set_integrator_type(RQ_CLOSEST COMPUTE)
add_gpu_sources(RQ_CLOSEST rt_intersect.comp COMPILE_DEFINITIONS WORKGROUP_SIZE_X=32 WORKGROUP_SIZE_Y=16)

add_integrator(RT_DEBUG "rt debug" INTEGRATOR_TYPE COMPUTE)
add_gpu_sources(RT_DEBUG rt_debug.comp COMPILE_DEFINITIONS WORKGROUP_SIZE_X=32 WORKGROUP_SIZE_Y=16)

set(VULKAN_RENDER_EXTENSION_SRC
    pointsets/render_bn.cpp
    pointsets/render_sobol.cpp
    light_sampling/render_binned_lights.cpp
    render_sky.cpp
    processing/process_taa.cpp
)

if (ENABLE_DYNAMIC_MESHES)
    add_gpu_program(ANIMATION_PIPELINE COMPUTE "animation pipeline")
    add_gpu_sources(ANIMATION_PIPELINE
        (animated_leaf: animation/animate_meshes.comp -DENABLE_ANIMATION_WIND)
        COMPILE_DEFINITIONS WORKGROUP_SIZE_X=256 WORKGROUP_SIZE_Y=1)

    list(APPEND VULKAN_RENDER_EXTENSION_SRC
        animation/animate_meshes.cpp
        animation/animate_objects.cpp
    )
endif ()

# todo: experimental
if (ENABLE_RASTER)
    add_integrator(RASTER_BASIC "basic raster" INTEGRATOR_TYPE RASTERIZATION)
    add_gpu_sources(RASTER_BASIC (pipeline_raster/basic.vert pipeline_raster/basic.frag))

    list(APPEND VULKAN_RENDER_EXTENSION_SRC
        pipeline_raster/raster_scene_vulkan.cpp)
    add_compile_definitions(ENABLE_RASTER)
endif ()

if (ENABLE_OIDN)
    list(APPEND VULKAN_RENDER_EXTENSION_SRC
        denoise/process_dl.cpp
    )
    list(APPEND VULKAN_RENDER_EXTENSION_DEP
        OpenImageDenoise-dpcpp
    )
endif ()
if (ENABLE_OIDN2)
    list(APPEND VULKAN_RENDER_EXTENSION_SRC
        denoise/process_oidn2.cpp
    )
    list(APPEND VULKAN_RENDER_EXTENSION_DEP
        OpenImageDenoise2-dpcpp
    )
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/CMakeLists-extended.cmake OPTIONAL)

# todo: we should move this somewhere else and allow typed mono?
#set_mono_gpu_programs(MASKED_MEGAKERNEL)

# helper gpu programs that are always on
add_gpu_sources(PROCESS_TAA processing/process_taa.comp COMPILE_DEFINITIONS WORKGROUP_SIZE_X=32 WORKGROUP_SIZE_Y=16)

add_gpu_program(PROCESS_SAMPLES COMPUTE "process samples")
add_gpu_sources(PROCESS_SAMPLES "process_samples.comp" COMPILE_DEFINITIONS WORKGROUP_SIZE_X=32 WORKGROUP_SIZE_Y=16)

if (ENABLE_EXAMPLES)
    add_gpu_program(PROCESS_EXAMPLE COMPUTE "example postprocessing")
    add_gpu_sources(PROCESS_EXAMPLE processing/example.comp COMPILE_DEFINITIONS WORKGROUP_SIZE_X=32 WORKGROUP_SIZE_Y=16)

    list(APPEND VULKAN_RENDER_EXTENSION_SRC
        processing/process_example.cpp
    )
endif ()


if (ENABLE_PROFILING_TOOLS)
    list(APPEND VULKAN_RENDER_EXTENSION_SRC
        processing/process_profiling_tools.cpp
    )
endif ()

if (ENABLE_DEBUG_VIEWS)
    list(APPEND VULKAN_RENDER_EXTENSION_SRC
        debug/process_debug_views.cpp
        debug/process_debug_views_ui_and_state.cpp
    )
    add_gpu_program(DEBUG_VIEWS COMPUTE "debug_views debugviews")
    add_gpu_sources(DEBUG_VIEWS debug/shaders/debug_views.comp COMPILE_DEFINITIONS WORKGROUP_SIZE_X=8 WORKGROUP_SIZE_Y=8)
endif()


resolve_gpu_programs()
generate_gpu_programs(vulkan)
add_library(render_vulkan_gpu STATIC ${GPU_EMBEDDED_CFILES} ${GPU_POGRAM_SOURCES} ${GPU_EMBEDDED_BUILD_CMDS})
add_project_files(render_vulkan_gpu ${CMAKE_CURRENT_SOURCE_DIR} SOURCE_GROUP Shaders *.glsl)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX Shaders FILES ${GPU_POGRAM_SOURCES})
source_group("Prebuilt Objects" REGULAR_EXPRESSION .*\\.spv)

add_library(render_vulkan
    compute_vulkan.cpp
    render_vulkan.cpp
    render_vulkan_extensions.cpp
    render_pipeline_vulkan.cpp
    resource_utils.cpp
    command_buffer_utils.cpp
    vulkan_utils.cpp
    vulkanrt_utils.cpp
    vkdisplay.cpp
    profiling/profiling_scopes.cpp
    ${VULKAN_RENDER_EXTENSION_SRC}
)
add_project_files(render_vulkan ${CMAKE_CURRENT_SOURCE_DIR} *.h)
target_precompile_headers(render_vulkan REUSE_FROM util)

target_compile_options(render_vulkan PUBLIC
    -DVK_ENABLE_BETA_EXTENSIONS=1)

target_link_libraries(render_vulkan PUBLIC
    render_vulkan_gpu ${VULKAN_RENDER_EXTENSION_DEP}
    librender util Vulkan::Vulkan Threads::Threads glfw)
add_dependencies(render_vulkan glslc-runtime-tools)

if (REPORT_RAY_STATS)
	target_compile_options(render_vulkan PUBLIC
		-DREPORT_RAY_STATS=1)
endif()

# IDE filters
set_main_targets(render_vulkan render_vulkan_gpu)
