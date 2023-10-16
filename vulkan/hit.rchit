// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#version 460
#extension GL_GOOGLE_include_directive : require

#extension GL_EXT_ray_tracing : require

#include "setup_iterative_pt.glsl"
#include "gpu_params.glsl"

#include "geometry.glsl"
#include "rt/hit.glsl"

layout(shaderRecordEXT, std430) buffer SBT {
    RenderMeshParams geom;
};

hitAttributeEXT vec2 attrib;

layout(location = PRIMARY_RAY) rayPayloadInEXT RTHit payload;

void main() {
    const uvec3 idx = 
#ifndef REQUIRE_UNROLLED_VERTICES
       ((geom.flags & GEOMETRY_FLAGS_IMPLICIT_INDICES) == 0) ? geom.indices.i[gl_PrimitiveID] :
#endif
       uvec3(gl_PrimitiveID * 3) + uvec3(0, 1, 2);

    mat3 vertices = calc_hit_vertices(geom.vertices,
#ifdef QUANTIZED_POSITIONS
        geom.quantized_scaling, geom.quantized_offset,
#endif
    idx);
    RTHit hit = calc_hit_attributes(gl_RayTmaxEXT, gl_PrimitiveID, attrib,
        vertices, idx, transpose(mat3(gl_WorldToObjectEXT)),
        geom.normals, geom.num_normals > 0,
#ifndef QUANTIZED_NORMALS_AND_UVS
        geom.uvs,
#endif
        geom.num_uvs > 0,
        geom.material_id, geom.materials
        );

#if NEED_MESH_ID_FOR_VISUALIZATION
    hit.parameterized_mesh_id = geom.paramerterized_mesh_data_id;
#endif

    payload = hit;
}
