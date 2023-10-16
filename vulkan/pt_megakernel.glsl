// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

// this file includes itself to enforce loop unrolling,
// skipping everything up to the loop and beyond then
#ifndef RECURSIVE_MEGAKERNEL_UNROLL

// options:
// #define DYNAMIC_LOOP_BOUNCES
 #define RECOMPUTE_HIT_ATTRIBUTES
// #define OPAQUE_SHADOWS

// make options accessible via UI
#ifdef RBO_unroll_bounces
#undef DYNAMIC_LOOP_BOUNCES
#endif

#include "setup_iterative_pt.glsl"
#include "language.glsl"
#include "gpu_params.glsl"

#include "pathspace.h"

#define MAKE_RANDOM_TABLE(TYPE, NAME) \
layout(binding = RANDOM_NUMBERS_BIND_POINT, set = 0, std430) buffer RNBuf {\
    TYPE NAME;\
};
#include "pointsets/selected_rng.glsl"
#include "pointsets/lcg_rng.glsl" // for alpha

#include "geometry.glsl"
#include "rt/hit.glsl"
#include "rt/footprint.glsl"

#include "rt/materials.glsl"
#include "bsdfs/gltf_bsdf.glsl"

#include "lights/tri.glsl"

#ifdef USE_RT_PIPELINE
#extension GL_EXT_ray_tracing : require
layout(location = PRIMARY_RAY) rayPayloadEXT RTHit payload;
layout(location = OCCLUSION_RAY) rayPayloadEXT int occlusion_hit;
#else
#extension GL_EXT_ray_query : require
layout(local_size_x=WORKGROUP_SIZE_X, local_size_y=WORKGROUP_SIZE_Y, local_size_z=1) in;
#endif
#include "setup_pixel_assignment.glsl"

layout(binding = SCENE_BIND_POINT, set = 0) uniform accelerationStructureEXT scene;
layout(binding = FRAMEBUFFER_BIND_POINT, set = 0, rgba8) uniform writeonly image2D framebuffer;

// todo: for motion vectors, we should switch from array of desc sets to desc set of arrays!
layout(binding = VIEW_PARAMS_BIND_POINT, set = 0, std140) uniform VPBuf {
    LOCAL_CONSTANT_PARAMETERS
};
layout(binding = SCENE_PARAMS_BIND_POINT, set = 0, std140) uniform GPBuf {
    GLOBAL_CONSTANT_PARAMETERS
};

#ifndef USE_RT_PIPELINE
layout(binding = INSTANCES_BIND_POINT, set = 0, std430) buffer GeometryParamsBuffer {
#ifdef IMPLICIT_INSTANCE_PARAMS
    RenderMeshParams instanced_geometry[];
#else
    InstancedGeometry instances[];
#endif
};
#endif

layout(binding = MATERIALS_BIND_POINT, set = 0, scalar) buffer MaterialParamsBuffer {
    MATERIAL_PARAMS material_params[];
};

layout(binding = LIGHTS_BIND_POINT, set = 0, std430) buffer LightParamsBuffer {
    TriLightData global_lights[];
};

layout(binding = 0, set = TEXTURE_BIND_SET) uniform sampler2D textures[];
#ifdef STANDARD_TEXTURE_BIND_SET
layout(binding = 0, set = STANDARD_TEXTURE_BIND_SET) uniform sampler2D standard_textures[];
#endif

layout(binding = RAYQUERIES_BIND_POINT, set = QUERY_BIND_SET, std430) buffer RayQueryBuf {
    RenderRayQuery ray_queries[];
};

layout(push_constant) uniform PushConstants {
    PUSH_CONSTANT_PARAMETERS
};

#define AOV_TARGET_PIXEL ivec2(gl_GlobalInvocationID.xy)
#include "accumulate.glsl"

// assemble light transport algorithm
#define SCENE_GET_TEXTURE(tex_id) textures[nonuniformEXT(tex_id)]
#define SCENE_GET_STANDARD_TEXTURE(tex_id) standard_textures[nonuniformEXT(tex_id)]

#define SCENE_GET_LIGHT_SOURCE(light_id) decode_tri_light(global_lights[nonuniformEXT(light_id)])
#define SCENE_GET_LIGHT_SOURCE_COUNT()   int(scene_params.light_sampling.light_count)

