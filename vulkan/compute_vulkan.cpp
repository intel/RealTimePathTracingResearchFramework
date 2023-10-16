// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "compute_vulkan.h"
#include <cstring>

extern "C" { extern struct GpuProgram const* const vulkan_gpu_programs[]; }

#include "../librender/gpu_programs.h"
#include "../librender/render_params.glsl.h"

ComputeVulkan::ComputeVulkan(vkrt::Device const& device)
    : device(device) {
}

ComputeVulkan::~ComputeVulkan() {
    for (auto& shader : shaders)
        vkDestroyPipeline(device->logical_device(), shader.pipeline, nullptr);
    vkDestroyPipelineLayout(device->logical_device(), pipeline_layout, nullptr);
    vkDestroyDescriptorPool(device->logical_device(), desc_pool, nullptr);
    vkDestroyDescriptorSetLayout(device->logical_device(), set_layout, nullptr);
}

std::string ComputeVulkan::name() {
    return "Vulkan Compute Pipeline";
}

int ComputeVulkan::add_buffer(int bindpoint, GpuBuffer* buffer, bool uniform_buffer) {
    bindings.push_back({ buffer, bindpoint, uniform_buffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER });
    if (uniform_buffer)
        ++uniformBufferCount;
    else
        ++shaderBufferCount;
    return bindpoint;
}

int ComputeVulkan::add_shader(char const* name) {
    for (int i = 0; vulkan_gpu_programs[i]; ++i) {
        if (strcmp(vulkan_gpu_programs[i]->name, name) == 0) {
            int shader_index = (int) shader_modules.size();
            shader_modules.push_back(vulkan_gpu_programs[i]);
            return shader_index;
        }
    }
    return -1;
}

int ComputeVulkan::add_pipeline(int bindpoint, ComputePipeline* pipeline) {
    if (bindpoint >= (int) bindings_other.size())
        bindings_other.resize((size_t) bindpoint + 1);
    bindings_other[bindpoint] = pipeline;
    return bindpoint;
}

