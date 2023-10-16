// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "render_binned_lights.h"
#include "../render_vulkan.h"

#include <librender/scene.h>
#include <librender/lights.h>

#include "types.h"
#include "util.h"
#include "profiling.h"

#include <algorithm>
#include <numeric>

namespace glsl {
    using namespace glm;
    #include "../rendering/language.hpp"
    #include "../gpu_params.glsl"
}

template <> std::unique_ptr<RenderExtension> create_render_extension<RenderBinnedLightsVulkan>(RenderBackend* backend) {
    return std::unique_ptr<RenderExtension>( new RenderBinnedLightsVulkan(&dynamic_cast<RenderVulkan&>(*backend)) );
}

RenderBinnedLightsVulkan::RenderBinnedLightsVulkan(RenderVulkan* backend)
    : device(backend->device)
    , backend(backend)
{
    try { // need to handle all exceptions from here for manual multi-resource cleanup!
    } catch (...) {
        internal_release_resources();
        throw;
    }
}

RenderBinnedLightsVulkan::~RenderBinnedLightsVulkan() {
    internal_release_resources();
}

void RenderBinnedLightsVulkan::internal_release_resources() {
    vkDeviceWaitIdle(device->logical_device());

    if (backend->binned_light_params == light_params)
        backend->binned_light_params = nullptr;
    light_params = nullptr;
}

std::string RenderBinnedLightsVulkan::name() const {
    return "Binned Light Sampling Vulkan Render Extension";
}


void RenderBinnedLightsVulkan::initialize(const int fb_width, const int fb_height) {
}

bool RenderBinnedLightsVulkan::is_active_for(RenderBackendOptions const& rbo) const {
    return rbo.light_sampling_variant == LIGHT_SAMPLING_VARIANT_RIS;
}

void RenderBinnedLightsVulkan::preprocess(CommandStream* cmd_stream, int variant_idx) {
    assert(is_active_for(backend->active_options));

    //printf("Todo: update lights\n");
}

void RenderBinnedLightsVulkan::update_scene_from_backend(const Scene &scene) {
    bool new_scene = this->unique_scene_id != scene.unqiue_id;

    if (new_scene) {
        lights.reset(nullptr);
        this->lights_revision = ~0;
    }

    if (this->lights_revision != scene.lights_revision) {
        if (!lights)
            lights = std::make_unique<LightSamplingSetup>();
        lights->emitters = collect_emitters(scene);
        update_lights(backend->lighting_params);
        this->lights_revision = scene.lights_revision;
    }

    device->flush_sync_and_async_device_copies();

    unique_scene_id = scene.unqiue_id;
}

void RenderBinnedLightsVulkan::register_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const {
    auto& set_layout = collector.set;
    set_layout
        .add_binding(
            LIGHTS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL)
        ;
}

void RenderBinnedLightsVulkan::update_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) {
    auto& updater = collector.set;
    updater
        .write_ssbo(desc_set, LIGHTS_BIND_POINT, light_params);
}

void RenderBinnedLightsVulkan::update_lights(LightSamplingConfig const& params) {
    update_light_sampling(lights->binned, lights->emitters, params);

    // todo once dynamic: cycle light buffers

    auto async_commands = device.async_command_stream();

    size_t lightBufferSize = std::max(size_t(1), lights->emitters.size());
    lightBufferSize = std::max(lightBufferSize, lights->binned.emitters.size());
    if (!light_params || light_params.size() / sizeof(TriLightData) < lightBufferSize) {
        light_params = vkrt::Buffer::device(reuse(vkrt::MemorySource(*device, backend->base_arena_idx + backend->StaticArenaOffset), light_params),
            sizeof(TriLightData) * lightBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }
    if (!lights->binned.emitters.empty())
    {
        auto upload_light_params = light_params->secondary_for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        void *map = upload_light_params->map();

        // todo: support quantization
        std::memcpy(map, lights->binned.emitters.data(), lights->binned.emitters.size() * sizeof(TriLightData));
        
        upload_light_params->unmap();

        async_commands->begin_record();

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = upload_light_params->size();
        vkCmdCopyBuffer(async_commands->current_buffer,
                        upload_light_params->handle(),
                        light_params->handle(),
                        1,
                        &copy_cmd);

        async_commands->end_submit();
        // do not need to wait since (secondary) upload buffer is kept for later updates
    }

    // todo: this needs to become more flexible for other techniques
    glsl::SceneParams& sceneParams = backend->global_params(true)->scene_params;
    sceneParams.light_sampling.light_count = lights->binned.emitters.size();
    sceneParams.light_sampling.optimized_bin_size = lights->binned.params.bin_size;
    sceneParams.light_sampling.optimized_light_bin_count = lights->binned.bin_count();

    // export for interop extensions
    backend->binned_light_params = light_params;
}

// todo: move somewhere more central when more light sampling algorithms come in
namespace vkrt {
    void create_default_light_sampling_extensions(std::vector<std::unique_ptr<RenderExtension>>& extensions, RenderVulkan* backend) {
        extensions.push_back( std::unique_ptr<RenderExtension>(new RenderBinnedLightsVulkan(backend)) );
    }
} // namespace