#define BINNED_LIGHTS_BIN_SIZE int(view_params.light_sampling.bin_size)
#define SCENE_GET_BINNED_LIGHTS_BIN_COUNT() (int(scene_params.light_sampling.light_count + (view_params.light_sampling.bin_size - 1)) / int(view_params.light_sampling.bin_size))

#define CUSTOM_MATERIAL_ALPHA
#include "rt/material_textures.glsl"
#include "mc/nee.glsl"

#include "mc/shade_megakernel.glsl"

#include "lights/sky_model_arhosek/sky_model.glsl"

vec3 compute_sky_illum(vec3 ray_origin, vec3 ray_dir, float prev_bsdf_pdf) {
    vec3 atmosphere_illum;
    vec3 sun_illum;

    vec3 dir = ray_dir;
    float ocean_coeff = 1.0f;
    if (dir.y <= 0.0f) {
        dir.y = -dir.y;
        ocean_coeff = 0.7 * pow(max(1.0 - abs(dir.y), 0.0), 5);
    }

    atmosphere_illum = max( skymodel_radiance(scene_params.sky_params, scene_params.sun_dir, dir), vec3(0.0f) ) * ocean_coeff;
    if (dot(dir, scene_params.sun_dir) >= scene_params.sun_cos_angle)
        sun_illum = vec3(scene_params.sun_radiance) * ocean_coeff;
    else
        sun_illum = vec3(0.0f);

    vec3 illum = vec3(0.0f);

    illum += abs(atmosphere_illum);
    {
        NEEQueryPoint query;
        query.point = ray_origin;
        query.normal = vec3(0.0f); // todo: query points need to be serializable, not currently using prev_n;
        query.w_o = vec3(0.0f); // todo: query points need to be serializable, not currently using prev_wo;
        query.info = NEEQueryInfo(0);
#ifndef PT_DISABLE_NEE
        float light_pdf = eval_direct_sun_light_pdf(query, ray_dir);
        float w = nee_mis_heuristic(1.f, prev_bsdf_pdf, 1.f, light_pdf);
#else
        float w = 1.0f;
#endif
        illum += w * abs(sun_illum);
    }

    return illum;
}

#ifndef USE_RT_PIPELINE
// returns true if traversal needs to continue
bool generate_candidate_hit(float dist, vec2 attrib, int instanceIdx, int geometryIdx, int primitiveIdx
    , vec3 local_ray_orig, vec3 local_ray_dir, inout RTHit hit, inout LCGRand alpha_rng, bool visibility_only) {
#ifdef IMPLICIT_INSTANCE_PARAMS
    #define geom instanced_geometry[geometryIdx]
#else
    //InstancedGeometry instance = instances[instanceIdx + geometryIdx];
    //Geometry geom = instance.geometry;
    #define instance instances[instanceIdx + geometryIdx]
    #define geom instances[instanceIdx + geometryIdx].geometry
    // note: this is currently required to pass incomplete implementation
    // of nested pointer assignment in the validation layer
#endif

    if (visibility_only) {
        if ((geom.flags & GEOMETRY_FLAGS_NOALPHA) != 0)
            return false;
    }

    const uvec3 idx =
#ifndef REQUIRE_UNROLLED_VERTICES
       ((geom.flags & GEOMETRY_FLAGS_IMPLICIT_INDICES) == 0) ? geom.indices.i[primitiveIdx] :
#endif
    uvec3(primitiveIdx * 3) + uvec3(0, 1, 2);

    mat3 vertices = calc_hit_vertices(geom.vertices,
#ifdef QUANTIZED_POSITIONS
        geom.quantized_scaling, geom.quantized_offset,
#endif
    idx);
    hit = calc_hit_attributes(dist, primitiveIdx, attrib,
        vertices, idx,
#ifdef IMPLICIT_INSTANCE_PARAMS
        mat3(0.0),
#ifndef RECOMPUTE_HIT_ATTRIBUTES
        #error "Implicit instance parameters require final hit attribute recomputation"
#endif
#else
        transpose(mat3(instance.world_to_instance)),
#endif
        geom.normals, geom.num_normals > 0,
#ifndef QUANTIZED_NORMALS_AND_UVS
        geom.uvs,
#endif
        geom.num_uvs > 0,
        geom.material_id, geom.materials
        );

    #undef instance
    #undef geom

    MATERIAL_PARAMS mat_params = material_params[nonuniformEXT(hit.material_id)];
    if ((mat_params.flags & BASE_MATERIAL_NOALPHA) == 0) {
        float alpha = get_material_alpha(hit.material_id, mat_params, HitPoint(local_ray_orig + dist * local_ray_dir, hit.uv, mat2x2(0.0), local_ray_dir));
        if (!(alpha > 0.0f) || alpha < 1.0f && lcg_randomf(alpha_rng) > alpha)
            return true;
    }

    return false;
}
#endif

