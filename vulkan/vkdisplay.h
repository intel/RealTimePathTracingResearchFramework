// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>
#include "display/display.h"
#include "vulkan_utils.h"
#include <glm/glm.hpp>

struct GLFWwindow;

struct VKDisplay : Display {
    vkrt::Device device;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swap_chain = VK_NULL_HANDLE;

    int preferred_swap_image_count = Display::MAX_SWAP_IMAGES;
    // some platforms have up to 5 for some reason when asking for 3
    static const int MAX_SWAP_IMAGES = Display::MAX_SWAP_IMAGES+2;
    int swap_image_count = 0;
    VkImage swap_chain_images[MAX_SWAP_IMAGES] = { VK_NULL_HANDLE };
    VkImageView swap_chain_image_views[MAX_SWAP_IMAGES] = { VK_NULL_HANDLE };
    VkFramebuffer framebuffers[MAX_SWAP_IMAGES] = { VK_NULL_HANDLE };

    vkrt::ParallelCommandStream command_stream;

    VkSemaphore img_avail_semaphores[MAX_SWAP_IMAGES] = { VK_NULL_HANDLE };
    VkSemaphore present_ready_semaphores[MAX_SWAP_IMAGES] = { VK_NULL_HANDLE };

    vkrt::Buffer upload_buffer = nullptr;
    vkrt::Texture2D upload_texture = nullptr;

    VkRenderPass imgui_render_pass = VK_NULL_HANDLE;
    VkDescriptorPool imgui_desc_pool = VK_NULL_HANDLE;

    VKDisplay(GLFWwindow *window, const char *device_override = nullptr);
    ~VKDisplay();

    std::string name() const override;
    std::string gpu_brand() const override;

    void resize(const int fb_width, const int fb_height) override;

    void init_ui_frame() override;
    void new_frame() override;

    void display(const std::vector<uint32_t> &img) override;
    void display_native(vkrt::Texture2D &img);
    void display(RenderGraphic *renderer) override;

    CommandStream* stream() override { return &command_stream; }
};
