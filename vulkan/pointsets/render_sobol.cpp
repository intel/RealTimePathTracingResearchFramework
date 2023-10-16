// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "render_sobol.h"
#include "../render_vulkan.h"

#include "types.h"
#include "util.h"
#include "profiling.h"

#include <algorithm>
#include <numeric>

#include "../rendering/pointsets/sobol_tables.h"
#include "../rendering/pointsets/sobol_data.h"

namespace glsl {
    using namespace glm;
    #include "../rendering/language.hpp"
    #include "../gpu_params.glsl"
}

template <> std::unique_ptr<RenderExtension> create_render_extension<RenderSobolVulkan>(RenderBackend* backend) {
    return std::unique_ptr<RenderExtension>( new RenderSobolVulkan(&dynamic_cast<RenderVulkan&>(*backend)) );
}

RenderSobolVulkan::RenderSobolVulkan(RenderVulkan* backend)
    : device(backend->device)
    , backend(backend)
{
    try { // need to handle all exceptions from here for manual multi-resource cleanup!
        update_random_buf();
    } catch (...) {
        internal_release_resources();
        throw;
    }
}

RenderSobolVulkan::~RenderSobolVulkan() {
    internal_release_resources();
}

void RenderSobolVulkan::internal_release_resources() {
    vkDeviceWaitIdle(device->logical_device());

    random_numbers_buf = nullptr;
}

std::string RenderSobolVulkan::name() const {
    return "Vulkan Sobol Render Extension";
}


void RenderSobolVulkan::initialize(const int fb_width, const int fb_height) {
}

void RenderSobolVulkan::update_scene_from_backend(const Scene &scene) {
}

bool RenderSobolVulkan::is_active_for(RenderBackendOptions const& rbo) const {
    return rbo.rng_variant == RNG_VARIANT_SOBOL || rbo.rng_variant == RNG_VARIANT_Z_SBL;
}

void RenderSobolVulkan::register_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const {
    auto& set_layout = collector.set;
    set_layout
        .add_binding(
            RANDOM_NUMBERS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL)
        ;
}

void RenderSobolVulkan::update_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) {
    auto& updater = collector.set;
    updater
        .write_ssbo(desc_set, RANDOM_NUMBERS_BIND_POINT, random_numbers_buf);
}

void RenderSobolVulkan::update_random_buf()
{
    auto async_commands = device.async_command_stream();

    random_numbers_buf = vkrt::Buffer::device(
        *device,
        sizeof(SobolData),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    {
        auto upload_random_numbers = random_numbers_buf.for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkrt::MemorySource(*device, vkrt::Device::ScratchArena));
        SobolData* map = (SobolData*) upload_random_numbers->map();
        static_assert(sizeof(map->matrix) == sizeof(SobolMatrix), "Sobol matrix size misconfigured");
        std::memcpy(&map->matrix, SobolMatrix, sizeof(map->matrix));
        static_assert(sizeof(map->tile_invert_1_0) == sizeof(SobolInversion_1_0), "Sobol tile inversion size misconfigured");
        std::memcpy(&map->tile_invert_1_0, SobolInversion_1_0, sizeof(map->tile_invert_1_0));
        upload_random_numbers->unmap();

        async_commands->begin_record();

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = upload_random_numbers->size();
        vkCmdCopyBuffer(async_commands->current_buffer,
                        upload_random_numbers->handle(),
                        random_numbers_buf->handle(),
                        1,
                        &copy_cmd);
        async_commands->hold_buffer(upload_random_numbers);

        async_commands->end_submit();
    }
}