float geometry_scale = 0.0f;

bool raytrace_test_visibility(const vec3 from, const vec3 dir, float dist) {
    const uint32_t occlusion_flags = gl_RayFlagsTerminateOnFirstHitEXT
        | gl_RayFlagsSkipClosestHitShaderEXT;
#ifdef USE_RT_PIPELINE
    occlusion_hit = 1;
#endif
    float epsilon = geometry_scale_to_tmin(from, geometry_scale);
    if (dist - 2.f * epsilon > 0.0f) {
#ifndef USE_RT_PIPELINE
        rayQueryEXT rayQuery;
        rayQueryInitializeEXT(rayQuery, scene, occlusion_flags, 0xff,
            from, epsilon, dir, dist - epsilon);

        while (rayQueryProceedEXT(rayQuery)) {
            if (rayQueryGetIntersectionTypeEXT(rayQuery, false) != gl_RayQueryCandidateIntersectionTriangleEXT)
                continue;

            float dist = rayQueryGetIntersectionTEXT(rayQuery, false);
            vec2 attrib = rayQueryGetIntersectionBarycentricsEXT(rayQuery, false);

#ifdef IMPLICIT_INSTANCE_PARAMS
            int instanceIdx = rayQueryGetIntersectionInstanceIdEXT(rayQuery, false);
            int geometryIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false) + rayQueryGetIntersectionGeometryIndexEXT(rayQuery, false);
#else
            int instanceIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
            int geometryIdx = rayQueryGetIntersectionGeometryIndexEXT(rayQuery, false);
#endif
            int primitiveIdx = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, false);

            vec3 local_orig = rayQueryGetIntersectionObjectRayOriginEXT(rayQuery, false);
            vec3 local_dir = rayQueryGetIntersectionObjectRayDirectionEXT(rayQuery, false);

#ifdef OPAQUE_SHADOWS
            rayQueryTerminateEXT(rayQuery);
            return false;
#else
            const uint32_t index = primitiveIdx ^ view_params.frame_id;
            const uint32_t frame = instanceIdx ^ view_params.frame_offset;
            LCGRand alpha_rng = get_lcg_rng(index, frame, uvec4(gl_GlobalInvocationID.xy, view_params.frame_dims.xy));

            RTHit hit;
            if (!generate_candidate_hit(dist, attrib, instanceIdx, geometryIdx, primitiveIdx
                , local_orig, local_dir, hit, alpha_rng, true)) {
                rayQueryTerminateEXT(rayQuery);
                return false;
            }
#endif
        }
        return rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCandidateIntersectionTriangleEXT;
#else
        traceRayEXT(scene, occlusion_flags, 0xff,
            PRIMARY_RAY, 1, OCCLUSION_RAY, from, epsilon, dir, dist - epsilon, OCCLUSION_RAY);
        return 0 == occlusion_hit;
#endif
    }
    return true;
}

