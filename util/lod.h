// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "../librender/bounds.h"
#include "../librender/scene.h"

// Utilities related to LoD system

struct LoDUtils {
    // computes bounding sphere for a mesh
    static void compute_bounds(Sphere &bounding_sphere, const Mesh &mesh);

    // computes bounding spheres for a lod group.
    // This is just the combination of the lod mesh bounds
    static void compute_bounds(Sphere &bounding_sphere,
                               const Scene &scene,
                               const LodGroup &lod_group);
    
    // computes LoD distances for a given camera and lod group
    // The only relevant camera parameter is the fovy
    // Caller must ensure that lod_distances is large enough to fit all detail reductions
    // We also assume that all detail reductions are already validated
    static void compute_lod_distances(float* lod_distances,
                                      float camera_fovy,
                                      const Sphere &bounding_sphere,
                                      const std::vector<float> &detail_reductions);
};

class LoDSystem {
public:
    struct Settings {
        float global_lod_range_scale = 1.0f;
        float global_lod_range_offset = 0.0f;
    };

    // Unfortunately the Scene class is not alive during rendering, so we need to retain
    // specific information for LOD groups
    struct LoDGroupInfo {
        Sphere bounds;
        float avg_scale; // the average scale transformation applied through instance transforms
        uint32_t lod_distance_offset = 0;
        std::vector<float> detail_reductions;
    };

    LoDSystem();

    // Call this method every time scene or mesh geometry changed
    void initialize(const Scene& scene);

    // Call this method after camera fovy changed
    void update_camera(float fov_y);
    
    const Settings &get_settings() const;
    void update_settings(Settings &settings);

    const std::vector<LoDGroupInfo>& get_lod_group_infos() const {
        return _lod_group_infos;
    }

    // returns a pointer to the lod distances for a given lod group
    // The pointer is safe to be incremented up to the number of LoD levels in the group
    const float *get_lod_distances_for_group(uint32_t lod_group_idx) const;

    // Force-invalidate the LOD system (to make sure dependent buffers get updated, etc)
    void force_dirty();
    
    // Returns true the first time some LoD parameters changed. 
    bool check_and_reset_dirty();

private:

    void apply_global_settings();

    float _cam_fov_y = -1.0f;
    Settings _settings;
    std::vector<LoDGroupInfo> _lod_group_infos;
    // camera-dependent (but not yet scaled) lod ranges for each lod group stored in a linearized array
    std::vector<float> _camera_lod_distances;
    std::vector<float> _final_lod_distances;
    bool _is_dirty = false;
};
