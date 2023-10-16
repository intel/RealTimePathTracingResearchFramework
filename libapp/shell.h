// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <vector>
#include "error_io.h"
#include "util/display/render_graphic.h"
#include "util/write_image.h"
#include "util.h"

#include "imgui.h"
#include "imstate.h"

struct GLFWwindow;

struct Display;
struct RenderBackend;
struct Scene;
struct RenderExtension;
struct RenderGraphic;

static const unsigned GLFW_WINDOWPOS_CENTERED = 0x2FFF0000u;

struct DataCaptureConfig {
    bool data_capture_mode { false };

    std::string img_prefix;
    float fps { 60.f };
    int target_spp { 1 };
    bool rgba { true };
    bool albedo_roughness { true };
    bool normal_depth { true };
    bool motion { true };
};

struct Shell {
    int win_x = int(GLFW_WINDOWPOS_CENTERED);
    int win_y = int(GLFW_WINDOWPOS_CENTERED);
    int win_width { 0 };
    int win_height { 0 };
    bool win_maximized = false;

    Display* display = nullptr;
    GLFWwindow *window = nullptr;

    bool wants_quit = false;
    bool was_reset = false;

    struct DefaultArgs {
        // todo: deduplicate with ProgramArgs, just inline program args here ...
        std::string renderer;
        std::vector<std::string> scene_files;
        bool got_camera_args = false;
        glm::vec3 eye = glm::vec3(0, 2, 5);
        glm::vec3 center = glm::vec3(0);
        glm::vec3 up = glm::vec3(0, 1, 0);
        float fov_y = 65.f;
        size_t camera_id = 0;

        bool disable_ui = false;
        bool freeze_frame = false;
        bool deduplicate_scene = false;

        int fixed_resolution_x = 0;
        int fixed_resolution_y = 0;
        int fixed_upscale_factor = 0;

        OutputImageFormat image_format { OUTPUT_IMAGE_FORMAT_EXR };

        // NOTE: Validation mode and profiling mode are mutually exclusive!

        // Validation mode is for generating images from the command line.
        bool validation_mode = false;
        std::string validation_img_prefix;
        int validation_target_spp = -1;

        // Profiling mode is for wedging parameters and obtaining performance
        // output.
        bool profiling_mode = false;
        std::string profiling_csv_prefix;
        std::string profiling_img_prefix;
        float profiling_fps = 60.f;

        // Data capture mode is designed for generating training data for
        // denoisers.
        // This is similar to profiling mode, but doesn't output stats.
        bool data_capture_mode = false;
        DataCaptureConfig data_capture;

        // Data that can be required by extensions (post process for instance
        std::string resource_dir = rooted_path("resources");
    };

    DefaultArgs cmdline_args;
    int render_width = 0, display_width = 0;
    int render_height = 0, display_height = 0;
    int render_upscale_factor = 1;
    bool delay_initialization = false;
    RenderBackend* renderer = nullptr;
    std::vector<RenderExtension*> renderer_extensions;
    std::vector<RenderExtension*> downscaled_rendering_extensions;
    std::vector<RenderExtension*> upscaled_processing_extensions;

    static std::unique_ptr<RenderBackend> create_standard_renderer(std::string const& name, Display* display);

    void initialize_display_and_renderer(RenderBackend* backend);
    void initialize_renderer_extension(RenderExtension* render_extensions);
    void initialize_upscaled_processing_extension(RenderExtension* render_extensions);
    void reinitialize_renderer_and_extensions(bool display_resize = false);

    void set_scene(Scene const& scene);

    glm::vec2 transform_mouse(glm::vec2 in) const {
        return glm::vec2(in.x * 2.f / float(win_width) - 1.f, 1.f - 2.f * in.y / float(win_height));
    }

    void setup_event_handlers();
    typedef struct { } Event;
    void gui_init_events();
    bool poll_event(Event* event);
    bool handle_event(Event const& event);
    void new_frame();

    void pad_frame_time(unsigned int minMilliseconds);

    void readwrite_window_state();

    double get_time() const;
};

extern Shell shell;

namespace command_line { struct ProgramArgs; }
bool run_app(std::vector<std::string> const& vargs);
