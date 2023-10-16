// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "../render_pipeline_vulkan.h"

struct RenderBNPointsVulkan : RenderPipelineExtensionVulkan {
    vkrt::Device device;
    RenderVulkan* backend;

    vkrt::Buffer random_numbers_buf = nullptr;

    RenderBNPointsVulkan(RenderVulkan* backend);
    virtual ~RenderBNPointsVulkan();
    void internal_release_resources();

    std::string name() const override;

    void initialize(const int fb_width, const int fb_height) override;
    void update_scene_from_backend(const Scene& scene) override;

    bool is_active_for(RenderBackendOptions const& rbo) const override;

    void register_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const override;
    void update_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) override;

    void update_random_buf();
};
