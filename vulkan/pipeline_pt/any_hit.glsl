// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifdef SANDBOX_PATH_TRACER
#include "../setup_iterative_pt.glsl"
#include "../gpu_params.glsl"
layout(push_constant) uniform PushConstants {
    PUSH_CONSTANT_PARAMETERS
};
layout(binding = VIEW_PARAMS_BIND_POINT, set = 0, std140) uniform VPBuf {
    LOCAL_CONSTANT_PARAMETERS
};
#else
#include "setup_recursive_pt.glsl"
#endif

#include "../gpu_params.glsl"
#include "../geometry.glsl"
#include "rt/hit.glsl"

#define NO_TEXTURE_GRAD
#include "rt/materials.glsl"
#include "bsdfs/gltf_bsdf.glsl"

#include "pointsets/lcg_rng.glsl"

layout(shaderRecordEXT, std430) buffer SBT {
    RenderMeshParams geom;
};

layout(binding = MATERIALS_BIND_POINT, set = 0, scalar) buffer MaterialParamsBuffer {
    MATERIAL_PARAMS material_params[];
};

layout(binding = SCENE_PARAMS_BIND_POINT, set = 0, std140) uniform GPBuf {
    GLOBAL_CONSTANT_PARAMETERS
};

layout(binding = 0, set = TEXTURE_BIND_SET) uniform sampler2D textures[];
#ifdef STANDARD_TEXTURE_BIND_SET
layout(binding = 0, set = STANDARD_TEXTURE_BIND_SET) uniform sampler2D standard_textures[];
#endif
#if (RBO_debug_mode != DEBUG_MODE_OFF)
layout(location = PRIMARY_RAY) rayPayloadInEXT RayPayload payload;
#endif

#define SCENE_GET_TEXTURE(tex_id) textures[nonuniformEXT(tex_id)]
#define SCENE_GET_STANDARD_TEXTURE(tex_id) standard_textures[nonuniformEXT(tex_id)]
#include "rt/material_textures.glsl"

hitAttributeEXT vec2 attrib;

void main()
{
  // Take into account this any hit
#if (RBO_debug_mode == DEBUG_MODE_ANY_HIT_COUNT_FULL_PATH) 
  payload.any_hit_count += 1.0f;
#elif (RBO_debug_mode == DEBUG_MODE_ANY_HIT_COUNT_PRIMARY_VISIBILITY)
    int bounce = get_bounce(payload);
    if (bounce == 0)
      payload.any_hit_count += 1.0f;
#endif

    const uvec3 primIdx = 
#ifndef REQUIRE_UNROLLED_VERTICES
       ((geom.flags & GEOMETRY_FLAGS_IMPLICIT_INDICES) == 0) ? geom.indices.i[gl_PrimitiveID] :
#endif
      uvec3(gl_PrimitiveID * 3) + uvec3(0, 1, 2);

#ifdef RT_GEOMETRY_LOOKUP_DYNAMIC
    mat3 vertices = calc_hit_vertices(geom.dynamic_vertices, primIdx);
#else
    mat3 vertices = calc_hit_vertices(geom.vertices,
#ifdef QUANTIZED_POSITIONS
      geom.quantized_scaling, geom.quantized_offset,
#endif
      primIdx);
#endif

    RTHit hit = calc_hit_attributes(gl_RayTmaxEXT,
      gl_PrimitiveID, attrib,
      vertices, primIdx, transpose(mat3(gl_WorldToObjectEXT)),
      geom.normals, geom.num_normals > 0,
#ifndef QUANTIZED_NORMALS_AND_UVS
      geom.uvs,
#endif
      geom.num_uvs > 0,
      geom.material_id, geom.materials
      );

#ifdef USE_MIPMAPPING
  float geometry_scale = geometry_scale_from_tmin(gl_WorldRayOriginEXT, gl_RayTminEXT) + gl_HitTEXT;
  float mip_bias = log2(max(4.0f * geometry_scale / render_params.focus_distance, 1.0f));
  mip_bias = min(mip_bias, 5.0f);
  material_texture_bias = mip_bias;
#endif

  mat2 duvdxy = mat2(0.f);
  float alpha = get_material_alpha(hit.material_id, material_params[nonuniformEXT(hit.material_id)]
    , HitPoint(gl_ObjectRayOriginEXT + gl_ObjectRayDirectionEXT * gl_HitTEXT
      , hit.uv, mat2x2(0.0), gl_ObjectRayDirectionEXT)
    );

#ifdef ANYHIT_FORCE_ALPHATEST
  if (!(alpha > 0.5f)) {
    ignoreIntersectionEXT;
  }
#else
  if (!(alpha > 0.f)) {
    ignoreIntersectionEXT;
  }
  else if (alpha < 1.f) {
    const uint32_t index = gl_PrimitiveID ^ view_params.frame_id;
    const uint32_t frame = gl_InstanceCustomIndexEXT ^ view_params.frame_offset;
    LCGRand rng = get_lcg_rng(index, frame, uvec4(gl_LaunchIDEXT.xy, gl_LaunchSizeEXT.xy));

    if (alpha < lcg_randomf(rng)) {
      ignoreIntersectionEXT;
    }
  }
#endif
}
