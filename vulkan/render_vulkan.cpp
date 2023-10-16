// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "render_vulkan.h"
#include "render_pipeline_vulkan.h"
#ifdef ENABLE_RASTER
#include "pipeline_raster/raster_scene_vulkan.h"
#endif

#include <librender/scene.h>
#include <librender/halton.h>

#include "types.h"
#include "util.h"
#include "profiling.h"
#include "resource_utils.h"

#include <algorithm>
#include <numeric>

#include <glm/ext.hpp>
#include <cstdlib>

namespace glsl {
    using namespace glm;
    #include "../rendering/language.hpp"

    #include "gpu_params.glsl"

    #include "../librender/dequantize.glsl"
    #include "../librender/quantize.h"
}

extern "C" { extern struct GpuProgram const* const vulkan_integrators[]; }
extern "C" { extern struct GpuProgram const* const vulkan_raytracers[]; } // todo: what would be a better name here? (integrators + auxiliary renderers)
extern "C" { extern struct GpuProgram const vulkan_program_PROCESS_SAMPLES; }

#include "../librender/gpu_programs.h"
const int GPU_INTEGRATOR_COUNT = []() -> int {
    int i = 0;
    while (vulkan_integrators[i])
        ++i;
    return i;
}();
const std::vector<char const*> GPU_RAYTRACER_NAMES = []() {
    std::vector<char const*> names;
    for (int i = 0; vulkan_raytracers[i]; ++i) {
        names.push_back(vulkan_raytracers[i]->id);
    }
    return names;
}();

static const VkShaderStageFlags RECURSE_AND_SINK_SHADER_STAGES = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT
    | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
static const VkPipelineStageFlags TRACE_PIPELINE_STAGES = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

static const VkShaderStageFlags PROCESSING_SHADER_STAGES = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
static const VkPipelineStageFlags PROCESSING_PIPELINE_STAGES = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

static const VkShaderStageFlags SHARED_PIPELINE_SHADER_STAGES = PROCESSING_SHADER_STAGES
#ifdef ENABLE_RASTER
    | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
#endif
    | RECURSE_AND_SINK_SHADER_STAGES | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;

struct RenderVulkan::ParameterCache {
    glsl::LocalParams locals;
    glsl::GlobalParams globals;
};

using vkrt::ProfilingMarker;

