// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "scene.h"
#include "error_io.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>
#include "profiling.h"
#include "util.h"
#include "compute_util.h"
#include <vkr.h>
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <map>
#include <unordered_map>

glm::mat4 AnimationData::dequantize(uint32_t index, uint32_t frame) const
{
    const uint64_t offset = vkr_get_transform_offset(
        index,
        numStaticTransforms,
        numAnimatedTransforms,
        frame);
    const uint64_t byteOffset = offset * VKR_QUANTIZED_TRANSFORM_SIZE;

    float transform[4][3];
    vkr_dequantize_transform(transform, quantized.data() + byteOffset);
    glm::mat4x3 tx;
    std::memcpy(&tx, transform, sizeof(tx));

    static const glm::mat4 vks_flip(glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
        glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    return vks_flip * glm::mat4(tx);
}

size_t AnimationData::size_in_bytes() const
{
    return (numStaticTransforms + numFrames * numAnimatedTransforms)
        * VKR_QUANTIZED_TRANSFORM_SIZE;
}

unsigned Scene::counter_unique_ids = 0;

Scene::Scene(const std::vector<std::string> &fnames, SceneLoaderParams const &scene_params)
{
    ProfilingScope profile_load("Scene load");
    DeduplicationInfo deduplication_info;

    int scene_count = ilen(fnames);
    for (int scene_idx = 0; scene_idx < scene_count; ++scene_idx) {
      const std::string &fname = fnames[scene_idx];
      SceneLoaderParams::PerFile const* loader_params = scene_idx < ilen(scene_params.per_file) ? &scene_params.per_file[scene_idx] : nullptr;
      const std::string ext = get_file_extension(fname);
      if (ext == ".vkrs" || ext == ".vks") {
          load_vkrs(fname, loader_params);
      } else
          throw_error("Unsupported file type %s in %s", ext.c_str(), fname.c_str());

        // We call deduplication more frequently to also keep CPU memory allocation low
        if (scene_params.use_deduplication) {
            deduplicate(deduplication_info);
            garbage_collect(deduplication_info);
        }
    }

    // clean up scene after overrides were applied
    if (!scene_params.per_file.empty() || scene_params.remove_lods) {
        if (scene_params.remove_lods) {
            for (auto& mesh : parameterized_meshes)
                if (mesh.lod_group && lod_groups[mesh.lod_group].mesh_ids.size() > 1) {
                    lod_groups[mesh.lod_group].mesh_ids.resize(1);
                    lod_groups[mesh.lod_group].detail_reduction.resize(1);
                }
        }
        unlink_pruned_lod_meshes(deduplication_info);
        garbage_collect(deduplication_info);
    }

    if (deduplication_info.num_removed_meshes > 0 ||
        deduplication_info.num_removed_lod_groups > 0) {
      println(CLL::INFORMATION, "Duplicate geometry detected! Removed %d meshes and %d LOD groups",
              int_cast(deduplication_info.num_removed_meshes),
              int_cast(deduplication_info.num_removed_lod_groups));
    }
    if (deduplication_info.num_removed_materials > 0) {
      println(CLL::INFORMATION, "Removed %d unused materials",
              int_cast(deduplication_info.num_removed_materials));
    }
    if (deduplication_info.num_removed_textures > 0) {
      println(CLL::INFORMATION, "Removed %d unused textures",
              int_cast(deduplication_info.num_removed_textures));
    }

    
    validate();
}

size_t Scene::unique_tris(uint32_t mesh_flags) const
{
    return std::accumulate(
        meshes.begin(), meshes.end(), size_t(0), [mesh_flags](size_t n, const Mesh &m) {
            if (mesh_flags && (m.flags & mesh_flags) == 0)
                return n;
            return n + m.num_tris();
        });
}

size_t Scene::total_tris(uint32_t mesh_flags) const
{
    return std::accumulate(
        instances.begin(), instances.end(), size_t(0), [&, mesh_flags](size_t n, const Instance &i) {
            auto& mesh = meshes[parameterized_meshes[i.parameterized_mesh_id].mesh_id];
            if (mesh_flags && (mesh.flags & mesh_flags) == 0)
                return n;
            return n + mesh.num_tris();
        });
}

size_t Scene::num_geometries() const
{
    return std::accumulate(
        meshes.begin(), meshes.end(), size_t(0), [](size_t n, const Mesh &m) {
            return n + m.geometries.size();
        });
}

size_t Scene::total_texture_bytes() const
{
    return std::accumulate(
        textures.begin(), textures.end(), size_t(0), [](size_t n, const Image &i) {
            return n + i.img.nbytes();
        });
}

void Scene::deduplicate(DeduplicationInfo &dedup_info)
{
    unlink_duplicate_instanced_meshes(dedup_info);
    unlink_duplicate_materials(dedup_info);
}

void Scene::garbage_collect(DeduplicationInfo &dedup_info)
{
    // note: order matters!
    remove_orphaned_instanced_meshes(dedup_info);
    remove_orphaned_lods_and_meshes(dedup_info);
    remove_orphaned_materials(dedup_info);
    remove_orphaned_textures(dedup_info);
}

bool Scene::unlink_duplicate_instanced_meshes(DeduplicationInfo& dedup_info) {
    int numOriginalMeshes = ilen(parameterized_meshes);

    // compact paramterized_meshes to contain only unique mesh names
    std::vector<int> mesh_dedup_index_LUT(numOriginalMeshes);
    bool remapped_meshes = false;
    {
        std::map<std::string, int> mesh_name_dedup_index_LUT;
        bool remapping_needs_refresh = false;
        for (int iMesh = 0; iMesh < numOriginalMeshes; iMesh++) {
            const std::string &mesh_name = parameterized_meshes[iMesh].mesh_name;
            auto mesh_index_and_unique = mesh_name_dedup_index_LUT.insert({mesh_name, iMesh});
            // mesh_index_and_unique.first contains either new or first dedup index
            int& remapped_idx = mesh_index_and_unique.first->second;
            // override conflict resolution: mesh with overrides wins
            if (parameterized_meshes[iMesh].has_overrides_applied && !parameterized_meshes[remapped_idx].has_overrides_applied) {
                remapped_idx = iMesh;
                remapping_needs_refresh = true; // propagate change to previous remapped meshes
            }
            mesh_dedup_index_LUT[iMesh] = remapped_idx;
            remapped_meshes |= !mesh_index_and_unique.second;
        }
        if (remapping_needs_refresh) {
            // apply final indices in second pass if necessary
            for (int iMesh = 0; iMesh < numOriginalMeshes; iMesh++) {
                const std::string &mesh_name = parameterized_meshes[iMesh].mesh_name;
                int remapped_idx = mesh_name_dedup_index_LUT.find(mesh_name)->second;
                mesh_dedup_index_LUT[iMesh] = remapped_idx;
            }
        }
    }
    if (!remapped_meshes)
        return false;

    // update instances
    for (Instance &instance : instances) {
        instance.parameterized_mesh_id = mesh_dedup_index_LUT[instance.parameterized_mesh_id];
    }
    return true;
}

bool Scene::unlink_duplicate_materials(DeduplicationInfo& dedup_info) {
    int numOriginalMaterials = ilen(materials);

    // compact materials to contain only unique material names
    std::vector<int> material_dedup_index_LUT(numOriginalMaterials);
    bool remapped_materials = false;
    {
        std::map<std::string, int> material_name_dedup_index_LUT;
        for (int iMaterial = 0; iMaterial < numOriginalMaterials; iMaterial++) {
            int remapped_idx = iMaterial;
            if (iMaterial < ilen(material_names) && !material_names[iMaterial].empty()) {
                auto material_index_and_unique = material_name_dedup_index_LUT.insert({material_names[iMaterial], iMaterial});
                // material_index_and_unique.first contains either new or first dedup index
                remapped_idx = material_index_and_unique.first->second;
                remapped_materials |= !material_index_and_unique.second;
            }
            material_dedup_index_LUT[iMaterial] = remapped_idx;
        }
    }
    if (!remapped_materials)
        return false;

    // update meshes
    for (auto &pmesh : parameterized_meshes) {
        if (pmesh.per_triangle_materials())
            throw_error("Cannot detect orphaned materials for per-triangle materials, aborting");
        for (int& material_id : pmesh.material_offsets)
            material_id = material_dedup_index_LUT[material_id];
    }
    return true;
}

bool Scene::unlink_pruned_lod_meshes(DeduplicationInfo& dedup_info) {
    bool remapped_meshes = false;
    for (Instance &instance : instances) {
        // re-align instances with first LoD
        int pm_id = instance.parameterized_mesh_id;
        if (int lod_group_id = parameterized_meshes[pm_id].lod_group) {
            int first_lod_mesh_id = lod_groups[lod_group_id].mesh_ids[0];
            if (first_lod_mesh_id != pm_id) {
                instance.parameterized_mesh_id = first_lod_mesh_id;
                pm_id = first_lod_mesh_id;
                remapped_meshes = true;
            }
        }
    }
    return remapped_meshes;
}

void Scene::remove_orphaned_instanced_meshes(DeduplicationInfo& dedup_info) {
    int numOriginalMeshes = ilen(parameterized_meshes);
    int numDedupMeshes = 0;

    // compact paramterized_meshes to contain only unique mesh names
    std::vector<int> mesh_dedup_index_LUT(numOriginalMeshes, -1);
    {
        std::vector<int> mesh_users(numOriginalMeshes);
        for (Instance &instance : instances) {
            int pm_id = instance.parameterized_mesh_id;
            mesh_users[pm_id]++;
            if (int lod_group_id = parameterized_meshes[pm_id].lod_group) {
                for (int lod_mesh_id : lod_groups[lod_group_id].mesh_ids)
                    mesh_users[lod_mesh_id]++;
            }
        }

        for (int iMesh = 0; iMesh < numOriginalMeshes; iMesh++) {
            if (mesh_users[iMesh] > 0) {
                mesh_dedup_index_LUT[iMesh] = numDedupMeshes;
                if (numDedupMeshes != iMesh)
                    parameterized_meshes[numDedupMeshes] = std::move(parameterized_meshes[iMesh]);
                ++numDedupMeshes;
            }
        }
        parameterized_meshes.resize(numDedupMeshes);
    }
    if (numOriginalMeshes == numDedupMeshes)
        return;

    // update LOD groups
    for (LodGroup &lod_group : lod_groups) {
      for (int &lod_mesh_id : lod_group.mesh_ids)
        lod_mesh_id = mesh_dedup_index_LUT[lod_mesh_id];
    }

    // update instances
    for (Instance &instance : instances) {
        instance.parameterized_mesh_id = mesh_dedup_index_LUT[instance.parameterized_mesh_id];
    }

    dedup_info.num_removed_pmeshes += numOriginalMeshes - numDedupMeshes;
}

void Scene::remove_orphaned_lods_and_meshes(DeduplicationInfo& dedup_info) {
    int numOriginalMeshes = ilen(meshes);
    int numOriginalLODGroups = ilen(lod_groups);

    int numUsedMeshes = 0;
    // note: by design the default LOD group 0 must stay intact!
    int numUsedLODGroups = 1;

    // compact meshes and LOD groups to only contain used items
    std::vector<int> used_mesh_indices(numOriginalMeshes, -1);
    std::vector<int> used_lodgroup_indices(numOriginalLODGroups, -1);
    used_lodgroup_indices[0] = 0;
    {
        std::vector<int> mesh_users(numOriginalMeshes);
        std::vector<int> lodgroup_users(numOriginalLODGroups);
        for (auto &pmesh : parameterized_meshes) {
            mesh_users[pmesh.mesh_id]++;
            lodgroup_users[pmesh.lod_group]++;
        }

        for (int mesh_id = 0; mesh_id < numOriginalMeshes; mesh_id++) {
            if (mesh_users[mesh_id] > 0) {
                used_mesh_indices[mesh_id] = numUsedMeshes;
                if (numUsedMeshes != mesh_id)
                    meshes[numUsedMeshes] = std::move(meshes[mesh_id]);
                numUsedMeshes++;
            }
        }
        meshes.resize(numUsedMeshes);

        for (int lod_group_id = 1; lod_group_id < numOriginalLODGroups; lod_group_id++) {
            if (lodgroup_users[lod_group_id] > 0) {
                used_lodgroup_indices[lod_group_id] = numUsedLODGroups;
                if (numUsedLODGroups != lod_group_id)
                    lod_groups[numUsedLODGroups] = std::move(lod_groups[lod_group_id]);
                numUsedLODGroups++;
            }
        }
        lod_groups.resize(numUsedLODGroups);
    }
    if (numOriginalMeshes == numUsedMeshes
     && numOriginalLODGroups == numUsedLODGroups)
        return;

    // update meshes
    for (auto &pmesh : parameterized_meshes) {
        pmesh.mesh_id = used_mesh_indices[pmesh.mesh_id];
        pmesh.lod_group = used_lodgroup_indices[pmesh.lod_group];
    }

    dedup_info.num_removed_meshes += numOriginalMeshes - numUsedMeshes;
    dedup_info.num_removed_lod_groups += numOriginalLODGroups - numUsedLODGroups;
}

void Scene::remove_orphaned_materials(DeduplicationInfo& dedup_info) {
    int numOriginalMaterials = ilen(materials);
    int numUsedMaterials = 0;

    // compact materials to only contain used materials
    std::vector<int> material_used_indices(numOriginalMaterials, -1);
    {
        std::vector<int> material_users(numOriginalMaterials);
        for (auto &pmesh : parameterized_meshes) {
            if (pmesh.per_triangle_materials()) {
                warning("Cannot detect orphaned materials for per-triangle materials, aborting");
                return;
            }
            for (int material_id : pmesh.material_offsets)
                material_users[material_id]++;
        }

        for (int material_id = 0; material_id < numOriginalMaterials; material_id++) {
            if (material_users[material_id] > 0) {
                material_used_indices[material_id] = numUsedMaterials;
                if (numUsedMaterials != material_id) {
                    materials[numUsedMaterials] = std::move(materials[material_id]);
                    material_names[numUsedMaterials] = std::move(material_names[material_id]);
                }
                numUsedMaterials++;
            }
        }
        materials.resize(numUsedMaterials);
        material_names.resize(numUsedMaterials);
    }
    if (numOriginalMaterials == numUsedMaterials)
        return;

    // update meshes
    for (auto &pmesh : parameterized_meshes) {
        for (int& material_id : pmesh.material_offsets)
            material_id = material_used_indices[material_id];
    }

    dedup_info.num_removed_materials += numOriginalMaterials - numUsedMaterials;
}

void Scene::remove_orphaned_textures(DeduplicationInfo& dedup_info) {
    int numOriginalTextures = ilen(textures);
    int numUsedTextures = 0;

    // compact textures to only contain used textures
    std::vector<int> texture_used_indices(numOriginalTextures, -1);
    {
        std::vector<int> texture_users(numOriginalTextures);
        for (auto &material : materials) {
            if (material.normal_map >= 0)
                texture_users[material.normal_map]++;

#define FOR_TEXTURED_MATERIAL_PROPERTIES(x) \
            x(base_color) \
            x(specular) \
            x(roughness) \
            x(metallic) \
            x(specular_transmission) \
            x(transmission_color) \
            x(ior) \

#define TEXTURED_MATERIAL_PROPERTY_INC(property) { \
                uint32_t tex_id; \
                memcpy(&tex_id, reinterpret_cast<char*>(&material.property), sizeof(tex_id)); \
                if (IS_TEXTURED_PARAM(tex_id)) \
                    texture_users[GET_TEXTURE_ID(tex_id)]++; \
            }
            FOR_TEXTURED_MATERIAL_PROPERTIES(TEXTURED_MATERIAL_PROPERTY_INC)
#undef TEXTURED_MATERIAL_PROPERTY_INC
        }

        for (int texture_id = 0; texture_id < numOriginalTextures; texture_id++) {
            if (texture_users[texture_id] > 0) {
                texture_used_indices[texture_id] = numUsedTextures;
                if (numUsedTextures != texture_id)
                    textures[numUsedTextures] = std::move(textures[texture_id]);
                numUsedTextures++;
            }
        }
        textures.resize(numUsedTextures);
    }
    if (numOriginalTextures == numUsedTextures)
        return;

    // update materials
    for (auto &material : materials) {
        if (material.normal_map >= 0)
            material.normal_map = texture_used_indices[material.normal_map];

#define TEXTURED_MATERIAL_PROPERTY_REMAP(property) { \
            uint32_t old_tex_id; \
            memcpy(&old_tex_id, reinterpret_cast<char*>(&material.property), sizeof(old_tex_id)); \
            if (IS_TEXTURED_PARAM(old_tex_id)) { \
                uint32_t new_tex_id = TEXTURED_PARAM_MASK; \
                SET_TEXTURE_ID(new_tex_id, texture_used_indices[GET_TEXTURE_ID(old_tex_id)]); \
                memcpy(reinterpret_cast<char*>(&material.property), &new_tex_id, sizeof(new_tex_id)); \
            } \
        }
        FOR_TEXTURED_MATERIAL_PROPERTIES(TEXTURED_MATERIAL_PROPERTY_REMAP)
#undef TEXTURED_MATERIAL_PROPERTY_REMAP
    }


    dedup_info.num_removed_textures += numOriginalTextures - numUsedTextures;
}


