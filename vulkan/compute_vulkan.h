// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once
#include "device_backend.h"
#include <vulkan/vulkan.h>
#include "vulkan_utils.h"
#include <glm/glm.hpp>

struct GpuProgram;

struct ComputeVulkan : ComputePipeline {
    vkrt::Device device;

    std::vector<GpuProgram const*> shader_modules;

    struct BufferBindings {
        GpuBuffer* buffer;
        int bind_point;
        VkDescriptorType desc_type;
    };
    std::vector<BufferBindings> bindings;
    int uniformBufferCount = 0;
    int shaderBufferCount = 0;

    std::vector<ComputePipeline*> bindings_other;

    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    VkDescriptorSet desc_set = VK_NULL_HANDLE;

    std::vector<VkDescriptorSet> bound_sets;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    struct Shader {
        VkPipeline pipeline;
        glm::ivec2 group_size;
    };
    std::vector<Shader> shaders;

    ComputeVulkan(vkrt::Device const& device);
    ComputeVulkan(ComputeVulkan const&) = delete;
    ComputeVulkan& operator=(ComputeVulkan const&) = delete;
    virtual ~ComputeVulkan();
    std::string name() override;

    int add_buffer(int bindpoint, GpuBuffer* buffer, bool uniform_buffer = false) override;
    int add_shader(char const* name) override;

    // inherit descriptor sets from another pipeline
    int add_pipeline(int bindpoint, ComputePipeline* pipeline) override;

    void finalize_build() override;
    void run(CommandStream* stream, int shader_index, glm::uvec2 dispatch_dim) override;
};

struct ComputeBufferVulkan : GpuBuffer {
    vkrt::Buffer buffer;
    ComputeBufferVulkan(vkrt::Buffer const& buffer);

    void* map() override;
    void unmap() override;
    size_t size() const override;
};
