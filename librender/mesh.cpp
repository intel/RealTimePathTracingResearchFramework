// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "mesh.h"
#include <algorithm>
#include <numeric>

int Geometry::num_verts() const
{
    return !(format_flags & QuantizedPositions) ? int_cast(vertices.count<glm::vec3>()) : int_cast(vertices.count<uint64_t>());
}

int Geometry::num_tris() const
{
    // note: we first compute and bound the number of indices to ensure that all potential calculations fit within int
    return !(format_flags & ImplicitIndices) ? int_cast(indices.size() * 3) / 3 : num_verts() / 3;
}

Mesh::Mesh(std::vector<Geometry> geometries)
    : geometries(std::move(geometries)) {
}

len_t Mesh::num_tris() const
{
    len_t n = 0;
    for (auto const& g : geometries)
        n += g.num_tris();
    return n;
}

int Mesh::num_geometries() const {
    return ilen(geometries);
}

ParameterizedMesh::ParameterizedMesh(size_t mesh_id, std::vector<uint32_t> material_ids)
    : mesh_id(int_cast(mesh_id))
    , triangle_material_ids(GenericBuffer(std::move(material_ids)))
    , material_id_bitcount(32)
{
}

int ParameterizedMesh::material_offset(int geo_idx) const {
    return material_offsets.empty() ? 0 : material_offsets.at(geo_idx); // note: at verifies bounds
}

bool ParameterizedMesh::per_triangle_materials() const {
    return !triangle_material_ids.empty();
}

len_t ParameterizedMesh::num_triangle_material_ids() const {
    return to_len(triangle_material_ids.nbytes() * 8 / material_id_bitcount);
}

int ParameterizedMesh::triangle_material_id(index_t idx) const {
    switch (material_id_bitcount) {
    case 8:
        return triangle_material_ids.bytes()[idx];
    case 16:
        return triangle_material_ids.as_range<uint16_t>().first[idx];
    case 32:
        return triangle_material_ids.as_range<uint32_t>().first[idx];
    default:
        assert(false);
        return 0;
    }
}
