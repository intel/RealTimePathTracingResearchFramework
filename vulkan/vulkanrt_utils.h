// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include "unordered_vector.hpp"
#include "material.h"
#include "mesh.h"
#include "vulkan_utils.h"
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace vkrt {

struct Geometry {
    Buffer float_vertex_buf = nullptr;
    Buffer vertex_buf = nullptr, normal_buf = nullptr, uv_buf = nullptr;
    uint32_t geom_flags = 0;
    int32_t index_offset = 0;
    Buffer index_buf = nullptr;

    // for later reference
    glm::vec3 quantized_scaling, quantized_offset;
    int num_active_vertices = -1, num_active_triangles = -1;

    uint32_t triangle_offset = 0, vertex_offset = 0;
    bool indices_are_implicit = false;

    int num_vertices() const;
    int num_triangles() const;

    VkAccelerationStructureGeometryKHR to_as_geometry() const;
};

struct ParameterizedMesh {
    int render_mesh_base_offset = -1;
    int render_mesh_count = 0;
    Buffer per_triangle_material_buf = nullptr;
    int32_t mesh_id = -1; // link back to source data (e.g. shaders)
    int32_t lod_group_id;
    bool no_alpha = false;

    unsigned material_revision = ~0;
    unsigned shader_revision = ~0;
    unsigned model_revision = ~0;
    unsigned mesh_model_revision = ~0;
};

struct Instance {
    int parameterized_mesh_id;
    glm::mat4 transform;
};

struct BVH {
    Device device = nullptr;
    std::vector<VkAccelerationStructureGeometryKHR> geom_descs;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> build_offset_info;
    size_t cached_build_size = 0;

    uint32_t build_flags = 0;

    Buffer bvh_buf = nullptr, scratch_buf = nullptr;
    Buffer staging_bvh_buf = nullptr;
    VkAccelerationStructureKHR staging_bvh = VK_NULL_HANDLE;

    VkQueryPool query_pool = VK_NULL_HANDLE;

    VkAccelerationStructureKHR bvh = VK_NULL_HANDLE;

    BVH(Device &dev, uint32_t build_flags);
    ~BVH();
    BVH(const BVH &) = delete;
    BVH &operator=(const BVH &) = delete;

    union BuildInfoEx;
    virtual void make_build_info(VkAccelerationStructureBuildGeometryInfoKHR& build_info, BuildInfoEx& build_info_ex) const = 0;
    virtual VkAccelerationStructureBuildSizesInfoKHR compute_build_size(VkAccelerationStructureBuildGeometryInfoKHR const& build_info) = 0;

    /* After calling build the commands are placed in the command list
     * with a barrier to wait on the completion of the build
     */
    virtual void enqueue_build(VkCommandBuffer cmd_buf, Buffer::MemorySource memory, Buffer::MemorySource scratch_memory
        , bool enqueue_barriers = true);
    /* If calling build without enqueue_barriers, call post-build ops manually.
     */
    virtual void enqueue_post_build_async(VkCommandBuffer cmd_buf);

    /* After calling build the commands are placed in the command list
     * with a barrier to wait on the completion of the build
     */
    virtual void enqueue_refit(VkCommandBuffer cmd_buf
        , bool enqueue_barriers = true);

    /* Enqueue the BVH compaction copy if the BVH was built with compaction enabled.
     * The BVH build must have been enqueued and completed so that the post build info is
     * available
     */
    virtual void enqueue_compaction(VkCommandBuffer cmd_buf, Buffer::MemorySource memory);

    /* Finalize the BVH build structures to release any scratch space.
     * Must call after enqueue compaction if performing compaction, otherwise
     * this can be called after the work from enqueue build has been finished
     */
    virtual void finalize();

    bool is_dynamic() const {
        return (build_flags & VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR)
            || (build_flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
    }

    bool is_rebuilt_regularly() const {
        return is_dynamic() && !(build_flags & VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
    }

    bool is_compacted() const {
        return (build_flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
            && bvh_buf
            && staging_bvh_buf != bvh_buf;
    }
};

struct TriangleMesh : BVH {
    uint64_t device_address = 0;

    int cached_triangle_count = 0;

    std::vector<Geometry> geometries;

    // link to representative of render mesh data (may have any material parameterization)
    int gpu_mesh_data_offset = -1;
    int cpu_mesh_data_index = -1;

    unsigned vertex_revision = ~0;
    unsigned attribute_revision = ~0;
    unsigned optimize_revision = ~0;
    unsigned model_revision = ~0;

    // TODO: Allow other vertex and index formats? Right now this
    // assumes vec3f verts and uint3 indices
    TriangleMesh(
        Device &dev,
        std::vector<Geometry> geometries,
        uint32_t build_flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                               VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
    ~TriangleMesh();
    TriangleMesh(const TriangleMesh &) = delete;
    TriangleMesh &operator=(const TriangleMesh &) = delete;

    void make_build_info(VkAccelerationStructureBuildGeometryInfoKHR& build_info, BuildInfoEx& build_info_ex) const override;
    VkAccelerationStructureBuildSizesInfoKHR compute_build_size(VkAccelerationStructureBuildGeometryInfoKHR const& build_info) override;

    void finalize() override;

    int triangle_count() const {
        return cached_triangle_count;
    }
};

struct TopLevelBVH : BVH {
    uint32_t instance_count = 0;

    Buffer instance_buf = nullptr;
    uint64_t device_address = 0;

    unsigned instance_revision = ~0;
    unsigned optimize_revision = ~0;

    // TODO: Re-check on compacting the top-level BVH in DXR, it seems to be do-able
    // in OptiX, maybe DXR and Vulkan too?
    TopLevelBVH(
        Device &dev,
        Buffer const& instance_buf,
        uint32_t instanceCount,
        uint32_t build_flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
    ~TopLevelBVH();
    TopLevelBVH(const TopLevelBVH &) = delete;
    TopLevelBVH &operator=(const TopLevelBVH &) = delete;

    void make_build_info(VkAccelerationStructureBuildGeometryInfoKHR& build_info, BuildInfoEx& build_info_ex) const override;
    VkAccelerationStructureBuildSizesInfoKHR compute_build_size(VkAccelerationStructureBuildGeometryInfoKHR const& build_info) override;

    // Free the BVH build scratch space
    void finalize() override;

    size_t num_instances() const;
};

struct RTPipeline : public ref_counted<RTPipeline> {
    VkPipeline pipeline = VK_NULL_HANDLE;

    friend ref_counted;
    struct shared_data {
        VkDeferredOperationKHR deferred_op = VK_NULL_HANDLE;
        std::vector<uint8_t> shader_identifiers;
        unordered_vector<std::string, size_t> shader_ident_offsets;
        size_t ident_size = 0;
        VkDevice vkdevice = VK_NULL_HANDLE;
    };

    friend class RTPipelineBuilder;
    void load_shader_identifiers();

protected:
    // use builder
    RTPipeline(Device& device);
public:
    RTPipeline(std::nullptr_t = nullptr) : ref_counted(nullptr) { }
    void release_resources();
    ~RTPipeline();

    const uint8_t *shader_ident(const std::string &name, bool throw_on_error = false) const;
    size_t shader_ident_size() const;

    VkPipeline handle();

    void wait_for_construction();
};

struct ShaderGroup {
    ShaderModule shader_module;
    VkShaderStageFlagBits stage;
    VkRayTracingShaderGroupTypeKHR group;
    std::string name;
    std::string entry_point;

    ShaderGroup(const std::string &name,
                const ShaderModule &shader_module,
                const std::string &entry_point,
                VkShaderStageFlagBits stage,
                VkRayTracingShaderGroupTypeKHR group);
};

class RTPipelineBuilder {
    std::vector<ShaderGroup> shaders;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    int recursion_depth = 1;

public:
    RTPipelineBuilder &set_raygen(const std::string &name,
                                  const ShaderModule &shader,
                                  const std::string &entry_point = "main");

    RTPipelineBuilder &add_miss(const std::string &name,
                                const ShaderModule &shader,
                                const std::string &entry_point = "main");

    RTPipelineBuilder &add_hitgroup(const std::string &name,
                                    const ShaderModule &shader,
                                    VkShaderStageFlagBits shader_type = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                                    const std::string &entry_point = "main");


    RTPipelineBuilder &set_layout(VkPipelineLayout layout);

    RTPipelineBuilder &set_recursion_depth(int depth);

    RTPipeline build(Device &device, bool defer = false);
};

struct ShaderBindingTable : ref_counted<ShaderBindingTable> {
    VkStridedDeviceAddressRegionKHR raygen = {};
    VkStridedDeviceAddressRegionKHR miss = {};
    VkStridedDeviceAddressRegionKHR hitgroup = {};

    struct shared_data {
        size_t raygen_param_offset;
        size_t hitgroup_param_offset;
        size_t miss_param_offset;

        Buffer buffer = nullptr;
    };

    ~ShaderBindingTable() { this->discard_reference(); }

    void release_resources();
    Buffer buffer() { return ref_data->buffer; }
    Buffer upload_buffer() { return ref_data->buffer.secondary(); }
    uint8_t* sbt_raygen_params(void* mapping, int i) { return (uint8_t*) mapping + (ref_data->raygen_param_offset + i * raygen.stride); }
    uint8_t* sbt_hitgroup_params(void* mapping, int i) { return (uint8_t*) mapping + (ref_data->hitgroup_param_offset + i * hitgroup.stride); }
    uint8_t* sbt_miss_params(void* mapping, int i) { return (uint8_t*) mapping + (ref_data->miss_param_offset + i * miss.stride); }
};

struct ShaderRecord {
    std::string name;
    uint8_t const* shader_ident;
    size_t param_size = 0;
};

class SBTBuilder {
    ShaderRecord raygen;
    std::vector<ShaderRecord> miss_records;
    std::vector<ShaderRecord> hitgroups;

public:
    SBTBuilder();

    SBTBuilder &set_raygen(const ShaderRecord &sr);
    SBTBuilder &add_miss(const ShaderRecord &sr);
    // TODO: Maybe similar to DXR where we take the per-ray type hit groups? Or should I change
    // the DXR one to work more like this? How would the shader indexing work out easiest if I
    // start mixing multiple geometries into a bottom level BVH?
    SBTBuilder &add_hitgroup(const ShaderRecord &sr);

    ShaderBindingTable build(Buffer::MemorySource source);
};

} // namespace
