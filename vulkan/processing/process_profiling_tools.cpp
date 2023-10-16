// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "../render_vulkan.h"
#include "process_profiling_tools.h"

#include "profiling.h"
#include "types.h"
#include "util.h"
#include "../resource_utils.h"

// ImGUI
#include "imgui.h"
#include "imstate.h"

#include <algorithm>
#include <numeric>
#include <math.h>

#define HASH_COLOR_SEED 666

using vkrt::ProfilingMarker;

template <>
std::unique_ptr<RenderExtension> create_render_extension<ProcessProfilingToolsVulkan>(
    RenderBackend *backend)
{
    return std::unique_ptr<RenderExtension>(
        new ProcessProfilingToolsVulkan(dynamic_cast<RenderVulkan *>(backend)));
}

ProcessProfilingToolsVulkan::ProcessProfilingToolsVulkan(RenderVulkan *backend)
    : device(backend->device), backend(backend), tracked_frames(0)
{
    // Initialize the markers
    for (uint32_t frame_idx = 0; frame_idx < stabilization_frames; ++frame_idx)
        memset(profiling_timings_ms[frame_idx], 0, sizeof(float) * (uint32_t)ProfilingMarker::Count);
}

ProcessProfilingToolsVulkan::~ProcessProfilingToolsVulkan()
{
}

std::string ProcessProfilingToolsVulkan::name() const
{
    return "Profiling Tools Extension";
}

void ProcessProfilingToolsVulkan::initialize(const int fb_width, const int fb_height)
{
}

void ProcessProfilingToolsVulkan::preprocess(CommandStream *cmd_stream, int variant_idx)
{
}

void ProcessProfilingToolsVulkan::update_scene_from_backend(const Scene &scene)
{
}

// processing pipeline
void ProcessProfilingToolsVulkan::register_custom_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const &options) const
{
}

// processing pipeline
void ProcessProfilingToolsVulkan::update_custom_shader_descriptor_table(
    vkrt::BindingCollector collector,
    vkrt::RenderPipelineOptions const &options,
    VkDescriptorSet desc_set)
{
}

void ProcessProfilingToolsVulkan::process(CommandStream *cmd_stream_, int variant_idx)
{
    if (params.pauseCapture)
        return;

    if (backend->reset_accumulation)
        tracked_frames = 0;

    vkrt::ProfilingResults* pd = backend->profiling_data.results.get();

    // Add the current frame results to the buffers
    uint32_t stab_frame = uint32_t(tracked_frames % stabilization_frames);
    for (uint32_t marker_idx = 0; marker_idx < (uint32_t)ProfilingMarker::Count; ++marker_idx)
    {
        profiling_timings_ms[stab_frame][marker_idx] = (float)pd->duration_ms[marker_idx];
        profiling_stamp_begin[stab_frame][marker_idx] = pd->time_stamp_begin[marker_idx];
        profiling_stamp_end[stab_frame][marker_idx] = pd->time_stamp_end[marker_idx];
    }
    tracked_frames++;

    // Average the previously existing frames
    uint32_t available_stab_frames = (uint32_t) std::min(tracked_frames, size_t(stabilization_frames));
    uint32_t stab_window_begin = (stab_frame + stabilization_frames + 1 - available_stab_frames) % stabilization_frames;
    for (uint32_t marker_idx = 0; marker_idx < (uint32_t)ProfilingMarker::Count; ++marker_idx)
    {
        size_t avg_begin = 0;
        size_t avg_end = 0;
        double avg_ms = 0.0;
        double min_ms = FLT_MAX;
        double max_ms = 0.0;
        uint32_t active_frames = 0;
        for (uint32_t i = 0; i < available_stab_frames; ++i)
        {
            uint32_t stab_frame = (stab_window_begin + i) % stabilization_frames;
            double d = profiling_timings_ms[stab_frame][marker_idx];
            if (d > 0.0)
            {
                avg_ms += d;
                min_ms = min(d, min_ms);
                max_ms = max(d, max_ms);
                avg_begin += profiling_stamp_begin[stab_frame][marker_idx];
                avg_end += profiling_stamp_end[stab_frame][marker_idx];
                ++active_frames;
            }
            // Reset average every time the timing was skipped / renderer composition changed
            else {
                avg_ms = 0.0;
                min_ms = FLT_MAX;
                max_ms = 0.0;
                avg_begin = 0;
                avg_end = 0;
                active_frames = 0;
            }
        }

        // Compute the basic metrics
        double frame_div = std::max((double)active_frames, 1e-5);
        double final_avg = avg_ms / frame_div;
        profiling_stamp_begin_avg[marker_idx] = size_t(avg_begin / frame_div);
        profiling_stamp_end_avg[marker_idx] = size_t(avg_end / frame_div);
        profiling_timings_avg_ms[marker_idx] = float(final_avg);
        profiling_timings_min_ms[marker_idx] = float(min_ms);
        profiling_timings_max_ms[marker_idx] = float(max_ms);

        // Compute the advanced metrics
        if (params.advancedMetrics)
        {
            // Loop a second time to get the standard deviation
            double std_dev_ms = 0.0;
            for (uint32_t i = 0; i < available_stab_frames; ++i) {
                uint32_t stab_frame = (stab_window_begin + i) % stabilization_frames;
                double d = profiling_timings_ms[stab_frame][marker_idx];
                if (d > 0.0)
                {
                    float dist2 = (d - final_avg);
                    std_dev_ms += (dist2 * dist2);
                }
                else
                {
                    std_dev_ms = 0.0;
                }
            }
            profiling_timings_stddev_ms[marker_idx] = float(sqrt(std_dev_ms / frame_div));
        }
    }

    if (params.graphView)
    {
        uint32_t current_frame = 0;
        for (uint32_t i = 0; i < available_stab_frames; ++i)
        {
            uint32_t stab_frame = (stab_window_begin + i) % stabilization_frames;
            float d = profiling_timings_ms[stab_frame][params.targetMarker];
            if (d > 0.0)
            {
                profiling_timings_raw_ms[current_frame] = d;
                current_frame++;
            }
        }
    }
}

