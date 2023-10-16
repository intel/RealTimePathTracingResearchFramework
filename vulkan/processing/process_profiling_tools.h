// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "../render_pipeline_vulkan.h"
#include "../libapp/benchmark_info.h"

struct ProfilingToolsParameters {
    bool enableTimeline GLCPP_DEFAULT(= false);
    bool detailedView GLCPP_DEFAULT(= false);
    bool pauseCapture GLCPP_DEFAULT(= false);
    bool advancedMetrics GLCPP_DEFAULT(= false);
    bool graphView GLCPP_DEFAULT(= false);
    int targetMarker GLCPP_DEFAULT(= 0);
};

struct ProcessProfilingToolsVulkan : ProcessingPipelineExtensionVulkan, BenchmarkCSVSource
{
    vkrt::Device device;
    RenderVulkan *backend;

    // UI STATE
    ProfilingToolsParameters params;

    // Internal storage of the stabilization times
    const static uint32_t stabilization_frames = 32;
    float profiling_timings_ms[stabilization_frames][(uint32_t)vkrt::ProfilingMarker::Count];
    uint64_t profiling_stamp_begin[stabilization_frames][(uint32_t)vkrt::ProfilingMarker::Count];
    uint64_t profiling_stamp_end[stabilization_frames][(uint32_t)vkrt::ProfilingMarker::Count];
    uint64_t tracked_frames;

    // Times
    float profiling_timings_avg_ms[(uint32_t)vkrt::ProfilingMarker::Count];
    float profiling_timings_min_ms[(uint32_t)vkrt::ProfilingMarker::Count];
    float profiling_timings_max_ms[(uint32_t)vkrt::ProfilingMarker::Count];
    float profiling_timings_stddev_ms[(uint32_t)vkrt::ProfilingMarker::Count];
    uint64_t profiling_stamp_begin_avg[(uint32_t)vkrt::ProfilingMarker::Count];
    uint64_t profiling_stamp_end_avg[(uint32_t)vkrt::ProfilingMarker::Count];

    // State Tracking for graph
    float profiling_timings_raw_ms[stabilization_frames];

    ProcessProfilingToolsVulkan(RenderVulkan *backend);
    virtual ~ProcessProfilingToolsVulkan();

    std::string name() const override;

    void initialize(const int fb_width, const int fb_height) override;
    void update_scene_from_backend(const Scene& scene) override;

    void preprocess(CommandStream *cmd_stream, int variant_idx = 0) override;
    void register_custom_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const override;
    void update_custom_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set) override;

    void process(CommandStream* cmd_stream, int variant_idx) override;

    void load_resources(const std::string &resource_dir) override;
    bool ui_and_state(bool &renderer_changed) override;

    bool profiling_csv_declare_column_names(std::vector<std::string>& col_names) const override;
    int write_profiling_csv_report_frame_values(float *values) const override;
};
