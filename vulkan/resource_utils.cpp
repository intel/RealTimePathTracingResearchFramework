// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

// Internal includes
#include "resource_utils.h"

void create_vulkan_textures_from_images(vkrt::CommandStream *async_commands,
                                        const std::vector<Image> &imageArray,
                                        std::vector<vkrt::Texture2D>& textureArray,
                                        vkrt::MemorySource& static_memory_arena,
                                        vkrt::MemorySource& scratch_memory_arena)
{
    for (size_t tex_idx = 0; tex_idx < textureArray.size(); ++tex_idx)
    {
        const auto &t = imageArray[tex_idx];
        vkrt::Texture2D cached_texture = textureArray[tex_idx];

        auto format = t.color_space == SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        switch (t.bcFormat) {
            case  0:
                if (t.channels != 4)
                    throw_error("unsupported channel layout");
                break;
            case  1: format = t.color_space == SRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK; break;
            case -1: format = t.color_space == SRGB ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
            case  2: format = t.color_space == SRGB ? VK_FORMAT_BC2_SRGB_BLOCK : VK_FORMAT_BC2_UNORM_BLOCK; break;
            case  3: format = t.color_space == SRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK; break;
            case  4: format = VK_FORMAT_BC4_UNORM_BLOCK; break;
            case -4: format = VK_FORMAT_BC4_SNORM_BLOCK; break;
            case  5: format = VK_FORMAT_BC5_UNORM_BLOCK; break;
            case -5: format = VK_FORMAT_BC5_SNORM_BLOCK; break;
            default: throw_error("unsupported block compression format");
        }
        int mip_levels = t.mip_levels();
        auto tex = vkrt::Texture2D::device(
            reuse(static_memory_arena, cached_texture),
            glm::ivec4(t.width, t.height, 0, mip_levels),
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        auto upload_buf = vkrt::Buffer::host(
            scratch_memory_arena, t.img.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        void *map = upload_buf->map();
        std::memcpy(map, t.img.data(), upload_buf->size());
        upload_buf->unmap();

        async_commands->begin_record();

        // Transition image to the general layout
        VkImageMemoryBarrier img_mem_barrier = {};
        IMAGE_BARRIER_DEFAULTS(img_mem_barrier);
        img_mem_barrier.image = tex->image_handle();
        img_mem_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img_mem_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        img_mem_barrier.srcAccessMask = 0;
        img_mem_barrier.subresourceRange.levelCount = mip_levels;

        vkCmdPipelineBarrier(async_commands->current_buffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &img_mem_barrier);

        VkImageSubresourceLayers copy_subresource = {};
        copy_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_subresource.mipLevel = 0;
        copy_subresource.baseArrayLayer = 0;
        copy_subresource.layerCount = 1;

        VkBufferImageCopy img_copy = {};
        img_copy.bufferOffset = 0;
        img_copy.bufferRowLength = 0;
        img_copy.bufferImageHeight = 0;
        img_copy.imageSubresource = copy_subresource;
        img_copy.imageOffset.x = 0;
        img_copy.imageOffset.y = 0;
        img_copy.imageOffset.z = 0;
        img_copy.imageExtent.width = t.width;
        img_copy.imageExtent.height = t.height;
        img_copy.imageExtent.depth = 1;

        int bpp = t.bits_per_pixel();
        int bw = t.bcFormat ? 4 : 1;
        for (int i = 0; i < mip_levels; ++i) {
            vkCmdCopyBufferToImage(async_commands->current_buffer,
                                upload_buf->handle(),
                                tex->image_handle(),
                                VK_IMAGE_LAYOUT_GENERAL,
                                1,
                                &img_copy);

            int wb = (img_copy.imageExtent.width + (bw-1)) / bw * bw;
            int hb = (img_copy.imageExtent.height + (bw-1)) / bw * bw;
            img_copy.bufferOffset += (VkDeviceSize) wb * hb * bpp / 8;
            if (img_copy.imageExtent.width > 1) img_copy.imageExtent.width /= 2;
            if (img_copy.imageExtent.height > 1) img_copy.imageExtent.height /= 2;
            ++img_copy.imageSubresource.mipLevel;
        }
        async_commands->hold_buffer(upload_buf);

        // Transition image to shader read optimal layout
        img_mem_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        img_mem_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        img_mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        img_mem_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; // requires Sync2: VK_ACCESS_2_SHADER_SAMPLED_READ_BIT_KHR;
        vkCmdPipelineBarrier(async_commands->current_buffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &img_mem_barrier);

        async_commands->end_submit();

        textureArray[tex_idx] = tex;
    }
}
