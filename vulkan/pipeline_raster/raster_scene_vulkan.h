// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <vulkan/vulkan.h>
#include "../render_pipeline_vulkan.h"
#include "unordered_vector.hpp"

struct RasterScenePipelineVulkan : RenderPipelineVulkan {
    std::vector<VkPipeline> raster_pipeline_table;
    std::vector<vkrt::Texture2D> framebuffer_targets;
    vkrt::Texture2D framebuffer_depth_target;

    unsigned unique_scene_id = ~0;
    unsigned meshes_revision = ~0;
    unsigned parameterized_meshes_revision = ~0;

    unordered_vector<std::string, VkPipeline> raster_pipeline_store;

    struct PendingBuild {
        struct ShaderGroup {
            std::string name;
            vkrt::ShaderModule vertex = nullptr;
            vkrt::ShaderModule fragment = nullptr;
        };
        std::vector<ShaderGroup> modules;
        VkFormat framebuffer_formats[vkrt::BindingLayoutCollector::MAX_FRAMEBUFFER_BINDINGS] = { VK_FORMAT_UNDEFINED };
        VkFormat framebuffer_depth_format = VK_FORMAT_UNDEFINED;
    };
    std::unique_ptr<PendingBuild> pending_build;

    RasterScenePipelineVulkan(RenderVulkan* backend, GpuProgram const* program
        , vkrt::RenderPipelineOptions const& pipeline_options
        , bool defer);
    ~RasterScenePipelineVulkan();
    void internal_release_resources();
    void wait_for_construction() override;

    void dispatch_rays(VkCommandBuffer render_cmd_buf, int width, int height, int batch_spp) override;

    std::string name() override;

    void build_shader_binding_table() override;
    void update_shader_binding_table() override;

    void update_shader_descriptor_table(vkrt::DescriptorSetUpdater& updater, int swap_index
        , CustomPipelineExtensionVulkan* optional_managing_extension) override;

    // internal
    void build_layout();
    bool build_pipeline(std::unique_ptr<PendingBuild> pipeline_build, GpuProgram const* program, bool defer); // returns false if deferred
    VkPipeline build_raster_pipelines(PendingBuild const& build);

    void record_raster_commands(VkCommandBuffer render_cmd_buf);
};
