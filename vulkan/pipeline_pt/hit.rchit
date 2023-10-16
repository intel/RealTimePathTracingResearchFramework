// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#version 460
#extension GL_GOOGLE_include_directive : require

#extension GL_EXT_ray_tracing : require

#include "language.glsl"
#include "../gpu_params.glsl"

#ifdef _DEBUG
#extension GL_EXT_debug_printf : enable
#endif

// Available feature flags:
// #define SHUFFLED_DOUBLE_SOBOL
// #define SOBOL_NO_SCRAMBLE
// #define SIMPLIFIED_SHADER
// #define ALPHA_RAY_RESTART

#include "setup_recursive_pt.glsl"

#include "../geometry.glsl"
#include "rt/hit.glsl"
#include "rt/footprint.glsl"

#include "rt/materials.glsl"
#ifdef SIMPLIFIED_SHADER
    #include "bsdfs/simple_bsdf.glsl"
#else
    #include "bsdfs/gltf_bsdf.glsl"
#endif


layout(binding = SCENE_BIND_POINT, set = 0) uniform accelerationStructureEXT scene;

layout(binding = SCENE_PARAMS_BIND_POINT, set = 0, std140) uniform GPBuf {
    GLOBAL_CONSTANT_PARAMETERS
};

layout(binding = MATERIALS_BIND_POINT, set = 0, scalar) buffer MaterialParamsBuffer {
    MATERIAL_PARAMS material_params[];
};

#ifndef DISABLE_AREA_LIGHT_SAMPLING
#include "lights/tri.glsl"
layout(binding = LIGHTS_BIND_POINT, set = 0, std430) buffer LightParamsBuffer {
    TriLightData global_lights[];
};
#endif

layout(binding = 0, set = TEXTURE_BIND_SET) uniform sampler2D textures[];
#ifdef STANDARD_TEXTURE_BIND_SET
layout(binding = 0, set = STANDARD_TEXTURE_BIND_SET) uniform sampler2D standard_textures[];
#endif

layout(location = PRIMARY_RAY) rayPayloadInEXT RayPayload payload;
layout(location = OCCLUSION_RAY) rayPayloadEXT int occlusion_hit;

layout(shaderRecordEXT, std430) buffer SBT {
    RenderMeshParams geom;
};

hitAttributeEXT vec2 attrib;

// assemble light transport algorithm
#define RENDERER_NUM_RAYQUERIES num_rayqueries

#define SCENE_GET_TEXTURE(tex_id) textures[nonuniformEXT(tex_id)]
#define SCENE_GET_STANDARD_TEXTURE(tex_id) standard_textures[nonuniformEXT(tex_id)]

#define SCENE_GET_LIGHT_SOURCE(light_id) decode_tri_light(global_lights[nonuniformEXT(light_id)])
#define SCENE_GET_LIGHT_SOURCE_COUNT()   int(scene_params.light_sampling.light_count)

#define BINNED_LIGHTS_BIN_SIZE int(view_params.light_sampling.bin_size)
#define SCENE_GET_BINNED_LIGHTS_BIN_COUNT() (int(scene_params.light_sampling.light_count + (view_params.light_sampling.bin_size - 1)) / int(view_params.light_sampling.bin_size))

#if defined(TAIL_RECURSIVE) || defined(ENABLE_AOV_BUFFERS)
#include "accumulate.glsl"
#endif

layout(binding = DEBUG_MODE_BUFFER, set = 0, r16f) uniform writeonly image2D debug_mode_buffer;

#include "rt/material_textures.glsl"
#include "mc/nee.glsl"

float geometry_scale = 0.0f;
uint ray_count = 0;

bool raytrace_test_visibility(const vec3 from, const vec3 dir, float dist) {
    return true;
}
bool raytrace_test_visibility_in_post(const vec3 from, const vec3 dir, float dist) {
  const uint32_t occlusion_flags = gl_RayFlagsTerminateOnFirstHitEXT
      | gl_RayFlagsSkipClosestHitShaderEXT;

    occlusion_hit = 1;
    float epsilon = geometry_scale_to_tmin(from, geometry_scale);
    if (dist - 2.f * epsilon > 0.0f) {
        traceRayEXT(scene, occlusion_flags, 0xff,
                PRIMARY_RAY, 1, OCCLUSION_RAY, from, epsilon, dir, dist - epsilon, OCCLUSION_RAY);

#ifdef REPORT_RAY_STATS
        ++ray_count;
#endif
    }
    return occlusion_hit == 0;
}