vec4 main_spp(uint sample_index, uint rnd_offset);
void main() {
#ifdef ENABLE_RAYQUERIES
    // note: this forbids any warp-wide collaborative work
    if (num_rayqueries > 0) {
        uint query_id = gl_GlobalInvocationIndex;
        if (query_id >= uint(num_rayqueries))
            return;
    }
#endif

    vec4 final_color;
    {
        uint sample_batch_offset = accumulation_frame_offset >= 0 ? uint(accumulation_frame_offset) : view_params.frame_id;
        uint sample_index = sample_batch_offset + gl_GlobalInvocationLayer;
        final_color = main_spp(sample_index, view_params.frame_offset);
    }

    // rematerialize just in case
    uint sample_batch_offset = accumulation_frame_offset >= 0 ? uint(accumulation_frame_offset) : view_params.frame_id;
    int sample_batch_size = accumulation_batch_size > 0 ? accumulation_batch_size : render_params.batch_spp;
    uint sample_index = sample_batch_offset + gl_GlobalInvocationLayer;

#ifdef ENABLE_RAYQUERIES
    if (num_rayqueries > 0) {
        accumulate_query(gl_GlobalInvocationIndex, final_color, sample_index);
        return;
    }
#endif

    ivec2 fb_pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 fb_dims = ivec2(view_params.frame_dims);
    if (fb_pixel.x < fb_dims.x && fb_pixel.y < fb_dims.y)
        accumulate(fb_pixel, final_color, sample_index, (accumulation_flags & ACCUMULATION_FLAGS_ATOMIC) != 0);
}