void ProcessProfilingToolsVulkan::load_resources(const std::string &resource_dir)
{
}

// A single iteration of Bob Jenkins' One-At-A-Time hashing algorithm.
uint32_t JenkinsHash(uint32_t x)
{
    x += (x << 10u);
    x ^= (x >> 6u);
    x += (x << 3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

glm::vec3 IntToColor(uint32_t val)
{
    int r = (val & 0xFF0000) >> 16;
    int g = (val & 0x00FF00) >> 8;
    int b = val & 0x0000FF;
    return glm::vec3(r / 255.0, g / 255.0, b / 255.0);
}

bool ProcessProfilingToolsVulkan::ui_and_state(bool &renderer_changed)
{
    if (!IMGUI_VOLATILE_HEADER(ImGui::Begin, "Profiling Tools")) {
        IMGUI_VOLATILE(ImGui::End());
        return false;
    }

    // Don't evaluate the timeine if not needed
    IMGUI_STATE(ImGui::Checkbox, "Enable Timeline", &params.enableTimeline);
    IMGUI_STATE(ImGui::Checkbox, "Pause Capture", &params.pauseCapture);
    IMGUI_STATE(ImGui::Checkbox, "Advanced Metrics", &params.advancedMetrics);

    // First we'll compute the frame duration
    vkrt::ProfilingResults *pd = backend->profiling_data.results.get();
    double frame_time_ms = pd->max_span_ms;
    bool valid_frame = std::isfinite(frame_time_ms);

    // We only operate in default mode
    if (ImState::InDefaultMode() && valid_frame && params.enableTimeline)
    {
        IMGUI_STATE(ImGui::Checkbox, "Detailed View", &params.detailedView);

        uint64_t frame_start = uint64_t(~0);
        uint64_t frame_end = 0;
        for (uint32_t marker_idx = 0; marker_idx < (uint32_t)ProfilingMarker::Count; ++marker_idx)
        {
            if (profiling_timings_avg_ms[marker_idx] == 0.0)
                continue;
            frame_start = std::min(profiling_stamp_begin_avg[marker_idx], frame_start);
            frame_end = std::max(profiling_stamp_end_avg[marker_idx], frame_end);
        }

        // Grab the draw list
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const uint32_t square_height = 4;
        const ImVec2 window_size = ImGui::GetWindowSize();
        const uint32_t frame_pixel_size = window_size.x - square_height;
        const size_t frame_duration = frame_end - frame_start;

        // Display all the makers
        for (uint32_t marker_idx = 0; marker_idx < (uint32_t)ProfilingMarker::Count; ++marker_idx)
        {
            float timings_ms = profiling_timings_avg_ms[marker_idx];

            if (timings_ms == 0.0)
                continue;

            if (!params.detailedView && vkrt::is_detailed_marker((ProfilingMarker)marker_idx))
                continue;

            float min_timing_ms = profiling_timings_min_ms[marker_idx];
            float max_timing_ms = profiling_timings_max_ms[marker_idx];
            float stddev_timing_ms = profiling_timings_stddev_ms[marker_idx];

            // Draw the duration of the marker
            if (params.advancedMetrics)
                ImGui::Text("%s %6.3f ms (min: %6.3f ms, max: %6.3f ms, stddev: %6.3fms)", get_profiling_marker_name((ProfilingMarker)marker_idx), timings_ms, min_timing_ms, max_timing_ms, stddev_timing_ms);
            else
                ImGui::Text("%s %6.3f ms", get_profiling_marker_name((ProfilingMarker)marker_idx), timings_ms);
            const ImVec2 p = ImGui::GetCursorScreenPos();

            uint64_t marker_start = profiling_stamp_begin_avg[marker_idx];
            uint64_t marker_end = profiling_stamp_end_avg[marker_idx];
            if (marker_start != marker_end)
            {
                float start = float(marker_start - frame_start) / float(frame_duration);
                float duration = float(marker_end - marker_start) / float(frame_duration);

                // Generate the color that shall be used
                glm::vec3 color = IntToColor(JenkinsHash(marker_idx + HASH_COLOR_SEED));
                const ImU32 col32 = ImColor(color.x, color.y, color.z, 1.0f);

                // Draw the rectangle
                draw_list->AddRectFilled(ImVec2(p.x + start * frame_pixel_size, p.y), ImVec2(p.x + frame_pixel_size * (start + duration), p.y + square_height), col32, 10.0f);
            }
            // Spacing
            ImGui::Dummy(ImVec2(square_height, square_height));
        }

        IMGUI_STATE(ImGui::Checkbox, "Graph View", &params.graphView);
        if (params.graphView)
        {
            static constexpr const char *marker_names[] = {PROFILING_MARKER_NAMES};

            int &op = params.targetMarker;
            const int last_active = std::max(std::min(op, (int)ProfilingMarker::Count - 1), 0);

            if (IMGUI_STATE_BEGIN_ATOMIC_COMBO(ImGui::BeginCombo,
                                               "marker",
                                               marker_names,
                                               marker_names[last_active])) {
                for (int i = 0; i < (int)ProfilingMarker::Count; ++i)
                {
                    if (IMGUI_STATE(ImGui::Selectable, marker_names[i], i == last_active))
                        op = i;
                }
                IMGUI_STATE_END(ImGui::EndCombo, marker_names);
            }

            ImGui::PlotHistogram(
                "Timing History",
                profiling_timings_raw_ms,
                stabilization_frames,
                0,
                get_profiling_marker_name((ProfilingMarker)params.targetMarker),
                0.0,
                profiling_timings_max_ms[params.targetMarker] * 1.5,
                ImVec2(0, 128));
        }
    }



    IMGUI_VOLATILE(ImGui::End());
    return false;
}

bool ProcessProfilingToolsVulkan::profiling_csv_declare_column_names(std::vector<std::string> &col_names) const
{
    for (uint32_t marker_idx = 0; marker_idx < (uint32_t)ProfilingMarker::Count; ++marker_idx) {
        col_names.push_back(get_profiling_marker_name((ProfilingMarker)marker_idx));
    }

    return true;
}

int ProcessProfilingToolsVulkan::write_profiling_csv_report_frame_values(float *values) const
{
    vkrt::ProfilingResults *pd = backend->profiling_data.results.get();
    for (int marker_idx = 0; marker_idx < (int)ProfilingMarker::Count; ++marker_idx) {
        values[marker_idx] = (float)pd->duration_ms[marker_idx];
    }
    return (int)ProfilingMarker::Count;
}
