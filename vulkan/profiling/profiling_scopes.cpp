// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "profiling_scopes.h"
#include "types.h"

namespace vkrt {

const char* get_profiling_marker_name(ProfilingMarker marker)
{
    switch (marker)
    {
        case ProfilingMarker::Animation:
            return "Animation";

        case ProfilingMarker::BuildBLAS:
            return "Build BLAS";
        case ProfilingMarker::UpdateBLAS:
            return "Update BLAS";
        case ProfilingMarker::BuildTLAS:
            return "Build TLAS";
        case ProfilingMarker::UpdateTLAS:
            return "Update TLAS";

        case ProfilingMarker::Rendering:
            return "Rendering";

        case ProfilingMarker::Denoise:
            return "Denoise";

#if defined(ENABLE_POST_PROCESSING)
        case ProfilingMarker::DepthOfField:
            return "DepthOfField";
        case ProfilingMarker::DOFSetup:
            return "\tSetup";
        case ProfilingMarker::DOFTileFlatten:
            return "\tTileFlatten";
        case ProfilingMarker::DOFTileDilate:
            return "\tTileDilate";
        case ProfilingMarker::DOFIndirectClear:
            return "\tIndirectClear";
        case ProfilingMarker::DOFTileClassification:
            return "\tTileClassification";
        case ProfilingMarker::DOFTemporalStabilization:
            return "\tTemporalStabilization";
        case ProfilingMarker::DOFPrefilterBackground:
            return "\tPrefilterBackground";
        case ProfilingMarker::DOFMipBuild:
            return "\tMipBuild";
        case ProfilingMarker::DOFGatherBackground:
            return "\tGatherBackground";
        case ProfilingMarker::DOFMedianBackground:
            return "\tMedianBackground";
        case ProfilingMarker::DOFCombineBackground:
            return "\tCombineBackground";
        case ProfilingMarker::DOFPrefilterForeground:
            return "\tPrefilterForeground";
        case ProfilingMarker::DOFGatherForeground:
            return "\tGatherForeground";
        case ProfilingMarker::DOFMedianForeground:
            return "\tMedianForeground";
        case ProfilingMarker::DOFCombineForeground:
            return "\tCombineForeground";

        case ProfilingMarker::PostProcess:
            return "PostProcess";
#endif
        case ProfilingMarker::Processing:
            return "Processing";

        case ProfilingMarker::TAA:
            return "TAA";
    }
    return "Unknown marker";
}

bool is_detailed_marker(ProfilingMarker marker)
{
    switch (marker)
    {
#if defined(ENABLE_POST_PROCESSING)
        case ProfilingMarker::DOFSetup:
        case ProfilingMarker::DOFTileFlatten:
        case ProfilingMarker::DOFTileDilate:
        case ProfilingMarker::DOFIndirectClear:
        case ProfilingMarker::DOFTileClassification:
        case ProfilingMarker::DOFTemporalStabilization:
        case ProfilingMarker::DOFPrefilterBackground:
        case ProfilingMarker::DOFMipBuild:
        case ProfilingMarker::DOFGatherBackground:
        case ProfilingMarker::DOFMedianBackground:
        case ProfilingMarker::DOFCombineBackground:
        case ProfilingMarker::DOFPrefilterForeground:
        case ProfilingMarker::DOFGatherForeground:
        case ProfilingMarker::DOFMedianForeground:
        case ProfilingMarker::DOFCombineForeground:
            return true;
#endif
        default:
            return false;
    }
}

ProfilingData::ProfilingData(vkrt::Device &dev)
    : device(dev)
    , profiling_queries()
    , results(new ProfilingResults)
{
}

ProfilingData::~ProfilingData() {
    destroy_queries();
}

void ProfilingData::initialize_queries()
{
    for (int swap_idx = 0; swap_idx < RenderGraphic::MAX_SWAP_BUFFERS; ++swap_idx)
    {
        // Get the query structure
        ProfilingQueries& pq = profiling_queries[swap_idx];

        // Initially we only allocate one pool, and will allocate more if needed
        pq.time_stamp_query_pools.resize(1);

        // Reset the next avaialble query
        pq.next_available_query.pool_idx = 0;
        pq.next_available_query.local_idx = 0;
        pq.markers_type.clear();

        // time stamps
        {
            VkQueryPoolCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = profiling_marker_pool_size;
            CHECK_VULKAN(vkCreateQueryPool(device->logical_device(), &info, nullptr, &pq.time_stamp_query_pools[0]));
            vkResetQueryPool(device->logical_device(), pq.time_stamp_query_pools[0], 0, info.queryCount);
        }
    }
}

void ProfilingData::destroy_queries()
{
    for (int swap_idx = 0; swap_idx < RenderGraphic::MAX_SWAP_BUFFERS; ++swap_idx)
    {
        ProfilingQueries& pq = profiling_queries[swap_idx];
        for (int pool_idx = 0; pool_idx < pq.time_stamp_query_pools.size(); ++pool_idx)
        {
            ProfilingQueries& pq = profiling_queries[swap_idx];
            vkDestroyQueryPool(device->logical_device(), pq.time_stamp_query_pools[pool_idx], nullptr);
            pq.time_stamp_query_pools[pool_idx] = VK_NULL_HANDLE;
        }
    }
}

ProfilingMakerDescriptor ProfilingData::get_next_available_query(int swap_index)
{
    // Use the next available query
    ProfilingQueries& pf = profiling_queries[swap_index];
    ProfilingMakerDescriptor desc = pf.next_available_query;

    // Did we reach the end of a pool?
    if (pf.next_available_query.local_idx == profiling_marker_pool_size)
    {
        // Do we need an extra pool?
        if (pf.next_available_query.pool_idx == pf.time_stamp_query_pools.size() - 1)
        {
            // Describe the pool
            VkQueryPoolCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = profiling_marker_pool_size;

            // Create a new query pool
            VkQueryPool new_query_pool;
            CHECK_VULKAN(vkCreateQueryPool(device->logical_device(), &info, nullptr, &new_query_pool));
            vkResetQueryPool(device->logical_device(), new_query_pool, 0, info.queryCount);
            pf.time_stamp_query_pools.push_back(new_query_pool);
        }

        // Append it
        pf.next_available_query.local_idx = 0;
        pf.next_available_query.pool_idx++;
        desc = pf.next_available_query;
    }

    // Reserve the TimeStamps::Count queries
    pf.next_available_query.local_idx += (int)TimeStamps::Count;

    // Return the query
    return desc;
}

ProfilingMakerDescriptor ProfilingData::start_timing(VkCommandBuffer cmd_stream, ProfilingMarker marker, int swap_index)
{
    // Grab the profiling queries for this marker
    ProfilingQueries& pq = profiling_queries[swap_index];

    // Grab the next available marker
    ProfilingMakerDescriptor md = get_next_available_query(swap_index);

    // Keep track of it in the right type
    pq.markers_type.push_back(marker);

    // Enqueue the query
    vkCmdWriteTimestamp(cmd_stream, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pq.time_stamp_query_pools[md.pool_idx], (int)md.local_idx + (int)TimeStamps::Begin);

    // Return the query we're using
    return md;
}

void ProfilingData::end_timing(VkCommandBuffer cmd_stream, ProfilingMakerDescriptor md, int swap_index)
{
    // Grab the profiling queries for this marker
    ProfilingQueries& pq = profiling_queries[swap_index];

    // Dequeue the query
    vkCmdWriteTimestamp(cmd_stream, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pq.time_stamp_query_pools[md.pool_idx], (int)md.local_idx + (int)TimeStamps::End);
}

void ProfilingData::evaluate_queries(int swap_index)
{
    // Get the current profiling queries for the current swap
    ProfilingQueries& pq = profiling_queries[swap_index];

    // Reset all the timers for the frame
    for (uint32_t marker_idx = 0; marker_idx < (uint32_t)ProfilingMarker::Count; ++marker_idx)
    {
        results->time_stamp_begin[marker_idx] = uint64_t(~0);
        results->time_stamp_end[marker_idx] = 0;
        results->duration_ms[marker_idx] = 0;
    }

    // If no markers were registered, we're done
    if (pq.markers_type.size() == 0)
        return;

    // Span of the frame
    uint64_t min_time_stamp = uint64_t(~0);
    uint64_t max_time_stamp = 0;
    uint32_t marker_offset = 0;

    // Do this for every pool that is allocated
    for (uint32_t pool_idx = 0; pool_idx < pq.time_stamp_query_pools.size(); ++pool_idx)
    {
        // Read all the queries of the pool
        uint64_t time_stamps_and_status[profiling_marker_pool_size * 2] = {0};
        auto timer_result = vkGetQueryPoolResults(device->logical_device(), pq.time_stamp_query_pools[pool_idx],
            0, profiling_marker_pool_size, sizeof(time_stamps_and_status),
            &time_stamps_and_status, sizeof(time_stamps_and_status[0]) * 2,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

        // Validation of the query as a whole
        if (timer_result != VK_NOT_READY)
            CHECK_VULKAN(timer_result);
    
        // Loop through all the queries of this pool
        for (int query_idx = 0; query_idx < markers_per_query_pool; ++query_idx)
        {
            // If the query is valid
            int begin_query_idx = 2 * query_idx;
            int end_query_idx = 2 * query_idx + 1;
            if ((int64_t) time_stamps_and_status[begin_query_idx * 2 + 1] > 0 && (int64_t) time_stamps_and_status[end_query_idx * 2 + 1] > 0)
            {
                // Grab it's marker type
                ProfilingMarker marker_type = pq.markers_type[query_idx + marker_offset];
                uint64_t begin_ticks = time_stamps_and_status[begin_query_idx * 2];
                uint64_t end_ticks = time_stamps_and_status[end_query_idx * 2];

                // Evaluate the stamps and the timings
                results->time_stamp_begin[(uint32_t)marker_type] = std::min(begin_ticks, results->time_stamp_begin[(uint32_t)marker_type]);
                results->time_stamp_end[(uint32_t)marker_type] = std::max(end_ticks, results->time_stamp_end[(uint32_t)marker_type]);
                results->duration_ms[(uint32_t)marker_type] += double(end_ticks - begin_ticks) * device->nanoseconds_per_tick() / 1000000.0;

                // Keep track of the frame data
                min_time_stamp = std::min(min_time_stamp, begin_ticks);
                max_time_stamp = std::max(max_time_stamp, end_ticks);
            }
        }

        // Now that we processed all the elements of this pool, we can move to the other set o
        marker_offset += markers_per_query_pool;
    }

    // If anything was valid in the frame, compute the frame duration
    if (min_time_stamp <= max_time_stamp)
        results->max_span_ms = double(max_time_stamp - min_time_stamp) * device->nanoseconds_per_tick() / 1000000.0;
    else
        results->max_span_ms = 0.0;
}

void ProfilingData::reset_all_queries()
{
    for (int i = 0; i < array_ilen(profiling_queries); ++i)
        reset_queries(i);
}

void ProfilingData::reset_queries(int swap_index)
{
    ProfilingQueries &pq = profiling_queries[swap_index];

    // Reset the next avaialble query
    pq.next_available_query.pool_idx = 0;
    pq.next_available_query.local_idx = 0;
    pq.markers_type.clear();

    // Reset all the previously created pools
    for (int pool_idx = 0; pool_idx < pq.time_stamp_query_pools.size(); ++pool_idx)
    {
        VkQueryPool qp = pq.time_stamp_query_pools[pool_idx];
        vkResetQueryPool(device.logical_device(), qp, 0, profiling_marker_pool_size);
    }
}

} // namespace
