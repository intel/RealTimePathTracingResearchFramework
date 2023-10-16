// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <vulkan/vulkan_utils.h>

namespace vkrt
{
    namespace command_buffer
    {
        void enqueue_memory_barrier(VkCommandBuffer command_buffer, vkrt::Buffer &buffer, VkPipelineStageFlagBits stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        void enqueue_memory_barrier(VkCommandBuffer command_buffer, vkrt::Texture2D &texture, VkPipelineStageFlagBits stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        void copy_texture(VkCommandBuffer command_buffer, vkrt::Texture2D& src, vkrt::Texture2D& dst);
        void copy_buffer(VkCommandBuffer command_buffer, vkrt::Buffer &src, vkrt::Buffer &dst);
        void clear_texture(VkCommandBuffer command_buffer, vkrt::Texture2D& dst, VkClearColorValue color);
    } // namespace
} // namespace