vec4 main_spp(uint sample_index, uint rnd_offset) {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    const vec2 dims = view_params.frame_dims;
   
    RANDOM_STATE rng = GET_RNG(sample_index, rnd_offset, uvec4(gl_GlobalInvocationID.xy, view_params.frame_dims.xy));
    vec2 point = vec2(pixel.x + 0.5f, pixel.y + 0.5f);
    if (render_params.enable_raster_taa == 0)
        point += SAMPLE_PIXEL_FILTER(RANDOM_FLOAT2(rng, DIM_PIXEL_X));
    point /= dims;
    if (render_params.enable_raster_taa != 0)
        point += 0.5f * view_params.screen_jitter;

    vec3 ray_origin = view_params.cam_pos.xyz;
    vec3 ray_dir = normalize(point.x * view_params.cam_du.xyz + point.y * view_params.cam_dv.xyz + view_params.cam_dir_top_left.xyz);
    float t_min = 0;
    float t_max = 2.e32f;

#ifdef ENABLE_RAYQUERIES
    if (num_rayqueries > 0) {
        uint query_id = gl_GlobalInvocationIndex;
        ray_origin = ray_queries[query_id].origin;
        ray_dir = ray_queries[query_id].dir;
        t_max = ray_queries[query_id].t_max;
    }
#endif

    float camera_footprint = 0.0f, transport_footprint = 0.0f;
#if defined(USE_MIPMAPPING) || defined(TRANSPORT_ROUGHENING)
    mat2 texture_footprint = mat2(0.0f);
    float total_t = 0.0f;
#endif
    {
        vec3 dpdx = view_params.cam_du.xyz / dims.x;
        vec3 dpdy = view_params.cam_dv.xyz / dims.y;
        // footprint for which we estimate and control the variance of averages
        camera_footprint += length(cross(dpdx, dpdy));
        // mipmapping/prefiltering footprint
        dpdx *= render_params.pixel_radius;
        dpdy *= render_params.pixel_radius;
#if defined(USE_MIPMAPPING)
        texture_footprint = dpdxy_to_footprint(ray_dir, dpdx, dpdy);
#endif
    }

#if RBO_rng_variant == RNG_VARIANT_UNIFORM
    #define alpha_rng rng
#else
    LCGRand alpha_rng = get_lcg_rng(sample_index, rnd_offset, uvec4(gl_GlobalInvocationID.xy, view_params.frame_dims.xy));
#endif

    //int realBounce = 0;
    vec3 illum = vec3(0.f);
    vec3 path_throughput = vec3(1.f);
    // data for emitter MIS
    ShadingSampleState shading_state = init_shading_sample_state();
    shading_state.output_channel = render_params.output_channel;
    //vec3 prev_wo = vec3(-ray_dir);
    //vec3 prev_n = ray_dir;

#ifdef EXPLICIT_MASK
    bool bounce_active_mask = true;
    #define EXPLICIT_MASK_BEGIN \
        if (bounce_active_mask) { \
            bool masked_execution_reached_end = false; \
            do {
    #define EXPLICIT_MASK_END \
                masked_execution_reached_end = true; \
            } while (false); \
            if (!masked_execution_reached_end) \
                bounce_active_mask = false; \
        }
    #define EXPLICIT_MASK_ACTIVE bounce_active_mask
    #define EXPLICIT_MASK_ANY_RUNNING subgroupAny(bounce_active_mask)
#else
    #define EXPLICIT_MASK_BEGIN
    #define EXPLICIT_MASK_END
    #define EXPLICIT_MASK_ACTIVE true
    #define EXPLICIT_MASK_ANY_RUNNING true
#endif

#ifndef DYNAMIC_LOOP_BOUNCES
    do {
    int unrollBounceIdx = 0;
    #define RECURSIVE_MEGAKERNEL_UNROLL
    #include "pt_megakernel.glsl"
    if (++unrollBounceIdx >= render_params.max_path_depth) break;
    #include "pt_megakernel.glsl"
    if (++unrollBounceIdx >= render_params.max_path_depth) break;
    #include "pt_megakernel.glsl"
    if (++unrollBounceIdx >= render_params.max_path_depth) break;
    #include "pt_megakernel.glsl"
    if (++unrollBounceIdx >= render_params.max_path_depth) break;
    #include "pt_megakernel.glsl"
    if (++unrollBounceIdx >= render_params.max_path_depth) break;
    #include "pt_megakernel.glsl"
    if (++unrollBounceIdx >= render_params.max_path_depth) break;
    #include "pt_megakernel.glsl"
    if (++unrollBounceIdx >= render_params.max_path_depth) break;
    #include "pt_megakernel.glsl"
    if (++unrollBounceIdx >= render_params.max_path_depth) break;
    #include "pt_megakernel.glsl"
    // note: these checks should not usually fail, recursive BSDFs should abort early
    if (++unrollBounceIdx >= render_params.max_path_depth) break;
    illum = vec3(1.0f, 0.0f, 1.0f); // error!
    } while (false);
    #undef RECURSIVE_MEGAKERNEL_UNROLL
#else
    DYNAMIC_FOR (int unrollBounceIdx = 0; unrollBounceIdx < render_params.max_path_depth; ++unrollBounceIdx)
#endif // DYNAMIC_LOOP_BOUNCES
#endif // RECURSIVE_MEGAKERNEL_UNROLL
#if defined(DYNAMIC_LOOP_BOUNCES) || defined(RECURSIVE_MEGAKERNEL_UNROLL)
    if (unrollBounceIdx == 0 || EXPLICIT_MASK_ANY_RUNNING)
    {
        RANDOM_SET_DIM(rng, DIM_CAMERA_END + unrollBounceIdx * (DIM_VERTEX_END + DIM_LIGHT_END));

        RTHit hit;
        vec3 motion_vector = vec3(0.0f);
        // note: currently unsupported in RT pipeline mode
        vec3 local_ray_orig = vec3(0.0); vec3 local_ray_dir = vec3(0.0);
        int primitiveIdx, instanceIdx;
EXPLICIT_MASK_BEGIN
        // ray query scope
        {

        const uint32_t traversalFlags = 0;
#ifdef USE_RT_PIPELINE
        traceRayEXT(scene, traversalFlags, 0xff, PRIMARY_RAY, 1, PRIMARY_RAY,
                ray_origin, t_min, ray_dir, t_max, PRIMARY_RAY);
        bool was_miss = payload.dist < 0.f;
#else
        rayQueryEXT rayQuery;
        rayQueryInitializeEXT(rayQuery, scene, traversalFlags, 0xff,
            ray_origin, t_min, ray_dir, t_max);

        while (rayQueryProceedEXT(rayQuery)) {
            if (rayQueryGetIntersectionTypeEXT(rayQuery, false) != gl_RayQueryCandidateIntersectionTriangleEXT)
                continue;

            float dist = rayQueryGetIntersectionTEXT(rayQuery, false);
            vec2 attrib = rayQueryGetIntersectionBarycentricsEXT(rayQuery, false);

#ifdef IMPLICIT_INSTANCE_PARAMS
            instanceIdx = rayQueryGetIntersectionInstanceIdEXT(rayQuery, false);
            int geometryIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false) + rayQueryGetIntersectionGeometryIndexEXT(rayQuery, false);
#else
            instanceIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, false);
            int geometryIdx = rayQueryGetIntersectionGeometryIndexEXT(rayQuery, false);
#endif
            primitiveIdx = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, false);

#ifdef RECOMPUTE_HIT_ATTRIBUTES
            vec3 local_ray_orig; vec3 local_ray_dir;
#endif
            local_ray_orig = rayQueryGetIntersectionObjectRayOriginEXT(rayQuery, false);
            local_ray_dir = rayQueryGetIntersectionObjectRayDirectionEXT(rayQuery, false);

            RTHit tentative_hit;
            if (!generate_candidate_hit(dist, attrib, instanceIdx, geometryIdx, primitiveIdx
                , local_ray_orig, local_ray_dir, tentative_hit, alpha_rng, false)) {
#ifndef RECOMPUTE_HIT_ATTRIBUTES
                hit = tentative_hit;
#endif
                rayQueryConfirmIntersectionEXT(rayQuery);
            }
        }
        bool was_miss = rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionTriangleEXT;
        //ray_origin = rayQueryGetWorldRayOriginEXT(rayQuery);
        //ray_dir = rayQueryGetWorldRayDirectionEXT(rayQuery);
