// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "shell.h"
#include "util/display/imgui_backend.h"
#include <GLFW/glfw3.h>

#include "util/display/display.h"
#include "librender/render_backend.h"

#include "profiling.h"
#include <cassert>
#include <cstdio>

void Shell::initialize_display_and_renderer(RenderBackend* backend) {
    this->renderer = backend;

    if (cmdline_args.fixed_resolution_x || cmdline_args.fixed_resolution_y) {
        display_width = cmdline_args.fixed_resolution_x;
        display_height = cmdline_args.fixed_resolution_y;
    }
    else {
        display_width = win_width;
        display_height = win_height;
    }
    render_width = display_width / render_upscale_factor;
    render_height = display_height / render_upscale_factor;

    display->resize(win_width, win_height);
    if (!delay_initialization)
        renderer->initialize(render_width, render_height);
    this->was_reset = true;
}

void Shell::initialize_renderer_extension(RenderExtension* render_extension) {
    this->renderer_extensions.push_back(render_extension);
    this->downscaled_rendering_extensions.push_back(render_extension);

    if (!delay_initialization)
        render_extension->initialize(render_width, render_height);
}

void Shell::initialize_upscaled_processing_extension(RenderExtension* render_extension) {
    this->renderer_extensions.push_back(render_extension);
    this->upscaled_processing_extensions.push_back(render_extension);

    if (!delay_initialization)
        render_extension->initialize(display_width, display_height);
}

void Shell::reinitialize_renderer_and_extensions(bool display_resize) {
    if (display_resize)
        println(CLL::VERBOSE, "Resizing shell to (%d, %d)", win_width, win_height);

    // compute new render & upscaled display resolutions
    bool render_resolution_changed = false;
    if (!cmdline_args.fixed_resolution_x && !cmdline_args.fixed_resolution_y) {
        render_resolution_changed = (display_width != win_width) || (display_height != win_height);
        display_width = win_width;
        display_height = win_height;
    }
    render_resolution_changed |= (render_width != display_width / render_upscale_factor)
        || (render_height != display_height / render_upscale_factor);
    render_width = display_width / render_upscale_factor;
    render_height = display_height / render_upscale_factor;

    // release outdated display resources
    if (render_resolution_changed) {
        println(CLL::VERBOSE, "Resizing renderer to (%d, %d)", render_width, render_height);
        for (size_t i = renderer_extensions.size(); i-- != 0; ) {
            renderer_extensions[i]->release_mapped_display_resources();
        }
    }
    // resize display if required
    if (display_resize) {
        if (display)
            display->resize(win_width, win_height);
        else
            warning("Spontaneous out-of-order resize event");
    }
    // reinitialize renderer and extensions with updated resolutions
    if (render_resolution_changed || !display_resize) {
        if (renderer) {
            renderer->initialize(render_width, render_height);
            this->was_reset = true;
        }
        for (size_t i = 0, ie = downscaled_rendering_extensions.size(); i != ie; ++i) {
            downscaled_rendering_extensions[i]->initialize(render_width, render_height);
        }
        for (size_t i = 0, ie = upscaled_processing_extensions.size(); i != ie; ++i) {
            upscaled_processing_extensions[i]->initialize(display_width, display_height);
        }
    }
}

void Shell::set_scene(Scene const& scene) {
    if (!this->renderer)
        throw_error("No renderer created");

    if (this->renderer->unique_scene_id) {
        ProfilingScope profile_upload("Scene Unmap");
        for (size_t i = renderer_extensions.size(); i-- != 0; ) {
            renderer_extensions[i]->release_mapped_scene_resources(&scene);
        }
    }

    {
        ProfilingScope profile_upload("Scene Upload");
        renderer->set_scene(scene);
    }

    {
        ProfilingScope profile_upload("Scene Extensions");
        for (size_t i = 0, ie = renderer_extensions.size(); i != ie; ++i) {
            BasicProfilingScope profile_ext(nullptr);
            renderer_extensions[i]->update_scene_from_backend(scene);
            profile_ext.end();
            register_profiling_time(-1, renderer_extensions[i]->name().c_str(), profile_ext.elapsedNS());
        }
    }

    if (!this->renderer->unique_scene_id) {
        this->renderer->unique_scene_id = ~0;
        warning("Old backend does not track correct scene ID");
    }
}

void Shell::new_frame() {
    ImGui_ImplGlfw_NewFrame();
    display->init_ui_frame();
    ImGui::NewFrame();
}

namespace {
    void shell_windowposfun(GLFWwindow* window, int xpos, int ypos) {
        shell.win_x = xpos;
        shell.win_y = ypos;
    }
    void shell_windowsizefun(GLFWwindow* window, int win_width, int win_height) {
        shell.win_width = win_width;
        shell.win_height = win_height;

        shell.reinitialize_renderer_and_extensions(true);

        auto& io = ImGui::GetIO();
        io.DisplaySize.x = win_width;
        io.DisplaySize.y = win_height;
    }
    void shell_windowmaximizefun(GLFWwindow* window, int maximized) {
        shell.win_maximized = (maximized != 0);
    }
    void shell_imgui_character_fun(GLFWwindow* window, unsigned int c) {
        ImGuiIO& io = ImGui::GetIO();
        // only treat input as characters when keyboard input is requested (avoid lag on camera movement)
        if (ImGui::GetIO().WantCaptureKeyboard)
            ImGui_ImplGlfw_CharCallback(window, c);
    }
}
void Shell::setup_event_handlers() {
    glfwSetWindowPosCallback(window, shell_windowposfun);
    glfwSetFramebufferSizeCallback(window, shell_windowsizefun);
    glfwSetWindowMaximizeCallback(window, shell_windowmaximizefun);
}

void Shell::gui_init_events() {
    glfwSetCharCallback(this->window, &shell_imgui_character_fun);
    this->poll_event(nullptr); // catch initial resize events
}

bool Shell::poll_event(Event* event) {
    glfwPollEvents();
    if (glfwWindowShouldClose(window))
        wants_quit = true;
    return false;
}
// note: GLFW does not have explicit event looping, therefore this is not used currently
bool Shell::handle_event(Event const& event) {
    return false;
}

void Shell::pad_frame_time(unsigned int minMilliseconds) {
    ImGui_Backend_PadFrame(window, minMilliseconds);
}

double Shell::get_time() const
{
    return glfwGetTime();
}
