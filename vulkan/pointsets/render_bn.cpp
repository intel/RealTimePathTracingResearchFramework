// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "render_bn.h"
#include "../render_vulkan.h"

#include "types.h"
#include "util.h"
#include "profiling.h"

#include <algorithm>
#include <numeric>

#include "../rendering/pointsets/bn_data.h"
#include "../rendering/pointsets/bn_tables.h"

namespace glsl {
    using namespace glm;
    #include "../rendering/language.hpp"
    #include "../gpu_params.glsl"
}

template <> std::unique_ptr<RenderExtension> create_render_extension<RenderBNPointsVulkan>(RenderBackend* backend) {
    return std::unique_ptr<RenderExtension>( new RenderBNPointsVulkan(&dynamic_cast<RenderVulkan&>(*backend)) );
}

RenderBNPointsVulkan::RenderBNPointsVulkan(RenderVulkan* backend)
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

RenderBNPointsVulkan::~RenderBNPointsVulkan() {
    internal_release_resources();
}

void RenderBNPointsVulkan::internal_release_resources() {
    vkDeviceWaitIdle(device->logical_device());

    random_numbers_buf = nullptr;
}

std::string RenderBNPointsVulkan::name() const {
    return "Vulkan Blue Noise Render Extension";
}

void RenderBNPointsVulkan::initialize(const int fb_width, const int fb_height) {
}

void RenderBNPointsVulkan::update_scene_from_backend(const Scene &scene) {
}

bool RenderBNPointsVulkan::is_active_for(RenderBackendOptions const& rbo) const {
    return rbo.rng_variant == RNG_VARIANT_BN;
}

void RenderBNPointsVulkan::register_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const {
    auto& set_layout = collector.set;
    set_layout
        .add_binding(
            RANDOM_NUMBERS_BIND_POINT, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL)
        ;
}

void RenderBNPointsVulkan::update_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) {
    auto& updater = collector.set;
    updater
        .write_ssbo(desc_set, RANDOM_NUMBERS_BIND_POINT, random_numbers_buf);
}

void RenderBNPointsVulkan::update_random_buf()
{
    auto async_commands = device.async_command_stream();

    random_numbers_buf = vkrt::Buffer::device(
        *device,
        sizeof(BNData),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    {
        auto upload_random_numbers = random_numbers_buf.for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkrt::MemorySource(*device, vkrt::Device::ScratchArena));
        BNData* map = (BNData*) upload_random_numbers->map();
        static_assert(sizeof(map->sobol_spp_d) == sizeof(sobol_256spp_256d), "Sobol sample array misconfigured");
        std::memcpy(&map->sobol_spp_d, sobol_256spp_256d, sizeof(map->sobol_spp_d));
        static_assert(sizeof(map->tile_scrambling_yx_d_1spp) == sizeof(scramblingTile_yx_d_1spp), "Scrambling tile size misconfigured");
        //static_assert(sizeof(map->tile_ranking_yx_d_1spp) == sizeof(rankingTile_yx_d_1spp), "Ranking tile size misconfigured");
        std::memcpy(&map->tile_scrambling_yx_d_1spp, scramblingTile_yx_d_1spp, sizeof(map->tile_scrambling_yx_d_1spp));
        //std::memcpy(&map->tile_ranking_yx_d_1spp, rankingTile_yx_d_1spp, sizeof(map->tile_ranking_yx_d_1spp)); // omitting all zeros:
        assert(rankingTile_yx_d_1spp[0] == 0 && 0 == memcmp(rankingTile_yx_d_1spp, &rankingTile_yx_d_1spp[1], sizeof(rankingTile_yx_d_1spp)-sizeof(rankingTile_yx_d_1spp[0])));
        static_assert(sizeof(map->tile_scrambling_yx_d_4spp) == sizeof(scramblingTile_yx_d_4spp), "Scrambling tile size misconfigured");
        static_assert(sizeof(map->tile_ranking_yx_d_4spp) == sizeof(rankingTile_yx_d_4spp), "Ranking tile size misconfigured");
        std::memcpy(&map->tile_scrambling_yx_d_4spp, scramblingTile_yx_d_4spp, sizeof(map->tile_scrambling_yx_d_4spp));
        std::memcpy(&map->tile_ranking_yx_d_4spp, rankingTile_yx_d_4spp, sizeof(map->tile_ranking_yx_d_4spp));
        static_assert(sizeof(map->tile_scrambling_yx_d_16spp) == sizeof(scramblingTile_yx_d_16spp), "Scrambling tile size misconfigured");
        static_assert(sizeof(map->tile_ranking_yx_d_16spp) == sizeof(rankingTile_yx_d_16spp), "Ranking tile size misconfigured");
        std::memcpy(&map->tile_scrambling_yx_d_16spp, scramblingTile_yx_d_16spp, sizeof(map->tile_scrambling_yx_d_16spp));
        std::memcpy(&map->tile_ranking_yx_d_16spp, rankingTile_yx_d_16spp, sizeof(map->tile_ranking_yx_d_16spp));
        static_assert(sizeof(map->tile_scrambling_yx_d_256spp) == sizeof(scramblingTile_yx_d_256spp), "Scrambling tile size misconfigured");
        static_assert(sizeof(map->tile_ranking_yx_d_256spp) == sizeof(rankingTile_yx_d_256spp), "Ranking tile size misconfigured");
        std::memcpy(&map->tile_scrambling_yx_d_256spp, scramblingTile_yx_d_256spp, sizeof(map->tile_scrambling_yx_d_256spp));
        std::memcpy(&map->tile_ranking_yx_d_256spp, rankingTile_yx_d_256spp, sizeof(map->tile_ranking_yx_d_256spp));
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

struct RenderSobolVulkan;

// todo: move somewhere more central when more pointsets come in
namespace vkrt {
    void create_default_pointset_extensions(std::vector<std::unique_ptr<RenderExtension>>& extensions, RenderVulkan* backend) {
        extensions.push_back( std::unique_ptr<RenderExtension>(new RenderBNPointsVulkan(backend)) );
        extensions.push_back( create_render_extension<RenderSobolVulkan>(backend) );
    }
} // namespace