#endif
        // miss
        if (was_miss) {
            illum += path_throughput * compute_sky_illum(ray_origin, ray_dir, shading_state.prev_bounce_pdf);
#ifdef ENABLE_AOV_BUFFERS
            if (shading_state.bounce == 0 && (accumulation_flags & ACCUMULATION_FLAGS_AOVS) != 0) {
                store_geometry_aovs(vec3(0.0f), vec3(2.e32f), vec3(0.0f));
                store_material_aovs(vec3(0.0f), 1.0f, 1.0f);
            }
#endif
            break;
        }

#ifdef USE_RT_PIPELINE
        hit = payload;
#else

#ifdef RECOMPUTE_HIT_ATTRIBUTES
        float dist = rayQueryGetIntersectionTEXT(rayQuery, true);
        vec2 attrib = rayQueryGetIntersectionBarycentricsEXT(rayQuery, true);

#ifdef IMPLICIT_INSTANCE_PARAMS
        instanceIdx = rayQueryGetIntersectionInstanceIdEXT(rayQuery, true);
        int geometryIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true) + rayQueryGetIntersectionGeometryIndexEXT(rayQuery, true);
#else
        instanceIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
        int geometryIdx = rayQueryGetIntersectionGeometryIndexEXT(rayQuery, true);
#endif
        primitiveIdx = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);

#ifdef IMPLICIT_INSTANCE_PARAMS
        #define geom instanced_geometry[geometryIdx]
#else
        //InstancedGeometry instance = instances[instanceIdx + geometryIdx];
        //Geometry geom = instance.geometry;
        #define instance instances[instanceIdx + geometryIdx]
        #define geom instances[instanceIdx + geometryIdx].geometry
        // note: this is currently required to pass incomplete implementation
        // of nested pointer assignment in the validation layer
#endif

        const uvec3 idx =
#ifndef REQUIRE_UNROLLED_VERTICES
            ((geom.flags & GEOMETRY_FLAGS_IMPLICIT_INDICES) == 0) ? geom.indices.i[primitiveIdx] :
#endif
            uvec3(primitiveIdx * 3) + uvec3(0, 1, 2);

        mat3 vertices;
#ifdef ENABLE_DYNAMIC_MESHES
        if ((geom.flags & GEOMETRY_FLAGS_DYNAMIC) != 0) {
            vertices = calc_hit_vertices(geom.dynamic_vertices, idx);
        } else
#endif
        {
            vertices = calc_hit_vertices(geom.vertices,
    #ifdef QUANTIZED_POSITIONS
                geom.quantized_scaling, geom.quantized_offset,
    #endif
                idx);
        }
        hit = calc_hit_attributes(dist, primitiveIdx, attrib,
            vertices, idx,
#ifdef IMPLICIT_INSTANCE_PARAMS
            transpose(mat3(rayQueryGetIntersectionWorldToObjectEXT(rayQuery, true))),
#else
            transpose(mat3(instance.world_to_instance)),
#endif
            geom.normals, geom.num_normals > 0,
    #ifndef QUANTIZED_NORMALS_AND_UVS
            geom.uvs,
    #endif
            geom.num_uvs > 0,
            geom.material_id, geom.materials
            );

