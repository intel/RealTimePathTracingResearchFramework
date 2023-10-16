// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "imgui.h"
#include "imstate.h"
#include "imutils.h"
#include "librender/render_backend.h"
#include "shell.h"
#include "util.h"
#include "benchmark_info.h"
#include <memory>
#include <vector>

struct BasicApplicationState {
    int active_backend_variant = 0;
    std::vector<std::string> renderer_variants;
    std::vector<std::string> renderer_variants_pretty;
    std::vector<char> renderer_variants_support;

    int target_spp = -1;
    int accumulated_spp = 0;

    double last_real_time = -1.0f;
    float delta_real_time = 0.0f;

    float delta_time = 0.0f;
    double last_time = 0;
    double current_time = 0; // May or may not be real-time depending on the mode.

    bool pause_rendering = false;
    bool continuous_restart = false;
    bool done_accumulating = false;
    bool frame_ready = false;
    bool enable_denoising = true;

    bool renderer_changed = false;

    bool freeze_frame = false;
    bool synchronous_rendering = false;

    bool validation_mode = false;
    std::string validation_img_prefix;

    bool profiling_mode = false;
    std::string profiling_img_prefix;
    std::string profiling_csv_prefix;
    float profiling_delta_time = 1.f/60.f;

    bool data_capture_mode = false;
    float data_capture_delta_time = 1.f/60.f;
    DataCaptureConfig data_capture;

    // Set to true when we are done rendering.
    bool done = false;

    OutputImageFormat framebuffer_format = OUTPUT_IMAGE_FORMAT_EXR;

    // Change tracking. This can be used to relaunch the app
    // when it is recompiled.
    double change_tracking_last_check = 0.0f;
    unsigned long long change_tracking_timestamp = 0;
    bool tracked_file_has_changed = false;
    const char *change_tracking_file;

    bool interactive() const {
        return !validation_mode && !profiling_mode && !data_capture_mode;
    }

    // The main state update, used both for serialization and UI.
    bool state(RenderBackend* renderer);

    int add_variants(RenderBackend* renderer);

    void begin_after_initialization(
        const Shell::DefaultArgs &config_args,
        // Relaunch app if this file changes. Can be nullptr.
        char const *change_tracking_file);

    bool request_new_frame();
    void progress_time();
    void handle_shell_updates(Shell& shell);
    void reset_render();
    void handle_mode_actions(const Shell &shell, RenderBackend* renderer);

    bool needs_rerender() const { return renderer_changed || (!pause_rendering && continuous_restart && done_accumulating); }
    bool needs_render() const { return !pause_rendering && !done_accumulating; }

    int next_frame_spp(int max_spp) const {
        if (target_spp > 0 && accumulated_spp > target_spp - max_spp)
            return target_spp - accumulated_spp;
        return max_spp;
    }
    void update_accumulated_spp(int new_spp, bool moving_average) {
        accumulated_spp = new_spp;
        done_accumulating = target_spp == 0 || target_spp > 0 && accumulated_spp >= target_spp && (!moving_average || continuous_restart);
        frame_ready = accumulated_spp > 0 && (done_accumulating || moving_average);
    }

    // Save the given framebuffer with the command line provided file format.
    bool save_framebuffer(const char *prefix, RenderBackend *renderer);

    private:
        void track_file_change(const Shell &shell);
        bool save_framebuffer(const char *prefix, RenderBackend *renderer,
            ExrCompression compression);
        bool save_framebuffer_png(const char *prefix, RenderBackend *renderer);
        bool save_framebuffer_pfm(const char *prefix, RenderBackend *renderer);
        bool save_framebuffer_exr(const char *prefix, RenderBackend *renderer,
                ExrCompression compression);
        bool save_aov_exr(const char *prefix, RenderBackend *renderer,
                RenderGraphic::AOVBufferIndex aovIndex,
                ExrCompression compression);

        std::vector<float>         readback_buffer_float;
        std::vector<uint16_t>      readback_buffer_half;
        std::vector<unsigned char> readback_buffer_byte;
};
