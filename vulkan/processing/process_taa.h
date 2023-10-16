// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "../render_pipeline_vulkan.h"

struct ProcessTAAVulkan : ProcessingPipelineExtensionVulkan {
    vkrt::Device device;
    RenderVulkan* backend;

    std::unique_ptr<RenderPipelineVulkan> processing_pipeline;

    ProcessTAAVulkan(RenderVulkan* backend);
    virtual ~ProcessTAAVulkan();
    void internal_release_resources();

    std::string name() const override;

    void initialize(const int fb_width, const int fb_height) override;
    void update_scene_from_backend(const Scene& scene) override;

    void register_custom_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const override;
    void update_custom_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) override;

    void process(CommandStream* cmd_stream, int variant_idx) override;
};