void ComputeVulkan::finalize_build() {
    // buffer set
    vkrt::DescriptorSetLayoutBuilder layoutBuilder;
    for (auto& binding : bindings)
        layoutBuilder.add_binding(
            (uint32_t) binding.bind_point, 1, binding.desc_type, VK_SHADER_STAGE_ALL);
    set_layout = layoutBuilder.build(*device);

    std::vector<VkDescriptorPoolSize> pool_sizes;
    if (uniformBufferCount)
        pool_sizes.push_back( VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t) uniformBufferCount} );
    if (shaderBufferCount)
        pool_sizes.push_back( VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t) shaderBufferCount} );
    VkDescriptorPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create_info.maxSets = 1;
    pool_create_info.poolSizeCount = pool_sizes.size();
    pool_create_info.pPoolSizes = pool_sizes.data();
    CHECK_VULKAN(vkCreateDescriptorPool(
        device->logical_device(), &pool_create_info, nullptr, &desc_pool));

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &set_layout;
    CHECK_VULKAN(vkAllocateDescriptorSets(device->logical_device(), &alloc_info, &desc_set));

    vkrt::DescriptorSetUpdater updater;
    for (auto& binding : bindings) {
        if (binding.desc_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            updater.write_ubo(desc_set, (uint32_t) binding.bind_point, dynamic_cast<ComputeBufferVulkan&>(*binding.buffer).buffer);
        else
            updater.write_ssbo(desc_set, (uint32_t) binding.bind_point, dynamic_cast<ComputeBufferVulkan&>(*binding.buffer).buffer);
    }
    updater.update(*device);

    // pipeline layout
    std::vector<VkDescriptorSetLayout> descriptor_layouts = { set_layout };
    bound_sets.push_back( desc_set );
    for (auto* pipeline_set : bindings_other) {
        descriptor_layouts.push_back({ pipeline_set ? static_cast<ComputeVulkan*>(pipeline_set)->set_layout : VK_NULL_HANDLE });
        bound_sets.push_back( pipeline_set ? static_cast<ComputeVulkan*>(pipeline_set)->desc_set : VK_NULL_HANDLE );
    }
    
    VkPushConstantRange push_constants[1] = {};
    push_constants[0].offset = 0;
    push_constants[0].size = sizeof(int) * 4;
    push_constants[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkPipelineLayoutCreateInfo pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_create_info.setLayoutCount = descriptor_layouts.size();
    pipeline_create_info.pSetLayouts = descriptor_layouts.data();
    pipeline_create_info.pPushConstantRanges = push_constants;
    pipeline_create_info.pushConstantRangeCount = sizeof(push_constants) / sizeof(push_constants[0]);

    CHECK_VULKAN(vkCreatePipelineLayout(
        device->logical_device(), &pipeline_create_info, nullptr, &pipeline_layout));

    // compute pipelines
    for (auto* program : shader_modules) {
        assert(program->modules[0] && !program->modules[1]);
        assert(program->modules[0]->units[0] && !program->modules[0]->units[1]);
        auto compute_unit = program->modules[0]->units[0];

        auto compute_shader = vkrt::ShaderModule(*device
            , read_gpu_shader_binary(compute_unit, {}));

        glm::ivec3 workgroup_size(1, 1, 1);
        std::vector<std::string> string_store;
        vkrt::get_workgroup_size(merge_to_old_defines(compute_unit->defines, string_store).data(), &workgroup_size.x, &workgroup_size.y, &workgroup_size.z);

        VkPipeline pipeline = VK_NULL_HANDLE;
        CHECK_VULKAN( build_compute_pipeline(*device, &pipeline, pipeline_layout, compute_shader) );
        shaders.push_back({pipeline, workgroup_size});
    }
}

void ComputeVulkan::run(CommandStream* stream_, int shader_index, glm::uvec2 dispatch_dim) {
    auto* stream = static_cast<vkrt::CommandStream*>(stream_);
    Shader shader = shaders[shader_index];

    vkCmdBindPipeline(
        stream->current_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader.pipeline);
    vkCmdBindDescriptorSets(stream->current_buffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout,
                            0,
                            (uint32_t) bound_sets.size(),
                            bound_sets.data(),
                            0,
                            nullptr);

    int push_constants[4] = { (int) dispatch_dim.x, (int) dispatch_dim.y };
    vkCmdPushConstants(stream->current_buffer, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push_constants), push_constants);

    glm::uvec2 workgroup_dim = (glm::uvec2) shader.group_size;
    dispatch_dim = (dispatch_dim + workgroup_dim - glm::uvec2(1)) / workgroup_dim;

    vkCmdDispatch(stream->current_buffer,
            dispatch_dim.x,
            dispatch_dim.y,
            1);
}

ComputeBufferVulkan::ComputeBufferVulkan(vkrt::Buffer const& buffer)
    : buffer(buffer) {
}
void* ComputeBufferVulkan::map() {
    return buffer.map();
}
void ComputeBufferVulkan::unmap() {
    buffer.unmap();
}
size_t ComputeBufferVulkan::size() const {
    return buffer.size();
}

std::unique_ptr<ComputeDevice> create_vulkan_compute_device(const char *device_override) {
    return std::unique_ptr<ComputeDevice>{ new ComputeDeviceVulkan( vkrt::Device({}, {}, device_override) ) };
}

ComputeDeviceVulkan::ComputeDeviceVulkan(vkrt::Device const& device)
    : device(device) {
}
CommandStream* ComputeDeviceVulkan::sync_command_stream() {
    return device.sync_command_stream();
}
std::unique_ptr<GpuBuffer> ComputeDeviceVulkan::create_uniform_buffer(size_t size) {
    return std::unique_ptr<GpuBuffer>{ new ComputeBufferVulkan(vkrt::Buffer::host(device
            , size
            , VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) };
}
std::unique_ptr<GpuBuffer> ComputeDeviceVulkan::create_buffer(size_t size) {
    return std::unique_ptr<GpuBuffer>{ new ComputeBufferVulkan(vkrt::Buffer::host(device
            , size
            , VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) };
}

std::unique_ptr<ComputePipeline> ComputeDeviceVulkan::create_pipeline() {
    return std::unique_ptr<ComputePipeline>{ new ComputeVulkan(device) };
}
