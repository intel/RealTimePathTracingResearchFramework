// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

// Includes
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_utils.h>
#include "util/display/render_graphic.h"

namespace vkrt {

enum class TimeStamps : int
{
    Begin,
    End,
    Count
};

enum class ProfilingMarker : int {
    Unknown = 0,

    LODComputePrepass,

    Animation,

    // RTAS
    BuildBLAS,
    UpdateBLAS,
    BuildTLAS,
    UpdateTLAS,

    // Rendering
    Rendering,

    // Frame processing
    Processing,

    // ReStir
    ReStirTotal,
    ReStirNewSamples,
    ReStirTemporalResampling,
    ReStirSpatialResampling,
    ReStirFinalShade,
    ReStirCombine,

    // Denoising
    Denoise,

    // DOF markers
    DepthOfField,
    DOFSetup,
    DOFTileFlatten,
    DOFTileDilate,
    DOFIndirectClear,
    DOFTileClassification,
    DOFTemporalStabilization,
    DOFPrefilterBackground,
    DOFMipBuild,
    DOFGatherBackground,
    DOFMedianBackground,
    DOFCombineBackground,
    DOFPrefilterForeground,
    DOFGatherForeground,
    DOFMedianForeground,
    DOFCombineForeground,

    // Post Process
    PostProcess,

    // TAA
    TAA,
    Count
};

// note: order has to match numbers!
#define PROFILING_MARKER_NAMES \
    "UNKNOWN", \
    "LOD_COMPUTE_PREPASS", \
    "ANIMATION", \
\
    "BUILD_BLAS", \
    "UPDATE_BLAS", \
    "BUILD_TLAS", \
    "UPDATE_TLAS", \
\
    "RENDERING", \
\
    "PROCESSING", \
\
    "RESTIR_TOTAL", \
    "RESTIR_NEW_SAMPLES", \
    "RESTIR_TEMPORAL_RESAMPLING", \
    "RESTIR_SPATIAL_RESAMPLING", \
    "RESTIR_FINAL_SHADE", \
    "RESTIR_COMBINE", \
\
    "DENOISE", \
\
    "DEPTH_OF_FIELD", \
    "DOF_SETUP", \
    "DOF_TILE_FLATTEN", \
    "DOF_TILE_DILATE", \
    "DOF_INDIRECT_CLEAR", \
    "DOF_TILE_CLASSIFICATION", \
    "DOF_TEMPORAL_STABILIZATION", \
    "DOF_PREFILTER_BACKGROUND", \
    "DOF_MIP_BUILD", \
    "DOF_GATHER_BACKGROUND", \
    "DOF_MEDIAN_BACKGROUND", \
    "DOF_COMBINE_BACKGROUND", \
    "DOF_PREFILTER_FOREGROUND", \
    "DOF_GATHER_FOREGROUND", \
    "DOF_MEDIAN_FOREGROUND", \
    "DOF_COMBINE_FOREGROUND", \
\
    "POST_PROCESS", \
\
    "TAA"

// The number of markers that can fit per pool
const uint32_t markers_per_query_pool = 16;

// The number of queries that are required to fit the number of markers in a given pool
const uint32_t profiling_marker_pool_size = markers_per_query_pool * (uint32_t)TimeStamps::Count;

// Function that returns the name of a marker as a string
const char *get_profiling_marker_name(ProfilingMarker marker);

// Function that tells if a marker is a detailed marker
bool is_detailed_marker(ProfilingMarker marker);

// Descriptor for a given query
struct ProfilingMakerDescriptor
{
    // Tracker of the pool where the marker is
    uint32_t pool_idx;

    // Index of the marker within the pool
    uint32_t local_idx;
};

// Structure that holds all the profiling data for a swap
struct ProfilingQueries
{
    // Attribute that tells us what is the next available query
    ProfilingMakerDescriptor next_available_query;

    // The set of query pools that have been allocated to far
    std::vector<VkQueryPool> time_stamp_query_pools;

    // Structure to track the set of queries for each markers
    std::vector<ProfilingMarker> markers_type;
};

// Structure that holds the profiling results for the current frame
struct ProfilingResults
{
    // Samp start for each marker
    uint64_t time_stamp_begin[(int) ProfilingMarker::Count];
    // Samp end for each marker
    uint64_t time_stamp_end[(int)ProfilingMarker::Count];
    // Duration for each marker
    double duration_ms[(int) ProfilingMarker::Count];
    // Frame span
    double max_span_ms;
};

struct ProfilingData
{
    Device device;
    ProfilingQueries profiling_queries[RenderGraphic::MAX_SWAP_BUFFERS];

    std::unique_ptr<ProfilingResults> results;

    ProfilingData(Device& dev);
    ProfilingData(ProfilingData const&) = delete;
    ProfilingData& operator=(ProfilingData const&) = delete;
    ~ProfilingData();

    // Cst & Dst
    void initialize_queries();
    void destroy_queries();

    // Grab the next available query
    ProfilingMakerDescriptor get_next_available_query(int swap_index);

    // Start and end a query
    ProfilingMakerDescriptor start_timing(VkCommandBuffer cmd_stream, ProfilingMarker marker, int swap_index);
    void end_timing(VkCommandBuffer cmd_stream, ProfilingMakerDescriptor pmd, int swap_index);

    // Evaluate the timings for all queries
    void evaluate_queries(int swap_index);

    // Reset times
    void reset_queries(int swap_index);
    void reset_all_queries();
};

} // namespace
