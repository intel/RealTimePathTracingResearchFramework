// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <vulkan/vulkan.h>
#include "render_backend.h"
#include "vulkan_utils.h"
#include "vulkanrt_utils.h"

#include "../librender/render_data.h"

struct RenderVulkan;
struct GpuProgram;
struct CustomPipelineExtensionVulkan;

namespace vkrt {
    enum struct RenderPipelineTarget : uint16_t {
        None,
        Accumulation = 0x1,
        AOV = 0x2,
        AccumulationAndAOV = (uint16_t) Accumulation | (uint16_t) AOV,
    };
    struct RenderPipelineUAVTarget {
        enum T : uint16_t {
            None = 0x0,
            Accumulation = 0x1,
            AOV = 0x2,
            DepthStencil = 0x4
        };
    };
    struct RenderPipelineOptions : RenderBackendOptions {
        bool enable_raytracing = false;
        bool depth_test = false;
        bool raster_depth = false;
        RenderPipelineTarget raster_target = RenderPipelineTarget::None;
        uint16_t access_targets = RenderPipelineUAVTarget::None;
        int custom_pipeline_index = 0;
        uint32_t default_push_constant_size = 0;
    };

    struct BindingLayoutCollector {
        vkrt::DescriptorSetLayoutBuilder& set;
        // raster pipelines
        static int const MAX_FRAMEBUFFER_BINDINGS = 6;
        VkFormat (&framebuffer_formats)[MAX_FRAMEBUFFER_BINDINGS];
        VkFormat &framebuffer_depth_format;
    };

    struct BindingCollector {
        vkrt::DescriptorSetUpdater& set;
        // raster pipelines
        static int const MAX_FRAMEBUFFER_BINDINGS = BindingLayoutCollector::MAX_FRAMEBUFFER_BINDINGS;
        vkrt::Texture2D (&framebuffer)[MAX_FRAMEBUFFER_BINDINGS];
        vkrt::Texture2D &framebuffer_depth;
    };
} // namespace vkrt

struct RenderPipelineVulkan : RenderPipeline {
    vkrt::RenderPipelineOptions pipeline_options;

    vkrt::Device device;
    RenderVulkan* backend;
    unsigned hot_reload_generation = 0;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;

    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    VkDescriptorSet desc_sets[RenderBackend::MAX_SWAP_BUFFERS] = { VK_NULL_HANDLE };
    size_t desc_frames[RenderBackend::MAX_SWAP_BUFFERS];

    VkPipeline pipeline_handle = VK_NULL_HANDLE;
    VkPipelineBindPoint pipeline_bindpoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;
    VkShaderStageFlags push_constant_stages = 0;

    RenderPipelineVulkan(RenderVulkan* backend, vkrt::RenderPipelineOptions const& pipeline_options);
    RenderPipelineVulkan(RenderPipelineVulkan const&) = delete;
    RenderPipeline& operator=(RenderPipelineVulkan const&) = delete;
    virtual ~RenderPipelineVulkan();
    void internal_release_resources();
    virtual void wait_for_construction() = 0;

    // if supported, try to regenerate from updated sources
    virtual bool hot_reload(std::unique_ptr<RenderPipelineVulkan>& next_pipeline, unsigned for_generation) { return false; }

    virtual void bind_pipeline(VkCommandBuffer render_cmd_buf
        , void const* push_constants, size_t push_size
        , int swap_index, CustomPipelineExtensionVulkan* optional_managing_extension = nullptr);
    virtual void dispatch_rays(VkCommandBuffer render_cmd_buf, int width, int height, int batch_spp) = 0;

    virtual void update_shader_descriptor_table(vkrt::DescriptorSetUpdater& updater, int swap_index
        , CustomPipelineExtensionVulkan* optional_managing_extension);

    virtual void build_shader_binding_table() = 0;
    // note: may want extensions + render backend to do this instead? would likely just need base offset(s)
    virtual void update_shader_binding_table() = 0;

