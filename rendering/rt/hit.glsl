// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef RT_HIT_GLSL
#define RT_HIT_GLSL

#include "../defaults.glsl"
#ifdef DEFAULT_GEOMETRY_BUFFER_TYPES
    #include "geometry.h.glsl"
#endif

struct RTHit {
    vec3 normal;
	float dist;
	vec3 geo_normal;
	int material_id;
	vec3 tangent;
	float bitangent_l;
	vec2 uv;
#if NEED_MESH_ID_FOR_VISUALIZATION
    uint parameterized_mesh_id;
#endif
};

struct HitContext {
    float time;
    uint32_t object_seed;
};

#if defined(QUANTIZED_VERTEX_BUFFER_TYPE) || defined(QUANTIZED_NORMAL_UV_BUFFER_TYPE)
    #include "../../librender/dequantize.glsl"
#endif

inline mat3 calc_hit_vertices(PLAIN_VERTEX_BUFFER_TYPE verts, uvec3 idx) {
    const vec3 va = verts.v[idx.x];
    const vec3 vb = verts.v[idx.y];
    const vec3 vc = verts.v[idx.z];
    return mat3(va, vb, vc);
}
#ifdef QUANTIZED_VERTEX_BUFFER_TYPE
inline mat3 calc_hit_vertices(QUANTIZED_VERTEX_BUFFER_TYPE verts, vec3 quantized_scaling, vec3 quantized_offset, uvec3 idx) {
    const vec3 va = DEQUANTIZE_POSITION(verts.v[idx.x], quantized_scaling, quantized_offset);
    const vec3 vb = DEQUANTIZE_POSITION(verts.v[idx.y], quantized_scaling, quantized_offset);
    const vec3 vc = DEQUANTIZE_POSITION(verts.v[idx.z], quantized_scaling, quantized_offset);
    return mat3(va, vb, vc);
}
#endif

inline int calc_hit_material_id(int material_id_in, MATERIAL_ID_BUFFER_TYPE materials, uint primitive_id) {
    if (material_id_in < 0) {
        uint32_t material_id = (materials.id_4pack[primitive_id >> 2u] >> 8u * (primitive_id & 3u)) & 0xffu;
        return int(material_id) - material_id_in - 1;
    }
    else
        return material_id_in;
}

inline RTHit calc_hit_attributes(float ray_t, uint primitive_id, vec2 attrib,
    mat3 verts, uvec3 idx,
    mat3 normals_to_world, // transpose(gl_WorldToObjectEXT)
    mat3 normals, bool has_normals, mat3x2 uvs, bool has_uvs,
    int material_id, MATERIAL_ID_BUFFER_TYPE materials) {
    RTHit payload;
    payload.dist = ray_t;

    vec3 gn = cross(verts[1] - verts[0], verts[2] - verts[0]);
    
    vec3 n = gn;
    if (has_normals) {
        n = normals * vec3(1.f - attrib.x - attrib.y, attrib.x, attrib.y);
        if (dot(n, gn) < 0.0f)
            gn = -gn;
    }

    payload.geo_normal = gn * 0.5f; // note: pass triangle area
    payload.normal = n;

    vec2 uv = vec2(0);
    if (has_uvs)
        uv = uvs * vec3(1.f - attrib.x - attrib.y, attrib.x, attrib.y);

    payload.uv = uv;
    payload.material_id = calc_hit_material_id(material_id, materials, primitive_id);

    bool requires_tangent = true;
#ifdef OBJECT_SPACE_HIT_TRANSFORMATION_EXTENSION
    //OBJECT_SPACE_HIT_TRANSFORMATION_EXTENSION(payload, requires_tangent);
#endif

    payload.geo_normal = normals_to_world * payload.geo_normal;
    payload.normal = normalize(normals_to_world * payload.normal);

    //float rescale_det = 1.0f; // USE_MIPMAPPING requires precise derivatives
    if (requires_tangent && has_uvs) {
        uv = uvs * vec3(1.f - attrib.x - attrib.y, attrib.x, attrib.y);

        float posframe_det = length(gn);
        vec3 frame_n = gn / (posframe_det * posframe_det);
        // compute tangent space
        vec3 dp2perp = cross(verts[2] - verts[0], frame_n);
        vec3 dp1perp = cross(frame_n, verts[1] - verts[0]);
        vec2 duv1 = uvs[1] - uvs[0];
        vec2 duv2 = uvs[2] - uvs[0];

        vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
        vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
        
        T = normals_to_world * T;
        B = normals_to_world * B;
        
        float Tlen = length(T);
        float Blen = length(B);
        //rescale_det = posframe_det / sqrt(Tlen * Blen);
        //rescale_det = posframe_det / max(Tlen, Blen);

        if (Tlen > 0.0 && !isinf(Tlen) && !isnan(Tlen)) {
            payload.tangent = T; // * rescale_det;
            payload.bitangent_l = dot(normalize(cross(payload.geo_normal, T)), B); // * rescale_det;
            requires_tangent = false;
        }
    }
    if (requires_tangent) {
        payload.tangent = normalize( normals_to_world * cross(verts[2] - verts[0], gn) );
        payload.bitangent_l = 1.0f;
    }

    return payload;
}

