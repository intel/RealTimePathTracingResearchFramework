// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "../render_pipeline_vulkan.h"

struct LightSamplingSetup;

struct RenderBinnedLightsVulkan : RenderPipelineExtensionVulkan {
    vkrt::Device device;
    RenderVulkan* backend;

    std::unique_ptr<LightSamplingSetup> lights;
    vkrt::Buffer light_params = nullptr;
    unsigned unique_scene_id = 0;
    unsigned lights_revision = ~0;
    // todo once dynamic: swap buffers

    RenderBinnedLightsVulkan(RenderVulkan* backend);
    virtual ~RenderBinnedLightsVulkan();
    void internal_release_resources();

    std::string name() const override;

    bool is_active_for(RenderBackendOptions const& rbo) const override;
    void preprocess(CommandStream* cmd_stream, int variant_idx) override;

    void initialize(const int fb_width, const int fb_height) override;
    void update_scene_from_backend(const Scene& scene) override;

    void register_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const override;
    void update_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) override;

    void update_lights(LightSamplingConfig const& params);
};
