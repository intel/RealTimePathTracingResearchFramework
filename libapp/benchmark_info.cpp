// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "benchmark_info.h"
#include "librender/render_backend.h"
#include "util/display/display.h"
#include "util/error_io.h"
#include "imstate.h"
#include "imgui.h"
#include <fstream>

void BenchmarkInfo::aggregate_frame(float frame_render_time,
    float frame_app_time)
{
    ++frames_total;
    ++frames_accumulated;

    /* The render time lags by a few frames, and it's negative if no result
     * is available yet. */
    if (frame_render_time > 0)
        render_time.update(frame_render_time);

    app_time.update(frame_app_time);
    //rays_per_second.update(frame_rays_per_second);
}

void BenchmarkInfo::reset()
{
    frames_accumulated = 0;
    render_time = OnlineStats<float>{};
    app_time = OnlineStats<float>{};
    //rays_per_second = OnlineStats<float>{};
}

void BenchmarkInfo::ui() const
{
    ImGui::Text("Render Time: %6.3f ms/frame [mean %6.3f sd %6.3f], %4.1f FPS",
                render_time.exponential_moving_average,
                render_time.sample_mean,
                render_time.sample_stddev,
                1000.f / render_time.exponential_moving_average);


    if (app_time.exponential_moving_average > 0) {
        ImGui::Text("Total Application Time: %6.3f ms/frame [mean %6.3f sd %6.3f], %4.1f FPS",
                    app_time.exponential_moving_average,
                    app_time.sample_mean,
                    app_time.sample_stddev,
                    1000.f / app_time.exponential_moving_average);
    } else {
        const float framerate = static_cast<float>(ImGui::GetIO().Framerate);
        ImGui::Text("Total Application Time: %6.3f ms/frame, %4.1f FPS",
                        1000.f / framerate, framerate);
    }

    ImGui::Text("RT Backend: %s", rt_backend.c_str());
    ImGui::Text("CPU: %s", cpu_brand.c_str());
    ImGui::Text("GPU: %s", gpu_brand.c_str());
    ImGui::Text("Display Frontend: %s", display_frontend.c_str());
}

void BenchmarkInfo::register_extended_benchmark_csv_source(const BenchmarkCSVSource *source)
{
    // allow extension to (optionally) declare custom benchmark CSV columns
    if (source->profiling_csv_declare_column_names(extended_benchmark_column_names))
        extended_benchmark_sources.push_back(source);
}

void BenchmarkInfo::open_csv(const std::string &fname)
{    
    csv = std::make_unique<BenchmarkInfoFile>(fname);

    // Write standard header
    *(csv->f) << "frames_total"
              << ",keyframe"
              << ",frames_accumulated"
              << ",render_time_ms"
              << ",app_time_ms";

    // Write extended header
    for (const std::string &col_name : extended_benchmark_column_names) {
        *(csv->f) << "," << col_name;
    }
    *(csv->f) << std::endl;

    // also allocate a buffer to store per-frame extended benchmark values
    extended_banchmark_frame_values.resize(extended_benchmark_column_names.size(), 0);
}

BenchmarkInfoFile::BenchmarkInfoFile(const std::string &fname)
{
    f = std::make_unique<std::ofstream>(fname, std::ios::out | std::ios::trunc);
    if (!f || !f.get()) {
        throw_error("Failed to open benchmark info file %s", fname.c_str());
    }
    println(CLL::INFORMATION, "Writing benchmark data to %s", fname.c_str());
}

BenchmarkInfoFile::~BenchmarkInfoFile() = default;

void BenchmarkInfo::write_csv()
{
    if (csv) {
        *(csv->f) << frames_total
           << "," << (ImState::CurrentKeyframe()+1)
           << "," << frames_accumulated
           << "," << render_time.current_sample
           << "," << app_time.current_sample;

        // collect extended values
        int value_offset = 0;
        for (const BenchmarkCSVSource *source : extended_benchmark_sources) {
            value_offset += source->write_profiling_csv_report_frame_values(&extended_banchmark_frame_values[value_offset]);
        }

        // Write extended values
        for (const float& val : extended_banchmark_frame_values) {
            *(csv->f) << "," << val;
        }
        *(csv->f) << std::endl;

    }

}
