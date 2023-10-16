// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>
#include "camera.h"
#include "lights.h"
#include "material.h"
#include "mesh.h"
#include "types.h"
#include "image.h"
#include "file_mapping.h"
//#include "phmap.h"


struct LodGroup {
    std::vector<int> mesh_ids; // todo: this should be called parameterized_mesh_id
    std::vector<float> detail_reduction;
};

struct AnimationData {
    mapped_vector<unsigned char> quantized;
    uint64_t numStaticTransforms = 0;
    uint64_t numAnimatedTransforms = 0;
    uint64_t numFrames = 0;

    size_t size_in_bytes() const;
    glm::mat4 dequantize(uint32_t index, uint32_t frame) const;
};

struct SceneLoaderParams {
    bool use_deduplication = false;
    bool remove_lods = false;
    struct PerFile {
        int remove_first_LODs = 0;
        float instance_pruning_probability = 0.0f;
        bool small_deformation = false;
        bool ignore_animation = false;
        bool ignore_textures = false;
        bool merge_partition_instances = false;
        bool load_specularity = false;
    };
    std::vector<PerFile> per_file;
};

struct Scene {
    std::vector<Mesh> meshes;
    std::vector<ParameterizedMesh> parameterized_meshes;
    std::vector<Instance> instances;
    std::vector<BaseMaterial> materials;
    std::vector<LodGroup> lod_groups = {LodGroup{}};
    std::vector<AnimationData> animation_data;

    std::vector<std::string> material_names;
    std::vector<Image> textures;
    std::vector<PointLight> pointLights;
    std::vector<QuadLight> quadLights;
    std::vector<CameraDesc> cameras;


    unsigned instances_revision = 0;
    unsigned materials_revision = 0;
    unsigned lights_revision = 0;
    unsigned textures_revision = 0;

    unsigned meshes_revision = 0;
    unsigned parameterized_meshes_revision = 0;

    static unsigned counter_unique_ids;
    unsigned unqiue_id = ++counter_unique_ids;

    Scene(const std::vector<std::string> &fnames, SceneLoaderParams const &params = {});
    Scene() = default;

    // Compute the unique number of triangles in the scene
    size_t unique_tris(uint32_t mesh_flags = 0) const;
    // Compute the total number of triangles in the scene (after instancing)
    size_t total_tris(uint32_t mesh_flags = 0) const;
    // Compute the total number of BVH geometries in the scene
    size_t num_geometries() const;
    // Texture memory
    size_t total_texture_bytes() const;

private:
    void load_vkrs(const std::string &file, SceneLoaderParams::PerFile const* params = nullptr);

    struct DeduplicationInfo {
        size_t num_removed_meshes = 0;
        size_t num_removed_pmeshes = 0;
        size_t num_removed_lod_groups = 0;
        size_t num_removed_materials = 0;
        size_t num_removed_textures = 0;
    };
    void deduplicate(DeduplicationInfo& dedup_info);
    void garbage_collect(DeduplicationInfo& dedup_info);
    bool unlink_duplicate_instanced_meshes(DeduplicationInfo& dedup_info);
    bool unlink_duplicate_materials(DeduplicationInfo& dedup_info);
    void remove_orphaned_instanced_meshes(DeduplicationInfo& dedup_info);
    void remove_orphaned_lods_and_meshes(DeduplicationInfo& dedup_info);
    void remove_orphaned_materials(DeduplicationInfo& dedup_info);
    void remove_orphaned_textures(DeduplicationInfo& dedup_info);
    bool unlink_pruned_lod_meshes(DeduplicationInfo& dedup_info);


    void validate();
};
