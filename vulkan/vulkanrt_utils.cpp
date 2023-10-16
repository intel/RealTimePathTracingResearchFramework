// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "vulkanrt_utils.h"
#include <algorithm>
#include <cmath>
#include <cassert>
#include <iterator>
#include <numeric>
#include <cstring>
#include "util.h"
#include "types.h"
#include <glm/glm.hpp>


namespace vkrt {

int Geometry::num_vertices() const
{
    if (num_active_vertices >= 0)
        return num_active_vertices;
    return int_cast( float_vertex_buf->size() / sizeof(glm::vec3) - vertex_offset );
}

int Geometry::num_triangles() const
{
    if (num_active_triangles >= 0)
        return num_active_triangles;
    return (index_buf)
        ? int_cast( index_buf->size() / sizeof(glm::uvec3) - triangle_offset )
        : int_cast( float_vertex_buf->size() / (sizeof(glm::vec3) * 3) - triangle_offset );
}

VkAccelerationStructureGeometryKHR Geometry::to_as_geometry() const {
    VkAccelerationStructureGeometryKHR geom_desc = {};
    if (!float_vertex_buf)
        return geom_desc;

    geom_desc.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom_desc.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom_desc.flags = geom_flags;

    geom_desc.geometry.triangles.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom_desc.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geom_desc.geometry.triangles.vertexData.deviceAddress = float_vertex_buf->device_address();
    geom_desc.geometry.triangles.vertexStride = sizeof(glm::vec3);
    geom_desc.geometry.triangles.maxVertex = num_vertices()-1;

    // note: all offsets should be handled via BVH build info now
    if (index_buf) {
        geom_desc.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        geom_desc.geometry.triangles.indexData.deviceAddress = index_buf->device_address();
        if (index_offset < 0)
            geom_desc.geometry.triangles.maxVertex -= index_offset;
    }
    else
        geom_desc.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;

    geom_desc.geometry.triangles.transformData.deviceAddress = 0;
    return geom_desc;
}

union BVH::BuildInfoEx {
    struct {
        VkAccelerationStructureGeometryKHR instance_desc;
    };
};

BVH::BVH(Device &dev, uint32_t build_flags)
    : device(dev),
      build_flags(build_flags) {
}
BVH::~BVH() {
    // todo: move to scratch space scheduler?
    if (staging_bvh != VK_NULL_HANDLE && staging_bvh != bvh)
        DestroyAccelerationStructureKHR(device->logical_device(), staging_bvh, nullptr);
    if (query_pool != VK_NULL_HANDLE)
        vkDestroyQueryPool(device->logical_device(), query_pool, nullptr);
}

void BVH::enqueue_build(VkCommandBuffer cmd_buf, Buffer::MemorySource memory, Buffer::MemorySource scratch_memory, bool enqueue_barriers)
{
    VkAccelerationStructureBuildGeometryInfoKHR build_info = {};
    BuildInfoEx build_info_ex = { };
    make_build_info(build_info, build_info_ex);

    // Determine how much memory the acceleration structure will need
    VkAccelerationStructureBuildSizesInfoKHR build_size_info = compute_build_size(build_info);

    if (staging_bvh == VK_NULL_HANDLE) {
        cached_build_size = build_size_info.accelerationStructureSize;

        Buffer::MemorySource build_memory_source = memory;
        if ((build_flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) && !is_rebuilt_regularly())
            build_memory_source = scratch_memory;
        staging_bvh_buf = Buffer::device(reuse(build_memory_source, staging_bvh_buf),
                                build_size_info.accelerationStructureSize,
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);

        // Create the acceleration structure
        VkAccelerationStructureCreateInfoKHR as_create_info = {};
        as_create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        as_create_info.buffer = staging_bvh_buf->handle();
        as_create_info.size = build_size_info.accelerationStructureSize;
        as_create_info.type = build_info.type;
        CHECK_VULKAN(CreateAccelerationStructureKHR(
            device->logical_device(), &as_create_info, nullptr, &staging_bvh));
    } else {
        if (!staging_bvh_buf || staging_bvh_buf.size() != build_size_info.accelerationStructureSize
         || cached_build_size && cached_build_size != build_size_info.accelerationStructureSize)
            throw_error("BVH size changed, needs to be recreated");
    }

    auto build_scratch_size = build_size_info.buildScratchSize;
    if (build_flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR)
        build_scratch_size = std::max(build_scratch_size, build_size_info.updateScratchSize);
    scratch_buf = Buffer::device(reuse(is_dynamic() ? memory : scratch_memory, scratch_buf),
        build_scratch_size,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        EXVK_MEMORY_PROPERTY_SCRATCH_SPACE_ALIGNMENT);

    // Enqueue the acceleration structure build
    build_info.dstAccelerationStructure = staging_bvh;
    build_info.scratchData.deviceAddress = scratch_buf->device_address();

    // Memory barrier to have build commands wait on buffer availability
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT
                            | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    if (enqueue_barriers)
        vkCmdPipelineBarrier(cmd_buf,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         0,
                         1, &barrier,
                         0, nullptr,
                         0, nullptr);

    VkAccelerationStructureBuildRangeInfoKHR *build_offset_info_ptr = build_offset_info.data();
    // Enqueue the build commands into the command buffer
    CmdBuildAccelerationStructuresKHR(cmd_buf, 1, &build_info, &build_offset_info_ptr);

    // Memory barrier to have subsequent commands wait on build completion
    barrier.srcAccessMask = barrier.dstAccessMask;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    if (build_flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
        barrier.dstAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;

    if (enqueue_barriers) {
        vkCmdPipelineBarrier(cmd_buf,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0,
                         1, &barrier,
                         0, nullptr,
                         0, nullptr);

        // only called automatically with barriers in place
        enqueue_post_build_async(cmd_buf);
    }
}

void BVH::enqueue_post_build_async(VkCommandBuffer cmd_buf) {
    // Read the compacted size if we're compacting
    if (build_flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) {
        if (query_pool == VK_NULL_HANDLE) {
            VkQueryPoolCreateInfo pool_ci = {};
            pool_ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            pool_ci.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
            pool_ci.queryCount = 1;
            CHECK_VULKAN(
                vkCreateQueryPool(device->logical_device(), &pool_ci, nullptr, &query_pool));
        }

        vkCmdResetQueryPool(cmd_buf, query_pool, 0, 1);
        //vkCmdBeginQuery(cmd_buf, query_pool, 0, 0);
        CmdWriteAccelerationStructuresPropertiesKHR(
            cmd_buf,
            1,
            &staging_bvh,
            VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
            query_pool,
            0);
        //vkCmdEndQuery(cmd_buf, query_pool, 0);
    }
}

void BVH::enqueue_refit(VkCommandBuffer cmd_buf, bool enqueue_barriers)
{
    VkAccelerationStructureBuildGeometryInfoKHR build_info = {};
    BuildInfoEx build_info_ex = { };
    make_build_info(build_info, build_info_ex);
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;

    build_info.srcAccelerationStructure = bvh;
    build_info.dstAccelerationStructure = bvh;
    build_info.scratchData.deviceAddress = scratch_buf->device_address();

    // Memory barrier to have build commands wait on buffer availability
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT
                            | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    if (enqueue_barriers)
        vkCmdPipelineBarrier(cmd_buf,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         0,
                         1, &barrier,
                         0, nullptr,
                         0, nullptr);

    VkAccelerationStructureBuildRangeInfoKHR *build_offset_info_ptr = build_offset_info.data();
    // Enqueue the build commands into the command buffer
    CmdBuildAccelerationStructuresKHR(cmd_buf, 1, &build_info, &build_offset_info_ptr);

    // Memory barrier to have subsequent commands wait on build completion
    barrier.srcAccessMask = barrier.dstAccessMask;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
//    if (build_flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
//        barrier.dstAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;

    if (enqueue_barriers)
        vkCmdPipelineBarrier(cmd_buf,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0,
                         1, &barrier,
                         0, nullptr,
                         0, nullptr);
}

void BVH::enqueue_compaction(VkCommandBuffer cmd_buf, Buffer::MemorySource memory)
{
    if (!(build_flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)) {
        return;
    }
    uint64_t compacted_size = 0;
    CHECK_VULKAN(vkGetQueryPoolResults(device->logical_device(),
                                       query_pool,
                                       0,
                                       1,
                                       sizeof(uint64_t),
                                       &compacted_size,
                                       sizeof(uint64_t),
                                       VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    if (!is_dynamic())
        scratch_buf = nullptr;

    if (!bvh) {
        bvh_buf = Buffer::device(reuse(memory, bvh_buf),
                                    compacted_size,
                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);

        // Create the compacted acceleration structure
        VkAccelerationStructureCreateInfoKHR as_create_info = {};
        as_create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        as_create_info.buffer = bvh_buf->handle();
        as_create_info.size = compacted_size;
        as_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        CHECK_VULKAN(CreateAccelerationStructureKHR(
            device->logical_device(), &as_create_info, nullptr, &bvh));
    } else {
        if (!bvh_buf || bvh_buf.size() != compacted_size)
            throw_error("BVH size changed, needs to be rebuilt");
    }

    VkCopyAccelerationStructureInfoKHR copy_info = {};
    copy_info.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
    copy_info.src = staging_bvh;
    copy_info.dst = bvh;
    copy_info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
    CmdCopyAccelerationStructureKHR(cmd_buf, &copy_info);

    // Enqueue a barrier on the compaction
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR |
                            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                            VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    vkCmdPipelineBarrier(cmd_buf,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0,
                         1, &barrier,
                         0, nullptr,
                         0, nullptr);
}

void BVH::finalize()
{
    // Release resources that are no longer needed when no rebuilds are requested
    if (!is_dynamic()) {
        scratch_buf = nullptr;
    }
    if (build_flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) {
        if (!is_rebuilt_regularly()) {
            vkDestroyQueryPool(device->logical_device(), query_pool, nullptr);
            query_pool = VK_NULL_HANDLE;

            DestroyAccelerationStructureKHR(device->logical_device(), staging_bvh, nullptr);
            staging_bvh = VK_NULL_HANDLE;
            staging_bvh_buf = nullptr;
        }
    }
    else {
        // no double buffering needed without compaction
        bvh = staging_bvh;
        bvh_buf = staging_bvh_buf;
    }

}

TriangleMesh::TriangleMesh(Device &dev, std::vector<Geometry> geoms_, uint32_t build_flags)
    : BVH(dev, build_flags),
      geometries(std::move(geoms_)),
      cached_triangle_count(-1)

{

    len_t total_triangle_count = 0;
    build_offset_info.reserve(geometries.size());
    std::transform(geometries.begin(),
                   geometries.end(),
                   std::back_inserter(build_offset_info),
                   [&total_triangle_count](Geometry &g) {
                        // cache relevant numbers in case buffers are discarded
                        g.num_active_vertices = g.num_vertices();
                        g.num_active_triangles = g.num_triangles();
                        total_triangle_count += g.num_active_triangles;

                        VkAccelerationStructureBuildRangeInfoKHR offset = {};
                        offset.primitiveCount = g.num_active_triangles;
                        if (g.index_buf) {
                            offset.primitiveOffset = g.triangle_offset * 3 * sizeof(uint32_t);
                            // firstVertex added to indices in this case
                            offset.firstVertex = uint_bound(g.vertex_offset + g.index_offset);
                        } else {
                            offset.firstVertex = g.vertex_offset;
                            assert(g.triangle_offset * 3 == g.vertex_offset);
                        }
                        offset.transformOffset = 0;
                        return offset;
                   });
    cached_triangle_count = int_cast(total_triangle_count);

    geom_descs.reserve(geometries.size());
    std::transform(geometries.begin(),
                   geometries.end(),
                   std::back_inserter(geom_descs),
                   [](Geometry const &g) {
                        return g.to_as_geometry();
                    });

    VkAccelerationStructureBuildGeometryInfoKHR build_info = {};
    BuildInfoEx build_info_ex = { };
    make_build_info(build_info, build_info_ex);
    cached_build_size = compute_build_size(build_info).accelerationStructureSize;
}

TriangleMesh::~TriangleMesh()
{
    if (bvh != VK_NULL_HANDLE)
        DestroyAccelerationStructureKHR(device->logical_device(), bvh, nullptr);
}

void TriangleMesh::make_build_info(VkAccelerationStructureBuildGeometryInfoKHR& build_info, BuildInfoEx& build_info_ex) const {
    build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info.flags = build_flags;
    build_info.geometryCount = geom_descs.size();
    build_info.pGeometries = geom_descs.data();
}

VkAccelerationStructureBuildSizesInfoKHR TriangleMesh::compute_build_size(VkAccelerationStructureBuildGeometryInfoKHR const& build_info) {
    std::vector<uint32_t> primitive_counts;
    primitive_counts.reserve(geometries.size());
    std::transform(geometries.begin(),
                   geometries.end(),
                   std::back_inserter(primitive_counts),
                   [](const Geometry &g) { return g.num_triangles(); });

    VkAccelerationStructureBuildSizesInfoKHR build_size_info = {};
    build_size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    GetAccelerationStructureBuildSizesKHR(device->logical_device(),
                                          VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                          &build_info,
                                          primitive_counts.data(),
                                          &build_size_info);
    return build_size_info;
}

void TriangleMesh::finalize()
{
    // Release resources that are no longer needed when no rebuilds are requested
    if (!is_dynamic()) {
        #if !defined(ENABLE_RASTER)
        for (auto& g : geometries) {
            g.float_vertex_buf = nullptr;
            if (g.indices_are_implicit)
                g.index_buf = nullptr;
        }
        #endif
    }

    BVH::finalize();

    VkAccelerationStructureDeviceAddressInfoKHR addr_info = {};
    addr_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addr_info.accelerationStructure = bvh;
    device_address = GetAccelerationStructureDeviceAddressKHR(device->logical_device(), &addr_info);
}

TopLevelBVH::TopLevelBVH(Device &dev,
                         Buffer const& inst_buf,
                         uint32_t instance_count,
                         uint32_t build_flags)
    : BVH(dev, build_flags),
      instance_buf(inst_buf),
      instance_count(instance_count)
{
    this->build_offset_info.resize(1);
    VkAccelerationStructureBuildRangeInfoKHR& build_offset_info = this->build_offset_info.front();
    build_offset_info.primitiveCount = instance_count;
    build_offset_info.primitiveOffset = 0;
    build_offset_info.firstVertex = 0;
    build_offset_info.transformOffset = 0;

    VkAccelerationStructureBuildGeometryInfoKHR build_info = {};
    BuildInfoEx build_info_ex = { };
    make_build_info(build_info, build_info_ex);
    cached_build_size = compute_build_size(build_info).accelerationStructureSize;
}

TopLevelBVH::~TopLevelBVH()
{
    if (bvh != VK_NULL_HANDLE) {
        DestroyAccelerationStructureKHR(device->logical_device(), bvh, nullptr);
    }
}

void TopLevelBVH::make_build_info(VkAccelerationStructureBuildGeometryInfoKHR& build_info, BuildInfoEx& build_info_ex) const {
    VkAccelerationStructureGeometryKHR& instance_desc = build_info_ex.instance_desc;
    instance_desc = { };
    instance_desc.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    instance_desc.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    instance_desc.flags = 0;
    instance_desc.geometry.instances.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instance_desc.geometry.instances.arrayOfPointers = false;
    instance_desc.geometry.instances.data.deviceAddress = instance_buf->device_address();


    build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build_info.flags = build_flags;
    build_info.geometryCount = 1;
    build_info.pGeometries = &instance_desc;
}

VkAccelerationStructureBuildSizesInfoKHR TopLevelBVH::compute_build_size(VkAccelerationStructureBuildGeometryInfoKHR const& build_info) {
    VkAccelerationStructureBuildSizesInfoKHR build_size_info = {};
    build_size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    GetAccelerationStructureBuildSizesKHR(device->logical_device(),
                                          VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                          &build_info,
                                          &instance_count,
                                          &build_size_info);
    return build_size_info;
}

void TopLevelBVH::finalize()
{
    BVH::finalize();

    VkAccelerationStructureDeviceAddressInfoKHR addr_info = {};
    addr_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addr_info.accelerationStructure = bvh;
    device_address = GetAccelerationStructureDeviceAddressKHR(device->logical_device(), &addr_info);
}

size_t TopLevelBVH::num_instances() const
{
    return instance_count;
}

RTPipeline::RTPipeline(Device& device) {
    ref_data->vkdevice = device.logical_device();
}

void RTPipeline::release_resources()
{
    if (ref_data && ref_data->vkdevice != VK_NULL_HANDLE) {
        auto vkdevice = ref_data->vkdevice;
        if (pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(vkdevice, pipeline, nullptr);
        if (ref_data->deferred_op != VK_NULL_HANDLE) {
            DestroyDeferredOperationKHR(vkdevice, ref_data->deferred_op, nullptr);
            ref_data->deferred_op = VK_NULL_HANDLE;
        }
        ref_data->vkdevice = VK_NULL_HANDLE;
    }
}

RTPipeline::~RTPipeline()
{
    this->discard_reference();
}

void RTPipeline::wait_for_construction() {
    if (ref_data->deferred_op != VK_NULL_HANDLE) {
        auto vkdevice = ref_data->vkdevice;
        CHECK_VULKAN(DeferredOperationJoinKHR(vkdevice, ref_data->deferred_op));

        this->load_shader_identifiers();

        DestroyDeferredOperationKHR(vkdevice, ref_data->deferred_op, nullptr);
        ref_data->deferred_op = VK_NULL_HANDLE;
    }
}

void RTPipeline::load_shader_identifiers() {
    CHECK_VULKAN(GetRayTracingShaderGroupHandlesKHR(ref_data->vkdevice,
                                                    pipeline,
                                                    0,
                                                    ref_data->shader_identifiers.size() / ref_data->ident_size,
                                                    ref_data->shader_identifiers.size(),
                                                    ref_data->shader_identifiers.data()));

}

const uint8_t *RTPipeline::shader_ident(const std::string &name, bool throw_on_error) const
{
    auto fnd = ref_data->shader_ident_offsets.find(name);
    if (fnd == ref_data->shader_ident_offsets.end()) {
        if (throw_on_error)
            throw_error("Shader identifier %s not found!", name.c_str());
        return nullptr;
    }
    return &ref_data->shader_identifiers[fnd->second];
}

size_t RTPipeline::shader_ident_size() const
{
    return ref_data->ident_size;
}

VkPipeline RTPipeline::handle()
{
    return pipeline;
}

ShaderGroup::ShaderGroup(const std::string &name,
                         const ShaderModule &shader_module,
                         const std::string &entry_point,
                         VkShaderStageFlagBits stage,
                         VkRayTracingShaderGroupTypeKHR group)
    : shader_module(shader_module),
      stage(stage),
      group(group),
      name(name),
      entry_point(entry_point)
{
}

RTPipelineBuilder &RTPipelineBuilder::set_raygen(const std::string &name,
                                                 const ShaderModule &shader,
                                                 const std::string &entry_point)
{
    shaders.emplace_back(name,
                         shader,
                         entry_point,
                         VK_SHADER_STAGE_RAYGEN_BIT_KHR,
                         VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR);
    return *this;
}

RTPipelineBuilder &RTPipelineBuilder::add_miss(const std::string &name,
                                               const ShaderModule &shader,
                                               const std::string &entry_point)
{
    shaders.emplace_back(name,
                         shader,
                         entry_point,
                         VK_SHADER_STAGE_MISS_BIT_KHR,
                         VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR);
    return *this;
}

RTPipelineBuilder &RTPipelineBuilder::add_hitgroup(const std::string &name,
                                                   const ShaderModule &shader,
                                                   VkShaderStageFlagBits shader_type,
                                                   const std::string &entry_point)
{
    shaders.emplace_back(name,
                         shader,
                         entry_point,
                         shader_type,
                         VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR);
    return *this;
}


RTPipelineBuilder &RTPipelineBuilder::set_layout(VkPipelineLayout l)
{
    layout = l;
    return *this;
}

RTPipelineBuilder &RTPipelineBuilder::set_recursion_depth(int depth)
{
    recursion_depth = depth;
    return *this;
}

RTPipeline RTPipelineBuilder::build(Device &device, bool defer)
{
    std::vector<VkPipelineShaderStageCreateInfo> shader_info;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> group_info;
    shader_info.reserve(shaders.size());
    group_info.reserve(shaders.size());

    RTPipeline pipeline(device);

    pipeline.ref_data->ident_size = device.raytracing_pipeline_properties().shaderGroupHandleSize;
    VkRayTracingShaderGroupCreateInfoKHR g_ci;
    std::string current_name;
    for (const auto &sg : shaders) {
        VkPipelineShaderStageCreateInfo ss_ci = {};
        ss_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ss_ci.stage = sg.stage;
        ss_ci.module = sg.shader_module->module;
        ss_ci.pName = sg.entry_point.c_str();

        if (current_name != sg.name) {
            if (!current_name.empty())
                group_info.push_back(g_ci);
            g_ci = VkRayTracingShaderGroupCreateInfoKHR{};
            g_ci.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            g_ci.type = sg.group;
            g_ci.generalShader = shader_info.size();
            g_ci.closestHitShader = VK_SHADER_UNUSED_KHR;
            g_ci.anyHitShader = VK_SHADER_UNUSED_KHR;
            g_ci.intersectionShader = VK_SHADER_UNUSED_KHR;
            current_name = sg.name;
        }

        if (sg.stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) {
            g_ci.generalShader = VK_SHADER_UNUSED_KHR;
            g_ci.closestHitShader = shader_info.size();
        } else if (sg.stage == VK_SHADER_STAGE_ANY_HIT_BIT_KHR) {
            g_ci.generalShader = VK_SHADER_UNUSED_KHR;
            g_ci.anyHitShader = shader_info.size();
        } else if (sg.stage == VK_SHADER_STAGE_INTERSECTION_BIT_KHR) {
            g_ci.generalShader = VK_SHADER_UNUSED_KHR;
            g_ci.intersectionShader = shader_info.size();
        }

        shader_info.push_back(ss_ci);
        pipeline.ref_data->shader_ident_offsets[sg.name] = group_info.size() * pipeline.ref_data->ident_size;
    }
    if (!current_name.empty())
        group_info.push_back(g_ci);

    if (defer)
        CHECK_VULKAN(
            CreateDeferredOperationKHR(device.logical_device(), nullptr, &pipeline.ref_data->deferred_op));

    VkRayTracingPipelineCreateInfoKHR pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipeline_create_info.stageCount = shader_info.size();
    pipeline_create_info.pStages = shader_info.data();
    pipeline_create_info.groupCount = group_info.size();
    pipeline_create_info.pGroups = group_info.data();
    pipeline_create_info.maxPipelineRayRecursionDepth = uint_bound(recursion_depth);
    pipeline_create_info.layout = layout;
    auto result = CreateRayTracingPipelinesKHR(device.logical_device(),
                                               pipeline.ref_data->deferred_op,
                                               device.pipeline_cache(),
                                               1,
                                               &pipeline_create_info,
                                               nullptr,
                                               &pipeline.pipeline);
    if (result == VK_OPERATION_NOT_DEFERRED_KHR) {
        DestroyDeferredOperationKHR(device.logical_device(), pipeline.ref_data->deferred_op, nullptr);
        pipeline.ref_data->deferred_op = VK_NULL_HANDLE;
        defer = false;
        result = VK_SUCCESS;
    }
    if (defer && result == VK_OPERATION_DEFERRED_KHR)
        result = VK_SUCCESS;
    CHECK_VULKAN(result);

    pipeline.ref_data->shader_identifiers.resize(group_info.size() * pipeline.ref_data->ident_size, 0);
    if (!defer)
        pipeline.load_shader_identifiers();

    return pipeline;
}

void ShaderBindingTable::release_resources() {
    ref_data->buffer = nullptr;
}

SBTBuilder::SBTBuilder() {}

SBTBuilder &SBTBuilder::set_raygen(const ShaderRecord &sr)
{
    raygen = sr;
    return *this;
}

SBTBuilder &SBTBuilder::add_miss(const ShaderRecord &sr)
{
    miss_records.push_back(sr);
    return *this;
}

SBTBuilder &SBTBuilder::add_hitgroup(const ShaderRecord &sr)
{
    hitgroups.push_back(sr);
    return *this;
}

ShaderBindingTable SBTBuilder::build(Buffer::MemorySource source)
{
    auto& device = *source.device;

    const uint32_t group_handle_size =
        device.raytracing_pipeline_properties().shaderGroupHandleSize;
    const uint32_t group_handle_alignment =
        device.raytracing_pipeline_properties().shaderGroupHandleAlignment;
    const uint32_t group_alignment =
        device.raytracing_pipeline_properties().shaderGroupBaseAlignment;

    ShaderBindingTable sbt;
    sbt.raygen.stride = align_to(group_handle_size + raygen.param_size, group_handle_alignment);
    sbt.raygen.size = sbt.raygen.stride;

    const uint32_t miss_offset = align_to(sbt.raygen.size, group_alignment);

    sbt.miss.stride = 0;
    for (const auto &m : miss_records) {
        sbt.miss.stride = std::max(
            sbt.miss.stride, align_to(group_handle_size + m.param_size, group_handle_alignment));
    }
    sbt.miss.size = sbt.miss.stride * miss_records.size();

    const uint32_t hitgroup_offset = align_to(miss_offset + sbt.miss.size, group_alignment);
    sbt.hitgroup.stride = 0;
    for (const auto &h : hitgroups) {
        sbt.hitgroup.stride =
            std::max(sbt.hitgroup.stride,
                     align_to(group_handle_size + h.param_size, group_handle_alignment));
    }
    sbt.hitgroup.size = sbt.hitgroup.stride * hitgroups.size();

    const size_t sbt_size = align_to(hitgroup_offset + sbt.hitgroup.size, group_alignment);
    auto sbt_buffer = Buffer::device(source,
                                    sbt_size,
                                    VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
                                    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    auto upload_sbt = sbt_buffer->secondary_for_host(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    auto sbt_device_address = sbt_buffer->device_address();
    sbt.ref_data->buffer = sbt_buffer;
    sbt.raygen.deviceAddress = sbt_device_address;
    sbt.miss.deviceAddress = sbt_device_address + miss_offset;
    sbt.hitgroup.deviceAddress = sbt_device_address + hitgroup_offset;

    uint8_t* sbt_mapping = (uint8_t*) upload_sbt.map();

    // Copy the shader identifier and record where to write the parameters
    size_t offset = 0;
    std::memcpy(
        sbt_mapping + offset,
        raygen.shader_ident,
        group_handle_size);
    sbt.ref_data->raygen_param_offset = offset + group_handle_size;

    offset = miss_offset;
    sbt.ref_data->miss_param_offset = offset + group_handle_size;
    for (const auto &m : miss_records) {
        std::memcpy(sbt_mapping + offset,
                    m.shader_ident,
                    group_handle_size);
        offset += sbt.miss.stride;
    }

    offset = hitgroup_offset;
    sbt.ref_data->hitgroup_param_offset = offset + group_handle_size;
    for (const auto &hg : hitgroups) {
        if (hg.shader_ident)
            std::memcpy(sbt_mapping + offset,
                        hg.shader_ident,
                        group_handle_size);
        else
            std::memset(sbt_mapping + offset, 0, group_handle_size);
        offset += sbt.hitgroup.stride;
    }

    upload_sbt.unmap();
    return sbt;
}

} // namespace