    // internal
    void build_shader_descriptor_table(CustomPipelineExtensionVulkan* optional_managing_extension
        , VkDescriptorSetLayout inherited_desc_layout = VK_NULL_HANDLE
        , VkFormat (*framebuffer_formats)[vkrt::BindingLayoutCollector::MAX_FRAMEBUFFER_BINDINGS] = nullptr
        , VkFormat *framebuffer_depth_format = nullptr);
    void build_layout(VkShaderStageFlags push_constant_stages, CustomPipelineExtensionVulkan* optional_managing_extension);
};

struct ComputeRenderPipelineVulkan : RenderPipelineVulkan {
    VkPipeline compute_pipeline = VK_NULL_HANDLE;
    glm::ivec3 workgroup_size;

    vkrt::ShaderModule deferred_module = nullptr;

    GpuProgram const* source_program = nullptr;
    std::string source_compile_options;
    CustomPipelineExtensionVulkan* source_managing_extension = nullptr;

    ComputeRenderPipelineVulkan(RenderVulkan* backend, GpuProgram const* program
        , vkrt::RenderPipelineOptions const& pipeline_options
        , bool defer = false
        , CustomPipelineExtensionVulkan* optional_managing_extension = nullptr
        , char const* compiler_options = nullptr
        , VkDescriptorSetLayout inherited_desc_layout = VK_NULL_HANDLE);
    ~ComputeRenderPipelineVulkan();
    void internal_release_resources();
    void wait_for_construction() override;

    void dispatch_rays(VkCommandBuffer render_cmd_buf, int width, int height, int batch_spp) override;

    std::string name() override;
    bool hot_reload(std::unique_ptr<RenderPipelineVulkan>& next_pipeline, unsigned for_generation) override;

    void update_shader_binding_table() override { }

    // internal
    bool build_pipeline(GpuProgram const* program, char const* compiler_options, bool defer); // returns false if deferred
    void build_shader_binding_table() override { }
};

struct RayTracingPipelineVulkan : RenderPipelineVulkan {
    vkrt::RTPipeline rt_pipeline;

    unsigned unique_scene_id = ~0;
    unsigned render_meshes_generation = ~0;
    unsigned parameterized_meshes_revision = ~0;

    vkrt::ShaderBindingTable shader_table;

    GpuProgram const* source_program = nullptr;

    RayTracingPipelineVulkan(RenderVulkan* backend, GpuProgram const* program
        , VkShaderStageFlags push_constant_stages
        , vkrt::RenderPipelineOptions const& pipeline_options
        , bool defer
        , CustomPipelineExtensionVulkan* optional_managing_extension = nullptr);
    virtual ~RayTracingPipelineVulkan();
    void internal_release_resources();
    void wait_for_construction() override;

    void dispatch_rays(VkCommandBuffer render_cmd_buf, int width, int height, int batch_spp) override;

    std::string name() override;

    void update_shader_binding_table() override;

    // internal
    bool build_pipeline(GpuProgram const* program, bool defer); // returns false if deferred
    void build_shader_binding_table() override;
};

struct RenderPipelineExtensionVulkan : RenderExtension {
    virtual void register_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const = 0;
    virtual void register_descriptor_sets(VkDescriptorSetLayout sets[], vkrt::RenderPipelineOptions const& options) const { }

    virtual void collect_descriptor_sets(VkDescriptorSet descriptor_sets[], vkrt::RenderPipelineOptions const& options) { }
    virtual void update_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) = 0;

    virtual bool update_tlas(bool rebuild_tlas) { return false; }
    virtual void update_shader_binding_table(void* sbt_mapped, vkrt::ShaderBindingTable& table, int32_t* hitgroup_start_index) { }
};

struct CustomPipelineExtensionVulkan : RenderPipelineExtensionVulkan {
    virtual void register_custom_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const = 0;
    virtual int register_custom_descriptor_sets(VkDescriptorSetLayout sets[], uint32_t& push_const_size, vkrt::RenderPipelineOptions const& options) const { return 0; }

    virtual int collect_custom_descriptor_sets(VkDescriptorSet descriptor_sets[], vkrt::RenderPipelineOptions const& options) { return 0; }
    virtual void update_custom_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) = 0;
};

struct ProcessingPipelineExtensionVulkan : CustomPipelineExtensionVulkan {
    // provide defaults as processing will likely not bind anything to main renderers
    virtual void register_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const { }
    virtual void update_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) { }
};