#if defined(PLAIN_NORMAL_BUFFER_TYPE) && defined(PLAIN_UV_BUFFER_TYPE)
inline RTHit calc_hit_attributes(float ray_t, uint primitive_id, vec2 attrib,
    mat3 verts, uvec3 idx,
    mat3 normals_to_world,
    PLAIN_NORMAL_BUFFER_TYPE n_buf, bool has_normals, PLAIN_UV_BUFFER_TYPE uv_buf, bool has_uvs,
    int material_id, MATERIAL_ID_BUFFER_TYPE material_id_buf) {

    mat3 normals;
    if (has_normals) {
        const vec3 na = n_buf.n[idx.x];
        const vec3 nb = n_buf.n[idx.y];
        const vec3 nc = n_buf.n[idx.z];
        normals = mat3(na, nb, nc);
    }

    mat3x2 uvs;
    if (has_uvs) {
        const vec2 uva = uv_buf.uv[idx.x];
        const vec2 uvb = uv_buf.uv[idx.y];
        const vec2 uvc = uv_buf.uv[idx.z];
        uvs = mat3x2(uva, uvb, uvc);
    }

    return calc_hit_attributes(ray_t, primitive_id, attrib,
        verts, idx,
        normals_to_world,
        normals, has_normals, uvs, has_uvs,
        material_id, material_id_buf
        );
}
#endif
#ifdef QUANTIZED_NORMAL_UV_BUFFER_TYPE
inline RTHit calc_hit_attributes(float ray_t, uint primitive_id, vec2 attrib,
    mat3 verts, uvec3 idx,
    mat3 normals_to_world,
    QUANTIZED_NORMAL_UV_BUFFER_TYPE normals_and_uvs, bool has_normals, bool has_uvs,
    int material_id, MATERIAL_ID_BUFFER_TYPE material_id_buf) {

    GLSL_UINT64 n_uv_a, n_uv_b, n_uv_c;
    if (has_normals || has_uvs) {
        n_uv_a = normals_and_uvs.n_uvs[idx.x];
        n_uv_b = normals_and_uvs.n_uvs[idx.y];
        n_uv_c = normals_and_uvs.n_uvs[idx.z];
    }

    mat3 normals;
    if (has_normals) {
        const vec3 na = dequantize_normal(uint32_t(n_uv_a));
        const vec3 nb = dequantize_normal(uint32_t(n_uv_b));
        const vec3 nc = dequantize_normal(uint32_t(n_uv_c));
        normals = mat3(na, nb, nc);
    }

    mat3x2 uvs;
    if (has_uvs) {
#ifdef GLSL_SPLIT_INT64
        const vec2 uva = dequantize_uv(uint32_t(n_uv_a.y));
        const vec2 uvb = dequantize_uv(uint32_t(n_uv_b.y));
        const vec2 uvc = dequantize_uv(uint32_t(n_uv_c.y));
#else
        const vec2 uva = dequantize_uv(uint32_t(n_uv_a >> 32));
        const vec2 uvb = dequantize_uv(uint32_t(n_uv_b >> 32));
        const vec2 uvc = dequantize_uv(uint32_t(n_uv_c >> 32));
#endif
        uvs = mat3x2(uva, uvb, uvc);
    }

    return calc_hit_attributes(ray_t, primitive_id, attrib,
        verts, idx,
        normals_to_world,
        normals, has_normals, uvs, has_uvs,
        material_id, material_id_buf
        );
}
#endif

#ifdef ENABLE_REALTIME_RESOLVE
inline mat3 calc_hit_motion_vectors(MOTION_VECTOR_BUFFER_TYPE verts, uvec3 idx) {
    const vec3 va = vec3(verts.m_components[idx.x*3], verts.m_components[idx.x*3+1], verts.m_components[idx.x*3+2]);
    const vec3 vb = vec3(verts.m_components[idx.y*3], verts.m_components[idx.y*3+1], verts.m_components[idx.y*3+2]);
    const vec3 vc = vec3(verts.m_components[idx.z*3], verts.m_components[idx.z*3+1], verts.m_components[idx.z*3+2]);
    return mat3(va, vb, vc);
}
#endif

#endif // RT_HIT_GLSL