RenderVulkan::RenderVulkan(vkrt::Device const& dev)
    : device(dev)
    , profiling_data(device)
{
    try { // need to handle all exceptions from here for manual multi-resource cleanup!
    base_arena_idx = device.next_arena(ArenaCount);

    {
        VkEventCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
        for (int i = 0; i < swap_buffer_count; ++i) {
            CHECK_VULKAN(vkCreateEvent(device->logical_device(), &info, nullptr, &render_done_events[i]));
            CHECK_VULKAN(vkSetEvent(device->logical_device(), render_done_events[i]));
        }
    }

    // Profiling structure
    profiling_data.initialize_queries();

    local_param_buf = vkrt::Buffer::device(*device,
                                        sizeof(glsl::LocalParams),
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        swap_buffer_count);
    global_param_buf = vkrt::Buffer::device(*device,
                                        sizeof(glsl::GlobalParams),
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        swap_buffer_count);

    cached_gpu_params.reset(new ParameterCache());

    null_buffer = vkrt::Buffer::device(*device, sizeof(uint64_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    null_texture = vkrt::Texture2D::device(*device
        , glm::ivec4(1, 1, 1, 0)
        , VK_FORMAT_R8G8B8A8_UNORM
        , VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        );
    {
        auto async_commands = device.async_command_stream();
        async_commands->begin_record();
        IMAGE_BARRIER(img_mem_barrier);
        img_mem_barrier.image = null_texture->image_handle();
        img_mem_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img_mem_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        img_mem_barrier.srcAccessMask = 0;
        vkCmdPipelineBarrier(async_commands->current_buffer,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            0,
                            0, nullptr,
                            0, nullptr,
                            1, &img_mem_barrier);
        async_commands->end_submit();
    }

    } catch (...) {
        internal_release_resources();
        throw;
    }
}

namespace vkrt {
    extern PFN_vkCmdTraceRaysKHR CmdTraceRaysKHR;
} // namespace

void RenderVulkan::create_pipelines(RenderExtension** active_extensions, int num_extensions, RenderBackendOptions* forceOptions) {
    this->available_pipeline_extensions.clear();
    for (int i = 0; i < num_extensions; ++i) {
        if (auto ext = dynamic_cast<RenderPipelineExtensionVulkan*>(active_extensions[i]))
            this->available_pipeline_extensions.push_back(ext);
    }

    // async build shaders
    prepare_raytracing_pipelines(true);
    for (int i = 0, ie = (int) pipeline_store.prepared.size(); i < ie; ++i) {
        if (!pipeline_store.prepared[i].pipeline) continue;
        pipeline_store.prepared[i].build = std::async([this, i]{
            ProfilingScope profile_pipeline("Build RT pipelines (total parallel)");
            pipeline_store.prepared[i].pipeline->wait_for_construction();
        });
    }
}

RenderVulkan::~RenderVulkan() {
    internal_release_resources();
}

void RenderVulkan::internal_release_resources()
{
    vkDeviceWaitIdle(device->logical_device());

    for (auto& pipeline_preparation : pipeline_store.prepared)
        if (pipeline_preparation.build.valid())
            pipeline_preparation.build.wait();
    pipeline_store.prepared.clear();
    pipeline_store.pipelines.release_resources();
    sample_processing_pipeline = nullptr;

    vkDestroyDescriptorPool(device->logical_device(), texture_desc_pool, nullptr);
    vkDestroyDescriptorPool(device->logical_device(), material_texture_desc_pool, nullptr);

    vkDestroyDescriptorSetLayout(device->logical_device(), null_desc_layout, nullptr);
    vkDestroyDescriptorSetLayout(device->logical_device(), textures_desc_layout, nullptr);
    vkDestroyDescriptorSetLayout(device->logical_device(), standard_textures_desc_layout, nullptr);

    vkDestroySampler(device->logical_device(), sampler, nullptr);
    vkDestroySampler(device->logical_device(), screen_sampler, nullptr);

    for (int i = 0; i < swap_buffer_count; ++i) {
        vkDestroyEvent(device->logical_device(), render_done_events[i], nullptr);
    }

    // Profiling structure
    profiling_data.destroy_queries();

    // Light data
    light_data_buf = nullptr;
}

std::string RenderVulkan::name() const
{
    return "Vulkan Ray Tracing";
}

ComputeDevice* RenderVulkan::create_compatible_compute_device() const {
    return new ComputeDeviceVulkan(device);
}

std::vector<std::string> const& RenderVulkan::variant_names() const {
    static const std::vector<std::string> public_variants = []() {
        std::vector<std::string> names;
        for (int i = 0; vulkan_integrators[i]; ++i) {
            names.push_back(vulkan_integrators[i]->id);
        }
        return names;
    }();
    return public_variants;
}

std::vector<std::string> const& RenderVulkan::variant_display_names() const {
    static const std::vector<std::string> public_variants = []() {
        std::vector<std::string> names;
        for (int i = 0; vulkan_integrators[i]; ++i) {
            std::string name = vulkan_integrators[i]->name;
            name += " (";
            name += vulkan_integrators[i]->id;
            name += ')';
            names.push_back(name);
        }
        return names;
    }();
    return public_variants;
}

void RenderVulkan::mark_unsupported_variants(char* support_flags) {
    for (int i = 0; vulkan_integrators[i]; ++i)
        if (!this->pipeline_store.support_flags[i])
            support_flags[i] = 0;
}

int RenderVulkan::variant_index(char const* name) {
    for (size_t i = 0, ie = GPU_RAYTRACER_NAMES.size(); i < ie; ++i)
        if (strcmp(GPU_RAYTRACER_NAMES[i], name) == 0)
            return (int) i;
    return -1;
}

static const VkFormat ACCUMULATION_BUFFER_FORMAT = VK_FORMAT_R32G32B32A32_SFLOAT;
static const VkFormat POST_PROCESSING_BUFFER_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
static const VkFormat AOV_BUFFER_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
static const VkFormat DEPTH_STENCIL_BUFFER_FORMAT = VK_FORMAT_D32_SFLOAT;

void RenderVulkan::initialize(const int render_width, const int render_height)
{
    frame_id = 0;
    frame_offset = 0;

    CHECK_VULKAN(vkDeviceWaitIdle(device->logical_device()));

    vkrt::MemorySource memory_arena(device, vkrt::Device::DisplayArena);

    int render_upscale_factor = std::max(this->RenderBackend::options.render_upscale_factor, 1);
    this->active_options.render_upscale_factor = render_upscale_factor;

    for (int i = 0; i < 2; ++i) {
        render_targets[i] = vkrt::Texture2D::device(memory_arena,
            glm::ivec4(render_width * render_upscale_factor, render_height * render_upscale_factor, 0, 0),
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_STORAGE_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    for (int i = 0; i < 2; ++i) {
#ifdef ATOMIC_ACCUMULATE
        atomic_accum_buffers[i] = vkrt::Texture2D::device(memory_arena,
                                                    glm::ivec4(render_width, render_height, 4, 0),
    #ifdef ATOMIC_ACCUMULATE_ADD
                                                    VK_FORMAT_R32_SFLOAT,
    #else
                                                    VK_FORMAT_R32_UINT,
    #endif
                                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
#else
        atomic_accum_buffers[i] = nullptr;
#endif
        accum_buffers[i] = vkrt::Texture2D::device(alias(memory_arena, atomic_accum_buffers[i]),
                                            glm::ivec4(render_width, render_height, 0, 0),
                                            ACCUMULATION_BUFFER_FORMAT,
                                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT
#ifdef ENABLE_RASTER
                                            | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
#endif
                                            | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                            | VK_IMAGE_USAGE_SAMPLED_BIT
#ifdef ENABLE_DPCPP
                                            , VK_IMAGE_TILING_LINEAR
#endif
);
    }
    // place half precision post processing buffers in memory of outdated history buffers
    for (int i = 0; i < 2; ++i) {
        half_post_processing_buffers[i] = vkrt::Texture2D::device(
#ifndef DENOISE_BUFFER_BIND_POINT
                                            alias(memory_arena, accum_buffers[!i]),
#else
                                            memory_arena,
#endif
                                            glm::ivec4(render_width, render_height, 0, 0),
                                            POST_PROCESSING_BUFFER_FORMAT,
                                            VK_IMAGE_USAGE_STORAGE_BIT
#ifdef ENABLE_RASTER
                                            | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
#endif
                                            | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                            | VK_IMAGE_USAGE_SAMPLED_BIT
#ifdef ENABLE_DPCPP
                                            , VK_IMAGE_TILING_LINEAR
#endif
);
    }

    img_readback_buf = vkrt::Buffer::host(memory_arena
        , render_width * render_height * render_upscale_factor * render_upscale_factor * sizeof(float) * 4
        , VK_BUFFER_USAGE_TRANSFER_DST_BIT
        , VK_MEMORY_PROPERTY_HOST_CACHED_BIT); // readback heap in DX is cached for reading

#ifdef REPORT_RAY_STATS
    // todo: why are these full images? clean into more lightweight and more general stats
    ray_stats =
        vkrt::Texture2D::device(memory_arena,
                                glm::ivec4(render_width, render_height, 0, 0),
                                VK_FORMAT_R16_UINT,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT, swap_buffer_count);
    ray_stats_readback_buf = vkrt::Buffer::host(
        memory_arena, render_width * render_height * ray_stats->pixel_size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, swap_buffer_count);
    ray_counts.resize(render_width * render_height, 0);
#endif

#ifdef ATOMIC_ACCUMULATE
    // aliased memory confuses validation layer with undefined layout states
    {
        auto async_commands = device.async_command_stream();
        async_commands->begin_record();
        for (int i = 0; i < 2; ++i) {
            IMAGE_BARRIER(img_mem_barrier);
            img_mem_barrier.image = (i == 0 ? atomic_accum_buffer : accum_buffer)->image_handle();
            img_mem_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            img_mem_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            img_mem_barrier.srcAccessMask = 0;
            vkCmdPipelineBarrier(async_commands->current_buffer,
                                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                0,
                                0, nullptr,
                                0, nullptr,
                                1, &img_mem_barrier);
        }
        async_commands->end_submit();
    }
#endif
    // recreate AOV buffers
#ifndef ENABLE_AOV_BUFFERS
    if (aov_buffers[0])
#endif
        enable_aovs();

#ifdef ENABLE_RASTER
    depth_buffer = vkrt::Texture2D::device(memory_arena,
        glm::ivec4(render_width, render_height, 0, 0),
        DEPTH_STENCIL_BUFFER_FORMAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT); // todo: can we access with VK_IMAGE_USAGE_STORAGE_BIT?
#endif

    if (per_pixel_ray_query_budget)
        enable_ray_queries(fixed_ray_query_budget, per_pixel_ray_query_budget);
}

void RenderVulkan::enable_aovs() {
#ifdef ENABLE_AOV_BUFFERS
    vkrt::MemorySource memory_arena(device, vkrt::Device::DisplayArena);

    int render_width = accum_buffers[0]->tdims.x;
    int render_height = accum_buffers[0]->tdims.y;

    auto aov_usage_flags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
#ifdef ENABLE_RASTER
    aov_usage_flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
#endif
    aov_usage_flags |= VK_IMAGE_USAGE_SAMPLED_BIT;

    for (int i = 0; i < 2; ++i) {
        aov_buffers[AOVAlbedoRoughnessIndex + i * AOVBufferCount] = vkrt::Texture2D::device(memory_arena,
            glm::ivec4(render_width, render_height, 0, 0),
            AOV_BUFFER_FORMAT,
            aov_usage_flags
    #ifdef ENABLE_DPCPP
            , VK_IMAGE_TILING_LINEAR
    #endif
            );

        aov_buffers[AOVNormalDepthIndex + i * AOVBufferCount] = vkrt::Texture2D::device(memory_arena,
            glm::ivec4(render_width, render_height, 0, 0),
            AOV_BUFFER_FORMAT,
            aov_usage_flags
#ifdef ENABLE_DPCPP
            , VK_IMAGE_TILING_LINEAR
#endif
            );
    }

    aov_buffers[AOVMotionJitterIndex] = vkrt::Texture2D::device(memory_arena,
        glm::ivec4(render_width, render_height, 0, 0),
        AOV_BUFFER_FORMAT,
        aov_usage_flags
#ifdef ENABLE_DPCPP
        , VK_IMAGE_TILING_LINEAR
#endif
    );
    for (int i = 1; i < 2; ++i)
        aov_buffers[AOVMotionJitterIndex + i * AOVBufferCount] = aov_buffers[AOVMotionJitterIndex];
#endif

    if (!screen_sampler) {
        VkSamplerCreateInfo sampler_info = {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        CHECK_VULKAN(
            vkCreateSampler(device->logical_device(), &sampler_info, nullptr, &screen_sampler));
    }
}

void RenderVulkan::enable_ray_queries(int max_queries, const int max_queries_per_pixel) {
    vkrt::MemorySource memory_arena(device, vkrt::Device::DisplayArena);

    fixed_ray_query_budget = max_queries;
    per_pixel_ray_query_budget = max_queries_per_pixel;

    // not initialized yet
    if (!accum_buffers[0])
        return;

    size_t max_query_budget = accum_buffers[0]->tdims.x * accum_buffers[0]->tdims.y * size_t(per_pixel_ray_query_budget);
    max_query_budget = std::max(max_query_budget, size_t(fixed_ray_query_budget));

    ray_query_buffer = vkrt::Buffer::device(memory_arena,
        max_query_budget * sizeof(RenderRayQuery),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    ray_result_buffer = vkrt::Buffer::device(memory_arena,
        max_query_budget * sizeof(float) * 4,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
}

void RenderVulkan::update_geometry(const Scene &scene, bool& update_sbt, bool& rebuild_tlas) {
    vkrt::MemorySource static_memory_arena(device, base_arena_idx + StaticArenaOffset);
    vkrt::MemorySource scratch_memory_arena(device, vkrt::Device::ScratchArena);

    bool blas_changed = false;
    bool blas_content_changed = false;

    auto async_commands = device.async_command_stream();
    auto sync_commands = device.sync_command_stream();
    // TODO: We can actually run all these uploads and BVH builds in parallel
    // using multiple command lists, as long as the BVH builds don't need so
    // much build + scratch that we run out of GPU memory.
    // Some helpers for managing the temp upload heap buf allocation and queuing of
    // the commands would help to make it easier to write the parallel load version
    meshes.resize(scene.meshes.size());

    const int max_pending_bvh_tris = 5000000;
    const int upload_batch_min_tri_count = max_pending_bvh_tris/4;
    len_t upload_batch_current_tri_count = 0;

    auto&& build_pending_bvhs = [&](int mesh_idx_begin, int mesh_idx_end) {
        // finish pending uploads
        if (upload_batch_current_tri_count != 0) {
            async_commands->end_submit();
            upload_batch_current_tri_count = 0;
        }
        async_commands->wait_complete();

        ProfilingScope profile_bvh("Scene BLAS");

        size_t totalBVHBytes = 0;
        if (vkrt::CmdTraceRaysKHR) {
            // Build initial BVH
            ProfilingScope build_bvh("Build BLAS");
            sync_commands->begin_record();
            for (int mesh_idx = mesh_idx_begin; mesh_idx < mesh_idx_end; ++mesh_idx) {
                // todo: respect optimization flag to check if a refit is enough?
                meshes[mesh_idx]->enqueue_build(sync_commands->current_buffer, static_memory_arena, scratch_memory_arena
                    , mesh_idx == mesh_idx_begin || mesh_idx == mesh_idx_end-1); // barriers in the beginning and end
                totalBVHBytes += meshes[mesh_idx]->cached_build_size;
            }
            sync_commands->end_submit();
            sync_commands->begin_record();
            for (int mesh_idx = mesh_idx_begin; mesh_idx < mesh_idx_end; ++mesh_idx)
                meshes[mesh_idx]->enqueue_post_build_async(sync_commands->current_buffer);
            sync_commands->end_submit();
            build_bvh.end();

            // Compact the BVH
            ProfilingScope compact_bvh("Compact BLAS");
            sync_commands->begin_record();
            for (int mesh_idx = mesh_idx_begin; mesh_idx < mesh_idx_end; ++mesh_idx)
                meshes[mesh_idx]->enqueue_compaction(sync_commands->current_buffer, static_memory_arena);
            sync_commands->end_submit();
        }

        size_t totalCompactBVHBytes = 0;
        // Update BVH & geometry state
        sync_commands->begin_record();
        for (int mesh_idx = mesh_idx_begin; mesh_idx < mesh_idx_end; ++mesh_idx) {
            auto& bvh = meshes[mesh_idx];
            auto& mesh = scene.meshes[mesh_idx];
            // Retrieve handles
            if (vkrt::CmdTraceRaysKHR) {
                bvh->finalize();
                totalCompactBVHBytes += meshes[mesh_idx]->bvh_buf->size();
            }

            bool model_changed = bvh->model_revision != mesh.model_revision;
            bool vertices_changed = bvh->vertex_revision != mesh.model_vertex_revision();

            bvh->model_revision = mesh.model_revision;
            bvh->vertex_revision = mesh.model_vertex_revision();
            bvh->attribute_revision = mesh.model_attribute_revision();
            bvh->optimize_revision = mesh.model_optimize_revision();

            rebuild_tlas |= model_changed;
            blas_changed |= vertices_changed;
        }
        sync_commands->end_submit();

        if (totalBVHBytes > 0) {
          println(CLL::VERBOSE, "BVH(s) compacted to %.1f%% from %sB to %sB"
              , 100.0 * totalCompactBVHBytes / totalBVHBytes
              , pretty_print_count(totalBVHBytes).c_str()
              , pretty_print_count(totalCompactBVHBytes).c_str());
        }
    };

    len_t pending_bvh_tris = 0;
    int pending_bvh_begin = 0, pending_bvh_end = 0;

    ProfilingScope profile_geometry("Upload geometry");

    for (int mesh_idx = 0; mesh_idx < (int) scene.meshes.size(); ++mesh_idx) {
        const auto &mesh = scene.meshes[mesh_idx];

        bool model_changed = true, vertices_changed = true, attributes_changed = true, optimize_changed = true;
        vkrt::TriangleMesh* cached_mesh = meshes[mesh_idx].get();
        if (cached_mesh) {
            model_changed = cached_mesh->model_revision != mesh.model_revision;
            vertices_changed = cached_mesh->vertex_revision != mesh.model_vertex_revision();
            attributes_changed = cached_mesh->attribute_revision != mesh.model_attribute_revision();
            optimize_changed = cached_mesh->optimize_revision != mesh.model_optimize_revision();
        }
        if (!model_changed && cached_mesh->geometries.size() != mesh.geometries.size())
            throw_error("Geometric structure changed without model revision increment");
        if (!vertices_changed && !attributes_changed && !optimize_changed)
            continue;

        len_t mesh_vertex_count = 0;
        len_t mesh_tri_count = 0;
        int mesh_quantized_pos = -1;
        int mesh_quantized_nrm_uv = -1;
        bool mesh_needs_indices = false;
        bool mesh_explicit_indexing = false;
        for (int geo_idx = 0; geo_idx < (int) mesh.geometries.size(); ++geo_idx) {
            const auto &geom = mesh.geometries[geo_idx];
            int num_verts = geom.num_verts();
            int num_tris = geom.num_tris();

            if (num_verts > 0) {
                if (mesh_quantized_pos == -1)
                    mesh_quantized_pos = int(geom.format_flags & Geometry::QuantizedPositions);
                else if (mesh_quantized_pos != int(geom.format_flags & Geometry::QuantizedPositions))
                    throw_error("Mismatching mesh geometry quantization flags not supported by Vulkan backend");
                if (mesh_quantized_nrm_uv == -1)
                    mesh_quantized_nrm_uv = int(geom.format_flags & Geometry::QuantizedNormalsAndUV);
                else if (mesh_quantized_nrm_uv != int(geom.format_flags & Geometry::QuantizedNormalsAndUV))
                    throw_error("Mismatching mesh geometry quantization flags not supported by Vulkan backend");
            }
            if (num_tris > 0) {
                assert(!geom.indices.empty() || (geom.format_flags & Geometry::NoIndices) == Geometry::NoIndices);
                if ((geom.format_flags & Geometry::NoIndices) != Geometry::NoIndices)
                    mesh_needs_indices = true;
                if (!(geom.format_flags & Geometry::ImplicitIndices)) {
                    mesh_explicit_indexing = true;
#ifdef REQUIRE_UNROLLED_VERTICES
                    throw_error("Expecting unindexed mesh data");
#endif
                }
            }

            mesh_vertex_count += num_verts;
            mesh_tri_count += num_tris;
        }

        if (pending_bvh_begin != pending_bvh_end && pending_bvh_tris + mesh_tri_count > max_pending_bvh_tris) {
            build_pending_bvhs(pending_bvh_begin, pending_bvh_end);
            pending_bvh_begin = pending_bvh_end;
            pending_bvh_tris = 0;
        }

        (void) uint_bound(mesh_vertex_count);
        (void) uint_bound(mesh_vertex_count-1);
        (void) uint_bound(mesh_tri_count);
        (void) uint_bound(mesh_tri_count-1);

        bool dynamic_vertices = (mesh.flags & Mesh::Dynamic) || (mesh.flags & Mesh::SubtlyDynamic);

        auto geometry_usage_flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT
            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
#ifdef ENABLE_RASTER
        geometry_usage_flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
#endif

        std::vector<vkrt::Geometry> geometries = cached_mesh ? std::move(cached_mesh->geometries) : std::vector<vkrt::Geometry>{};
        geometries.resize(mesh.geometries.size());
        vkrt::Geometry const* cached_geom0 = &geometries.front();

        vkrt::Buffer mesh_vertex_buf = vkrt::Buffer::device(reuse(static_memory_arena, cached_geom0->vertex_buf),
#ifdef QUANTIZED_POSITIONS
            mesh_vertex_count * sizeof(uint64_t),
#else
            mesh_vertex_count * sizeof(glm::vec3),
#endif
            geometry_usage_flags
            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
            | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
#ifdef QUANTIZED_POSITIONS
        vkrt::Buffer mesh_float_vertex_buf = vkrt::Buffer::device(
              reuse(dynamic_vertices ? static_memory_arena : scratch_memory_arena, cached_geom0->float_vertex_buf)
            , sizeof(glm::vec3) * mesh_vertex_count
            , geometry_usage_flags
            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
            | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
#else
        vkrt::Buffer mesh_float_vertex_buf = mesh_vertex_buf;
#endif

        vkrt::Buffer mesh_normal_buf = vkrt::Buffer::device(reuse(static_memory_arena, cached_geom0->normal_buf),
#ifdef QUANTIZED_NORMALS_AND_UVS
            mesh_vertex_count * sizeof(uint64_t),
#else
            mesh_vertex_count * sizeof(glm::vec3),
#endif
            geometry_usage_flags);
        vkrt::Buffer mesh_uv_buf = nullptr;
#ifdef QUANTIZED_NORMALS_AND_UVS
        mesh_uv_buf = mesh_normal_buf; // quantized uvs share the same buffer if present
#else
        mesh_uv_buf = vkrt::Buffer::device(reuse(static_memory_arena, cached_geom0->uv_buf),
                                           mesh_vertex_count * sizeof(glm::vec2),
                                           geometry_usage_flags);
#endif

        vkrt::Buffer bvh_indices = nullptr;
        if (mesh_needs_indices) {
            vkrt::Geometry const* cached_geomN = cached_geom0;
            for (auto& g : geometries)
                if (g.index_buf) {
                    cached_geomN = &g;
                    break;
                }
            bool keep_indices = mesh_explicit_indexing || dynamic_vertices;
            bvh_indices = vkrt::Buffer::device(reuse(keep_indices ? static_memory_arena : scratch_memory_arena, cached_geomN->index_buf),
                mesh_tri_count * sizeof(glm::uvec3),
                geometry_usage_flags
                | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
        }

        if (upload_batch_current_tri_count == 0)
            async_commands->begin_record();
        upload_batch_current_tri_count += std::max(mesh_tri_count, (len_t) 1);

        if (vertices_changed) {
            vkrt::Buffer upload_float_verts = mesh_float_vertex_buf->for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, scratch_memory_arena);
#ifdef QUANTIZED_POSITIONS
            vkrt::Buffer upload_verts = mesh_vertex_buf->for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, scratch_memory_arena);
#endif
            glm::vec3* float_map = (glm::vec3*) upload_float_verts->map();

            for (int geo_idx = 0; geo_idx < (int) mesh.geometries.size(); ++geo_idx) {
                const auto &geom = mesh.geometries[geo_idx];
                vkrt::Geometry* cached_geom = &geometries[geo_idx];

                int vertexCount = geom.num_verts();
                glm::vec3 quantized_offset = geom.quantized_offset;
                glm::vec3 quantized_scaling = geom.quantized_scaling;
                {
                    // dequantize positions for BVH build and/or rendering
                    if (geom.format_flags & Geometry::QuantizedPositions) {
                        uint64_t const* quantized_vertices = (uint64_t const*) geom.vertices.data();
                        for (int i = 0; i < vertexCount; ++i) {
                            using namespace glm;
                            float_map[i] = DEQUANTIZE_POSITION(
                                quantized_vertices[i], quantized_scaling, quantized_offset
                            );
                        }
                    }
                    else
                        std::memcpy(float_map, geom.vertices.data(), geom.vertices.nbytes());
                }
                float_map += vertexCount;
            }
            upload_float_verts->unmap();

            VkBufferCopy copy_cmd = {};
            copy_cmd.size = upload_float_verts->size();
            vkCmdCopyBuffer(async_commands->current_buffer,
                            upload_float_verts->handle(),
                            mesh_float_vertex_buf->handle(),
                            1,
                            &copy_cmd);
            async_commands->hold_buffer(upload_float_verts);

#ifdef QUANTIZED_POSITIONS
            void* map = upload_verts->map();

            for (int geo_idx = 0; geo_idx < (int) mesh.geometries.size(); ++geo_idx) {
                const auto &geom = mesh.geometries[geo_idx];
                vkrt::Geometry* cached_geom = &geometries[geo_idx];

                int vertexCount = geom.num_verts();

                uint64_t* quantized_vertices = (uint64_t*) map;
                {
                    // quantized position for rendering
                    if (geom.format_flags & Geometry::QuantizedPositions)
                        std::memcpy(quantized_vertices, geom.vertices.data(), geom.vertices.nbytes());
                    else {
                        glm::vec3 const* unquantized_vertices = (glm::vec3 const*) geom.vertices.data();
                        glm::vec3 quantized_scaling = glsl::dequantization_scaling(geom.extent);
                        glm::vec3 quantized_offset = glsl::dequantization_offset(geom.base, geom.extent);
                        for (int i = 0; i < vertexCount; ++i)
                            quantized_vertices[i] = glsl::quantize_position(unquantized_vertices[i], geom.extent, geom.base);
                    }
                }
                map = quantized_vertices + vertexCount;
            }

            upload_verts->unmap();

            copy_cmd.size = upload_verts->size();
            vkCmdCopyBuffer(async_commands->current_buffer,
                            upload_verts->handle(),
                            mesh_vertex_buf->handle(),
                            1,
                            &copy_cmd);
            async_commands->hold_buffer(upload_verts);
#endif
        }

        if (vertices_changed || attributes_changed) {
            vkrt::Buffer upload_normals = mesh_normal_buf->for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, scratch_memory_arena);
            vkrt::Buffer upload_uvs = nullptr;
#ifndef QUANTIZED_NORMALS_AND_UVS
            if (attributes_changed)
                upload_uvs = mesh_uv_buf->for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, scratch_memory_arena);
#endif

            void *map = upload_normals->map();

            for (int geo_idx = 0; geo_idx < (int) mesh.geometries.size(); ++geo_idx) {
                const auto &geom = mesh.geometries[geo_idx];
                vkrt::Geometry* cached_geom = &geometries[geo_idx];

                int vertexCount = geom.num_verts();

                if (!geom.normals.empty()
#ifdef QUANTIZED_NORMALS_AND_UVS
                    || (geom.format_flags & Geometry::QuantizedNormalsAndUV) && !geom.uvs.empty() // quantized uvs share the same buffer if present
#endif
                ) {
#ifdef QUANTIZED_NORMALS_AND_UVS
                    if (!(geom.format_flags & Geometry::QuantizedNormalsAndUV)) {
                        glm::vec3 const* unquantized_normals = (glm::vec3 const*) geom.normals.data();
                        glm::vec2 const* unquantized_uvs = (glm::vec2 const*) geom.uvs.data();
                        uint64_t* quantized_vertices = (uint64_t*) map;
                        for (int i = 0; i < vertexCount; ++i) {
                            uint64_t n_uvs = 0;
                            if (unquantized_normals)
                                n_uvs |= glsl::quantize_normal(unquantized_normals[i]);
                            if (unquantized_uvs)
                                n_uvs |= (uint64_t) glsl::quantize_uv(unquantized_uvs[i], glm::vec3(4.0f)) << 32; // uv base 4, to support negative wraparound
                            quantized_vertices[i] = n_uvs;
                        }
                    }
#else
                    if (geom.format_flags & Geometry::QuantizedNormalsAndUV) {
                        uint64_t const* quantized_vertices = (uint64_t const*) geom.normals.data();
                        glm::vec3* dequantized_vertices = (glm::vec3*) map;
                        for (int i = 0; i < vertexCount; ++i)
                            dequantized_vertices[i] = glsl::dequantize_normal(quantized_vertices[i]);
                    }
#endif
                    else {
                        auto* src_data = geom.normals.data();
                        size_t src_size = geom.normals.nbytes();
                        if (!src_data) {
                            assert((geom.format_flags & Geometry::QuantizedNormalsAndUV) && !geom.uvs.empty());
                            src_data = geom.uvs.data(); // quantized uvs share the same buffer if present
                            src_size = geom.uvs.nbytes();
                        }
                        std::memcpy(map, src_data, src_size);
                    }
                }
#ifdef QUANTIZED_NORMALS_AND_UVS
                map = (uint64_t*) map + vertexCount;
#else
                map = (glm::vec3*) map + vertexCount;
#endif
            }

            upload_normals->unmap();

            VkBufferCopy copy_cmd = {};
            copy_cmd.size = upload_normals->size();
            vkCmdCopyBuffer(async_commands->current_buffer,
                            upload_normals->handle(),
                            mesh_normal_buf->handle(),
                            1,
                            &copy_cmd);
            async_commands->hold_buffer(upload_normals);

            if (upload_uvs) {
                void* map_uv = upload_uvs->map();

                for (int geo_idx = 0; geo_idx < (int) mesh.geometries.size(); ++geo_idx) {
                    const auto &geom = mesh.geometries[geo_idx];
                    vkrt::Geometry* cached_geom = &geometries[geo_idx];

                    int vertexCount = geom.num_verts();

                    if (!geom.uvs.empty()) {
                        if (geom.format_flags & Geometry::QuantizedNormalsAndUV) {
                            uint64_t const* quantized_vertices = (uint64_t const*) geom.uvs.data();
                            glm::vec2* dequantized_vertices = (glm::vec2*) map_uv;
                            for (int i = 0; i < vertexCount; ++i)
                                dequantized_vertices[i] = glsl::dequantize_uv(uint32_t(quantized_vertices[i] >> 32));
                        }
                        else
                            std::memcpy(map_uv, geom.uvs.data(), geom.uvs.nbytes());
                    }
                    map_uv = (glm::vec2*) map_uv + vertexCount;
                }

                upload_uvs->unmap();

                VkBufferCopy copy_cmd = {};
                copy_cmd.size = upload_uvs->size();
                vkCmdCopyBuffer(async_commands->current_buffer,
                                upload_uvs->handle(),
                                mesh_uv_buf->handle(),
                                1,
                                &copy_cmd);
                async_commands->hold_buffer(upload_uvs);
            }
        }

        if (model_changed && bvh_indices) {
            vkrt::Buffer upload_indices = bvh_indices->for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, scratch_memory_arena);

            void *map = upload_indices->map();

            for (int geo_idx = 0; geo_idx < (int) mesh.geometries.size(); ++geo_idx) {
                const auto &geom = mesh.geometries[geo_idx];
                vkrt::Geometry* cached_geom = &geometries[geo_idx];

                int triCount = geom.num_tris();

                assert(!geom.indices.empty() || (geom.format_flags & Geometry::NoIndices) == Geometry::NoIndices);
                if (!geom.indices.empty() && (geom.format_flags & Geometry::NoIndices) != Geometry::NoIndices)
                    std::memcpy(map, geom.indices.data(), geom.indices.nbytes());
                map = (glm::uvec3*) map + triCount;
            }

            upload_indices->unmap();

            VkBufferCopy copy_cmd = {};
            copy_cmd.size = upload_indices->size();
            vkCmdCopyBuffer(async_commands->current_buffer,
                            upload_indices->handle(),
                            bvh_indices->handle(),
                            1,
                            &copy_cmd);
            async_commands->hold_buffer(upload_indices);
        }

        if (upload_batch_current_tri_count >= upload_batch_min_tri_count) {
            async_commands->end_submit();
            upload_batch_current_tri_count = 0;
        }

        if (model_changed) {
            int vertexOffset = 0;
            int triOffset = 0;
            for (int geo_idx = 0; geo_idx < (int) mesh.geometries.size(); ++geo_idx) {
                const auto &geom = mesh.geometries[geo_idx];
                vkrt::Geometry* cached_geom = &geometries[geo_idx];

                vkrt::Geometry& vkgeo = geometries[geo_idx];
                vkgeo.float_vertex_buf = mesh_float_vertex_buf;
                vkgeo.vertex_buf = mesh_vertex_buf;
                vkgeo.normal_buf = !geom.normals.empty() ? mesh_normal_buf : nullptr;
                vkgeo.uv_buf = !geom.uvs.empty() ? mesh_uv_buf : nullptr;
                vkgeo.index_buf = !geom.indices.empty() && (geom.format_flags & Geometry::NoIndices) != Geometry::NoIndices ? bvh_indices : nullptr;
                vkgeo.indices_are_implicit = bool(geom.format_flags & Geometry::ImplicitIndices);
                vkgeo.index_offset = geom.index_offset;
                vkgeo.vertex_offset = vertexOffset;
                vkgeo.triangle_offset = triOffset;
                vkgeo.num_active_vertices = geom.num_verts();
                vkgeo.num_active_triangles = geom.num_tris();
                vkgeo.quantized_offset = geom.quantized_offset;
                vkgeo.quantized_scaling = geom.quantized_scaling;
                // vkgeo.geo_flags = 0;

                vertexOffset += vkgeo.num_active_vertices;
                triOffset += vkgeo.num_active_triangles;
            }
            update_sbt = true;
        }

        bool need_new_bvh = model_changed || /* todo: remove */ vertices_changed;

        if (!need_new_bvh) {
            cached_mesh->geometries = std::move(geometries);
            cached_mesh->attribute_revision = mesh.model_attribute_revision();
        }

        if (!vertices_changed && !optimize_changed)
            continue;

        std::unique_ptr<vkrt::TriangleMesh> bvh = std::move(meshes[mesh_idx]);
        if (need_new_bvh) {
            uint32_t bvh_flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
            if (mesh.flags & Mesh::SubtlyDynamic) {
                bvh_flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                    | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
                    | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
            }
            else if (mesh.flags & Mesh::Dynamic) {
                bvh_flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
                    | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
            }

            // Build the bottom level acceleration structure
            bvh = std::make_unique<vkrt::TriangleMesh>(*device, std::move(geometries), bvh_flags);
        }

        meshes[mesh_idx] = std::move(bvh);
        pending_bvh_tris += mesh_tri_count;
        pending_bvh_end = mesh_idx + 1;
    }

    if (pending_bvh_begin != pending_bvh_end) {
        build_pending_bvhs(pending_bvh_begin, pending_bvh_end);
        pending_bvh_begin = pending_bvh_end;
        pending_bvh_tris = 0;
    }
    assert(upload_batch_current_tri_count == 0);

    profile_geometry.end();

    size_t max_staging_bvh_bytes = 0;
    size_t total_staging_bvh_bytes = 0;
    for (auto& mesh : meshes)
        if (mesh->scratch_buf) {
            max_staging_bvh_bytes = std::max(max_staging_bvh_bytes, mesh->scratch_buf.size());
            total_staging_bvh_bytes += mesh->scratch_buf.size();
        }
    if (max_staging_bvh_bytes > 0) {
        println(CLL::INFORMATION, "BVH staging storage is %sB, max needed would be %sB"
            , pretty_print_count(total_staging_bvh_bytes).c_str()
            , pretty_print_count(max_staging_bvh_bytes).c_str());
    }

    if (blas_changed)
        ++blas_generation;
    if (blas_changed || blas_content_changed)
        ++blas_content_generation;

    mesh_shader_names.resize(meshes.size());
    for (size_t i = 0, ie = mesh_shader_names.size(); i < ie; ++i) {
        int mc = ilen(meshes[i]->geometries);
        mesh_shader_names[i].resize(mc);

        auto& custom_shader_names = scene.meshes[i].mesh_shader_names;
        int num_custom_shader_names = std::min((int) custom_shader_names.size(), mc);
        int j = 0;
        for (; j < num_custom_shader_names; ++j)
            mesh_shader_names[i][j] = custom_shader_names[j];
        for (; j < mc; ++j)
            mesh_shader_names[i][j] = { };
    }
}

void RenderVulkan::update_lights(const Scene &scene)
{
    uint32_t numPointLights = scene.pointLights.size();
    uint32_t numQuadLights = scene.quadLights.size();
    lightData.resize(numPointLights/* + numQuadLights*/);

    // Add the point lights
    for (uint32_t lightIdx = 0; lightIdx < numPointLights; ++lightIdx)
    {
        // Grab the current light to convert
        const PointLight& light = scene.pointLights[lightIdx];

        // Convert 
        LightData data;
        data.type = LIGHT_TYPE_POINT;
        data.positionWS = light.positionWS;
        data.range = light.range;
        data.radiance = light.radiance;
        data.falloff = light.falloff;

        // Export to the buffer
        lightData[lightIdx] = data;
    }

    /*
    // Add the quad lights
    for (uint32_t lightIdx = 0; lightIdx < numQuadLights; ++lightIdx)
    {
        // Grab the current light to convert
        const QuadLight& light = scene.quadLights[lightIdx];

        // Convert 
        LightData data;

        data.type = LIGHT_TYPE_QUAD;

        data.positionWS = light.position;
        data.radiance = light.emission;

        data.range = 10.0f;
        data.falloff = 1.0f;

        data.width = light.width;
        data.height = light.height;

        // Export to the buffer
        lightData[lightIdx] = data;
    }
    */
    // Add the tri lights

    // Upload to the GPU
    upload_light_data();
}

void RenderVulkan::update_meshes(const Scene &scene, bool& update_sbt, bool& rebuild_sbt) {
    vkrt::MemorySource static_memory_arena(device, base_arena_idx + StaticArenaOffset);
    vkrt::MemorySource scratch_memory_arena(device, vkrt::Device::ScratchArena);

    auto async_commands = device.async_command_stream();

    // Compute the offsets each parameterized mesh will be written too in the SBT,
    // these are then the instance SBT offsets shared by each instance
    len_t unrolled_geometry_offset = 0;
    parameterized_meshes.resize(scene.parameterized_meshes.size());
    lod_groups.resize(scene.parameterized_meshes.size());
    scene_lod_group_count = scene.lod_groups.size();
    for (int pm_idx = 0; pm_idx < (int) scene.parameterized_meshes.size(); ++pm_idx) {
        auto const& pm = scene.parameterized_meshes[pm_idx];

        // store lod group for leading meshes
        auto& lodGroup = scene.lod_groups[pm.lod_group];
        if (!lodGroup.mesh_ids.empty() && lodGroup.mesh_ids[0] == pm_idx)
            this->lod_groups[pm_idx] = lodGroup;
        else
            this->lod_groups[pm_idx] = { };

        bool model_changed = true, materials_changed = true, shaders_changed = true;
        bool assigned_mesh_changed = true;
        vkrt::ParameterizedMesh* cached_mesh = &parameterized_meshes[pm_idx];
        if (cached_mesh && cached_mesh->mesh_id >= 0) {
            model_changed = cached_mesh->model_revision != pm.model_revision;
            materials_changed = cached_mesh->material_revision != pm.model_material_revision();
            shaders_changed = cached_mesh->shader_revision != pm.model_shader_revision();
            assigned_mesh_changed = cached_mesh->mesh_id != pm.mesh_id || cached_mesh->mesh_model_revision != meshes[pm.mesh_id]->model_revision;
        }
        if (!model_changed && cached_mesh->mesh_id != pm.mesh_id)
            throw_error("Mesh index changed without model revision increment");
        if (!materials_changed && !shaders_changed)
            continue;

        bool pm_no_alpha;

        async_commands->begin_record();

        vkrt::Buffer upload_materials = nullptr;
        vkrt::Buffer materials_buf = nullptr;
        if (pm.per_triangle_materials()) {
            materials_buf = vkrt::Buffer::device(reuse(static_memory_arena, cached_mesh->per_triangle_material_buf),
                pm.num_triangle_material_ids() * sizeof(uint8_t),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT
                | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

            if (materials_changed) {
                upload_materials = materials_buf->secondary_for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

                uint8_t *map = (uint8_t*) upload_materials->map();
                uint8_t *mapped_id = map;
                switch (pm.material_id_bitcount) {
                case 8:
                    memcpy(mapped_id, pm.triangle_material_ids.data(), upload_materials->size());
                    break;
                case 16:
                    for (unsigned id : pm.triangle_material_ids.as_range<uint16_t>())
                        *mapped_id++ = static_cast<uint8_t>(id);
                    break;
                case 32:
                    for (unsigned id : pm.triangle_material_ids.as_range<uint32_t>())
                        *mapped_id++ = static_cast<uint8_t>(id);
                    break;
                default:
                    assert(false);
                }

                pm_no_alpha = true;
                len_t i = 0;
                for (int j = 0, je = scene.meshes[pm.mesh_id].num_geometries(); j < je; ++j) {
                    int materialOffset = pm.material_offset(j);
                    for (len_t ie = i + scene.meshes[pm.mesh_id].geometries[j].num_tris(); i < ie; ++i)
                        pm_no_alpha &= (scene.materials[map[i] + materialOffset].flags & BASE_MATERIAL_NOALPHA) != 0;
                }

                upload_materials->unmap();

                VkBufferCopy copy_cmd = {};
                copy_cmd.size = upload_materials->size();
                vkCmdCopyBuffer(async_commands->current_buffer,
                                upload_materials->handle(),
                                materials_buf->handle(),
                                1,
                                &copy_cmd);
                async_commands->hold_buffer(upload_materials);
            }
        }
        else if (materials_changed) {
            pm_no_alpha = true;
            for (int i = 0, ie = scene.meshes[pm.mesh_id].num_geometries(); i < ie; ++i)
                pm_no_alpha &= (scene.materials[pm.material_offset(i)].flags & BASE_MATERIAL_NOALPHA) != 0;
        }

        async_commands->end_submit();

        auto& vkpm = parameterized_meshes[pm_idx];
        if (model_changed) {
            vkpm.mesh_id = (int32_t) pm.mesh_id;
            vkpm.lod_group_id = (int32_t)pm.lod_group;
            vkpm.model_revision = pm.model_revision;
        }
        if (materials_changed) {
            vkpm.per_triangle_material_buf = materials_buf;
            vkpm.no_alpha = pm_no_alpha;
            vkpm.material_revision = pm.model_material_revision();
        }

        update_sbt |= materials_changed; // includes | model_changed
        rebuild_sbt |= shaders_changed | assigned_mesh_changed; // includes | model_changed

        // will be handled by SBT rebuild
        vkpm.shader_revision = pm.model_shader_revision();
        vkpm.mesh_model_revision = meshes[pm.mesh_id]->model_revision;

        vkpm.render_mesh_base_offset = int_cast(unrolled_geometry_offset);
        vkpm.render_mesh_count = (int) meshes[pm.mesh_id]->geometries.size();
        unrolled_geometry_offset += vkpm.render_mesh_count;
    }

    render_meshes.resize(parameterized_meshes.size());
    for (size_t i = 0, ie = render_meshes.size(); i < ie; ++i)
        render_meshes[i] = collect_render_mesh_params(i, scene);
    // ensure stable parameterized mesh ID across LoD groups for e.g. proc. animation
    for (int pm_id = 0, ie = ilen(parameterized_meshes); pm_id < ie; ++pm_id)
        for (int lod_pm_id : this->lod_groups[pm_id].mesh_ids)
            for (auto& rm : render_meshes[lod_pm_id])
                rm.paramerterized_mesh_id = pm_id;
    ++render_meshes_generation;

    // link back to data of first render mesh to be able to share render buffer data
    for (int i = ilen(parameterized_meshes); i-- > 0; ) {
        auto& mesh = *meshes[parameterized_meshes[i].mesh_id];
        mesh.cpu_mesh_data_index = i;
        mesh.gpu_mesh_data_offset = parameterized_meshes[i].render_mesh_base_offset;
    }

    shader_names.resize(parameterized_meshes.size());
    for (size_t i = 0, ie = shader_names.size(); i < ie; ++i) {
        int rmc = parameterized_meshes[i].render_mesh_count;
        shader_names[i].resize(rmc);

        auto& custom_shader_names = scene.parameterized_meshes[i].shader_names;
        int num_custom_shader_names = std::min((int) custom_shader_names.size(), rmc);
        int j = 0;
        for (; j < num_custom_shader_names; ++j)
            shader_names[i][j] = custom_shader_names[j];
        for (; j < rmc; ++j)
            shader_names[i][j] = { };

        auto& mesh_shader_names = scene.meshes[parameterized_meshes[i].mesh_id].mesh_shader_names;
        int num_mesh_shader_names = std::min((int) mesh_shader_names.size(), rmc);
        for (j = 0; j < num_mesh_shader_names; ++j)
            (shader_names[i][j] += '+') += mesh_shader_names[j];
    }
}

void RenderVulkan::default_update_tlas(std::unique_ptr<vkrt::TopLevelBVH>& scene_bvh, bool rebuild_tlas
               , int lod_offset, uint32_t instance_mask) {
    vkrt::MemorySource static_memory_arena(device, base_arena_idx + StaticArenaOffset);
    vkrt::MemorySource pageable_static_memory_arena(device, base_arena_idx + StaticArenaOffset, 0.5f);
    vkrt::MemorySource scratch_memory_arena(device, vkrt::Device::ScratchArena, 0.0f);

    auto async_commands = device.async_command_stream();
    auto sync_commands = device.sync_command_stream();

    vkrt::Buffer instance_buf = vkrt::Buffer::device(pageable_static_memory_arena,
        instances.size() * sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    uint32_t instancedGeometryCount = 0;
    bool have_procedural_instances = false;
    {
        auto upload_instances = instance_buf->secondary_for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

        VkAccelerationStructureInstanceKHR *map =
            reinterpret_cast<VkAccelerationStructureInstanceKHR *>(upload_instances->map());

        for (int i = 0, ie = int_cast(instances.size()); i < ie; ++i) {
            const auto &inst = instances[i];
            VkAccelerationStructureInstanceKHR vkinst = { };
            int parameterized_mesh_id = inst.parameterized_mesh_id;
            auto& lodGroup = this->lod_groups[parameterized_mesh_id];
            if (!lodGroup.mesh_ids.empty())
                parameterized_mesh_id = lodGroup.mesh_ids[std::min(lod_offset, (int) lodGroup.mesh_ids.size() -1)];
#ifdef IMPLICIT_INSTANCE_PARAMS
            vkinst.instanceCustomIndex = parameterized_meshes[parameterized_mesh_id].render_mesh_base_offset;
#else
            vkinst.instanceCustomIndex = instancedGeometryCount;
#endif
            vkinst.instanceShaderBindingTableRecordOffset =
                parameterized_meshes[parameterized_mesh_id].render_mesh_base_offset;
            vkinst.flags = parameterized_meshes[parameterized_mesh_id].no_alpha ? VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR : 0;
            int mesh_id = parameterized_meshes[parameterized_mesh_id].mesh_id;
            vkinst.accelerationStructureReference = meshes[mesh_id]->device_address;
            vkinst.mask = instance_mask;

            // Note: 4x3 row major
            const glm::mat4 m = glm::transpose(inst.transform);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 4; ++c) {
                    vkinst.transform.matrix[r][c] = m[r][c];
                }
            }

            map[i] = vkinst;

            instancedGeometryCount += parameterized_meshes[parameterized_mesh_id].render_mesh_count;
        }

        upload_instances->unmap();

        async_commands->begin_record();

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = upload_instances->size();
        vkCmdCopyBuffer(async_commands->current_buffer,
                        upload_instances->handle(),
                        instance_buf->handle(),
                        1,
                        &copy_cmd);
        async_commands->hold_buffer(upload_instances);

        async_commands->end_submit();
    }

    async_commands->wait_complete();

    ProfilingScope profile_bvh("Build TLAS");
    // Build the top level BVH
    // Assumption for the multi-TLAS use case: TLAS builds are called from LoD0 and no other sync commands are issued in-between.
    // This would allow having multiple default_update_tlas(...lod_offset=i) calls tro use a single profiling marker
    scene_bvh = std::make_unique<vkrt::TopLevelBVH>(*device
        , instance_buf
        , static_cast<uint32_t>(instances.size())
        , VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
        | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
    );
    instance_aabb_buf = nullptr;
    if (vkrt::CmdTraceRaysKHR) {
        sync_commands->begin_record();
        {
            auto md = profiling_data.start_timing(sync_commands->current_buffer, ProfilingMarker::BuildTLAS, swap_index);
            scene_bvh->enqueue_build( sync_commands->current_buffer, static_memory_arena, scratch_memory_arena);
            profiling_data.end_timing(sync_commands->current_buffer, md, swap_index);
        }
        sync_commands->end_submit();

        sync_commands->begin_record();
        scene_bvh->enqueue_compaction(sync_commands->current_buffer, static_memory_arena);
        sync_commands->end_submit();

        scene_bvh->finalize();
    }

    // todo: make more selective?
    ++tlas_generation;
    ++tlas_content_generation;
}

void RenderVulkan::request_tlas_operation(BVHOperation op) {
    if (op == BVHOperation::Rebuild)
        pending_tlas_request = BVHOperation::Rebuild;
    else if (op == BVHOperation::Refit && pending_tlas_request != BVHOperation::Rebuild)
        pending_tlas_request = BVHOperation::Refit;
}

bool RenderVulkan::has_pending_tlas_operations() {
    // we can call this function to avoid redundant command stream / timing in each frame
    return pending_tlas_request != BVHOperation::None;
}

void RenderVulkan::execute_pending_tlas_operations(VkCommandBuffer command_buffer) {
    if (pending_tlas_request == BVHOperation::None)
        return;
    
    auto tlasMarker = (pending_tlas_request == BVHOperation::Refit)
        ? vkrt::ProfilingMarker::UpdateTLAS
        : vkrt::ProfilingMarker::BuildTLAS;
    
    auto tlas_pqd = profiling_data.start_timing(command_buffer, tlasMarker, swap_index);
    if (pending_tlas_request == BVHOperation::Rebuild) {
        vkrt::MemorySource staticMemoryArena(device, base_arena_idx + StaticArenaOffset);
        vkrt::MemorySource scratchMemoryArena(device, vkrt::Device::ScratchArena, 0.0f);
        scene_bvh->enqueue_build(command_buffer, staticMemoryArena, scratchMemoryArena);
    } else if (pending_tlas_request == BVHOperation::Refit){
        scene_bvh->enqueue_refit(command_buffer);
    }
    profiling_data.end_timing(command_buffer, tlas_pqd, swap_index);

    pending_tlas_request = BVHOperation::None;
}

void RenderVulkan::update_tlas(bool rebuild_tlas) {
    bool handled_by_extension = false;

    for (auto* ext : available_pipeline_extensions)
        if (ext->is_active_for(active_options))
            if (handled_by_extension |= ext->update_tlas(rebuild_tlas))
                break;

    if (!handled_by_extension)
        default_update_tlas(scene_bvh, rebuild_tlas, 0, 0xff);
}

void RenderVulkan::update_instances(const Scene &scene, bool rebuild_tlas) {
    vkrt::MemorySource static_memory_arena(device, base_arena_idx + StaticArenaOffset);
    vkrt::MemorySource pageable_static_memory_arena(device, base_arena_idx + StaticArenaOffset, 0.5f);
    vkrt::MemorySource scratch_memory_arena(device, vkrt::Device::ScratchArena);

    auto async_commands = device.async_command_stream();
    auto sync_commands = device.sync_command_stream();

    instances.resize(scene.instances.size());
    parameterized_instances.resize(parameterized_meshes.size());
    for (auto& pi : parameterized_instances)
        pi.clear();
    int instanced_geometry_count = 0;

    instance_aabb_buf = vkrt::Buffer::device(reuse(pageable_static_memory_arena, instance_aabb_buf),
        scene.instances.size() * sizeof(VkAabbPositionsKHR),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    {
        auto upload_instances = instance_aabb_buf->secondary_for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        VkAabbPositionsKHR *map = reinterpret_cast<VkAabbPositionsKHR*>(upload_instances->map());
        for (int i = 0, ie = int_cast(scene.instances.size()); i < ie; ++i) {
            const auto &inst = scene.instances[i];

            vkrt::Instance vkinst;
            vkinst.parameterized_mesh_id = inst.parameterized_mesh_id;

            const auto &animData = scene.animation_data.at(inst.animation_data_index);
            constexpr uint32_t frame = 0;
            vkinst.transform = animData.dequantize(inst.transform_index, frame);

            instances[i] = vkinst;

            // todo: incorporate lod groups
            glm::vec3 aabbMin(FLT_MAX, FLT_MAX, FLT_MAX);
            glm::vec3 aabbMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            for (auto& geo : scene.meshes[scene.parameterized_meshes[vkinst.parameterized_mesh_id].mesh_id].geometries) {
                aabbMin = min(geo.base, aabbMin);
                aabbMax = max(geo.base + geo.extent, aabbMax);
            }

            map[i].minX = aabbMin.x;
            map[i].minY = aabbMin.y;
            map[i].minZ = aabbMin.z;
            map[i].maxX = aabbMax.x;
            map[i].maxY = aabbMax.y;
            map[i].maxZ = aabbMax.z;


            parameterized_instances[vkinst.parameterized_mesh_id].push_back(i);
            instanced_geometry_count += parameterized_meshes[vkinst.parameterized_mesh_id].render_mesh_count;
        }

        upload_instances->unmap();

        async_commands->begin_record();

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = upload_instances->size();
        vkCmdCopyBuffer(async_commands->current_buffer,
                        upload_instances->handle(),
                        instance_aabb_buf->handle(),
                        1,
                        &copy_cmd);
        async_commands->hold_buffer(upload_instances);

        async_commands->end_submit();
    }

    update_tlas(rebuild_tlas);

    update_instance_params();

    parameterized_instance_buf = vkrt::Buffer::device(reuse(static_memory_arena, parameterized_instance_buf),
        scene.instances.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
#ifdef ENABLE_RASTER
        | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
#endif
        );
    {
        auto upload_buf = parameterized_instance_buf->secondary_for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

        uint32_t *map = (uint32_t*) upload_buf->map();
        for (auto const& pmi : parameterized_instances) {
            for (uint32_t offset : pmi)
                *map++ = offset;
        }
        upload_buf->unmap();

        async_commands->begin_record();

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = upload_buf->size();
        vkCmdCopyBuffer(async_commands->current_buffer
            , upload_buf->handle()
            , parameterized_instance_buf->handle(), 1, &copy_cmd);
        async_commands->hold_buffer(upload_buf);

        async_commands->end_submit();
    }
}

void RenderVulkan::update_instance_params() {
    if (instance_params_generation == render_meshes_generation)
        return;

    vkrt::MemorySource static_memory_arena(device, base_arena_idx + StaticArenaOffset);
    vkrt::MemorySource scratch_memory_arena(device, vkrt::Device::ScratchArena);

    auto sync_commands = device.sync_command_stream();

    int instanced_geometry_count = 0;
#ifdef IMPLICIT_INSTANCE_PARAMS
    for (auto& pm : parameterized_meshes)
        instanced_geometry_count = std::max(pm.render_mesh_base_offset + pm.render_mesh_count, instanced_geometry_count);
#else
    for (auto& inst : instances)
        instanced_geometry_count += parameterized_meshes[inst.parameterized_mesh_id].render_mesh_count;
#endif

    instance_param_buf = vkrt::Buffer::device(reuse(static_memory_arena, instance_param_buf),
#ifdef IMPLICIT_INSTANCE_PARAMS
        instanced_geometry_count * sizeof(RenderMeshParams),
#else
        instanced_geometry_count * sizeof(InstancedGeometry),
#endif
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    {
#ifdef IMPLICIT_INSTANCE_PARAMS
        std::vector<RenderMeshParams> geo_instances;
        geo_instances.reserve(instanced_geometry_count);
        for (int pm_idx = 0, pm_idx_end = ilen(parameterized_meshes); pm_idx < pm_idx_end; ++pm_idx) {
            const auto& geoms = this->render_meshes[pm_idx];
            int inst_geom_idx = ilen(geo_instances);

            assert(inst_geom_idx == parameterized_meshes[pm_idx].render_mesh_base_offset);
            assert(geoms.size() == parameterized_meshes[pm_idx].render_mesh_count);

            geo_instances.resize(inst_geom_idx + geoms.size());
            for (auto const& g : geoms)
                geo_instances[inst_geom_idx++] = g;
        }
#else
        std::vector<InstancedGeometry> geo_instances;
        geo_instances.reserve(instanced_geometry_count);
        for (const auto& inst : this->instances) {
            auto const& geoms = render_meshes[inst.parameterized_mesh_id];

            size_t inst_geom_idx = geo_instances.size();
            geo_instances.resize(geo_instances.size() + geoms.size());

            // TODO: remove this, and instead update the buffer on the GPU.
            const glm::mat4 m = inst.transform;
            glm::mat4 mI = inverse(m);

            for (auto const& g : geoms) {
                auto& i = geo_instances[inst_geom_idx++];
                i.instance_to_world = m;
                i.world_to_instance = mI;
                i.geometry = g;
            }
        }
#endif

        auto upload_params = instance_param_buf->secondary_for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

        void *map = upload_params->map();
        std::memcpy(map, geo_instances.data(), upload_params->size());
        upload_params->unmap();

        sync_commands->begin_record();

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = upload_params->size();
        vkCmdCopyBuffer(sync_commands->current_buffer
            , upload_params->handle()
            , instance_param_buf->handle(), 1, &copy_cmd);
        sync_commands->hold_buffer(upload_params);

        sync_commands->end_submit();
    }

    instance_params_generation = render_meshes_generation;
}

void RenderVulkan::set_scene(const Scene &scene)
{
    frame_id = 0;

    // currently any kind of reallocation may occur
    CHECK_VULKAN(vkDeviceWaitIdle(device->logical_device()));

    if (pipeline_store.prepared.empty())
        create_pipelines(nullptr, 0);

    bool new_scene = this->unique_scene_id != scene.unqiue_id;
    bool update_sbt = false;
    bool rebuild_sbt = false;
    bool rebuild_tlas = false;

    // note on order:
    // do the bulk of data upload before waiting on RT pipelines
    // to be built, to allow overlaying compilation time with
    // upload time

    if (new_scene) {
        meshes.clear();
        this->meshes_revision = ~0;
        parameterized_meshes.clear();
        this->parameterized_meshes_revision = ~0;

        this->blas_generation = 0;
        this->blas_content_generation = 0;
        this->tlas_generation = 0;
        this->tlas_content_generation = 0;
    }

    if (this->meshes_revision != scene.meshes_revision)
        update_geometry(scene, update_sbt, rebuild_tlas);
    if (this->parameterized_meshes_revision != scene.parameterized_meshes_revision)
        update_meshes(scene, update_sbt, rebuild_sbt);

    if (this->lights_revision != scene.lights_revision)
        update_lights(scene);

    this->meshes_revision = scene.meshes_revision;
    this->parameterized_meshes_revision = scene.parameterized_meshes_revision;
    this->lights_revision = scene.lights_revision;

    // todo: update cached PM and render mesh params as needed,
    // potentially write index of changes per PM?

    if (new_scene)
        this->instances_revision = ~0;

    if (rebuild_tlas || this->instances_revision != scene.instances_revision) {
        update_instances(scene, rebuild_tlas);
        this->instances_revision = scene.instances_revision;
    }

    if (new_scene) {
        textures.clear();
        this->textures_revision = ~0;
        standard_textures.clear();
        this->materials_revision = ~0;
    }

    if (this->textures_revision != scene.textures_revision)
        update_textures(scene);
    if (this->materials_revision != scene.materials_revision)
        update_materials(scene);

    this->textures_revision = scene.textures_revision;
    this->materials_revision = scene.materials_revision;

    if (rebuild_sbt) {
        ProfilingScope profile_pipeline("Finalize RT pipelines");

        prepare_raytracing_pipelines(false);
        for (int i = 0, ie = (int) pipeline_store.prepared.size(); i < ie; ++i) {
            if (pipeline_store.prepared[i].build.valid())
                pipeline_store.prepared[i].build.wait();
            if (auto* pipeline = pipeline_store.prepared[i].pipeline) {
                pipeline->build_shader_binding_table();
                pipeline->update_shader_binding_table();
            }
        }
        pipeline_store.prepared.clear();

        device->update_pipeline_cache();
    }

    device->flush_sync_and_async_device_copies();

    unique_scene_id = scene.unqiue_id;
}

void RenderVulkan::update_textures(const Scene &scene) {
    vkrt::MemorySource static_memory_arena(device, base_arena_idx + StaticArenaOffset);
    vkrt::MemorySource scratch_memory_arena(device, vkrt::Device::ScratchArena);

    auto async_commands = device.async_command_stream();

    bool resize_desc_table = false;
    bool update_desc_table = false;

    if (!sampler)
    {
        VkSamplerCreateInfo sampler_info = {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.minLod = 0;
        sampler_info.maxLod = 16.0f;
        sampler_info.anisotropyEnable = VK_TRUE;
        sampler_info.maxAnisotropy = 12;
        CHECK_VULKAN(
            vkCreateSampler(device->logical_device(), &sampler_info, nullptr, &sampler));
    }

    ProfilingScope profile_textures("Upload textures");

    resize_desc_table |= textures.size() != scene.textures.size();
    textures.resize(scene.textures.size(), nullptr);

    // Enqueue all the uploads
    update_desc_table = textures.size() != 0;  // todo: do we want selective texture update?
    create_vulkan_textures_from_images(async_commands, scene.textures, textures, static_memory_arena, scratch_memory_arena);
   
    if (resize_desc_table)
    {
        VkDescriptorPoolCreateInfo pool_create_info = {};
        pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_create_info.maxSets = 1;

        if (textures.size() > MAX_TEXTURE_COUNT)
            throw_error("too many textures");

        uint32_t textureDescriptorCount = std::max(uint32_t(textures.size()), uint32_t(1));
        std::vector<VkDescriptorPoolSize> pool_sizes = {
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureDescriptorCount}};

        if (texture_desc_pool) {
            vkDestroyDescriptorPool(device->logical_device(), texture_desc_pool, nullptr);
            texture_desc_pool = VK_NULL_HANDLE;
        }

        pool_create_info.poolSizeCount = pool_sizes.size();
        pool_create_info.pPoolSizes = pool_sizes.data();
        pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        CHECK_VULKAN(vkCreateDescriptorPool(
            device->logical_device(), &pool_create_info, nullptr, &texture_desc_pool));

        VkDescriptorSetAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = texture_desc_pool;

        uint32_t texture_set_size = (uint32_t) textures.size();
        VkDescriptorSetVariableDescriptorCountAllocateInfo texture_set_size_info = {};
        texture_set_size_info.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        texture_set_size_info.descriptorSetCount = 1;
        texture_set_size_info.pDescriptorCounts = &texture_set_size;

        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &textures_desc_layout;
        alloc_info.pNext = &texture_set_size_info;
        CHECK_VULKAN(
            vkAllocateDescriptorSets(device->logical_device(), &alloc_info, &textures_desc_set));
        alloc_info.pNext = nullptr;

        update_desc_table = true;
    }

    if (update_desc_table)
    {
        vkrt::DescriptorSetUpdater updater;
        if (!textures.empty())
            updater.write_combined_sampler_array(textures_desc_set, 0, textures, std::vector<VkSampler>{sampler});
        updater.update(*device);
    }

    // wait for last copy to end
    async_commands->wait_complete();

    profile_textures.end();
}

void RenderVulkan::update_materials(const Scene &scene) {
    vkrt::MemorySource static_memory_arena(device, base_arena_idx + StaticArenaOffset);
    vkrt::MemorySource scratch_memory_arena(device, vkrt::Device::ScratchArena);

    auto async_commands = device.async_command_stream();

    mat_params = vkrt::Buffer::device(reuse(static_memory_arena, mat_params),
        scene.materials.size() * sizeof(BaseMaterial),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    {
        auto upload_mat_params = mat_params->for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, scratch_memory_arena);

        void *map = upload_mat_params->map();
        std::memcpy(map, scene.materials.data(), upload_mat_params->size());
        upload_mat_params->unmap();

        async_commands->begin_record();

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = upload_mat_params->size();
        vkCmdCopyBuffer(
            async_commands->current_buffer, upload_mat_params->handle(), mat_params->handle(), 1, &copy_cmd);
        async_commands->hold_buffer(upload_mat_params);

        async_commands->end_submit();
    }

    bool resize_desc_table = (standard_textures_desc_set == VK_NULL_HANDLE);
    bool update_desc_table = true; // todo: any more detailed change tracking useful?
#ifdef UNROLL_STANDARD_TEXTURES
    size_t standard_texture_count = scene.materials.size() * STANDARD_TEXTURE_COUNT;
    resize_desc_table |= standard_texture_count != standard_textures.size();
    standard_textures.resize(standard_texture_count, null_texture);
    for (size_t i = 0, ie = scene.materials.size(); i < ie; ++i) {
        auto const& material = scene.materials[i];
        size_t tex_base_idx = i * STANDARD_TEXTURE_COUNT;

        if ((size_t) material.normal_map >= textures.size())
            throw_error("Material %d is missing a normal texture", (int) i);
        standard_textures[tex_base_idx + STANDARD_TEXTURE_NORMAL_SLOT] = textures[material.normal_map];

        uint32_t tex_mask;

        memcpy(&tex_mask, (char*) &material.base_color, sizeof(uint32_t));
        if (!IS_TEXTURED_PARAM(tex_mask)) {
            // allow un-textured emitter overrides
            if (!(material.emission_intensity > 0.0f))
                throw_error("Material %d is missing a base_color texture", (int) i);
        }
        else
            standard_textures[tex_base_idx + STANDARD_TEXTURE_BASECOLOR_SLOT] = textures[GET_TEXTURE_ID(tex_mask)];

        memcpy(&tex_mask, (char*) &material.roughness, sizeof(uint32_t));
        if (!IS_TEXTURED_PARAM(tex_mask))
            throw_error("Material %d is missing a roughness texture", (int) i);
        standard_textures[tex_base_idx + STANDARD_TEXTURE_SPECULAR_SLOT] = textures[GET_TEXTURE_ID(tex_mask)];
    }
#endif

    if (resize_desc_table)
    {
        VkDescriptorPoolCreateInfo pool_create_info = {};
        pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_create_info.maxSets = 1;

        uint32_t textureDescriptorCount = 0;
        uint32_t bufferTextureDescriptorCount = 0;
    #ifdef UNROLL_STANDARD_TEXTURES
        if (standard_textures.size() > MAX_TEXTURE_COUNT)
            throw_error("too many materials");
        textureDescriptorCount += (uint32_t) standard_textures.size();
    #endif
        std::vector<VkDescriptorPoolSize> pool_sizes;
        if (textureDescriptorCount)
            pool_sizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureDescriptorCount});
        if (bufferTextureDescriptorCount)
            pool_sizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, bufferTextureDescriptorCount});

        if (material_texture_desc_pool) {
            vkDestroyDescriptorPool(device->logical_device(), material_texture_desc_pool, nullptr);
            material_texture_desc_pool = VK_NULL_HANDLE;
        }

        pool_create_info.poolSizeCount = pool_sizes.size();
        pool_create_info.pPoolSizes = pool_sizes.data();
        pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        CHECK_VULKAN(vkCreateDescriptorPool(
            device->logical_device(), &pool_create_info, nullptr, &material_texture_desc_pool));

        VkDescriptorSetAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = material_texture_desc_pool;

        uint32_t texture_set_size = (uint32_t) standard_textures.size();
        VkDescriptorSetVariableDescriptorCountAllocateInfo texture_set_size_info = {};
        texture_set_size_info.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        texture_set_size_info.descriptorSetCount = 1;
        texture_set_size_info.pDescriptorCounts = &texture_set_size;

        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &standard_textures_desc_layout;
    #ifdef UNROLL_STANDARD_TEXTURES
        texture_set_size = standard_textures.size();
        alloc_info.pNext = &texture_set_size_info;
    #endif
        CHECK_VULKAN(
            vkAllocateDescriptorSets(device->logical_device(), &alloc_info, &standard_textures_desc_set));
        alloc_info.pNext = nullptr;

        update_desc_table = true;
    }

    if (update_desc_table)
    {
        std::vector<VkSampler> default_texture_samplers{sampler};

        vkrt::DescriptorSetUpdater updater;

        if (!standard_textures.empty())
            updater.write_combined_sampler_array(standard_textures_desc_set, 0, standard_textures, default_texture_samplers);

        updater.update(*device);
    }
}

bool RenderVulkan::render_ray_queries(int num_queries, const RenderParams &params, int variant_idx, CommandStream* cmd_stream_) {
    if (!cmd_stream_)
        cmd_stream_ = device.sync_command_stream();
    auto cmd_stream = static_cast<vkrt::CommandStream*>(cmd_stream_);

    cmd_stream->begin_record();
    record_frame(cmd_stream->current_buffer, variant_idx, num_queries);
    cmd_stream->end_submit();
    return true;
}

void RenderVulkan::normalize_options(RenderBackendOptions& rbo, int variant_idx) const {
    GpuProgram const* active_program = nullptr;
    if (variant_idx >= 0 && variant_idx < (int) GPU_RAYTRACER_NAMES.size())
        active_program = vulkan_raytracers[variant_idx];
    rbo = normalized_options(rbo, nullptr, RBO_STAGES_ALL, active_program);
}

bool RenderVulkan::configure_for(RenderBackendOptions const& rbo, int variant_idx,
    AvailableRenderBackendOptions* available_recovery_options) {
    // query available option set for error recovery, if requested
    if (available_recovery_options) {
        GpuProgram const* active_program = nullptr;
        if (variant_idx >= 0 && variant_idx < (int) GPU_RAYTRACER_NAMES.size())
            active_program = vulkan_raytracers[variant_idx];
        normalized_options(rbo, nullptr, RBO_STAGES_ALL, active_program, available_recovery_options);
    }
    // update renderer for new configuration
    if (!equal_options(rbo, active_options)) {
        CHECK_VULKAN(vkDeviceWaitIdle(device.logical_device()));
        bool update_tlas = false;
        active_options = rbo;
        if (update_tlas)
            this->update_tlas(true);
    }
    // pre-load/compile required GPU programs before new frame / profiling begins
    bool fallback_exists = false;
    try {
        if (variant_idx >= 0 && variant_idx < (int) GPU_RAYTRACER_NAMES.size())
            build_raytracing_pipeline(variant_idx, rbo, false, &fallback_exists);
        sample_processing_pipeline->hot_reload(sample_processing_pipeline, pipeline_store.hot_reload_generation);
    } catch (logged_exception const&) {
        return fallback_exists;
    }
#ifndef ENABLE_REALTIME_RESOLVE
    // built without temporal features, may be overridden by extensions
    // to set to DISCARD_HISTORY, if they do their own reprojection
    this->params.reprojection_mode = REPROJECTION_MODE_NONE;
#endif
    return true;
}

void RenderVulkan::begin_frame(CommandStream* cmd_stream_, const RenderConfiguration &config) {
    // ensure backend configuration for current set of options
    configure_for(this->RenderBackend::options, config.active_variant);

    auto cmd_stream = static_cast<vkrt::CommandStream*>(cmd_stream_);
    if (!cmd_stream)
        cmd_stream = device.sync_command_stream();

    if (config.active_swap_buffer_count > 0) {
        active_swap_buffer_count = std::min(config.active_swap_buffer_count, swap_buffer_count);
        swap_index %= active_swap_buffer_count;
    }
    else
        active_swap_buffer_count = swap_buffer_count;

    // next frame needs to use next set of swap buffers
    swap_index = (swap_index + 1) % active_swap_buffer_count;

    if (config.reset_accumulation) {
        if (!config.freeze_frame)
            frame_offset += frame_id;
        frame_id = 0;
    }

    if (frame_id == 0) {
        active_accum_buffer = 0;
        active_render_target = 0;
    }
    else {
        active_accum_buffer = !active_accum_buffer;
        active_render_target = !active_render_target;
    }

    VkDevice logical_device = device.logical_device();
    // wait for buffers to become available
    VkResult buffer_available_status = VK_EVENT_RESET;
    int event_tries = 0;
    do {
        if (buffer_available_status != VK_EVENT_RESET)
            CHECK_VULKAN(buffer_available_status);
        buffer_available_status = vkGetEventStatus(logical_device, render_done_events[swap_index]);
        if (++event_tries >= 10) {
            if (VkFence fence = render_done_fences[swap_index]) {
                VkResult result = vkWaitForFences(logical_device, 1, &fence, VK_TRUE, event_tries / 200 * 1000 * 1000); // ns
                if (result != VK_TIMEOUT) {
                    CHECK_VULKAN(result);
                    break; // render done, supersedes event
                }
            }
            else
                chrono_sleep(event_tries / 200); // ms
        }
    } while (buffer_available_status != VK_EVENT_SET);
    render_done_fences[swap_index] = VK_NULL_HANDLE;

    // Resolve all the timequeries
    profiling_data.evaluate_queries(swap_index);
    rendering_time_ms = profiling_data.results->duration_ms[(uint32_t)ProfilingMarker::Rendering];
    profiling_data.reset_queries(swap_index);

    RenderBackend::begin_frame(cmd_stream_, config); // update params

    // note: if needed:
    //if (!cmd_stream_)
    //    cmd_stream->begin_record();
    // ... run init commands ...

    const glsl::ViewParams* new_past_reference_frame = nullptr;
    if (frame_id == 0 || this->params.reprojection_mode != REPROJECTION_MODE_NONE)
        new_past_reference_frame = view_params();
    else
        new_past_reference_frame = ref_view_params();

    update_view_parameters(
          config.camera.pos, config.camera.dir, config.camera.up, config.camera.fovy
        , true
        , new_past_reference_frame);

    // For the following frame
    *ref_view_params() = *view_params();

    //if (!cmd_stream_)
    //    cmd_stream->end_submit();
}

void RenderVulkan::lazy_update_shader_descriptor_table(RenderPipelineVulkan* pipeline, int swap_index
    , CustomPipelineExtensionVulkan* optional_managing_extension) {
    if (pipeline->desc_frames[swap_index] == frame_offset + frame_id)
        return;

    desc_set_updater.reset();
    pipeline->update_shader_descriptor_table(desc_set_updater, swap_index, optional_managing_extension);
    desc_set_updater.update(*device);
    desc_set_updater.reset();

    pipeline->desc_frames[swap_index] = frame_offset + frame_id;
}

void RenderVulkan::end_frame(CommandStream* cmd_stream_, int variant_index) {
    auto cmd_stream = static_cast<vkrt::CommandStream*>(cmd_stream_);
    if (!cmd_stream)
        cmd_stream = device.sync_command_stream();

    // note: if needed:
    if (!cmd_stream_)
        cmd_stream->begin_record();

    // sample processing
    {
        VkCommandBuffer render_cmd_buf = cmd_stream->current_buffer;

        auto dst_stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        // todo: should these barriers go somewhere else?
        vkrt::MemoryBarriers<1, 1 + 2 * (1 + AOVBufferCount)> mem_barriers;

        auto& current_accum_buffer = accumulate_atomically ? atomic_accum_buffers[active_accum_buffer] : accum_buffers[active_accum_buffer];
        mem_barriers.add(dst_stages, current_accum_buffer->transition_color(VK_IMAGE_LAYOUT_GENERAL
            , VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
        mem_barriers.add(dst_stages, render_targets[active_render_target]->transition_color(VK_IMAGE_LAYOUT_GENERAL
            , VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
#ifdef ENABLE_AOV_BUFFERS
        for (int i = 0; i < AOVBufferCount; ++i) {
            mem_barriers.add(dst_stages, aov_buffer((AOVBufferIndex) i)->transition_color(VK_IMAGE_LAYOUT_GENERAL
                , VK_ACCESS_SHADER_READ_BIT
#ifdef REPROJECTION_ACCUM_GBUFFER
                | VK_ACCESS_SHADER_WRITE_BIT
#endif
                ));
        }
#endif
#ifdef ENABLE_REALTIME_RESOLVE
        // history buffers
        mem_barriers.add(dst_stages, accum_buffers[!active_accum_buffer]->transition_color(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            , VK_ACCESS_SHADER_READ_BIT));
        for (int i = 0; i < AOVBufferCount; ++i) {
            auto& history_buf = aov_buffers[i + (!active_accum_buffer) * AOVBufferCount];
            if (history_buf == aov_buffer((AOVBufferIndex) i))
                continue;
            mem_barriers.add(dst_stages, history_buf->transition_color(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                , VK_ACCESS_SHADER_READ_BIT));
        }
#endif

        mem_barriers.set(render_cmd_buf, PROCESSING_PIPELINE_STAGES | TRACE_PIPELINE_STAGES);

        glsl::PushConstantParams push_constants = { };
        //push_constants.local_params = cached_gpu_params->locals;
        push_constants.accumulation_frame_offset = frame_id;
        push_constants.accumulation_batch_size = this->params.batch_spp;
        if (accumulate_atomically)
            push_constants.accumulation_flags |= ACCUMULATION_FLAGS_ATOMIC;
#ifdef ENABLE_AOV_BUFFERS
        if (true)
            push_constants.accumulation_flags |= ACCUMULATION_FLAGS_AOVS;
#endif

        // do we need variants / options? potentially connect
        auto md = profiling_data.start_timing(render_cmd_buf, ProfilingMarker::Processing, swap_index);
        {
            lazy_update_shader_descriptor_table(sample_processing_pipeline.get(), swap_index);
            sample_processing_pipeline->bind_pipeline(render_cmd_buf, &push_constants, sizeof(push_constants), swap_index);
            glm::ivec2 dispatch_dim = accum_buffers[active_accum_buffer]->dims();
            sample_processing_pipeline->dispatch_rays(render_cmd_buf, dispatch_dim.x, dispatch_dim.y, 1);
        }
        profiling_data.end_timing(render_cmd_buf, md, swap_index);

        // for going back to post processing in linear HDR space after accumulation
        current_color_buffer = current_accum_buffer;

        // allow post processing in half-precision linear space, without interfering with accumulated results
        // todo: a better mechanism would only do this on demand if next PP cannot do ping pong
#if defined(ENABLE_POST_PROCESSING) || defined(ENABLE_ODIN) // todo: make ENABLE_DENOISING
        current_color_buffer = half_post_processing_buffers[active_accum_buffer];
#ifndef DENOISE_BUFFER_BIND_POINT // when DENOISE_BUFFER_BIND_POINT is defined, we write this during sample processing already
        {
            {
                vkrt::MemoryBarriers<1, 3> mem_barriers;
#ifdef ENABLE_REALTIME_RESOLVE
                // barrier for aliased history buffer
                auto history_end_of_life_barrier = accum_buffers[!active_accum_buffer]->transition_color(VK_IMAGE_LAYOUT_UNDEFINED);
                history_end_of_life_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                mem_barriers.add(VK_PIPELINE_STAGE_TRANSFER_BIT, history_end_of_life_barrier);
#endif
                mem_barriers.add(VK_PIPELINE_STAGE_TRANSFER_BIT, current_accum_buffer->transition_color(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                    , VK_ACCESS_TRANSFER_READ_BIT));
                // discard previous contents of aliased color buffer
                current_color_buffer->ref_data->img_layout = VK_IMAGE_LAYOUT_UNDEFINED;
                mem_barriers.add(VK_PIPELINE_STAGE_TRANSFER_BIT, current_color_buffer->transition_color(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                    , VK_ACCESS_TRANSFER_WRITE_BIT));

                mem_barriers.set(render_cmd_buf, PROCESSING_PIPELINE_STAGES);
            }

            VkImageBlit blit = {};
            // crop to smallest common area
            blit.srcSubresource = current_accum_buffer->color_subresource();
            blit.srcOffsets[1].x = current_accum_buffer->dims().x;
            blit.srcOffsets[1].y = current_accum_buffer->dims().y;
            blit.srcOffsets[1].z = 1;
            blit.dstSubresource = current_color_buffer->color_subresource();
            blit.dstOffsets[1].x = current_color_buffer->dims().x;
            blit.dstOffsets[1].y = current_color_buffer->dims().y;
            blit.dstOffsets[1].z = 1;

            vkCmdBlitImage(render_cmd_buf,
                        current_accum_buffer->image_handle(),
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        current_color_buffer->image_handle(),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &blit,
                        VK_FILTER_NEAREST);

            {
                vkrt::MemoryBarriers<1, 1> mem_barriers;
                mem_barriers.add(PROCESSING_PIPELINE_STAGES, current_color_buffer->transition_color(VK_IMAGE_LAYOUT_GENERAL));
                mem_barriers.set(render_cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT);
            }
        }
#endif
#endif
    }

    // note: we currently omit event-based synchronization in the synchronized stream
    if (cmd_stream_) {
        CHECK_VULKAN(vkResetEvent(device->logical_device(), render_done_events[swap_index]));
        vkCmdSetEvent(cmd_stream->current_buffer, render_done_events[swap_index], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        render_done_fences[swap_index] = cmd_stream->current_fence;
    }

    if (!cmd_stream_)
        cmd_stream->end_submit();

    accumulated_spp = unsigned(frame_id + this->params.batch_spp);
    if (!freeze_frame)
        frame_id += this->params.batch_spp;
}

void RenderVulkan::draw_frame(CommandStream* cmd_stream_, int variant_idx) {
    auto cmd_stream = static_cast<vkrt::CommandStream*>(cmd_stream_);
    if (!cmd_stream)
        cmd_stream = device.sync_command_stream();

    if (!cmd_stream_)
        cmd_stream->begin_record();

    execute_pending_tlas_operations(cmd_stream->current_buffer);

    auto md = profiling_data.start_timing(cmd_stream->current_buffer, ProfilingMarker::Rendering, swap_index);
    {
        record_frame(cmd_stream->current_buffer, variant_idx);
    }
    profiling_data.end_timing(cmd_stream->current_buffer, md, swap_index);

    // for raw sample processing before accumulation
    current_color_buffer = accum_buffers[active_accum_buffer];

    if (!cmd_stream_)
        cmd_stream->end_submit();
}

RenderStats RenderVulkan::render(const RenderConfiguration &config) {
    return this->render(nullptr, config);
}

RenderStats RenderVulkan::render(CommandStream* cmd_stream, const RenderConfiguration &config) {
    begin_frame(cmd_stream, config);

    draw_frame(cmd_stream, config.active_variant);

    end_frame(cmd_stream, config.active_variant);

/*
#ifdef REPORT_RAY_STATS
    img_copy.bufferOffset = 0;
    img_copy.bufferRowLength = 0;
    img_copy.bufferImageHeight = 0;
    img_copy.imageSubresource = copy_subresource;
    img_copy.imageOffset.x = 0;
    img_copy.imageOffset.y = 0;
    img_copy.imageOffset.z = 0;
    img_copy.imageExtent.width = render_target->dims().x;
    img_copy.imageExtent.height = render_target->dims().y;
    img_copy.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(readback_cmd_bufs[i],
                           ray_stats->image_handle(),
                           VK_IMAGE_LAYOUT_GENERAL,
                           ray_stats_readback_buf->handle(),
                           1,
                           &img_copy);
#endif

#ifdef REPORT_RAY_STATS
    std::memcpy(ray_counts.data(),
                ray_stats_readback_buf->map(),
                ray_counts.size() * sizeof(uint16_t));
    ray_stats_readback_buf->unmap();

    const uint64_t total_rays =
        std::accumulate(ray_counts.begin(),
                        ray_counts.end(),
                        uint64_t(0),
                        [](const uint64_t &total, const uint16_t &c) { return total + c; });
    stats.rays_per_second = total_rays / (stats.render_time * 1.0e-3);
#endif
*/
    return stats();
}

RenderStats RenderVulkan::stats() {
    RenderStats stats = {};
    stats.has_valid_frame_stats = rendering_time_ms != 0.0;
    if (stats.has_valid_frame_stats) {
        stats.render_time = rendering_time_ms;
        stats.rays_per_second = -1; // todo
        stats.frame_stats_delay = short(swap_buffer_count);
    }
    stats.spp = accumulated_spp;
    auto& mem_stats = device->memory_statistics();
    stats.total_device_bytes_allocated = mem_stats.total_bytes_allocated;
    stats.max_device_bytes_allocated = mem_stats.max_device_bytes_allocated;
    stats.device_bytes_currently_allocated = mem_stats.device_bytes_currently_allocated;
    return stats;
}

void RenderVulkan::flush_pipeline() {
    CHECK_VULKAN(vkDeviceWaitIdle(device.logical_device()));
    profiling_data.reset_all_queries();
}

glm::uvec3 RenderVulkan::get_framebuffer_size() const
{
    const auto fb_dims = render_targets[0].dims();
    return glm::uvec3{ fb_dims.x, fb_dims.y, 4 };
}

template <class T>
size_t RenderVulkan::readback_framebuffer_generic(size_t bufferSize, T *buffer,
    vkrt::Texture2D *texture)
{
    auto fb_dims = texture->dims();
    const size_t size = fb_dims.x * static_cast<size_t>(fb_dims.y) * 4;
    if (bufferSize < size)
        return 0;

    auto sync_commands = device.sync_command_stream();
    sync_commands->begin_record();
    record_readback(sync_commands->current_buffer, texture);
    sync_commands->end_submit();

    void* readback_data = img_readback_buf->map();
    img_readback_buf->invalidate_all();
    std::memcpy(buffer, readback_data, sizeof(T) * size);
    img_readback_buf->unmap();
    return size;
}

size_t RenderVulkan::readback_framebuffer(size_t bufferSize, unsigned char *buffer,
        bool force_refresh)
{
    return readback_framebuffer_generic(bufferSize, buffer, &render_targets[active_render_target]);
}

size_t RenderVulkan::readback_framebuffer(size_t bufferSize, float *buffer,
        bool force_refresh)
{
    return readback_framebuffer_generic(bufferSize, buffer, &accum_buffers[active_accum_buffer]);
}


size_t RenderVulkan::readback_aov(AOVBufferIndex aovIndex, size_t bufferSize,
        uint16_t *buffer, bool force_refesh)
{
    return readback_framebuffer_generic(bufferSize, buffer, &aov_buffer(aovIndex));
}

void RenderVulkan::register_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const {
    auto& set_layout = collector.set;

    if (options.enable_raytracing) {
        set_layout
            .add_binding(SCENE_BIND_POINT,
                         1,
                         VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                         RECURSE_AND_SINK_SHADER_STAGES)
            ;
    }

    set_layout
        .add_binding(
            VIEW_PARAMS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL)
        .add_binding(
            SCENE_PARAMS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL)
        .add_binding(
            MATERIALS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL)
        .add_binding(
            INSTANCES_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL)
        ;

    if (options.enable_rayqueries) {
        set_layout
            .add_binding(
                RAYQUERIES_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT)
            .add_binding(
                RAYRESULTS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RECURSE_AND_SINK_SHADER_STAGES | PROCESSING_SHADER_STAGES)
            ;
    }
    if (true) { // todo: debugging / stats
#ifdef REPORT_RAY_STATS
        set_layout
            .add_binding(
                RAYSTATS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RECURSE_AND_SINK_SHADER_STAGEST | PROCESSING_SHADER_STAGES)
            ;
#endif
    }

    if ((uint32_t) options.raster_target & (uint32_t) vkrt::RenderPipelineTarget::Accumulation) {
        collector.framebuffer_formats[0] = ACCUMULATION_BUFFER_FORMAT;
    } else if (options.access_targets & vkrt::RenderPipelineUAVTarget::Accumulation) {
        set_layout
            .add_binding(
                FRAMEBUFFER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RECURSE_AND_SINK_SHADER_STAGES | PROCESSING_SHADER_STAGES)
            .add_binding(
                ACCUMBUFFER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RECURSE_AND_SINK_SHADER_STAGES | PROCESSING_SHADER_STAGES)
#ifdef DENOISE_BUFFER_BIND_POINT
            .add_binding(
                DENOISE_BUFFER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RECURSE_AND_SINK_SHADER_STAGES | PROCESSING_SHADER_STAGES)
#endif
#ifdef ATOMIC_ACCUMULATE
            .add_binding(
                ATOMIC_ACCUMBUFFER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RECURSE_AND_SINK_SHADER_STAGES | PROCESSING_SHADER_STAGES)
#endif
            .add_binding(
                HISTORY_BUFFER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PROCESSING_SHADER_STAGES)
            .add_binding(
                HISTORY_AOV_BUFFER_BIND_POINT + 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PROCESSING_SHADER_STAGES)
            .add_binding(
                HISTORY_AOV_BUFFER_BIND_POINT + 1, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PROCESSING_SHADER_STAGES)
            ;
    }

    if (options.raster_depth || options.depth_test) {
        collector.framebuffer_depth_format = DEPTH_STENCIL_BUFFER_FORMAT;
    } else if (options.access_targets & vkrt::RenderPipelineUAVTarget::DepthStencil) {
        // todo: probably want different bindpoint
        set_layout
            .add_binding(
                FRAMEBUFFER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RECURSE_AND_SINK_SHADER_STAGES | PROCESSING_SHADER_STAGES)
            ;
    }

    if ((uint32_t) options.raster_target & (uint32_t) vkrt::RenderPipelineTarget::AOV) {
        int binding_offset = (options.raster_target == vkrt::RenderPipelineTarget::AOV) ? 0 : 1;
        collector.framebuffer_formats[binding_offset++] = AOV_BUFFER_FORMAT;
        collector.framebuffer_formats[binding_offset++] = AOV_BUFFER_FORMAT;
        collector.framebuffer_formats[binding_offset++] = AOV_BUFFER_FORMAT;
    } else if (options.access_targets & vkrt::RenderPipelineUAVTarget::AOV) {
#ifdef ENABLE_AOV_BUFFERS
        set_layout
            .add_binding(
                AOV_ALBEDO_ROUGHNESS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RECURSE_AND_SINK_SHADER_STAGES | PROCESSING_SHADER_STAGES)
            .add_binding(
                AOV_NORMAL_DEPTH_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RECURSE_AND_SINK_SHADER_STAGES | PROCESSING_SHADER_STAGES)
            .add_binding(
                AOV_MOTION_JITTER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RECURSE_AND_SINK_SHADER_STAGES | PROCESSING_SHADER_STAGES)
            ;
#endif
    }

    for (auto* ext : available_pipeline_extensions)
        if (ext->is_active_for(options))
            ext->register_descriptors(collector, options);
}

void RenderVulkan::update_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) {
    auto& updater = collector.set;

    if (options.enable_raytracing) {
        updater
            .write_acceleration_structures(desc_set, SCENE_BIND_POINT, &scene_bvh->bvh, 1)
        ;
    }

    updater
        .write_ubo(desc_set, VIEW_PARAMS_BIND_POINT, local_param_buf)
        .write_ubo(desc_set, SCENE_PARAMS_BIND_POINT, global_param_buf)
        .write_ssbo(desc_set, MATERIALS_BIND_POINT, mat_params)
        .write_ssbo(desc_set, INSTANCES_BIND_POINT, instance_param_buf)
    ;

    if (options.enable_rayqueries) {
        // avoid null descriptor writes for validation layer
        if (ray_query_buffer) {
            updater
                .write_ssbo(desc_set, RAYQUERIES_BIND_POINT, ray_query_buffer)
                .write_ssbo(desc_set, RAYRESULTS_BIND_POINT, ray_result_buffer)
            ;
        }
    }
    if (true) { // todo: debugging / stats
#ifdef REPORT_RAY_STATS
        updater
            .write_storage_image(desc_set, RAYSTATS_BIND_POINT, ray_stats);
#endif
    }

    if ((uint32_t) options.raster_target & (uint32_t) vkrt::RenderPipelineTarget::Accumulation) {
        collector.framebuffer[0] = accum_buffers[active_accum_buffer];
    } else if (options.access_targets & vkrt::RenderPipelineUAVTarget::Accumulation) {
        updater
            .write_storage_image(desc_set, FRAMEBUFFER_BIND_POINT, render_targets[active_render_target])
            .write_storage_image(desc_set, ACCUMBUFFER_BIND_POINT, accum_buffers[active_accum_buffer])
#ifdef DENOISE_BUFFER_BIND_POINT
            .write_storage_image(desc_set, DENOISE_BUFFER_BIND_POINT, half_post_processing_buffers[active_accum_buffer])
#endif
#ifdef ATOMIC_ACCUMULATE
            .write_storage_image(desc_set, ATOMIC_ACCUMBUFFER_BIND_POINT, atomic_accum_buffers[active_accum_buffer])
#endif
            .write_combined_sampler(desc_set, HISTORY_BUFFER_BIND_POINT, accum_buffers[!active_accum_buffer], screen_sampler)
            .write_combined_sampler(desc_set, HISTORY_AOV_BUFFER_BIND_POINT + 0, aov_buffers[(!active_accum_buffer) * AOVBufferCount + AOVNormalDepthIndex], screen_sampler)
            .write_combined_sampler(desc_set, HISTORY_AOV_BUFFER_BIND_POINT + 1, aov_buffers[(!active_accum_buffer) * AOVBufferCount + AOVAlbedoRoughnessIndex], screen_sampler)
        ;
    }

    if (options.raster_depth || options.depth_test) {
        collector.framebuffer_depth = depth_buffer;
    } else if (options.access_targets & vkrt::RenderPipelineUAVTarget::DepthStencil) {
        // todo: probably want different bindpoint
        updater
            .write_storage_image(desc_set, FRAMEBUFFER_BIND_POINT, depth_buffer)
            ;
    }

#ifdef ENABLE_AOV_BUFFERS
    if ((uint32_t) options.raster_target & (uint32_t) vkrt::RenderPipelineTarget::AOV) {
        int binding_offset = (options.raster_target == vkrt::RenderPipelineTarget::AOV) ? 0 : 1;
        collector.framebuffer[binding_offset++] = aov_buffer(AOVAlbedoRoughnessIndex);
        collector.framebuffer[binding_offset++] = aov_buffer(AOVNormalDepthIndex);
        collector.framebuffer[binding_offset++] = aov_buffer(AOVMotionJitterIndex);
    } else if (options.access_targets & vkrt::RenderPipelineUAVTarget::AOV) {
        // omit null writes to pass validation layer
        if (aov_buffers[0]) {
            updater
                .write_storage_image(desc_set, AOV_ALBEDO_ROUGHNESS_BIND_POINT, aov_buffer(AOVAlbedoRoughnessIndex))
                .write_storage_image(desc_set, AOV_NORMAL_DEPTH_BIND_POINT, aov_buffer(AOVNormalDepthIndex))
                .write_storage_image(desc_set, AOV_MOTION_JITTER_BIND_POINT, aov_buffer(AOVMotionJitterIndex))
            ;
        }
    }
#endif

    for (auto* ext : available_pipeline_extensions)
        if (ext->is_active_for(options))
            ext->update_shader_descriptor_table(collector, options, desc_set);
}

int RenderVulkan::register_descriptor_sets(VkDescriptorSetLayout sets[], uint32_t& push_constants_size, vkrt::RenderPipelineOptions const& options) const {
    if (!push_constants_size || options.access_targets || (int) options.raster_target)
        push_constants_size = sizeof(glsl::PushConstantParams);

#ifdef UNROLL_STANDARD_TEXTURES
    sets[STANDARD_TEXTURE_BIND_SET] = standard_textures_desc_layout;
#endif
    sets[TEXTURE_BIND_SET] = textures_desc_layout;

    for (auto* ext : available_pipeline_extensions)
        if (ext->is_active_for(options))
            ext->register_descriptor_sets(sets, options);

    int desc_set_count = 0;
    // bound max range of non-null descriptors
    for (int i = 0; i < MAX_DESC_SETS; ++i)
        if (sets[i] != VK_NULL_HANDLE)
            desc_set_count = i + 1;
    // no null handles allowed within range
    for (int i = 0; i < desc_set_count; ++i)
        if (sets[i] == VK_NULL_HANDLE)
            sets[i] = null_desc_layout;
    return desc_set_count;
}

int RenderVulkan::collect_descriptor_sets(VkDescriptorSet descriptor_sets[], vkrt::RenderPipelineOptions const& options) {
#ifdef UNROLL_STANDARD_TEXTURES
    descriptor_sets[STANDARD_TEXTURE_BIND_SET] = standard_textures_desc_set;
#endif
    descriptor_sets[TEXTURE_BIND_SET] = textures_desc_set;

    for (auto* ext : available_pipeline_extensions)
        if (ext->is_active_for(options))
            ext->collect_descriptor_sets(descriptor_sets, options);

    int desc_set_count = 0;
    // bound max range of non-null descriptors
    for (int i = 0; i < MAX_DESC_SETS; ++i)
        if (descriptor_sets[i] != VK_NULL_HANDLE)
            desc_set_count = i + 1;
    return desc_set_count;
}

glsl::LocalParams* RenderVulkan::local_params(bool needs_update) {
    return &cached_gpu_params->locals;
}

glsl::GlobalParams* RenderVulkan::global_params(bool needs_update) {
    return &cached_gpu_params->globals;
}

glsl::ViewParams* RenderVulkan::view_params(bool needs_update) {
    return &cached_gpu_params->locals.view_params;
}

glsl::ViewParams *RenderVulkan::ref_view_params(bool needs_update)
{
    return &cached_gpu_params->locals.ref_view_params;
}

RenderParams* RenderVulkan::render_params(bool needs_update) {
    return &cached_gpu_params->globals.render_params;
}

std::vector<LightData> &RenderVulkan::light_data()
{
    return lightData;
}

void RenderVulkan::upload_light_data()
{
    vkrt::MemorySource static_memory_arena(device, base_arena_idx + StaticArenaOffset);
    vkrt::MemorySource scratch_memory_arena(device, vkrt::Device::ScratchArena);
    auto async_commands = device.async_command_stream();

    // Make sure the buffer is the right size
    uint32_t lightDataSize = std::max<size_t>(lightData.size(), 1ull) * sizeof(LightData);
    if (light_data_buf == nullptr || light_data_buf.size() < lightDataSize)
        light_data_buf = vkrt::Buffer::device(reuse(static_memory_arena, light_data_buf), lightDataSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, swap_buffer_count);

    // Upload to a scratch buffer then, copy "async"
    if (lightData.size() > 0)
    {
        // Create a temporary buffer and copy to it
        auto upload_light_params = light_data_buf->for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, scratch_memory_arena);
        void *map = upload_light_params->map();
        std::memcpy(map, lightData.data(), lightDataSize);
        upload_light_params->unmap();

        // "Async" copy
        async_commands->begin_record();
        VkBufferCopy copy_cmd = {};
        copy_cmd.size = upload_light_params->size();
        vkCmdCopyBuffer(async_commands->current_buffer, upload_light_params->handle(), light_data_buf->handle(), 1, &copy_cmd);
        async_commands->hold_buffer(upload_light_params);
        async_commands->end_submit();
    }
}

void RenderVulkan::prepare_raytracing_pipelines(bool defer_build)
{
    // only run this function once
    if (textures_desc_layout != VK_NULL_HANDLE)
        return;

    if (null_desc_layout == VK_NULL_HANDLE)
        null_desc_layout = vkrt::DescriptorSetLayoutBuilder().build(*device);

    textures_desc_layout =
        vkrt::DescriptorSetLayoutBuilder()
            .add_binding(0,
                         defer_build ? MAX_TEXTURE_COUNT : std::max(textures.size(), size_t(1)),
                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                         VK_SHADER_STAGE_ALL,
                         VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
                         | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
            .build(*device);
    standard_textures_desc_layout =
        vkrt::DescriptorSetLayoutBuilder()
#ifdef UNROLL_STANDARD_TEXTURES
            .add_binding(0,
                         defer_build ? MAX_TEXTURE_COUNT : std::max(standard_textures.size(), size_t(1)),
                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                         VK_SHADER_STAGE_ALL,
                         VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
                         | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
#endif
            .build(*device);

    // Load the shader modules for our pipelines and build the pipelines
    int num_shader_variants = 0;
    while (vulkan_raytracers[num_shader_variants])
        ++num_shader_variants;
    auto& pipeline_builds = pipeline_store.prepared;
    pipeline_builds.resize(num_shader_variants);
    auto& pipelines_supported = pipeline_store.support_flags;
    pipelines_supported.resize(num_shader_variants, (char) true);
    for (int variant_index = 0; variant_index < num_shader_variants; ++variant_index) {
        auto* gpu_program = vulkan_raytracers[variant_index];
        char const* variant_name = GPU_RAYTRACER_NAMES[variant_index];

        if (gpu_program->type == GPU_PROGRAM_TYPE_RASTERIZATION) {
#ifndef ENABLE_RASTER
            pipelines_supported[variant_index] = (char) false;
            println(CLL::WARNING, "Built without raster support, skipping raster pipeline %s", variant_name);
            continue;
#endif
        }
        else if (!vkrt::CmdTraceRaysKHR) {
            pipelines_supported[variant_index] = (char) false;
            println(CLL::WARNING, "Skipping potentially unsupported RT/RQ pipeline %s", variant_name);
            continue;
        }

        // heuristic: only auto-load pre-built GPU programs
        // todo: query for a different set of default options?
        RenderBackendOptions default_options = { };
        if (!gpu_program_binary_changed(gpu_program, default_options))
            pipeline_builds[variant_index].pipeline = build_raytracing_pipeline(variant_index, default_options, defer_build);
    }

    // processing shader
    {
        vkrt::RenderPipelineOptions options;
        options.access_targets = (uint16_t) vkrt::RenderPipelineUAVTarget::Accumulation
            | (uint16_t) vkrt::RenderPipelineUAVTarget::AOV;
        sample_processing_pipeline.reset( new ComputeRenderPipelineVulkan(this, &vulkan_program_PROCESS_SAMPLES, options) );
    }
}

void RenderVulkan::hot_reload() {
    ++pipeline_store.hot_reload_generation;
}

RenderPipelineVulkan* RenderVulkan::build_raytracing_pipeline(int variant_index, const RenderBackendOptions& for_options
    , bool defer_initialization, bool* set_if_fallback_exists) {
    auto* gpu_program = vulkan_raytracers[variant_index];
    bool is_integrator = (variant_index < GPU_INTEGRATOR_COUNT);
    auto& pipelines = pipeline_store.pipelines;

    int additional_stage_flags = (is_integrator ? RBO_STAGES_INTEGRATOR : 0) | gpu_program->feature_flags;
    if (gpu_program->type == GPU_PROGRAM_TYPE_COMPUTE)
        additional_stage_flags |= RBO_STAGES_RAYTRACED;
    auto options = normalized_options(for_options, nullptr, additional_stage_flags, gpu_program);
    RenderPipelineVulkan* current_pipeline = pipelines.find(gpu_program, options);

    bool needs_rebuild = false;
    // todo: make this into nicer wrappers, combine with some error fallbacks where possible
    if (current_pipeline && current_pipeline->hot_reload_generation != pipeline_store.hot_reload_generation) {
        if (gpu_program_binary_changed(gpu_program, options))
            needs_rebuild = true;
        // note: on error, we will not attempt reload for this generation (allows recovery using old pipeline)
        current_pipeline->hot_reload_generation = pipeline_store.hot_reload_generation;
    }
    if (set_if_fallback_exists)
        *set_if_fallback_exists = (current_pipeline != nullptr);

    if (!current_pipeline || needs_rebuild) {
        char const* variant_name = GPU_RAYTRACER_NAMES[variant_index];
        bool defer_build = defer_initialization;
    // Todo: Some issue in the validation layers prevents us from doing the work asynchronously (version 1.3.211)
    // Make sure to remove this when a new version comes out
#if defined(_DEBUG)
        defer_build = false;
#endif
        std::unique_ptr<RenderPipelineVulkan> new_pipeline;

        if (gpu_program->type == GPU_PROGRAM_TYPE_RASTERIZATION) {
#ifndef ENABLE_RASTER
            throw_error("Built without raster support, refusing to build raster pipeline %s", variant_name);
#else
            println(CLL::VERBOSE, "Building raster pipeline %s", variant_name);

            vkrt::RenderPipelineOptions pipeline_options;
            (RenderBackendOptions&) pipeline_options = options;
            pipeline_options.raster_target = vkrt::RenderPipelineTarget::AccumulationAndAOV;
            pipeline_options.raster_depth = true;
            new_pipeline.reset( new RasterScenePipelineVulkan(
                this, gpu_program, pipeline_options, defer_build
            ) );
#endif
        }
        else {
            if (!vkrt::CmdTraceRaysKHR)
                throw_error("Refusing to build potentially unsupported RT/RQ pipeline %s", variant_name);

            vkrt::RenderPipelineOptions pipeline_options;
            (RenderBackendOptions&) pipeline_options = options;
            pipeline_options.enable_raytracing = true;
            // todo: this needs to be adjusted to only be in research variants?
            pipeline_options.enable_rayqueries = true;
            // todo: this should ultimately not be necessary anymore
            get_defined_backend_options(pipeline_options, gpu_program->modules[0]->units[0]->defines);
            pipeline_options.access_targets = (uint16_t) vkrt::RenderPipelineUAVTarget::Accumulation
                | (uint16_t) vkrt::RenderPipelineUAVTarget::AOV;

            // allow pure ray query / compute-based RT pipelines
            if (gpu_program->type == GPU_PROGRAM_TYPE_COMPUTE) {
                println(CLL::VERBOSE, "Building RQ compute pipeline %s", variant_name);

                new_pipeline.reset( new ComputeRenderPipelineVulkan(
                    this, gpu_program, pipeline_options, defer_build
                ) );
            } else {
                println(CLL::VERBOSE, "Building RT pipeline %s", variant_name);

                new_pipeline.reset( new RayTracingPipelineVulkan(
                    this, gpu_program, SHARED_PIPELINE_SHADER_STAGES, pipeline_options, defer_build
                ) );
            }
        }
        assert(new_pipeline.get());

        if (current_pipeline) {
            pipelines.remove(gpu_program, options);
            current_pipeline = nullptr;
        }
        current_pipeline = pipelines.add(std::move(new_pipeline), gpu_program, options);
        // todo: combine into a nicer wrapper? how to enable only where supported? SFINAE/enable_if? :/
        current_pipeline->hot_reload_generation = pipeline_store.hot_reload_generation;
    }
    // only initialize up to this point on startup
    assert(current_pipeline);
    if (defer_initialization)
        return current_pipeline;

    // note: scene may have changed
    current_pipeline->update_shader_binding_table();

    return current_pipeline;
}

std::vector<RenderMeshParams> RenderVulkan::collect_render_mesh_params(int parameterized_mesh, Scene const& scene) const {
    const auto &pm = scene.parameterized_meshes[parameterized_mesh];
    const auto &vkpm = parameterized_meshes[parameterized_mesh];
    std::vector<RenderMeshParams> hit_params( meshes[pm.mesh_id]->geometries.size() );
    len_t primOffset = 0;
    for (int j = 0; j < (int) meshes[pm.mesh_id]->geometries.size(); ++j) {
        auto &geom = meshes[pm.mesh_id]->geometries[j];
        RenderMeshParams *params = hit_params.data() + j;

#ifdef QUANTIZED_POSITIONS
        int vertex_stride = sizeof(uint64_t);
#else
        int vertex_stride = sizeof(float) * 3;
#endif
#ifdef QUANTIZED_NORMALS_AND_UVS
        int normal_stride = sizeof(uint64_t);
        int uv_stride = sizeof(uint64_t);
#else
        int normal_stride = sizeof(float) * 3;
        int uv_stride = sizeof(float) * 2;
#endif

        int vertex_count = geom.num_vertices();
        int triangle_count = geom.num_triangles();
        int vertex_offset = geom.vertex_offset;

        if (geom.index_buf && !geom.indices_are_implicit) {
            params->indices.i = (decltype(params->indices.i)) geom.index_buf->device_address() + geom.triangle_offset;
            params->num_indices = triangle_count * 3;

            vertex_offset += geom.index_offset;
        }
        else {
            params->indices.i = 0;
            params->num_indices = 0;
            params->flags |= GEOMETRY_FLAGS_IMPLICIT_INDICES;
        }

        params->vertices.v = decltype(params->vertices.v)(
            geom.vertex_buf->device_address() + vertex_offset * vertex_stride);
        params->num_vertices = vertex_count;

        params->quantized_offset = glm::vec4(geom.quantized_offset, 1.0f);
        params->quantized_scaling = glm::vec4(geom.quantized_scaling, 1.0f);

        if (geom.normal_buf) {
            params->normals.n = decltype(params->normals.n)(
                geom.normal_buf->device_address() + vertex_offset * normal_stride);
            params->num_normals = 1;
        } else {
            params->num_normals = 0;
        }

        if (geom.uv_buf) {
            params->uvs.uv = decltype(params->uvs.uv)(
                geom.uv_buf->device_address() + vertex_offset * uv_stride);
            params->num_uvs = 1;
        } else {
            params->num_uvs = 0;
        }
        bool no_alpha = vkpm.no_alpha;
        bool extended_shader = false;
        bool is_thin = false;
        if (vkpm.per_triangle_material_buf) {
            params->materials.id_4pack = (decltype(params->materials.id_4pack))
                (vkpm.per_triangle_material_buf->device_address() + primOffset);
            // mark with negative offset, as 64 bit pointer checks not always supported
            params->material_id = -1-pm.material_offset(j);
            extended_shader = true;
        }
        else {
            assert(pm.material_id_bitcount == 32);
            params->material_id = pm.material_offset(j);
            if (!no_alpha)
                no_alpha = (scene.materials[params->material_id].flags & BASE_MATERIAL_NOALPHA) != 0;
            if (!extended_shader)
                extended_shader = (scene.materials[params->material_id].flags & BASE_MATERIAL_EXTENDED) != 0;
            is_thin = (scene.materials[params->material_id].flags & BASE_MATERIAL_ONESIDED) == 0;
        }
        if (no_alpha)
            params->flags |= GEOMETRY_FLAGS_NOALPHA;
        if (extended_shader)
            params->flags |= GEOMETRY_FLAGS_EXTENDED_SHADER;
        if (is_thin)
            params->flags |= GEOMETRY_FLAGS_THIN;

        if (meshes[pm.mesh_id]->is_dynamic() && !mesh_shader_names[pm.mesh_id].empty())
            params->flags |= GEOMETRY_FLAGS_DYNAMIC;

        // for access to dynamic geometry
        if (geom.float_vertex_buf) {
            params->dynamic_vertices.v = (glm::vec3*) geom.float_vertex_buf->device_address() + vertex_offset;
        }

        params->paramerterized_mesh_data_id = parameterized_mesh;
        // note: this should be stable between LoD groups, used e.g. for proc animation
        // it is fixed in post to match for LoD groups
        params->paramerterized_mesh_id = parameterized_mesh;

        primOffset += triangle_count;
    }
    return hit_params;
}

void RenderVulkan::update_shader_binding_table(void* sbt_mapped, vkrt::ShaderBindingTable& shader_table) {
    // Raygen shader(s)?
    if (uint32_t *params = reinterpret_cast<uint32_t *>(shader_table.sbt_raygen_params(sbt_mapped, 0)))
    {
        //*params = light_params->size() / sizeof(QuadLight);
        (void) params;
    }

    // todo: make sure this is always available und fully constructed etc.?
    int hitgroup_index = 0;

    for (auto* ext : available_pipeline_extensions)
        if (ext->is_active_for(active_options))
            ext->update_shader_binding_table(sbt_mapped, shader_table, &hitgroup_index);

    for (size_t i = 0; i < render_meshes.size(); ++i) {
        std::vector<RenderMeshParams> const &hit_group_params = render_meshes[i];

        //const auto &pm = parameterized_meshes[i];
        for (size_t j = 0; j < hit_group_params.size(); ++j) {
            RenderMeshParams *params = reinterpret_cast<RenderMeshParams *>(
                shader_table.sbt_hitgroup_params(sbt_mapped, hitgroup_index));
            memcpy(params, hit_group_params.data() + j, sizeof(*params));
            ++hitgroup_index;
        }
    }
}

void RenderVulkan::update_view_parameters(const glm::vec3 &pos,
                                          const glm::vec3 &dir,
                                          const glm::vec3 &up,
                                          const float fovy,
                                          bool update_globals,
                                          const glsl::ViewParams *vp_ref)
{
    glm::vec2 img_plane_size;
    img_plane_size.y = 2.f * std::tan(glm::radians(0.5f * fovy));
    float aspect = static_cast<float>(render_targets[0]->dims().x) / render_targets[0]->dims().y;
    img_plane_size.x = img_plane_size.y * aspect;

    const glm::vec3 dir_du = glm::normalize(glm::cross(dir, up)) * img_plane_size.x;
    const glm::vec3 dir_dv = -glm::normalize(glm::cross(dir_du, dir)) * img_plane_size.y;
    const glm::vec3 dir_top_left = dir - 0.5f * dir_du - 0.5f * dir_dv;

    glsl::ViewParams viewParams;
    memset((void*) &viewParams, 0, sizeof(viewParams));
    viewParams.cam_pos = pos;
    viewParams.time = (float) fmod(time, (double) TIME_PERIOD);
    viewParams.cam_du = glm::vec4(dir_du, 0.f);
    viewParams.cam_dv = glm::vec4(dir_dv, 0.f);
    viewParams.cam_dir_top_left = glm::vec4(dir_top_left, 0.f);

    // Reference values
    glsl::ViewParams const* prevViewParams = &cached_gpu_params->locals.view_params;
    viewParams.prev_time = vp_ref ? vp_ref->time : prevViewParams->prev_time;
    viewParams.cam_pos_reference = vp_ref ? vp_ref->cam_pos : prevViewParams->cam_pos_reference;
    viewParams.cam_du_reference = vp_ref ? vp_ref->cam_du : prevViewParams->cam_du_reference;
    viewParams.cam_dv_reference = vp_ref ? vp_ref->cam_dv : prevViewParams->cam_dv_reference;
    viewParams.cam_dir_top_left_reference = vp_ref ? vp_ref->cam_dir_top_left : prevViewParams->cam_dir_top_left_reference;
    viewParams.VP_reference = vp_ref ? vp_ref->VP : prevViewParams->VP_reference;

    viewParams.frame_id = frame_id;
    viewParams.frame_offset = frame_offset;
    viewParams.frame_dims = accum_buffers[0]->dims();
    viewParams.light_sampling = this->lighting_params;
    if (this->params.enable_raster_taa > 0) {
        constexpr size_t num_sample_offsets = RASTER_TAA_NUM_SAMPLES;
        static_assert((num_sample_offsets > 0) && (num_sample_offsets <= halton_23_size),
            "RASTER_TAA_NUM_SAMPLES is out of range");

        const size_t idx = (frame_offset + frame_id) % num_sample_offsets;
        viewParams.screen_jitter = glm::vec2(halton_23[idx][0], halton_23[idx][1])
            * 2.0f / glm::vec2(viewParams.frame_dims) - 1.0f / glm::vec2(viewParams.frame_dims);
    } else
        viewParams.screen_jitter = glm::vec2(0.0f);
    glm::mat4 GLToVulkan = glm::mat4(1.0f);
    GLToVulkan[1][1] = -1.0f;
    GLToVulkan[2][2] = 0.5f;
    GLToVulkan[3][2] = 0.5f;
    viewParams.VP = GLToVulkan * glm::infinitePerspective(glm::radians(fovy), aspect, 0.5f) * inverse(glm::mat4(glm::mat4x3(cross(dir, up), up, -dir, viewParams.cam_pos)));
    local_param_buf.cycle_swap(active_swap_buffer_count);
    auto lp = (glsl::LocalParams*) local_param_buf->map();
    memcpy(&lp->view_params, &viewParams, sizeof(viewParams));
    local_param_buf->unmap();
    memcpy(&cached_gpu_params->locals.view_params, &viewParams, sizeof(viewParams));
    if (update_globals) {
        global_param_buf.cycle_swap(active_swap_buffer_count);
        async_refresh_global_parameters();
    }
}

void RenderVulkan::async_refresh_global_parameters() {
    memcpy(&cached_gpu_params->globals.render_params, &this->params, sizeof(this->params));
    if (!this->active_options.enable_raytraced_dof) {
        cached_gpu_params->globals.render_params.aperture_radius = 0.0f;
        cached_gpu_params->globals.render_params.focal_length = 0.0f;
    }
    auto gp = (glsl::GlobalParams*) global_param_buf->map();
    memcpy(gp, &cached_gpu_params->globals, sizeof(*gp));
    global_param_buf->unmap();
}

void RenderVulkan::update_config(SceneConfig const& config) {
    glsl::SceneParams& sceneParams = cached_gpu_params->globals.scene_params;
    sceneParams.normal_z_scale = 1.0f / config.bump_scale;

    update_sky_light(config);
}

void RenderVulkan::record_frame(VkCommandBuffer render_cmd_buf, int variant_index, int num_rayqueries, int samples_per_query) {
    auto& variant_pipeline = *build_raytracing_pipeline(variant_index, this->active_options);
    auto pipeline_bind_point = variant_pipeline.pipeline_bindpoint;
    auto pipeline_stage = (pipeline_bind_point == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR) ? VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    int batch_spp = samples_per_query > 0 ? samples_per_query : this->params.batch_spp;
    bool render_ray_queries = num_rayqueries > 0;
    if (!render_ray_queries) {
#ifdef ATOMIC_ACCUMULATE
        // note: don't switch accumulation method mid-accumulation
        // as this may change the data layout
        if (frame_id == 0)
            accumulate_atomically = (batch_spp > 1);
#else
        accumulate_atomically = false;
#endif
    }

    glsl::PushConstantParams push_constants = { };
    //push_constants.local_params = cached_gpu_params->locals;
    push_constants.num_rayqueries = render_ray_queries ? num_rayqueries : 0;
    push_constants.accumulation_frame_offset = render_ray_queries ? 0 : -1;
    push_constants.accumulation_batch_size = samples_per_query;
    if (accumulate_atomically)
        push_constants.accumulation_flags |= ACCUMULATION_FLAGS_ATOMIC;
#ifdef ENABLE_AOV_BUFFERS
    if (true)
        push_constants.accumulation_flags |= ACCUMULATION_FLAGS_AOVS;
#endif

    lazy_update_shader_descriptor_table(&variant_pipeline, swap_index);

    variant_pipeline.bind_pipeline(render_cmd_buf
        , &push_constants, sizeof(push_constants)
        , swap_index);

    // transitions & barriers of framebuffer resources
    if (!render_ray_queries) {
        VkPipelineStageFlags src_stages = VK_PIPELINE_STAGE_TRANSFER_BIT | PROCESSING_PIPELINE_STAGES;
        VkPipelineStageFlags dst_stages = pipeline_stage;

        // todo: should these barriers go somewhere else?
        vkrt::MemoryBarriers<1, 2 + AOVBufferCount> mem_barriers;

        auto& current_accum_buffer = accumulate_atomically ? atomic_accum_buffers[active_accum_buffer] : accum_buffers[active_accum_buffer];
        current_accum_buffer->layout_invalidate();
        mem_barriers.add(dst_stages, current_accum_buffer->transition_color(VK_IMAGE_LAYOUT_GENERAL
            , VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));

        render_targets[active_render_target]->layout_invalidate();
        mem_barriers.add(dst_stages, render_targets[active_render_target]->transition_color(VK_IMAGE_LAYOUT_GENERAL
            , VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));

#ifdef ENABLE_AOV_BUFFERS
        for (int i = 0; i < AOVBufferCount; ++i) {
            auto& aovbuf = aov_buffer((AOVBufferIndex) i);
            aovbuf->layout_invalidate();
            mem_barriers.add(dst_stages, aovbuf->transition_color(VK_IMAGE_LAYOUT_GENERAL
                , VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
        }
#endif

        // reset atomic accumulation buffer
        if (accumulate_atomically) {
            VkImageMemoryBarrier img_mem_barrier = mem_barriers.image_barriers[0];
            img_mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(render_cmd_buf,
                                 src_stages,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &img_mem_barrier);

            VkClearColorValue clearValue = { };
            vkCmdClearColorImage(render_cmd_buf
                , img_mem_barrier.image, VK_IMAGE_LAYOUT_GENERAL
                , &clearValue
                , 1, &img_mem_barrier.subresourceRange);

            src_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            mem_barriers.image_barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mem_barriers.image_barriers[0].oldLayout = img_mem_barrier.newLayout;
        }

        mem_barriers.set(render_cmd_buf, src_stages);
    }

    glm::ivec2 dispatch_dim = accum_buffers[active_accum_buffer]->dims();
    // dispatch ray queries into a virtual screen square to allow for 2D locality if required
    if (render_ray_queries) {
        int dispatch_size = (int) std::abs(num_rayqueries);
        dispatch_dim.x = (int) std::ceil( std::sqrt((float) dispatch_size) );
        dispatch_dim.y = (int) (dispatch_size + dispatch_dim.x - 1) / dispatch_dim.x;
    }

    variant_pipeline.dispatch_rays(render_cmd_buf, dispatch_dim.x, dispatch_dim.y, batch_spp);
}

void RenderVulkan::record_readback(VkCommandBuffer cmd_buf, vkrt::Texture2D* target)
{
    BUFFER_BARRIER(buf_barrier);
    buf_barrier.buffer = img_readback_buf;
    buf_barrier.srcAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
    buf_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    auto img_barrier = target->transition_color(VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT);

    vkCmdPipelineBarrier(cmd_buf,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         1, &buf_barrier,
                         1, &img_barrier);

    VkBufferImageCopy img_copy = {};
    img_copy.bufferOffset = 0;
    img_copy.bufferRowLength = 0;
    img_copy.bufferImageHeight = 0;
    img_copy.imageSubresource = target->color_subresource();
    img_copy.imageOffset.x = 0;
    img_copy.imageOffset.y = 0;
    img_copy.imageOffset.z = 0;
    img_copy.imageExtent.width = target->dims().x;
    img_copy.imageExtent.height = target->dims().y;
    img_copy.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(cmd_buf,
                           *target,
                           VK_IMAGE_LAYOUT_GENERAL,
                           img_readback_buf->handle(),
                           1,
                           &img_copy);

    buf_barrier.srcAccessMask = buf_barrier.dstAccessMask;
    buf_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(cmd_buf,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT ,
                         0,
                         0, nullptr,
                         1, &buf_barrier,
                         0, nullptr);
}
