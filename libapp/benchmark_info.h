// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "util/util.h"
#include "util/online_stats.h"
#include <string>
#include <memory>
#include <iosfwd>

/*
 * Stores progressive benchmark info in a .csv file.
 */
class BenchmarkInfoFile
{
    public:
        BenchmarkInfoFile(const std::string &fname);
        BenchmarkInfoFile(BenchmarkInfoFile const&) = delete;
        BenchmarkInfoFile& operator=(BenchmarkInfoFile const&) = delete;
        ~BenchmarkInfoFile();

    private:
        friend struct BenchmarkInfo;
        std::unique_ptr<std::ofstream> f;
};

// interface for providing extended benchmark metrics
struct BenchmarkCSVSource {
    virtual ~BenchmarkCSVSource() {}
    
    // allows recording custom measurements into CSV. The vector 'col_names' may contain existing elements that should be unchanged
    virtual bool profiling_csv_declare_column_names(std::vector<std::string>& col_names) const = 0; 
    // returns the number of written values. Extensions must make sure the number matches the declared columns
    virtual int write_profiling_csv_report_frame_values(float *values) const = 0;
};

struct BenchmarkInfo {
    std::string rt_backend; // = renderer->name();
    std::string cpu_brand = get_cpu_brand();
    std::string gpu_brand; // = display->gpu_brand();
    std::string display_frontend; // = display->name();

    size_t frames_total {0};
    size_t frames_accumulated {0};

    OnlineStats<float> render_time;
    OnlineStats<float> app_time;
    //OnlineStats<float> rays_per_second; // TODO (we currently get no data back).

    std::vector<const BenchmarkCSVSource *> extended_benchmark_sources;
    std::vector<std::string> extended_benchmark_column_names;
    std::vector<float> extended_banchmark_frame_values; // since a CSV row is written in each frame, enough to remmember only the most recent values
    
    void aggregate_frame(float frame_render_time, float frame_app_time);
    void reset();
    void ui() const;

    // Must be called before open_csv()
    void register_extended_benchmark_csv_source(const BenchmarkCSVSource* source);
    
    void open_csv(const std::string &fname);
    void write_csv();

    private:
        std::unique_ptr<BenchmarkInfoFile> csv;
};
