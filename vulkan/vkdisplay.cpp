// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "vkdisplay.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include "display/imgui_backend.h"
#include "backends/imgui_impl_vulkan.h"
#include "vulkan_utils.h"
#include "error_io.h"

const static std::vector<std::string> logical_device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

std::vector<std::string> get_instance_extensions(GLFWwindow *window)
{
    uint32_t glfw_extension_count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (!extensions) {
        throw std::runtime_error("Failed to get GLFW vulkan extensions");
    }

    std::vector<std::string> instance_extensions;
    std::transform(extensions,
                   extensions + glfw_extension_count,
                   std::back_inserter(instance_extensions),
                   [](const char *str) { return std::string(str); });
    return instance_extensions;
}

Display* create_vulkan_display(GLFWwindow *window, const char *device_override) {
    return new VKDisplay(window, device_override);
}

VKDisplay::VKDisplay(GLFWwindow *window, const char *device_override)
    : device(get_instance_extensions(window), logical_device_extensions,
        device_override)
    , command_stream(device, vkrt::CommandQueueType::Main, MAX_SWAP_IMAGES)
{
    if (!glfwVulkanSupported())
        throw_error("GLFW cannt support Vulkan display frontend on this platform");
    CHECK_VULKAN(glfwCreateWindowSurface(device->instance(), window, NULL, &surface));

    for (int i = 0; i < MAX_SWAP_IMAGES; ++i) {
        VkSemaphoreCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        CHECK_VULKAN(vkCreateSemaphore(device->logical_device()
            , &info, nullptr, &img_avail_semaphores[i]));
        CHECK_VULKAN(vkCreateSemaphore(device->logical_device()
            , &info, nullptr, &present_ready_semaphores[i]));
    }

    VkBool32 present_supported = false;
    CHECK_VULKAN(vkGetPhysicalDeviceSurfaceSupportKHR(device->physical_device(), device->main_queue_index(), surface, &present_supported));
    if (!present_supported) {
        throw std::runtime_error("Present is not supported on the graphics queue!?");
    }

    // Setup ImGui render pass
    {
        VkAttachmentDescription attachment = {};
        attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;
        VkSubpassDependency dependencies[2] = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;
        CHECK_VULKAN(
            vkCreateRenderPass(device->logical_device(), &info, nullptr, &imgui_render_pass));
    }

    {
        VkDescriptorPoolSize pool_size = {};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = 1;

        VkDescriptorPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets = 1;
        info.poolSizeCount = 1;
        info.pPoolSizes = &pool_size;
        CHECK_VULKAN(vkCreateDescriptorPool(
            device->logical_device(), &info, nullptr, &imgui_desc_pool));
    }

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = device->instance();
    init_info.PhysicalDevice = device->physical_device();
    init_info.Device = device->logical_device();
    init_info.QueueFamily = device->main_queue_index();
    init_info.Queue = device->main_queue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imgui_desc_pool;
    init_info.Allocator = nullptr;
    init_info.MinImageCount = 2; // irrelevant for exteral swap chains
    init_info.ImageCount = preferred_swap_image_count;
    init_info.CheckVkResultFn = [](const VkResult err) { CHECK_VULKAN(err); };
    ImGui_ImplVulkan_Init(&init_info, imgui_render_pass);

    auto* sync_commands = device.sync_command_stream();

    sync_commands->begin_record();
    ImGui_ImplVulkan_CreateFontsTexture(sync_commands->current_buffer);
    sync_commands->end_submit();

    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

VKDisplay::~VKDisplay()
{
    vkDeviceWaitIdle(device->logical_device());

    ImGui_ImplVulkan_Shutdown();
    for (int i = 0; i < MAX_SWAP_IMAGES; ++i) {
        if (i < swap_image_count) {
            vkDestroyImageView(device->logical_device(), swap_chain_image_views[i], nullptr);
            vkDestroyFramebuffer(device->logical_device(), framebuffers[i], nullptr);
        }
        vkDestroySemaphore(device->logical_device(), img_avail_semaphores[i], nullptr);
        vkDestroySemaphore(device->logical_device(), present_ready_semaphores[i], nullptr);
    }
    vkDestroyDescriptorPool(device->logical_device(), imgui_desc_pool, nullptr);
    vkDestroyRenderPass(device->logical_device(), imgui_render_pass, nullptr);
    vkDestroySwapchainKHR(device->logical_device(), swap_chain, nullptr);
    vkDestroySurfaceKHR(device->instance(), surface, nullptr);

    upload_buffer = nullptr;
    upload_texture = nullptr;
    command_stream = nullptr;

    if (device->ref_data->ref_count > 1)
        warning("%d other device references alive on display close. Check for resource leaks?", device->ref_data->ref_count-1);
}

std::string VKDisplay::gpu_brand() const
{
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(device->physical_device(), &properties);
    return properties.deviceName;
}

std::string VKDisplay::name() const
{
    return "Vulkan";
}

void VKDisplay::resize(const int win_width, const int win_height)
{
    command_stream.wait_complete();
    CHECK_VULKAN(vkDeviceWaitIdle(device->logical_device()));

    if (swap_chain != VK_NULL_HANDLE) {
        for (int i = 0; i < swap_image_count; ++i) {
            vkDestroyImageView(device->logical_device(), swap_chain_image_views[i], nullptr);
            vkDestroyFramebuffer(device->logical_device(), framebuffers[i], nullptr);
        }
        vkDestroySwapchainKHR(device->logical_device(), swap_chain, nullptr);
    }

    // Make sure the framebuffer is clampled to the size required by the physical device (necessary for resizing properly)
    VkSurfaceCapabilitiesKHR capabilities;
    CHECK_VULKAN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical_device(), surface, &capabilities));
    
    this->fb_dims.x = (int) std::clamp((uint32_t) win_width
        , capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    this->fb_dims.y = (int) std::clamp((uint32_t) win_height
        , capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    upload_texture = vkrt::Texture2D::device(
        vkrt::MemorySource(device, vkrt::Device::DisplayArena),
        glm::uvec4(fb_dims, 0, 0),
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    upload_buffer = vkrt::Buffer::host(
        vkrt::MemorySource(device, vkrt::Device::DisplayArena),
        sizeof(uint32_t) * fb_dims.x * fb_dims.y,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    VkExtent2D swapchain_extent = {};
    swapchain_extent.width = fb_dims.x;
    swapchain_extent.height = fb_dims.y;

    VkSwapchainCreateInfoKHR create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface;
    create_info.minImageCount = preferred_swap_image_count;
    create_info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    create_info.imageExtent = swapchain_extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR; // VK_PRESENT_MODE_FIFO_KHR;
    create_info.clipped = true;
    create_info.oldSwapchain = VK_NULL_HANDLE;
    CHECK_VULKAN(
        vkCreateSwapchainKHR(device->logical_device(), &create_info, nullptr, &swap_chain));

    test_println("Swap Chain created");

    // Get the swap chain images
    uint32_t num_swapchain_imgs = 0;
    vkGetSwapchainImagesKHR(
        device->logical_device(), swap_chain, &num_swapchain_imgs, nullptr);
    if (num_swapchain_imgs > (uint32_t) MAX_SWAP_IMAGES)
        throw_error("Device asked for %d swap lanes, only supporting up to %d swap images"
            , int(num_swapchain_imgs), MAX_SWAP_IMAGES);

    swap_image_count = int(num_swapchain_imgs);
    command_stream.ref_data->async_command_buffer_count = swap_image_count;
    vkGetSwapchainImagesKHR(
        device->logical_device(), swap_chain, &num_swapchain_imgs, swap_chain_images);

    // Make image views and framebuffers for the imgui render pass
    for (size_t i = 0; i < num_swapchain_imgs; ++i) {
        VkImageViewCreateInfo view_create_info = {};
        view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_create_info.image = swap_chain_images[i];
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_create_info.format = VK_FORMAT_B8G8R8A8_UNORM;

        view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.baseMipLevel = 0;
        view_create_info.subresourceRange.levelCount = 1;
        view_create_info.subresourceRange.baseArrayLayer = 0;
        view_create_info.subresourceRange.layerCount = 1;

        CHECK_VULKAN(vkCreateImageView(
            device->logical_device(), &view_create_info, nullptr, &swap_chain_image_views[i]));
    }

    for (size_t i = 0; i < num_swapchain_imgs; ++i) {
        VkFramebufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = imgui_render_pass;
        info.attachmentCount = 1;
        info.pAttachments = &swap_chain_image_views[i];
        info.width = fb_dims.x;
        info.height = fb_dims.y;
        info.layers = 1;

        CHECK_VULKAN(
            vkCreateFramebuffer(device->logical_device(), &info, nullptr, &framebuffers[i]));
    }
}

void VKDisplay::init_ui_frame()
{
    ImGui_ImplVulkan_NewFrame();
}

void VKDisplay::new_frame()
{
    command_stream.begin_record();
}

void VKDisplay::display(const std::vector<uint32_t> &img)
{
    std::memcpy(upload_buffer->map(), img.data(), upload_buffer->size());
    upload_buffer->unmap();

    auto* sync_commands = device.sync_command_stream();
    
    sync_commands->begin_record();
    auto command_buffer = sync_commands->current_buffer;

    upload_texture->layout_invalidate();

    {
        vkrt::MemoryBarriers<1, 1> barriers;
        barriers.add(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            upload_texture->transition_color(VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT)
            );
        barriers.set(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }

    VkBufferImageCopy img_copy = {};
    img_copy.imageSubresource = upload_texture->color_subresource();
    img_copy.imageExtent.width = fb_dims.x;
    img_copy.imageExtent.height = fb_dims.y;
    img_copy.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(command_buffer,
                           upload_buffer->handle(),
                           upload_texture->image_handle(),
                           VK_IMAGE_LAYOUT_GENERAL,
                           1,
                           &img_copy);

    sync_commands->end_submit();

    display_native(upload_texture);
}

void VKDisplay::display_native(vkrt::Texture2D &img)
{
    auto command_buffer = command_stream.current_buffer;

    int command_buffer_index = command_stream.current_index();
    uint32_t back_buffer_idx = 0;
    auto img_avail_semaphore = img_avail_semaphores[command_buffer_index];
    auto present_result = vkAcquireNextImageKHR(device->logical_device(),
                                       swap_chain,
                                       std::numeric_limits<uint64_t>::max(),
                                       img_avail_semaphore,
                                       VK_NULL_HANDLE,
                                       &back_buffer_idx);

    if (present_result == VK_SUCCESS || present_result == VK_SUBOPTIMAL_KHR) {

    if (img) {
    IMAGE_BARRIER(swap_image_barrier);
    swap_image_barrier.image = swap_chain_images[back_buffer_idx];
    swap_image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swap_image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swap_image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    {
        vkrt::MemoryBarriers<1, 2> barriers;
        barriers.add(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            img.transition_color(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT)
            );
        barriers.add(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            swap_image_barrier
            );
        barriers.set(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }

    VkImageBlit blit = {};
    // crop to smallest common area
    blit.srcSubresource = img.color_subresource();
    blit.srcOffsets[1].x = (int) img.tdims.x; // std::min((int) fb_dims.x, (int) img.tdims.x);
    blit.srcOffsets[1].y = (int) img.tdims.y; // std::min((int) fb_dims.y, (int) img.tdims.y);
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[1].x = fb_dims.x; // blit.srcOffsets[1].x;
    blit.dstOffsets[1].y = fb_dims.y; // blit.srcOffsets[1].y;
    blit.dstOffsets[1].z = 1;

    vkCmdBlitImage(command_buffer,
                   img->image_handle(),
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swap_chain_images[back_buffer_idx],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1,
                   &blit,
                   VK_FILTER_NEAREST);

    swap_image_barrier.oldLayout = swap_image_barrier.newLayout;
    swap_image_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swap_image_barrier.srcAccessMask = swap_image_barrier.dstAccessMask;
    swap_image_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &swap_image_barrier);
    }

    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = imgui_render_pass;
    render_pass_info.framebuffer = framebuffers[back_buffer_idx];
    render_pass_info.renderArea.extent.width = fb_dims.x;
    render_pass_info.renderArea.extent.height = fb_dims.y;
    render_pass_info.clearValueCount = 0;
    vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
    vkCmdEndRenderPass(command_buffer);

    }
    else
        img_avail_semaphore = VK_NULL_HANDLE;

    auto present_ready_semaphore = present_ready_semaphores[command_buffer_index];
    command_stream.end_submit(img_avail_semaphore, present_ready_semaphore);

    if (present_result == VK_SUCCESS) {

    // Finally, present the updated image in the swap chain
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &present_ready_semaphore;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swap_chain;
    present_info.pImageIndices = &back_buffer_idx;
    present_result = vkQueuePresentKHR(device->main_queue(), &present_info);

    }

    switch (present_result) {
    case VK_SUCCESS:
        break;
    case VK_SUBOPTIMAL_KHR:
        // On Linux it seems we get the error failing to present before we get the window
        // resized event from SDL to update the swap chain, so filter out these errors
    case VK_ERROR_OUT_OF_DATE_KHR:
        // todo: should we check to ensure this only happens rarely on resize?
        warning("Swap chain still needed update on present");
        break;
    default:
        // Other errors are actual problems
        CHECK_VULKAN(present_result);
    }
}
