// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "file_mapping.h"

struct Geometry {
    enum FormatFlags {
        QuantizedPositions = 0x01,
        QuantizedNormalsAndUV = 0x02,
        ImplicitIndices = 0x04,
        NoIndices = 0x08 | ImplicitIndices
    };

    mapped_vector<void> vertices;
    mapped_vector<void> normals;
    mapped_vector<void> uvs;
    mapped_vector<glm::uvec3> indices;

    glm::vec3 base;
    glm::vec3 extent;

    glm::vec3 quantized_scaling;
    glm::vec3 quantized_offset;

    int index_offset = 0;

    uint32_t format_flags = 0;

    int num_verts() const;
    int num_tris() const;

    // extracts vertex positions to a destination. Caller must ensure that enough memory is allocated by checking num_verts() first
    void get_vertex_positions(glm::vec3 *dst_array) const;
    void tri_positions(int tri_idx, glm::vec3& v1, glm::vec3& v2, glm::vec3& v3) const;
    void tri_normals(int tri_idx, glm::vec3& v1, glm::vec3& v2, glm::vec3& v3) const;
    void tri_uvs(int tri_idx, glm::vec2& v1, glm::vec2& v2, glm::vec2& v3) const;
};

struct Mesh {
    enum Flags {
        Dynamic = 0x01,
        SubtlyDynamic = 0x02,
    };

    std::vector<Geometry> geometries;

    uint32_t flags = 0;

    std::string mesh_name;
    // Name of mesh shaders to apply to each geometry for e.g. animation
    std::vector<std::string> mesh_shader_names;

    unsigned vertices_revision = 0; // refit to vertices
    unsigned attributes_revision = 0; // copy attributes
    unsigned optimize_revision = 0; // re-optimize for vertices
    unsigned model_revision = 0; // completely replace with new data

    Mesh() = default;
    // legacy
    Mesh(std::vector<Geometry> geometries);

    len_t num_tris() const;
    int num_geometries() const;

    unsigned model_vertex_revision() const { return (vertices_revision & 0xffff) + (model_revision << 16); };
    unsigned model_attribute_revision() const { return (attributes_revision & 0xffff) + (model_revision << 16); };
    unsigned model_optimize_revision() const { return (optimize_revision & 0xffff) + (model_revision << 16); };
};

/* A parameterized mesh is a combination of a mesh containing the geometries
 * with a set of material parameters to set the appearance information for those
 * geometries.
 */
struct ParameterizedMesh {
    int mesh_id;
    int lod_group = 0;     // use this to index scene.lod_groups. Group 0 is a catch all
                           // for meshes that do not have levels of detail.
    // Material IDs for the geometry to parameterize this mesh with
    std::vector<int> material_offsets;
    mapped_vector<void> triangle_material_ids;
    int material_id_bitcount = 32; // default to uint32

    std::string mesh_name;
    // Names of material shaders to apply to each geometry
    std::vector<std::string> shader_names;

    bool has_overrides_applied = false;

    unsigned materials_revision = 0; // update material assignments
    unsigned shaders_revision = 0; // rebuild shader tables
    unsigned model_revision = 0; // completely replace with new data

    ParameterizedMesh() = default;
    // legacy
    ParameterizedMesh(size_t mesh_id, std::vector<uint32_t> material_ids);

    int material_offset(int geo_idx) const;
    len_t num_triangle_material_ids() const;
    int triangle_material_id(index_t tri_idx) const;
    bool per_triangle_materials() const;

    unsigned model_material_revision() const { return (materials_revision & 0xffff) + (model_revision << 16); };
    unsigned model_shader_revision() const { return (shaders_revision & 0xffff) + (model_revision << 16); };
};

/* An instance places a parameterized mesh at some location in the scene
 */
struct Instance {
    uint32_t animation_data_index { 0 }; // index in scene.animationData
    uint32_t transform_index { 0 }; // index in scene.animationData[animation_data_index]
    int32_t parameterized_mesh_id { 0 };
};