void Scene::validate()
{
    // Bounds checks
    int numMeshes = ilen(meshes);
    int numParameterizedMeshes = ilen(parameterized_meshes);
    int numLodGroups = ilen(lod_groups);
    int numInstances = ilen(instances);
    int numMaterials = ilen(materials);
    int numTextures = ilen(textures);

    if (!numMaterials) {
        warning("No materials defined, adding a default material");
        materials.push_back(BaseMaterial());
        numMaterials = 1;
    }

    for (int i = 0; i < numMeshes; ++i) {
        auto& mesh = meshes[i];
        int numGeometries = ilen(mesh.geometries);
        for (int j = 0; j < numGeometries; ++j) {
            auto& geom = mesh.geometries[j];
            int numVertices = geom.num_verts();
            if (numVertices > 0 && geom.indices.empty() && (geom.format_flags & Geometry::NoIndices) != Geometry::NoIndices)
                throw_error("Geometry has vertices but no indices, and NoIndices flag is missing");
            int numTris = geom.num_tris();
            if (numVertices > numTris * 3)
                warning("More vertices than referenced by triangles in mesh %d, geometry %d", i, j);
        }
    }

    for (int i = 0; i < numParameterizedMeshes; ++i) {
        auto& pmesh = parameterized_meshes[i];
        if (pmesh.mesh_id < 0 || pmesh.mesh_id >= numMeshes)
            throw_error("Invalid mesh reference %d in parameterized mesh %d", pmesh.mesh_id, i);
        auto& mesh = meshes[pmesh.mesh_id];
        int numGeometries = mesh.num_geometries();

        int numMaterialOffsets = ilen(pmesh.material_offsets);
        if (numMaterialOffsets > 0 && numMaterialOffsets != numGeometries)
            throw_error("Number of material offsets in parameterized mesh %d not matching number of geometries in mesh %d", i, pmesh.mesh_id);

        if (pmesh.per_triangle_materials()) {
            if (pmesh.num_triangle_material_ids() != mesh.num_tris())
                throw_error("Number of material IDs in parameterized mesh %d not matching number of triangles in mesh %d", i, pmesh.mesh_id);
        }
        else {
            for (int j = 0; j < numGeometries; ++j) {
                int material_id = pmesh.material_offset(j);
                if (material_id < 0 || material_id >= numMaterials)
                    throw_error("Invalid material reference %d in parameterized mesh %d", material_id, i);
            }
        }
    }

    for (int i = 0; i < numLodGroups; ++i) {
        float lastDetailReduction = 0.0f;
        auto& lod_group = lod_groups[i];
        // todo: why are these two separate vectors?
        if (lod_group.detail_reduction.size() != lod_group.mesh_ids.size())
            throw_error("Mismatching LOD detail and LOD ID counts in lod group %d", i);
        for (auto dr : lod_group.detail_reduction) {
            if (dr < lastDetailReduction)
                throw_error("Out-of-order LOD detail reduction %f in lod group %d", dr, i);
            lastDetailReduction = dr;
        }
        for (auto pmesh_id : lod_group.mesh_ids) {
            if (pmesh_id < 0 || pmesh_id >= numParameterizedMeshes)
                throw_error("Out-of-bounds parameterized mesh ID %d in lod group %d", pmesh_id, i);
            if (parameterized_meshes[pmesh_id].lod_group != i)
                throw_error("Inconsistent lod group assignment in pmesh ID %d to lod group %d", pmesh_id, i);
        }
    }

    for (int i = 0; i < numInstances; ++i) {
        auto& instance = instances[i];
        if (instance.parameterized_mesh_id < 0 || instance.parameterized_mesh_id >= numParameterizedMeshes)
            throw_error("Invalid parameterized mesh reference %d in instance %d", instance.parameterized_mesh_id, i);
        //auto& pmesh = parameterized_meshes[i];
    }

    for (int i = 0; i < numMaterials; ++i) {
        // complete flags to enable necessary optional features
        if (materials[i].specular_transmission > 0.0f)
            materials[i].flags |= BASE_MATERIAL_EXTENDED;
        // todo: might want to check texture pointers?
        (void) numTextures;
    }
}


