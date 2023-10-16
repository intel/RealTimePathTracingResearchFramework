// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

struct RenderGraphic;
struct CommandStream;

struct Display {
    static const int MAX_SWAP_IMAGES = 3;
    glm::ivec2 fb_dims;

    Display() = default;
    Display(Display const&) = delete;
    Display& operator=(Display const&) = delete;
    virtual ~Display() {}

    virtual std::string gpu_brand() const = 0;
    virtual std::string name() const = 0;

    virtual void resize(const int fb_width, const int fb_height) = 0;

    virtual void init_ui_frame() = 0;
    virtual void new_frame() = 0;

    virtual void display(const std::vector<uint32_t> &img) = 0;
    virtual void display(RenderGraphic *renderer);

    virtual CommandStream* stream() { return nullptr; }

private:
    // on-demand contains persistently allocated intermediate storage
    std::vector<uint32_t> framebuffer;
};

struct GLFWwindow;
typedef Display* (*create_display_function)(GLFWwindow *window, const char *device_override);

Display* create_opengl_display(GLFWwindow *window, const char *device_override = nullptr);
#ifdef ENABLE_VULKAN
Display* create_vulkan_display(GLFWwindow *window, const char *device_override = nullptr);
#endif
