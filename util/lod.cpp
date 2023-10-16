// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "lod.h"
#include "error_io.h"

void LoDUtils::compute_bounds(Sphere &bounding_sphere, const Mesh &mesh) {
    // unpack positions for all geometries and contruct a bounding sphere
    size_t num_total_verts = 0;
    for (const auto &geom : mesh.geometries)
        num_total_verts += geom.num_verts();
    std::vector<glm::vec3> unpacked_positions(num_total_verts);
    size_t offset = 0;
    for (const auto &geom : mesh.geometries) {
        geom.get_vertex_positions(&unpacked_positions[offset]);
        offset += geom.num_verts();
    }
    bounding_sphere = Sphere::boundPoints(unpacked_positions.data(), (int)num_total_verts);
}

// helper function to access LoD mesh by lod group index
inline int get_mesh_id_from_lod_group(const Scene &scene,
                                      const LodGroup &lod_group,
                                      int lod_idx)
{
    return scene.parameterized_meshes[lod_group.mesh_ids[lod_idx]].mesh_id;
}

void LoDUtils::compute_bounds(Sphere &bounding_sphere,
                              const Scene &scene,
                              const LodGroup &lod_group)
{
    if (lod_group.mesh_ids.empty())
        throw_error("LoDUtils::compute_bounds() cannot bound empty LoD Group");
    
    const auto &first_mesh = scene.meshes[get_mesh_id_from_lod_group(scene, lod_group, 0)];
    compute_bounds(bounding_sphere, first_mesh);

    for (size_t iLod = 1; iLod < lod_group.mesh_ids.size(); iLod++) {
        Sphere next_bounding_sphere;
        const auto &next_mesh = scene.meshes[get_mesh_id_from_lod_group(scene, lod_group, iLod)];
        compute_bounds(next_bounding_sphere, next_mesh);
        bounding_sphere += next_bounding_sphere;
    }
}

void LoDUtils::compute_lod_distances(float* lod_distances,
                                     float camera_fovy,
                                     const Sphere &bounding_sphere,
                                     const std::vector<float> &detail_reductions)
{
    const float fovy_radians = (camera_fovy * M_PI) / 180.0f;
    
    // how far is the most detailed LoD to cover 100% of the screen height?
    // (crude approximation, ignoring the sphere and focusing on a vertical line parallel to the screen with radius of the sphere)
    const float dist_reference = bounding_sphere.radius / tanf(0.5f * fovy_radians);
    
    // scale lod distances proportionally based on their detail reductions. 
    // 0 reduction is the reference details
    for (size_t iLod = 0; iLod < detail_reductions.size(); iLod++) {
        lod_distances[iLod] = dist_reference / (1.0f - detail_reductions[iLod]);
    }
}

LoDSystem::LoDSystem(){}

void LoDSystem::initialize(const Scene &scene) {
    // make sure we recompute lod distances, so "invalidate" the remembered fov_y
    _cam_fov_y = -1.0f;
    
    // compute bounds, reserve lod distances
    const size_t num_lod_groups = scene.lod_groups.size();
    size_t num_lod_distances = 0;
    _lod_group_infos.resize(num_lod_groups);
    // current assumption is that lod group 0 is empty, therefore we don't store lod distances
    _lod_group_infos[0].lod_distance_offset = 0;
    for (size_t iGroup = 1; iGroup < num_lod_groups; iGroup++) {
        const auto &lod_group = scene.lod_groups[iGroup];
        auto &lod_info = _lod_group_infos[iGroup];
        LoDUtils::compute_bounds(lod_info.bounds, scene, lod_group);
        
        lod_info.lod_distance_offset = num_lod_distances;
        num_lod_distances += lod_group.detail_reduction.size();

        lod_info.detail_reductions = lod_group.detail_reduction;
    }

    std::vector<float> lod_group_inst_counts(num_lod_groups, 0.0f);
    std::vector<float> lod_group_avg_scales(num_lod_groups, 0.0f);

    for (auto inst : scene.instances) {
        const auto &animData = scene.animation_data.at(inst.animation_data_index);
        constexpr uint32_t frame = 0;
        auto transform = animData.dequantize(inst.transform_index, frame);

        int lod_group_idx = scene.parameterized_meshes[inst.parameterized_mesh_id].lod_group;
        if (lod_group_idx > 0) {
            lod_group_inst_counts[lod_group_idx] += 1.0f;
            lod_group_avg_scales[lod_group_idx] += abs(transform[1][1]);
        }
    }
    for (size_t iGroup = 1; iGroup < num_lod_groups; iGroup++) {
        const float inst_count = lod_group_inst_counts[iGroup];
        if (inst_count > 0.0f) {
            _lod_group_infos[iGroup].avg_scale = lod_group_avg_scales[iGroup] / inst_count;
            _lod_group_infos[iGroup].bounds.radius *= _lod_group_infos[iGroup].avg_scale;
        }
    }

    _camera_lod_distances.resize(num_lod_distances);
    _final_lod_distances.resize(num_lod_distances);
}

void LoDSystem::update_camera(float fov_y) {
    if (_cam_fov_y == fov_y)
        return;
    // current assumption is that lod group 0 is empty.
    for (size_t iGroup = 1; iGroup < _lod_group_infos.size(); iGroup++) {
        const auto &lod_info = _lod_group_infos[iGroup];
        LoDUtils::compute_lod_distances(&_camera_lod_distances[lod_info.lod_distance_offset],
                                        fov_y,
                                        lod_info.bounds,
                                        lod_info.detail_reductions);
    }
    _cam_fov_y = fov_y;
    apply_global_settings();
}

void LoDSystem::apply_global_settings() {
    for (size_t i = 0; i < _camera_lod_distances.size(); i++) {
        _final_lod_distances[i] = _settings.global_lod_range_offset +
                               _camera_lod_distances[i] * _settings.global_lod_range_scale;
    }
    _is_dirty = true;
}

const LoDSystem::Settings &LoDSystem::get_settings() const
{
    return _settings;
}

void LoDSystem::update_settings(LoDSystem::Settings &settings) {
    _settings = settings;
    apply_global_settings();
}

const float *LoDSystem::get_lod_distances_for_group(uint32_t lod_group_idx) const
{
    if (_final_lod_distances.empty()) {
        return nullptr;
    }
    return &_final_lod_distances[_lod_group_infos[lod_group_idx].lod_distance_offset];
}

void LoDSystem::force_dirty() {
    _is_dirty = true;
}

bool LoDSystem::check_and_reset_dirty() {
    bool was_dirty = _is_dirty;
    _is_dirty = false;
    return was_dirty;
}