void main() {
    vec3 ray_origin = gl_WorldRayOriginEXT;
    vec3 ray_dir = gl_WorldRayDirectionEXT;

    const uvec3 idx = 
#ifndef REQUIRE_UNROLLED_VERTICES
       ((geom.flags & GEOMETRY_FLAGS_IMPLICIT_INDICES) == 0) ? geom.indices.i[gl_PrimitiveID] :
#endif
       uvec3(gl_PrimitiveID * 3) + uvec3(0, 1, 2);

#ifdef RT_GEOMETRY_LOOKUP_DYNAMIC
    mat3 vertices = calc_hit_vertices(geom.dynamic_vertices, idx);
#else
    mat3 vertices = calc_hit_vertices(geom.vertices,
#ifdef QUANTIZED_POSITIONS
        geom.quantized_scaling, geom.quantized_offset,
#endif
        idx);
#endif
    RTHit hit = calc_hit_attributes(gl_RayTmaxEXT, gl_PrimitiveID, attrib,
        vertices, idx, transpose(mat3(gl_WorldToObjectEXT)),
        geom.normals, geom.num_normals > 0,
#ifndef QUANTIZED_NORMALS_AND_UVS
        geom.uvs,
#endif
        geom.num_uvs > 0,
        geom.material_id, geom.materials
        );

#if defined(ENABLE_REALTIME_RESOLVE) && defined(RT_GEOMETRY_LOOKUP_DYNAMIC)
    vec3 motion_vector = vec3( gl_ObjectToWorldEXT * vec4(
          calc_hit_motion_vectors(geom.motion_vectors, idx)
        * vec3(1.f - attrib.x - attrib.y, attrib.x, attrib.y)
        , 0.0f) );
#else
    vec3 motion_vector = vec3(0.0f);
#endif

    int bounce = get_bounce(payload);
#ifdef TRANSPORT_RELIABILITY
    float reliability = get_reliability(payload);
#endif
    int realBounce = bounce;


    RANDOM_STATE rng = UNPACK_RNG(payload);
    RANDOM_SET_DIM(rng, DIM_CAMERA_END + realBounce * (DIM_VERTEX_END + DIM_LIGHT_END));

    vec3 path_throughput = get_throughput(payload).xyz;
    float prev_bsdf_pdf = payload.prev_bsdf_pdf;
    vec3 illum = payload.illum.xyz;
    
    float approx_tri_solid_angle = length(hit.geo_normal);
    hit.geo_normal /= approx_tri_solid_angle;
    approx_tri_solid_angle *= abs(dot(hit.geo_normal, ray_dir)) / (hit.dist * hit.dist);

#ifdef USE_MIPMAPPING
    float total_t = 0.0f;
    mat2 texture_footprint = decode_footprint(payload.footprint, total_t);
    total_t += hit.dist;

    mat2 duvdxy;
    {
        vec3 dpdx, dpdy;
        footprint_to_dpdxy(dpdx, dpdy, ray_dir, texture_footprint);
        mat2x3 dpdxy = mat2x3(dpdx, dpdy);
        vec3 dir_tangent_un = ray_dir - hit.geo_normal * dot(ray_dir, hit.geo_normal);
        float cosTheta2 = max(1.0f - dot(dir_tangent_un, dir_tangent_un), 0.0f);
        vec3 dir_tangent_elong = dir_tangent_un / (sqrt(cosTheta2) + cosTheta2);
        // GLSL non-square matrix order seems broken
        vec3 dpdx_ = dpdxy[0] + dir_tangent_elong * dot(dpdxy[0], dir_tangent_un);
        vec3 dpdy_ = dpdxy[1] + dir_tangent_elong * dot(dpdxy[1], dir_tangent_un);
#ifdef TRANSPORT_MIPMAPPING
        dpdx_ = (dpdx_) * (1.0f / sqrt(reliability)); //sqrt(length2(dpdx_) + transport_footprint);
        dpdy_ = (dpdy_) * (1.0f / sqrt(reliability)); //sqrt(length2(dpdy_) + transport_footprint);
#endif
        vec3 bitangent = hit.bitangent_l * cross(hit.geo_normal, normalize(hit.tangent));
        duvdxy = mat2x2(
                dot(hit.tangent, dpdx_), dot(bitangent, dpdx_),
                dot(hit.tangent, dpdy_), dot(bitangent, dpdy_)
            ) * total_t;
    }
    geometry_scale = total_t;
#else
    mat2 duvdxy = mat2(0.0f);
#endif
    vec3 w_o = -ray_dir;
    InteractionPoint interaction;
    interaction.p = ray_origin + hit.dist * ray_dir;

    interaction.gn = hit.geo_normal;
    interaction.n = hit.normal;

    int normal_map = material_params[nonuniformEXT(hit.material_id)].normal_map;
    uint32_t material_flags = material_params[nonuniformEXT(hit.material_id)].flags;

    // For opaque objects (or in the future, thin ones) make the normal face forward
    if ((material_flags & BASE_MATERIAL_ONESIDED) == 0 && dot(w_o, interaction.gn) < 0.0) {
        interaction.n = -interaction.n;
        interaction.gn = -interaction.gn;
    }

    // apply normal mapping
#ifndef SIMPLIFIED_SHADER
    if (normal_map != -1) {
        vec3 v_y = normalize( cross(hit.normal, hit.tangent) );
        vec3 v_x = cross(v_y, hit.normal);
        v_x *= length(hit.tangent);
        v_y *= hit.bitangent_l;

#ifdef USE_MIPMAPPING
    #ifdef TRANSPORT_MIPMAPPING
        float normal_lod = -log2(sqrt(reliability));
    #else
        // for now, reduce normal resolution with bounces (current representation not really filterable)
        float normal_lod = float(bounce);
    #endif
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
#endif

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
    // todo: this might impact performance and register allocation!
    if (bounce == 0 && (accumulation_flags & ACCUMULATION_FLAGS_AOVS) != 0)
        store_geometry_aovs(interaction.n, interaction.p, motion_vector);
#endif

    interaction.v_y = normalize( cross(interaction.n, hit.tangent) );
    interaction.v_x = cross(interaction.v_y, interaction.n);

    MATERIAL_TYPE mat;
    EmitterParams emit;
    float material_alpha = unpack_material(mat, emit
        , hit.material_id, material_params[nonuniformEXT(hit.material_id)]
        , HitPoint(gl_ObjectRayOriginEXT + gl_ObjectRayDirectionEXT * hit.dist
            , hit.uv, duvdxy, gl_ObjectRayDirectionEXT)
        );
#ifdef ENABLE_DEMO_FEATURES
    // todo: remove. we trained for excessive roughening :(
    float distance_roughening_leadin_fadeout = 0.0f; // clamp(0.1f * view_params.time - 0.2f, 0.0f, 1.0f);
    if (distance_roughening_leadin_fadeout < 1.0f)
        apply_roughening(mat, min(max(0.0f, (1.0f - distance_roughening_leadin_fadeout) * total_t / render_params.focus_distance - 1.0f), 0.4f));
#endif
#ifdef WORLD_SPACE_INTERACTION_EXTENSION
    WORLD_SPACE_INTERACTION_EXTENSION(interaction, mat, emit);
#endif
    vec3 scatter_throughput = path_throughput; // alpha now handled by any-hit shader: * material_alpha
#ifdef ALPHA_RAY_RESTART
    scatter_throughput *= material_alpha;
#endif

    // direct emitter hit
    if (render_params.output_channel == 0 && emit.radiance != vec3(0.0f))
    {
#ifndef DISABLE_AREA_LIGHT_SAMPLING
        float light_pdf;
        if (view_params.light_sampling.light_mis_angle > 0.0f)
            light_pdf = 1.0f / view_params.light_sampling.light_mis_angle;
        else
            light_pdf = wpdf_direct_tri_light(approx_tri_solid_angle);
        float w = nee_mis_heuristic(1.f, prev_bsdf_pdf, 1.f, light_pdf);
#else
        float w = 1.0f;
#endif
        illum += w * scatter_throughput * emit.radiance;
    }

    // AOVs
    if (render_params.output_channel != 0) {
#ifndef TRANSPORT_RELIABILITY
        float reliability = pow(0.25f, float(bounce));
#endif
        if (render_params.output_channel == 1)
            illum += scatter_throughput * mat.base_color * reliability;
        else if (render_params.output_channel == 2) {
            vec3 pathspace_normal = interaction.n;
            illum += pathspace_normal * reliability;
        }
        else if (render_params.output_channel == 3) {
            illum += interaction.p * reliability;
        }
    }

#ifdef ENABLE_AOV_BUFFERS
    // todo: this might impact performance and register allocation!
    if (bounce == 0 && (accumulation_flags & ACCUMULATION_FLAGS_AOVS) != 0)
        store_material_aovs(mat.base_color, mat.roughness, mat.ior);
#endif

    vec3 shadow_ray_illum = vec3(0.0f);
    vec3 shadow_ray_dir;
    float shadow_ray_dist;

    bool continue_path = false;
    do { // path continuation

    if (bounce+1 >= render_params.max_path_depth)
        break;

#if defined(TRANSPORT_ROUGHENING) && defined(USE_MIPMAPPING)
    mat.roughness = reliability_roughening(
          determinant(texture_footprint) / (render_params.variance_radius * render_params.variance_radius)
        , reliability
        , mat.roughness);
#endif

    if (render_params.output_channel == 0) {
        // first two dimensions light position selection, last light selection (sky/direct)
        vec4 nee_rng_sample = vec4(RANDOM_FLOAT2(rng, DIM_POSITION_X), RANDOM_FLOAT2(rng, DIM_LIGHT_SEL_1));
        NEEQueryAux nee_aux;
        nee_aux.mis_pdf = view_params.light_sampling.light_mis_angle > 0.0f ? 1.0f / view_params.light_sampling.light_mis_angle : 0.0f;
        shadow_ray_illum += scatter_throughput * sample_direct_light(mat, interaction, w_o, nee_rng_sample.xy, nee_rng_sample.zw, nee_aux);
        shadow_ray_dir = nee_aux.light_dir;
        shadow_ray_dist = nee_aux.light_dist;
    }
    RANDOM_SHIFT_DIM(rng, DIM_LIGHT_END);

    if (render_params.glossy_only_mode != 0 && !(mat.roughness < GLOSSY_MODE_ROUGHNESS_THRESHOLD && mat.ior != 1.0f))
        break;

    // alpha now handled by any-hit shader
#ifdef ALPHA_RAY_RESTART
#ifdef ANYHIT_FORCE_ALPHATEST
    if (material_alpha < 0.5f) {
#else
    if (material_alpha < 1.0f && (0.0f >= material_alpha || RANDOM_FLOAT1(rng, DIM_FREE_PATH) >= material_alpha)) {
#endif
        // transparent
        RANDOM_SHIFT_DIM(rng, DIM_VERTEX_END);
    } else
#endif
    {
#ifndef SIMPLIFIED_SHADER
        vec2 bsdfLobeSample = RANDOM_FLOAT2(rng, DIM_LOBE);
#endif
        vec2 bsdfDirSample = RANDOM_FLOAT2(rng, DIM_DIRECTION_X);

        vec3 w_i;
        float sampling_pdf, mis_wpdf;
        vec3 bsdf = sample_bsdf(mat, interaction, w_o, w_i, sampling_pdf, mis_wpdf, bsdfDirSample, bsdfLobeSample, rng);
        RANDOM_SHIFT_DIM(rng, DIM_VERTEX_END);

        // Must increment before the break statement below or the alpha channel
        // will be accumulated incorrectly.
        ++bounce;
        if (mis_wpdf == 0.f || bsdf == vec3(0.f) || !(dot(w_i, interaction.n) * dot(w_i, interaction.gn) > 0.0f)) {
            break;
        }
        path_throughput *= bsdf;

#ifdef USE_MIPMAPPING
        if (dot(w_i, interaction.n) * dot(w_o, interaction.n) > -0.999f) {
            texture_footprint = reflect_footprint(w_i, ray_dir, texture_footprint);
        }
#endif
#ifdef TRANSPORT_RELIABILITY
        reliability = next_reliability(
            reliability,
            roughness_to_reliability_change(determinant(texture_footprint), mat.roughness)
        );
#endif
        //prev_wo = w_o;
        prev_bsdf_pdf = mis_wpdf;
        //prev_n = interaction.n;

        ray_dir = w_i;
    }
    ray_origin = interaction.p;
    ++realBounce;

    // Russian roulette termination
    if (bounce >= render_params.rr_path_depth) {
        float prefix_weight = max(path_throughput.x, max(path_throughput.y, path_throughput.z));

        float rr_prob = prefix_weight;
        float rr_sample = RANDOM_FLOAT1(rng, DIM_RR);
        if (bounce > 6)
            rr_prob = min(0.95f, rr_prob); // todo: good?
        else
            rr_prob = min(1.0f, rr_prob);

        if (rr_sample < rr_prob)
            path_throughput /= rr_prob;
        else
            break;
    }

    continue_path = true;
    
    set_throughput(payload, path_throughput, 1.0f);
    PACK_RNG(rng, payload);
#ifndef TRANSPORT_RELIABILITY
    float reliability = 1.0f;
#endif
    set_bounce_and_reliability(payload, realBounce, reliability);
    // keep: payload.sample_index = uint16_t(sample_index);
    payload.prev_bsdf_pdf = prev_bsdf_pdf;
    // keep: payload.location;
    // todo: payload.transport_reliability = transport_reliability;
#if defined(USE_MIPMAPPING)
    //payload.dpdxy = encode_dpdxy(dpdxy[0], dpdxy[1], total_t);
    payload.footprint = encode_footprint(texture_footprint, total_t);
#endif

    } while (false);

    payload.illum = illum; // todo: could add some kind of reliability falloff to (1.0f - payload.illum.w)

    if (shadow_ray_illum != vec3(0.0f)) {
        if (raytrace_test_visibility_in_post(interaction.p, shadow_ray_dir, shadow_ray_dist))
            payload.illum += shadow_ray_illum;
    }


    if (!continue_path) {
#ifdef TAIL_RECURSIVE

        accumulate(~0u, vec4(payload.illum, 1.0f), ~0u);
#elif defined(NON_RECURSIVE)
        set_bounce_and_reliability(payload, MAX_PATH_DEPTH, 0.0f);
#else
        // Make sure stack_recursive gets the correct bounce, or alpha will be
        // incorrect.
        set_bounce_and_reliability(payload, bounce, get_reliability(payload));
#endif

#if (RBO_debug_mode == DEBUG_MODE_ANY_HIT_COUNT_FULL_PATH || RBO_debug_mode == DEBUG_MODE_ANY_HIT_COUNT_PRIMARY_VISIBILITY)
        imageStore(debug_mode_buffer, ivec2(gl_LaunchIDEXT.xy), vec4(payload.any_hit_count));
#endif
#if (RBO_debug_mode == DEBUG_MODE_BOUNCE_COUNT)
        imageStore(debug_mode_buffer, ivec2(gl_LaunchIDEXT.xy), vec4(bounce));
#endif
        return;
    }

#ifndef NON_RECURSIVE
    float t_min = geometry_scale_to_tmin(ray_origin, total_t);
    float t_max = 1e20f;

    uint32_t traversalFlags = 0;
#ifdef ALPHA_RAY_RESTART
    traversalFlags |= gl_RayFlagsOpaqueEXT;
#endif
    traceRayEXT(scene, traversalFlags, 0xff // ... hit mask,
        , PRIMARY_RAY, 1, PRIMARY_RAY // hit index, default stride, miss index
        , ray_origin, t_min, ray_dir, t_max
        , PRIMARY_RAY); // payload index
#else
    payload.next_dir = ray_dir;
    payload.t = hit.dist;
#endif
}
