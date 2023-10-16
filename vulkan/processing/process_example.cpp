// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "process_example.h"
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

extern "C" { extern struct GpuProgram const vulkan_program_PROCESS_EXAMPLE; }

template <> std::unique_ptr<RenderExtension> create_render_extension<ProcessExampleVulkan>(RenderBackend* backend) {
    return std::unique_ptr<RenderExtension>( new ProcessExampleVulkan(&dynamic_cast<RenderVulkan&>(*backend)) );
}

ProcessExampleVulkan::ProcessExampleVulkan(RenderVulkan* backend)
    : device(backend->device)
    , backend(backend)
{
    try { // need to handle all exceptions from here for manual multi-resource cleanup!
        vkrt::RenderPipelineOptions options;
        options.access_targets = vkrt::RenderPipelineUAVTarget::Accumulation
            | vkrt::RenderPipelineUAVTarget::AOV;
        processing_pipeline.reset( new ComputeRenderPipelineVulkan(backend
                , &vulkan_program_PROCESS_EXAMPLE
                , options, false, this
            ) );
    } catch (...) {
        internal_release_resources();
        throw;
    }
}

ProcessExampleVulkan::~ProcessExampleVulkan() {
    internal_release_resources();
}

void ProcessExampleVulkan::internal_release_resources() {
    vkDeviceWaitIdle(device->logical_device());
}

std::string ProcessExampleVulkan::name() const {
    return "Vulkan Example Processing Extension";
}

void ProcessExampleVulkan::initialize(const int fb_width, const int fb_height) {
}

void ProcessExampleVulkan::update_scene_from_backend(const Scene &scene) {
}

// processing pipeline
void ProcessExampleVulkan::register_custom_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const {
    auto& set_layout = collector.set;
    set_layout
        .add_binding(
            ACCUMBUFFER_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .add_binding(
            VIEW_PARAMS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL)
        ;
}
// processing pipeline
void ProcessExampleVulkan::update_custom_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) {
    auto& updater = collector.set;
    updater
        .write_storage_image(desc_set, ACCUMBUFFER_BIND_POINT, backend->current_color_buffer)
        .write_ubo(desc_set, VIEW_PARAMS_BIND_POINT, backend->local_param_buf)
        ;
}

void ProcessExampleVulkan::process(CommandStream* cmd_stream_, int variant_idx) {
    auto cmd_stream = dynamic_cast<vkrt::CommandStream*>(cmd_stream_);
    if (!cmd_stream)
        cmd_stream = device.sync_command_stream();

    if (!cmd_stream_)
        cmd_stream->begin_record();
    VkCommandBuffer render_cmd_buf = cmd_stream->current_buffer;

    backend->lazy_update_shader_descriptor_table(processing_pipeline.get(), backend->swap_index, this);

    processing_pipeline->bind_pipeline(render_cmd_buf
        , nullptr, 0
        , backend->swap_index, this);

    {
        vkrt::MemoryBarriers<1, 1> mem_barriers;
        mem_barriers.add(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, backend->current_color_buffer->transition_color(VK_IMAGE_LAYOUT_GENERAL));
        mem_barriers.set(render_cmd_buf, DEFAULT_IMAGEBUFFER_PIPELINE_STAGES);
    }

    glm::ivec2 dispatch_dim = backend->accum_buffer()->dims();
    processing_pipeline->dispatch_rays(render_cmd_buf, dispatch_dim.x, dispatch_dim.y, 1);

    if (!cmd_stream_)
        cmd_stream->end_submit();
}