void Scene::load_vkrs(const std::string &file, SceneLoaderParams::PerFile const* override_params)
{
    std::cout << "Loading VulkanRenderer scene: " << file << "\n";

    auto errorHandler = [](VkrResult result, const char *msg)
    {
      throw_error(msg);
    };

    VkrScene vkrs{};
    if (vkr_open_scene(file.c_str(), &vkrs, errorHandler) != VKR_SUCCESS)
    {
      throw_error("Error opening %s", file.c_str());
    }

    FileMapping file_mapping(file);

    // note: load_vkrs is supported to be called on different files successively,
    // to assemble scenes distributed over multiple files
    int meshBase = ilen(this->meshes);
    int instanceBase = ilen(this->instances);
    int matBase = ilen(this->materials);
    int texBase = ilen(this->textures);
    int lodGroupBase = ilen(this->lod_groups);

    if (vkrs.numLodGroups > 0)
    {
        assert(vkrs.lodGroups[0].numLevelsOfDetail == 0);
        // todo: can we simplify this to more linear buffers?
        this->lod_groups.resize(lodGroupBase + vkrs.numLodGroups - 1);
        // Skip the first group, it's empty. We also construct our scene with
        // an empty group initially.
        for (size_t i = 1; i < vkrs.numLodGroups; ++i)
        {
            const VkrLodGroup &inputLodGroup = vkrs.lodGroups[i];
            LodGroup &group = lod_groups[lodGroupBase + i - 1];
            const size_t numLods = inputLodGroup.numLevelsOfDetail;
            group.mesh_ids.resize(numLods);
            group.detail_reduction.resize(numLods);

            for (size_t j = 0; j < numLods; ++j) {
                group.mesh_ids[j] = int_cast(inputLodGroup.meshIds[j] + meshBase);
                group.detail_reduction[j] = inputLodGroup.detailReduction[j];
            }
        }
    }

    index_t enforce_max_primitive_count = INT_MAX;
    this->meshes.resize(uint_bound(meshBase + vkrs.numMeshes));
    this->parameterized_meshes.resize(uint_bound(meshBase + vkrs.numMeshes));

    index_t maxTriCount = 0;
    for (int i = 0; i < (int) vkrs.numMeshes; ++i) {
        Mesh& mesh = this->meshes[meshBase + i];
        VkrMesh const& vkrm = vkrs.meshes[i];

        mesh.mesh_name = vkrm.name;

        mesh.geometries.resize(uint_bound(vkrm.numSegments));
        index_t baseTriangle = 0;
        int num_complete_segments = 0;
        for (int j = 0; j < (int) vkrm.numSegments; ++j) {
            Geometry& geom = mesh.geometries[num_complete_segments++];
            int numTriangles = to_ilen(vkrm.segmentNumTriangles[j]);
            int fullNumTriangles = numTriangles;
            geom.format_flags = Geometry::ImplicitIndices;

            if (size_t(baseTriangle + numTriangles) > enforce_max_primitive_count) {
                numTriangles = std::max(int(enforce_max_primitive_count - baseTriangle), 0);
                warning("Clamping mesh %d segment %d primitive count from %d to %d"
                    , i, j
                    , fullNumTriangles, numTriangles);
            }

            if (numTriangles == 0)
                continue;

            geom.vertices = { file_mapping, static_cast<size_t>(vkrm.vertexBufferOffset)
                + sizeof(uint64_t) * 3 * baseTriangle
                , sizeof(uint64_t) * 3 * numTriangles };
            geom.quantized_offset = glm::vec3(
                vkrm.vertexOffset[0], vkrm.vertexOffset[1], vkrm.vertexOffset[2]);
            geom.quantized_scaling = glm::vec3(
                vkrm.vertexScale[0], vkrm.vertexScale[1], vkrm.vertexScale[2]);
            geom.base = geom.quantized_offset;
            geom.extent = geom.quantized_scaling * float(0x1FFFFFu);
            geom.format_flags |= Geometry::QuantizedPositions;

            geom.normals = { file_mapping, static_cast<size_t>(vkrm.normalUvBufferOffset)
                + sizeof(uint64_t) * 3 * baseTriangle
                , sizeof(uint64_t) * 3 * numTriangles };
            geom.uvs = geom.normals;
            geom.format_flags |= Geometry::QuantizedNormalsAndUV;

            if (vkrm.flags & VKR_MESH_FLAGS_INDICES) {
                geom.indices = { file_mapping, static_cast<size_t>(vkrm.indexBufferOffset)
                    + sizeof(uint32_t) * 3 * baseTriangle
                    , sizeof(uint32_t) * 3 * numTriangles };
                geom.index_offset = int_cast(-3 * baseTriangle);
            }
            else
                geom.format_flags |= Geometry::NoIndices;

            baseTriangle += fullNumTriangles;
        }
        if (num_complete_segments < vkrm.numSegments) {
            warning("Removed %d empty geometry segments from mesh %d"
                    , int(vkrm.numSegments - num_complete_segments)
                    , i);
            mesh.geometries.resize(num_complete_segments);
        }

        maxTriCount = std::max(baseTriangle, maxTriCount);

        uint32_t dynamic_mesh_flags = (override_params && override_params->small_deformation) ? Mesh::SubtlyDynamic : Mesh::Dynamic;
        bool ignore_animation = override_params && override_params->ignore_animation;

        ParameterizedMesh& pmesh = this->parameterized_meshes[meshBase + i];
        pmesh.mesh_name = vkrm.name;
        pmesh.mesh_id = meshBase + i;
        pmesh.lod_group = (vkrm.lodGroup == 0) ? 0 : int_cast(lodGroupBase-1 + vkrm.lodGroup);
        if (vkrm.numSegments == 1 && vkrm.numMaterialsInRange > 1) {
            pmesh.material_offsets = { matBase + vkrm.materialIdBufferBase };
            pmesh.triangle_material_ids = { file_mapping, static_cast<size_t>(vkrm.materialIdBufferOffset),
                static_cast<size_t>(vkrm.materialIdSize * vkrm.numTriangles) };
            pmesh.material_id_bitcount = vkrm.materialIdSize * 8;
        } else {
            pmesh.material_offsets = std::vector<int32_t>(vkrm.segmentMaterialBaseOffsets, vkrm.segmentMaterialBaseOffsets + vkrm.numSegments);
            for (int& material_id : pmesh.material_offsets)
                material_id += matBase;
            pmesh.material_id_bitcount = 32;

            // allow setting alternative shaders via referenced material names
            char const SHADER_KEYWORD_PREFIX[] = "_SHADER";
            char const SHADERMAT_KEYWORD[] = "_SHADERMATERIAL_";
            char const SHADERMESH_KEYWORD[] = "_SHADERMESH_";
            char const SHADERSUBMESH_KEYWORD[] = "_SHADERSUBMESH_";
            for (int i = 0, ie = (int) pmesh.material_offsets.size(); i < ie; ++i) {
                char const* name = vkrs.materials[pmesh.material_offsets[i] - matBase].extended_name;
                char const* next_shader = strstr(name, SHADER_KEYWORD_PREFIX);
                while (char const* shader_begin = next_shader) {
                    next_shader = strstr(shader_begin + sizeof(SHADER_KEYWORD_PREFIX) - 1, SHADER_KEYWORD_PREFIX);
                    char const* shader_end = next_shader ? next_shader : shader_begin + strlen(shader_begin);
                    if (strncmp(shader_begin, SHADERMAT_KEYWORD, sizeof(SHADERMAT_KEYWORD) - 1) == 0) {
                        shader_begin += sizeof(SHADERMAT_KEYWORD) - 1;
                        pmesh.shader_names.resize(i + 1);
                        pmesh.shader_names[i].assign(shader_begin, shader_end);
#ifdef ENABLE_DYNAMIC_MESHES
                    } else if (strncmp(shader_begin, SHADERMESH_KEYWORD, sizeof(SHADERMESH_KEYWORD) - 1) == 0 && !ignore_animation) {
                        shader_begin += sizeof(SHADERMESH_KEYWORD) - 1;
                        mesh.mesh_shader_names.resize(mesh.geometries.size());
                        for (auto& mesh_shader_name : mesh.mesh_shader_names)
                            if (mesh_shader_name.empty()) {
                                mesh_shader_name.assign(shader_begin, shader_end);
                                mesh.flags |= dynamic_mesh_flags;
                            }
                    } else if (strncmp(shader_begin, SHADERSUBMESH_KEYWORD, sizeof(SHADERSUBMESH_KEYWORD) - 1) == 0 && !ignore_animation) {
                        shader_begin += sizeof(SHADERSUBMESH_KEYWORD) - 1;
                        mesh.mesh_shader_names.resize(i + 1);
                        mesh.mesh_shader_names[i].assign(shader_begin, shader_end);
                        mesh.flags |= dynamic_mesh_flags;
#endif
                    }
                }
            }
        }
    }

    this->instances.reserve(uint_bound(instanceBase + vkrs.numInstances));

    AnimationData animationData;
    animationData.numStaticTransforms = vkrs.numStaticTransforms;
    animationData.numAnimatedTransforms = vkrs.numAnimatedTransforms;
    animationData.numFrames = vkrs.numFrames;
    if (vkrs.animationData) {
        animationData.quantized = mapped_vector<unsigned char>(
            std::vector<unsigned char>(vkrs.animationData,
            vkrs.animationData + animationData.size_in_bytes())
        );
    } else {
        animationData.quantized = { 
            file_mapping,
            static_cast<size_t>(vkrs.animationOffset),
            animationData.size_in_bytes()
        };
    }

    const uint32_t animDataIndex = static_cast<uint32_t>(this->animation_data.size());
    animation_data.push_back(animationData);

    float instance_pruning_p = override_params ? override_params->instance_pruning_probability : 0.0f;
    for (int i = 0; i < (int) vkrs.numInstances; ++i) {
        // We ignore all instances that aren't the base level. This is because
        // currently, .vks stores instances even for higher levels.
        // This is wasteful, so we might consider changing this.
        VkrInstance const& vkri = vkrs.instances[i];
        const VkrMesh &mesh = vkrs.meshes[vkri.meshId];
        const VkrLodGroup &lodGroup = vkrs.lodGroups[mesh.lodGroup];
        const bool isBaseLevel = (lodGroup.numLevelsOfDetail == 0)
                               || (lodGroup.meshIds[0] == vkri.meshId);
        if (!isBaseLevel) {
            continue;
        }

        if (instance_pruning_p && halton2(i) < instance_pruning_p)
            continue;

        Instance& instance = this->instances.emplace_back();
        instance.animation_data_index = animDataIndex;
        instance.transform_index = vkri.transformIndex;
        instance.parameterized_mesh_id = int_cast(vkri.meshId + meshBase);
    }

    if (override_params && override_params->merge_partition_instances && vkrs.numInstances) {
        auto& transform_data = animation_data[animDataIndex];
        glm::mat4 cursor_transform(-1.0f);
        int cursor_i = instanceBase, ic = instanceBase;
        for (int i = instanceBase, ie = ilen(instances); i < ie; ++i) {
            auto& pmesh = parameterized_meshes[instances[i].parameterized_mesh_id];
            auto& mesh = meshes[pmesh.mesh_id];

            bool mergeable = pmesh.lod_group == 0 || lod_groups[pmesh.lod_group].mesh_ids.size() <= 1
                && !pmesh.per_triangle_materials()
                && pmesh.shader_names.empty()
                && mesh.mesh_shader_names.empty();
            if (!mergeable) {
                cursor_transform[3][3] = -1.0f;
                if (ic != i)
                    instances[ic] = instances[i];
                ++ic;
                continue;
            }

            glm::mat4 transform = transform_data.dequantize(instances[i].transform_index, 0);
            auto& ci_pmesh = parameterized_meshes[instances[cursor_i].parameterized_mesh_id];
            auto& ci_mesh = meshes[ci_pmesh.mesh_id];
            bool merge_with_prev = cursor_transform[3][3] > 0.0f
                && transform == cursor_transform
                && mesh.flags == ci_mesh.flags;
            if (!merge_with_prev) {
                cursor_transform = transform;
                if (ic != i)
                    instances[ic] = instances[i];
                cursor_i = ic++;
                continue;
            }

            ci_mesh.geometries.insert(ci_mesh.geometries.end()
                , mesh.geometries.begin(), mesh.geometries.end());
            ci_pmesh.material_offsets.insert(ci_pmesh.material_offsets.end()
                , pmesh.material_offsets.begin(), pmesh.material_offsets.end());
            ci_pmesh.has_overrides_applied = true;
        }
        instances.resize(ic);
    }

    // apply LOD overrides after loading correct instances
    if (override_params && override_params->remove_first_LODs) {
        int remove_first_LODs = override_params->remove_first_LODs;
        for (int i = lodGroupBase, ie = ilen(lod_groups); i < ie; ++i) {
            LodGroup& group = lod_groups[i];
            int first_lod = std::min(remove_first_LODs, ilen(group.mesh_ids)-1);
            if (first_lod > 0) {
                for (int mesh_id : group.mesh_ids)
                    parameterized_meshes[mesh_id].has_overrides_applied = true;
                for (int j = 0; j < first_lod; ++j)
                    group.mesh_ids[j] = group.mesh_ids[first_lod];
                parameterized_meshes[group.mesh_ids[0]].lod_group = i;
            }
        }
    }

    std::string material_name_prefix;
    if (!strstr(file.c_str(), "Terrain"))
        material_name_prefix = get_file_basename(file) + '/';

    this->textures.resize(uint_bound(texBase + vkrs.numMaterials * 3));
    this->materials.resize(uint_bound(matBase + vkrs.numMaterials));
    this->material_names.resize(uint_bound(matBase + vkrs.numMaterials));
    bool ignore_textures = override_params && override_params->ignore_textures;
    bool load_specularity = override_params && override_params->load_specularity;
    for (int i = 0; i < (int) vkrs.numMaterials; ++i) {
      int materialId = matBase + i;
      BaseMaterial& material = this->materials[materialId];
      const VkrMaterial& vkrm = vkrs.materials[i];

      material_names[materialId] = material_name_prefix + vkrm.name;

      int id = texBase + i*3;

      const VkrTexture& color = vkrm.texBaseColor;
      Image& color_img = this->textures[id];
      bool hasAlpha = false;
      if (color.filename && !ignore_textures) {
        // VK_FORMAT_BC1_RGBA_UNORM_BLOCK = 133
        // VK_FORMAT_BC1_RGBA_SRGB_BLOCK = 134
        // VK_FORMAT_BC3_UNORM_BLOCK = 137
        // VK_FORMAT_BC3_SRGB_BLOCK = 138
        int bcFormat = 0;
        switch (color.format) {
          case 131: // fallthrough
          case 132:
            bcFormat = 1;
            break;
          case 133: // fallthrough
          case 134:
            bcFormat = -1;
            break;
          case 137: // fallthrough
          case 138:
            bcFormat = 3;
            break;
          default:
            break;
        }
        // VK_FORMAT_R8G8B8A8_SRGB = 43,
        // VK_FORMAT_R8G8B8A8_UNORM = 37,
        hasAlpha = (color.format == 133 || color.format == 134 || color.format == 137 || color.format == 138 || color.format == 37 || color.format == 43);
        color_img = Image{ .name = color.filename
          , .width = color.width
          , .height = color.height
          , .channels = 4
          , .img = { FileMapping(color.filename), static_cast<size_t>(color.dataOffset), static_cast<size_t>(color.dataSize) }
          , .color_space = ColorSpace::SRGB
          , .bcFormat = bcFormat
        };
      } else {
        color_img = Image{ .name = std::string(vkrm.name) + "_DefaultBaseColor"
          , .width = 1
          , .height = 1
          , .channels = 4
          , .img = { Buffer<uint8_t>({ 255, 255, 255, 255 }) }
          , .color_space = ColorSpace::SRGB
        };
        if (!ignore_textures)
            warning("missing color texture for %s (texture dir %s)",
              vkrm.name, vkrs.textureDir);
      }
      if (!hasAlpha)
          material.flags |= BASE_MATERIAL_NOALPHA;
      {
        uint32_t tex_mask = TEXTURED_PARAM_MASK;
        SET_TEXTURE_ID(tex_mask, id);
        memcpy(reinterpret_cast<char*>(&material.base_color.r), &tex_mask, sizeof(float));
      }

      ++id;

      const VkrTexture& normal = vkrm.texNormal;
      Image& normal_img = this->textures[id];
      if (normal.filename && !ignore_textures) {
        normal_img = Image{ .name = normal.filename
          , .width = normal.width
          , .height = normal.height
          , .channels = 4
          , .img = { FileMapping(normal.filename), static_cast<size_t>(normal.dataOffset), static_cast<size_t>(normal.dataSize) }
          , .color_space = ColorSpace::LINEAR
          , .bcFormat = 5
        };
      } else {
        normal_img = Image{ .name = std::string(vkrm.name) + "_DefaultNormal"
          , .width = 1
          , .height = 1
          , .channels = 4
          , .img = { Buffer<uint8_t>({ 127, 127, 127, 255 }) }
          , .color_space = ColorSpace::LINEAR
        };
        if (!ignore_textures)
            warning(
              "missing normal texture for %s (texture dir %s)", vkrm.name, vkrs.textureDir);
      }
      material.normal_map = id;

      ++id;

      const VkrTexture& specular = vkrm.texSpecularRoughnessMetalness;
      Image& specular_img = this->textures[id];
      if (specular.filename && !ignore_textures) {
        specular_img = Image{ .name = specular.filename
          , .width = specular.width
          , .height = specular.height
          , .channels = 4
          , .img = { FileMapping(specular.filename), static_cast<size_t>(specular.dataOffset), static_cast<size_t>(specular.dataSize) }
          , .color_space = ColorSpace::LINEAR
          , .bcFormat = 1
        };
      } else {
        specular_img = Image{ .name = std::string(vkrm.name) + "_DefaultSpecular"
          , .width = 1
          , .height = 1
          , .channels = 4
          , .img = { Buffer<uint8_t>({ 255, 127, 0, 255 }) }
          , .color_space = ColorSpace::LINEAR
        };
        if (!ignore_textures)
            warning(
              "missing specular texture for %s (texture dir %s)", vkrm.name, vkrs.textureDir);
      }
      {
        uint32_t tex_mask = TEXTURED_PARAM_MASK;
        SET_TEXTURE_ID(tex_mask, id);
        SET_TEXTURE_CHANNEL(tex_mask, 1);
        memcpy(reinterpret_cast<char*>(&material.roughness), &tex_mask, sizeof(float));
        tex_mask = TEXTURED_PARAM_MASK;
        SET_TEXTURE_ID(tex_mask, id);
        SET_TEXTURE_CHANNEL(tex_mask, 2);
        memcpy(reinterpret_cast<char*>(&material.metallic), &tex_mask, sizeof(float));
        if (load_specularity) {
            tex_mask = TEXTURED_PARAM_MASK;
            SET_TEXTURE_ID(tex_mask, id);
            SET_TEXTURE_CHANNEL(tex_mask, 0);
            memcpy(reinterpret_cast<char*>(&material.specular), &tex_mask, sizeof(float));
        }
      }

      if (vkrm.emissionIntensity > 0) {
          glm::vec3 override_base_color = glm::vec3(vkrm.emitterBaseColor[0], vkrm.emitterBaseColor[1], vkrm.emitterBaseColor[2]);
          if (override_base_color != glm::vec3(0.0f))
              material.base_color = override_base_color;
          material.emission_intensity = vkrm.emissionIntensity;
      }
      material.specular_transmission = vkrm.specularTransmission;
      if (material.specular_transmission
        && !std::strstr(vkrm.extended_name, "twosided") && !std::strstr(vkrm.extended_name, "doublesided")
        && !std::strstr(vkrm.extended_name, "TwoSided") && !std::strstr(vkrm.extended_name, "DoubleSided")) {
        material.flags |= BASE_MATERIAL_ONESIDED;
      }
      material.ior = vkrm.iorEta;

    }

    vkr_close_scene(&vkrs);
}