#if defined(ENABLE_REALTIME_RESOLVE) && defined(ENABLE_DYNAMIC_MESHES)
        if ((geom.flags & GEOMETRY_FLAGS_DYNAMIC) != 0) {
            motion_vector = vec3(
#ifdef IMPLICIT_INSTANCE_PARAMS
              rayQueryGetIntersectionObjectToWorldEXT(rayQuery, true) * vec4(
#else
              instance.instance_to_world * vec4(
#endif
              calc_hit_motion_vectors(geom.motion_vectors, idx)
            * vec3(1.f - attrib.x - attrib.y, attrib.x, attrib.y)
            , 0.0f) );
        }
#endif

        #undef instance
        #undef geom

        local_ray_orig = rayQueryGetIntersectionObjectRayOriginEXT(rayQuery, true);
        local_ray_dir = rayQueryGetIntersectionObjectRayDirectionEXT(rayQuery, true);
#endif

#endif
        }
EXPLICIT_MASK_END

        float approx_tri_solid_angle = length(hit.geo_normal);
        hit.geo_normal /= approx_tri_solid_angle;
        approx_tri_solid_angle *= abs(dot(hit.geo_normal, ray_dir)) / (hit.dist * hit.dist);

#ifdef USE_MIPMAPPING
        mat2 duvdxy;
EXPLICIT_MASK_BEGIN
        total_t += hit.dist;
        {
            vec3 dpdx, dpdy;
            footprint_to_dpdxy(dpdx, dpdy, ray_dir, texture_footprint);
            vec3 dir_tangent_un = ray_dir - hit.geo_normal * dot(ray_dir, hit.geo_normal);
            float cosTheta2 = max(1.0f - dot(dir_tangent_un, dir_tangent_un), 0.0f);
            vec3 dir_tangent_elong = dir_tangent_un / (sqrt(cosTheta2) + cosTheta2);
            // GLSL non-square matrix order seems broken
            vec3 dpdx_ = dpdx + dir_tangent_elong * dot(dpdx, dir_tangent_un);
            vec3 dpdy_ = dpdy + dir_tangent_elong * dot(dpdy, dir_tangent_un);
    #ifdef TRANSPORT_MIPMAPPING
            //dpdx_ = normalize(dpdx_) * sqrt(length2(dpdx_) + transport_footprint);
            //dpdy_ = normalize(dpdy_) * sqrt(length2(dpdy_) + transport_footprint);
    #endif
            vec3 bitangent = hit.bitangent_l * cross(hit.geo_normal, normalize(hit.tangent));
            duvdxy = mat2x2(
                    dot(hit.tangent, dpdx_), dot(bitangent, dpdx_),
                    dot(hit.tangent, dpdy_), dot(bitangent, dpdy_)
                ) * total_t;
        }
        geometry_scale = total_t;
EXPLICIT_MASK_END
#else
        mat2 duvdxy = mat2(0.0f);
#endif
        //vec3 w_o = -ray_dir;
        #define w_o (-ray_dir)
        InteractionPoint interaction;
        interaction.p = ray_origin + hit.dist * ray_dir;
        interaction.instanceId = instanceIdx;
        interaction.primitiveId = primitiveIdx;

        interaction.gn = hit.geo_normal;
        interaction.n = hit.normal;

        uint32_t material_flags = 0;
