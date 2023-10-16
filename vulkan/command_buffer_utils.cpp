// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "command_buffer_utils.h"

namespace vkrt::command_buffer
{
    static VkPipelineStageFlags DEFAULT_IMAGEBUFFER_BARRIER_STAGES = DEFAULT_IMAGEBUFFER_PIPELINE_STAGES | VK_PIPELINE_STAGE_TRANSFER_BIT;

    void enqueue_memory_barrier(VkCommandBuffer command_buffer, vkrt::Buffer& buffer, VkPipelineStageFlagBits stage)
    {
        BUFFER_BARRIER(buffer_mem_barrier);
        BUFFER_BARRIER_DEFAULTS(buffer_mem_barrier);
        buffer_mem_barrier.buffer = buffer->buf;
        vkrt::MemoryBarriers<1, 1> mem_barriers;
        mem_barriers.add(stage, buffer_mem_barrier);
        mem_barriers.set(command_buffer, DEFAULT_IMAGEBUFFER_BARRIER_STAGES);
    }

    void enqueue_memory_barrier(VkCommandBuffer command_buffer, vkrt::Texture2D& texture, VkPipelineStageFlagBits stage)
    {
        vkrt::MemoryBarriers<1, 1> mem_barriers;
        mem_barriers.add(stage, texture->transition_color(VK_IMAGE_LAYOUT_GENERAL));
        mem_barriers.set(command_buffer, DEFAULT_IMAGEBUFFER_BARRIER_STAGES);
    }

    void copy_texture(VkCommandBuffer command_buffer, vkrt::Texture2D& src, vkrt::Texture2D& dst)
    {
        VkImageCopy imageCopyRegion{};
        imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.srcSubresource.layerCount = 1;
        imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.dstSubresource.layerCount = 1;
        imageCopyRegion.extent.width = std::min(src.dims().x, dst.dims().x);
        imageCopyRegion.extent.height = std::min(src.dims().y, dst.dims().y);
        imageCopyRegion.extent.depth = 1;
        vkCmdCopyImage(command_buffer, src, VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_GENERAL, 1, &imageCopyRegion);
    }

    void copy_buffer(VkCommandBuffer command_buffer, vkrt::Buffer& src, vkrt::Buffer& dst)
    {
        VkBufferCopy copy;
        copy.srcOffset = 0;
        copy.dstOffset = 0;
        copy.size = src.size();
        vkCmdCopyBuffer(command_buffer, src, dst, 1, &copy);
    }

    void clear_texture(VkCommandBuffer command_buffer, vkrt::Texture2D& dst, VkClearColorValue clearColor) {
        VkImageMemoryBarrier img_mem_barrier = dst.transition_color(VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT);

        vkCmdPipelineBarrier(command_buffer,
                             DEFAULT_IMAGEBUFFER_BARRIER_STAGES,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &img_mem_barrier);

        VkImageSubresourceRange imageRange = subresource_range(dst.color_subresource());
        vkCmdClearColorImage(command_buffer, dst.image_handle(), VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &imageRange);
    }

} // namespace

