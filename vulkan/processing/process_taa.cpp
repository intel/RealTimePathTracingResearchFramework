// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "process_taa.h"
#include "../render_vulkan.h"

#include "types.h"
#include "util.h"
#include "profiling.h"

#include <algorithm>
#include <numeric>

namespace glsl {
    using namespace glm;
    #include "../../rendering/language.hpp"
    #include "../gpu_params.glsl"
}

extern "C" { extern struct GpuProgram const vulkan_program_PROCESS_TAA; }

template <> std::unique_ptr<RenderExtension> create_render_extension<ProcessTAAVulkan>(RenderBackend* backend) {
    return std::unique_ptr<RenderExtension>( new ProcessTAAVulkan(&dynamic_cast<RenderVulkan&>(*backend)) );
}

ProcessTAAVulkan::ProcessTAAVulkan(RenderVulkan* backend)
    : device(backend->device)
    , backend(backend)
{
    try { // need to handle all exceptions from here for manual multi-resource cleanup!
        vkrt::RenderPipelineOptions options;
        options.access_targets = vkrt::RenderPipelineUAVTarget::Accumulation;
        options.default_push_constant_size = sizeof(glm::ivec4);
        processing_pipeline.reset( new ComputeRenderPipelineVulkan(backend
                , &vulkan_program_PROCESS_TAA
                , options, false, this
            ) );
    } catch (...) {
        internal_release_resources();
        throw;
    }
}

ProcessTAAVulkan::~ProcessTAAVulkan() {
    internal_release_resources();
}

void ProcessTAAVulkan::internal_release_resources() {
    vkDeviceWaitIdle(device->logical_device());
}

std::string ProcessTAAVulkan::name() const {
    return "Vulkan TAA Processing Extension";
}

void ProcessTAAVulkan::initialize(const int fb_width, const int fb_height) {
}

void ProcessTAAVulkan::update_scene_from_backend(const Scene &scene) {
}

// processing pipeline
void ProcessTAAVulkan::register_custom_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const {
    auto& set_layout = collector.set;
    set_layout
        .add_binding(
            FRAMEBUFFER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(
            HISTORY_FRAMEBUFFER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(
            VIEW_PARAMS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
#ifdef ENABLE_AOV_BUFFERS
        .add_binding(
            AOV_MOTION_JITTER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
#endif
        ;
}
// processing pipeline
void ProcessTAAVulkan::update_custom_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) {
    auto& updater = collector.set;
    updater
        .write_storage_image(desc_set, FRAMEBUFFER_BIND_POINT, backend->render_targets[backend->active_render_target])
        .write_combined_sampler(desc_set, HISTORY_FRAMEBUFFER_BIND_POINT, backend->render_targets[!backend->active_render_target], backend->screen_sampler)
        .write_ubo(desc_set, VIEW_PARAMS_BIND_POINT, backend->local_param_buf)
        ;
#ifdef ENABLE_AOV_BUFFERS
    updater
        .write_storage_image(desc_set, AOV_MOTION_JITTER_BIND_POINT, backend->aov_buffer(backend->AOVMotionJitterIndex))
    ;
#endif
}

void ProcessTAAVulkan::process(CommandStream* cmd_stream_, int variant_idx) {
    // note: end frame already happened!
    if (backend->frame_id <= 1)
        return;

    auto cmd_stream = dynamic_cast<vkrt::CommandStream*>(cmd_stream_);
    if (!cmd_stream)
        cmd_stream = device.sync_command_stream();

    if (!cmd_stream_)
        cmd_stream->begin_record();
    VkCommandBuffer render_cmd_buf = cmd_stream->current_buffer;

    // Start the profiling
    auto taa_marker = backend->profiling_data.start_timing(render_cmd_buf, vkrt::ProfilingMarker::TAA, backend->swap_index);

    backend->lazy_update_shader_descriptor_table(processing_pipeline.get(), backend->swap_index, this);

    glm::ivec2 fb_dim = backend->render_targets[0]->dims();

    glm::ivec4 push_const = glm::ivec4(fb_dim, backend->active_options.render_upscale_factor, 0);
    processing_pipeline->bind_pipeline(render_cmd_buf
        , &push_const, sizeof(push_const)
        , backend->swap_index, this);

    {
        vkrt::MemoryBarriers<1, 2> mem_barriers;
        mem_barriers.add(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            backend->render_targets[backend->active_render_target]->transition_color(VK_IMAGE_LAYOUT_GENERAL)
        );
        mem_barriers.add(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            backend->render_targets[!backend->active_render_target]->transition_color(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT)
        );
        mem_barriers.set(render_cmd_buf, DEFAULT_IMAGEBUFFER_PIPELINE_STAGES);
    }

    processing_pipeline->dispatch_rays(render_cmd_buf, fb_dim.x, fb_dim.y, 1);

    // End the profiling
    backend->profiling_data.end_timing(render_cmd_buf, taa_marker, backend->swap_index);

    if (!cmd_stream_)
        cmd_stream->end_submit();
}