EXPLICIT_MASK_BEGIN
        material_flags = material_params[nonuniformEXT(hit.material_id)].flags;
        // For opaque objects (or in the future, thin ones) make the normal face forward
        if (dot(w_o, interaction.gn) < 0.0) {
            if ((material_flags & BASE_MATERIAL_VOLUME) != 0) {
                interaction.p = ray_origin;
                hit.dist = 0.0f;
            }
            else if ((material_flags & BASE_MATERIAL_ONESIDED) == 0) {
                interaction.n = -interaction.n;
                interaction.gn = -interaction.gn;
            }
        }
        int normal_map = material_params[nonuniformEXT(hit.material_id)].normal_map;
        // apply normal mapping
        if (normal_map != -1) {
            vec3 v_y = normalize( cross(hit.normal, hit.tangent) );
            vec3 v_x = cross(v_y, hit.normal);
            v_x *= length(hit.tangent);
            v_y *= hit.bitangent_l;

#ifdef USE_MIPMAPPING
            // for now, reduce normal resolution with bounces (current representation not really filterable)
            float normal_lod = float(shading_state.bounce);
#else
            float normal_lod = 0.0f;
#endif
            vec3 map_nrm = textureLod(get_standard_texture_sampler(normal_map, hit.material_id, STANDARD_TEXTURE_NORMAL_SLOT), hit.uv, normal_lod).rgb;
            map_nrm = vec3(2.0f, 2.0f, 1.0f) * map_nrm - vec3(1.0f, 1.0f, 0.0f);
            // Z encoding might be unclear, just reconstruct
            map_nrm.z = sqrt(max(1.0f - map_nrm.x * map_nrm.x - map_nrm.y * map_nrm.y, 0.0f));
            mat3 iT_shframe = mat3(v_x, v_y, scene_params.normal_z_scale * interaction.n);
            interaction.n = normalize(iT_shframe * map_nrm);
        }

        // fix incident directions under geo hemisphere
        // note: this may violate strict energy conservation, due to scattering into more than the hemisphere
        {
            float nw = dot(w_o, interaction.n);
            float gnw = dot(w_o, interaction.gn);
            if (nw * gnw <= 0.0f) {
                float blend = gnw / (gnw - nw);
                //   dot(blend * interaction.n + (1-blend) * interaction.gn, w_o)
                // = (nw * gnw + (gnw - nw) * gnw - gnw * gnw) / (gnw - nw)
                // = 0
                interaction.n = normalize( mix(interaction.gn, interaction.n, blend - EPSILON) );
            }
        }

#ifdef ENABLE_AOV_BUFFERS
        if (shading_state.bounce == 0 && (accumulation_flags & ACCUMULATION_FLAGS_AOVS) != 0)
            store_geometry_aovs(interaction.n, interaction.p, motion_vector);
#endif

EXPLICIT_MASK_END

        interaction.v_y = normalize( cross(interaction.n, hit.tangent) );
        interaction.v_x = cross(interaction.v_y, interaction.n);

        {
            NEESampledArea nee_area;
            nee_area.type = LIGHT_TYPE_TRIANGLE;
            nee_area.approx_solid_angle = approx_tri_solid_angle;
            vec3 w_i;
            ShadingQueryAux aux;
            int shading_result = shade_megakernel(shading_state
                , illum, path_throughput
                , hit.material_id, material_params[nonuniformEXT(hit.material_id)]
                , HitPoint(local_ray_orig + hit.dist * local_ray_dir, hit.uv, duvdxy, local_ray_dir)
                , nee_area
                , w_o, interaction
                , rng, w_i, aux, EXPLICIT_MASK_ACTIVE);

EXPLICIT_MASK_BEGIN
            if (shading_result == SHADING_RESULT_TERMINATE)
                break;

#ifdef USE_MIPMAPPING
            if (dot(w_i, interaction.n) * dot(w_o, interaction.n) > -0.999f) {
                texture_footprint = reflect_footprint(w_i, ray_dir, texture_footprint);
            }
#endif
            ray_dir = w_i;
            ray_origin = interaction.p;
            if (shading_result > SHADING_RESULT_NULL)
                t_min = geometry_scale_to_tmin(ray_origin, total_t);
            else
                t_min = 0.0f;
            t_max = 1e20f;
EXPLICIT_MASK_END
        }

        // Russian roulette termination
EXPLICIT_MASK_BEGIN
        if (shading_state.bounce >= render_params.rr_path_depth) {
            float prefix_weight = max(path_throughput.x, max(path_throughput.y, path_throughput.z));

            float rr_prob = prefix_weight;
            float rr_sample = RANDOM_FLOAT1(rng, DIM_RR);
            if (shading_state.bounce > 6)
                rr_prob = min(0.95f, rr_prob); // todo: good?
            else
                rr_prob = min(1.0f, rr_prob);

            if (rr_sample < rr_prob)
                path_throughput /= rr_prob;
            else
                break;
        }
EXPLICIT_MASK_END
    }

#endif // not RECURSIVE_MEGAKERNEL_UNROLL
#ifndef RECURSIVE_MEGAKERNEL_UNROLL

    return vec4(illum, shading_state.bounce == 0 ? 0.0f : 1.0f);
}

#endif // RECURSIVE_MEGAKERNEL_UNROLL

