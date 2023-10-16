// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef RT_GEOMETRY_GLSL
#define RT_GEOMETRY_GLSL

#ifdef DEFAULT_GEOMETRY_BUFFER_TYPES

#ifndef INDEX_BUFFER_TYPE
struct IndexBuffer { GLM(uvec3)* i; };
#define INDEX_BUFFER_TYPE IndexBuffer
#endif

#ifndef PLAIN_VERTEX_BUFFER_TYPE
struct VertexBuffer { GLM(vec3)* v; };
#define PLAIN_VERTEX_BUFFER_TYPE VertexBuffer
#endif
#ifndef QUANTIZED_VERTEX_BUFFER_TYPE
struct QuantizedVertexBuffer { uint64_t* v; };
#define QUANTIZED_VERTEX_BUFFER_TYPE QuantizedVertexBuffer
#endif

#ifndef PLAIN_NORMAL_BUFFER_TYPE
struct NormalBuffer { GLM(vec3)* n; };
#define PLAIN_NORMAL_BUFFER_TYPE NormalBuffer
#endif
#ifndef PLAIN_UV_BUFFER_TYPE
struct UVBuffer { GLM(vec2)* uv; };
#define PLAIN_UV_BUFFER_TYPE UVBuffer
#endif
#ifndef QUANTIZED_NORMAL_UV_BUFFER_TYPE
struct QuantizedNormalUVBuffer { uint64_t* n_uvs; };
#define QUANTIZED_NORMAL_UV_BUFFER_TYPE QuantizedNormalUVBuffer
#endif

#ifndef MATERIAL_ID_BUFFER_TYPE
struct MaterialIDBuffer { uint32_t* id_4pack; };
#define MATERIAL_ID_BUFFER_TYPE MaterialIDBuffer
#endif

#ifndef MOTION_VECTOR_BUFFER_TYPE
struct MotionVectorBuffer { uint16_t* m_components; };
#define MOTION_VECTOR_BUFFER_TYPE MotionVectorBuffer
#endif

#ifndef VERTEX_BUFFER_TYPE
#ifdef QUANTIZED_POSITIONS
#define VERTEX_BUFFER_TYPE QUANTIZED_VERTEX_BUFFER_TYPE
#else
#define VERTEX_BUFFER_TYPE PLAIN_VERTEX_BUFFER_TYPE
#endif
#endif
#ifndef NORMAL_BUFFER_TYPE
#ifdef QUANTIZED_NORMALS_AND_UVS
#define NORMAL_BUFFER_TYPE QUANTIZED_NORMAL_UV_BUFFER_TYPE
#else
#define NORMAL_BUFFER_TYPE PLAIN_NORMAL_BUFFER_TYPE
#endif
#endif
#ifndef UV_BUFFER_TYPE
#define UV_BUFFER_TYPE PLAIN_UV_BUFFER_TYPE
#endif

#endif

#define GEOMETRY_FLAGS_NOALPHA 0x01
#define GEOMETRY_FLAGS_IMPLICIT_INDICES 0x02
#define GEOMETRY_FLAGS_EXTENDED_SHADER 0x04
#define GEOMETRY_FLAGS_THIN 0x08
#define GEOMETRY_FLAGS_DYNAMIC 0x10

struct RenderMeshParams {
    INDEX_BUFFER_TYPE indices GLCPP_DEFAULT(= { });
    VERTEX_BUFFER_TYPE vertices GLCPP_DEFAULT(= { });
    GLM(vec3) quantized_scaling GLCPP_DEFAULT(= GLM(vec3)(1.0f)); int num_indices GLCPP_DEFAULT(= 0);
    GLM(vec3) quantized_offset GLCPP_DEFAULT(= GLM(vec3)(0.0f)); int num_vertices GLCPP_DEFAULT(= 0);

    PLAIN_VERTEX_BUFFER_TYPE dynamic_vertices GLCPP_DEFAULT(= { });
    NORMAL_BUFFER_TYPE normals GLCPP_DEFAULT(= { });
    UV_BUFFER_TYPE uvs GLCPP_DEFAULT(= { });
    MATERIAL_ID_BUFFER_TYPE materials GLCPP_DEFAULT(= { });

    uint32_t flags GLCPP_DEFAULT(= 0);
    int num_normals GLCPP_DEFAULT(= 0);
    int num_uvs GLCPP_DEFAULT(= 0);
    int material_id GLCPP_DEFAULT(= 0);

    MOTION_VECTOR_BUFFER_TYPE motion_vectors GLCPP_DEFAULT(= { });
    int paramerterized_mesh_id;      // consistent across LoDs (lead mesh)
    int paramerterized_mesh_data_id; // precise, different across LoDs
};

struct InstancedGeometry {
    GLM(mat4) instance_to_world;
    GLM(mat4) world_to_instance;

    RenderMeshParams geometry;
};

#endif
