// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#define _USE_MATH_DEFINES
#include <cmath>
#include "lights.h"
#include "scene.h"
#include "mesh_decode.h"
#include "compute_util.h"
#include "types.h"
#include "error_io.h"
#include <algorithm>

std::vector<TriLight> collect_emitters(Scene const& scene) {
    std::vector<char> pmesh_nonemissive(scene.parameterized_meshes.size());
    std::vector<TriLight> emitters;
    for (auto& i : scene.instances) {
        if (pmesh_nonemissive[i.parameterized_mesh_id])
            continue;
        auto& pm = scene.parameterized_meshes[i.parameterized_mesh_id];
        const auto &animData = scene.animation_data.at(i.animation_data_index);
        constexpr uint32_t frame = 0;
        const glm::mat4 transform = animData.dequantize(i.transform_index, frame);
        auto next = collect_emitters(transform, pm, scene.meshes[pm.mesh_id], scene.materials);
        if (!next.empty())
            emitters.insert(emitters.begin(), next.begin(), next.end());
        else
            pmesh_nonemissive[i.parameterized_mesh_id] = 1; // skip next time;
    }
    return emitters;
}

std::vector<TriLight> collect_emitters(glm::mat4 const& transform, ParameterizedMesh const& pm, Mesh const& mesh, std::vector<BaseMaterial> const& materials) {
    std::vector<TriLight> lights;
    if (mesh.num_tris() == 0)
        return lights;
    bool per_triangle_ids = pm.per_triangle_materials();

    len_t mesh_tri_idx_base = 0;
    for (int i = 0, ie = mesh.num_geometries(); i < ie; ++i) {
        Geometry currentGeom = mesh.geometries[i];
        int material_offset = pm.material_offset(i);

        TriLight light;
        if (!per_triangle_ids) {
            auto& material = materials[material_offset];
            if (!(material.emission_intensity > 0.0f)) {
                mesh_tri_idx_base += currentGeom.num_tris();
                continue;
            }
            light.radiance = material.emission_intensity * material.base_color;
        }
        if (lights.capacity() == 0)
            lights.reserve(mesh.num_tris());
        for (int tri_idx = 0, tri_idx_end = currentGeom.num_tris(); tri_idx < tri_idx_end; ++tri_idx) {
            if (per_triangle_ids) {
                int material_id = material_offset + pm.triangle_material_id(mesh_tri_idx_base + tri_idx);
                auto& material = materials[material_id];
                if (!(material.emission_intensity > 0.0f))
                    continue;
                light.radiance = material.emission_intensity * material.base_color;
            }
            currentGeom.tri_positions(tri_idx, light.v0, light.v1, light.v2);
            light.v0 = glm::vec3(transform * glm::vec4(light.v0, 1.0f));
            light.v1 = glm::vec3(transform * glm::vec4(light.v1, 1.0f));
            light.v2 = glm::vec3(transform * glm::vec4(light.v2, 1.0f));
            lights.push_back(light);
        }

        mesh_tri_idx_base += currentGeom.num_tris();
    }
    return lights;
}

void update_light_sampling(BinnedLightSampling& binned, std::vector<TriLight> const& emitters, LightSamplingConfig params) {
    bool invalidated = (binned.params.bin_size == 0);
    if (binned.params.min_radiance != params.min_radiance ||
        binned.params.min_perceived_receiver_dist != params.min_perceived_receiver_dist ||
        invalidated) {
        binned.radiances = estimate_normalized_radiance(nullptr, emitters, params.min_perceived_receiver_dist);
        binned.emitters = emitters;
        if (params.min_radiance > 0.0f)
            trim_dim_emitters(binned.emitters, binned.radiances, params.min_radiance);
        invalidated = true;
    }
    if (binned.params.bin_size != params.bin_size || invalidated) {
        equalize_emitter_bins(binned.emitters, binned.radiances, params.bin_size);
    }
    binned.params = params;
}

namespace glsl { namespace {

// "BRDF Importance Sampling for Polygonal Lights"
// Copyright (C) 2021, Christoph Peters, Karlsruhe Institute of Technology
// Three-clause BSD license
// https://github.com/MomentsInGraphics/vulkan_renderer/blob/main/src/shaders/polygon_sampling.glsl

/*! A piecewise polynomial approximation to positive_atan(y). The maximal
	absolute error is 1.16e-05f. At least on Turing GPUs, it is faster but also
	significantly less accurate. The proper atan has at most 2 ulps of error
	there.*/
float fast_positive_atan(float y) {
	float rx;
	float ry;
	float rz;
	rx = (abs(y) > 1.0f) ? (1.0f / abs(y)) : abs(y);
	ry = rx * rx;
	rz = fma(ry, 0.02083509974181652f, -0.08513300120830536);
	rz = fma(ry, rz, 0.18014100193977356f);
	rz = fma(ry, rz, -0.3302994966506958f);
	ry = fma(ry, rz, 0.9998660087585449f);
	rz = fma(-2.0f * ry, rx, float(0.5f * M_PI));
	rz = (abs(y) > 1.0f) ? rz : 0.0f;
	rx = fma(rx, ry, rz);
	return (y < 0.0f) ? (M_PI - rx) : rx;
}
/*! Returns an angle between 0 and M_PI such that tan(angle) == tangent. In
	other words, it is a version of atan() that is offset to be non-negative.
	Note that it may be switched to an approximate mode by the
	USE_BIASED_PROJECTED_SOLID_ANGLE_SAMPLING flag.*/
float positive_atan(float tangent) {
#ifdef USE_BIASED_PROJECTED_SOLID_ANGLE_SAMPLING
	return fast_positive_atan(tangent);
#else
	float offset = (tangent < 0.0f) ? M_PI : 0.0f;
	return atan(tangent) + offset;
#endif
}
float triangle_solid_angle(glm::vec3 v0, glm::vec3 v1, glm::vec3 v2) {
    // Prepare a Householder transform that maps vertex 0 onto (+/-1, 0, 0). We
	// only store the yz-components of that Householder vector and a factor of
	// 2.0f / sqrt(abs(polygon.vertex_dirs[0].x) + 1.0f) is pulled in there to
	// save on multiplications later. This approach is necessary to avoid
	// numerical instabilities in determinant computation below.
	float householder_sign = (v0.x > 0.0f) ? -1.0f : 1.0f;
	vec2 householder_yz = vec2(v0.y, v0.z) * (1.0f / (abs(v0.x) + 1.0f));
    // Compute solid angles
    float dot_0_1 = dot(v0, v1);
    float dot_0_2 = dot(v1, v2);
    float dot_1_2 = dot(v0, v2);
    // Compute the bottom right minor of vertices after application of the
    // Householder transform
    float dot_householder_0 = fma(-householder_sign, v1.x, dot_0_1);
    float dot_householder_2 = fma(-householder_sign, v2.x, dot_1_2);
    mat2 bottom_right_minor = mat2(
        fma2(vec2(-dot_householder_0), householder_yz, vec2(v1.y, v1.z)),
        fma2(vec2(-dot_householder_2), householder_yz, vec2(v2.y, v2.z)));
    // The absolute value of the determinant of vertices equals the 2x2
    // determinant because the Householder transform turns the first column
    // into (+/-1, 0, 0)
    float simplex_volume = abs(determinant(bottom_right_minor));
    // Compute the solid angle of the triangle using a formula proposed by:
    // A. Van Oosterom and J. Strackee, 1983, The Solid Angle of a
    // Plane Triangle, IEEE Transactions on Biomedical Engineering 30:2
    // https://doi.org/10.1109/TBME.1983.325207
    float dot_0_2_plus_1_2 = dot_0_2 + dot_1_2;
    float one_plus_dot_0_1 = 1.0f + dot_0_1;
    float tangent = simplex_volume / (one_plus_dot_0_1 + dot_0_2_plus_1_2);
    return 2.0f * positive_atan(tangent);
}

} } // namespace

// compute representative radiance value based on closest shading points to light source where variance is still visibly perceived (depends on viewer scale)
std::vector<float> estimate_normalized_radiance(Scene const* scene, std::vector<TriLight> const& emitters, float min_perceived_receiver_dist) {
    // note: scene not used for now, could be used for automatic scale detection, or randomized test points on surfaces?

    std::vector<float> radiances(emitters.size());

    for (size_t i = 0, ie = emitters.size(); i < ie; ++i) {
        // Emitters cannot contribute more than their radiances integrated over the hemisphere,
        // as seen from anywhere. However, this metric becomes useless for small emitters with
        // high energy density, which may act light point lights, with radiance values approaching
        // infinity. Nevertheless, the contribution of such lights is limited except when shading
        // points come close to them. At a distance of `min_perceived_receiver_dist`, we compute
        // a representative "normalized" radiance to replace the true radiance values of small emitters.

        TriLight light = emitters[i];
        glm::vec3 n = normalize(cross(light.v1 - light.v0, light.v2 - light.v0));
        // remove degenerate triangles
        if (!(std::abs(length(n) - 1.0f) < 0.05f)) {
            radiances[i] = 0.0f;
            continue;
        }

        glm::vec3 c = (light.v0 + light.v1 + light.v2) / 3.0f;
        glm::vec3 o = n * min_perceived_receiver_dist;
        float solid_angle = glsl::triangle_solid_angle(
              normalize(light.v0 - c - o) // note: only apply small offset `o` after recentering to the origin!
            , normalize(light.v1 - c - o)
            , normalize(light.v2 - c - o)
        );

        radiances[i] = luminance(emitters[i].radiance) * (solid_angle / M_2_PI);
    }

    return radiances;
}

// remove short-range emitters that contribute no noticeable light outside their local environment (depends on camera exposure)
void trim_dim_emitters(std::vector<TriLight>& emitters, std::vector<float> &radiances, float min_radiance) {
    size_t newCount = 0;

    for (size_t i = 0, ie = emitters.size(); i < ie; ++i) {
        if (radiances[i] >= min_radiance) {
            if (i != newCount) {
                emitters[newCount] = emitters[i];
                radiances[newCount] = radiances[i];
            }
            ++newCount;
        }
    }

    emitters.resize(newCount);
    radiances.resize(newCount);
}

// partition emitters into approx. equal-weight bins for importance sampling
void equalize_emitter_bins(std::vector<TriLight>& emitters, std::vector<float> &radiances, int bin_size) {
    if (bin_size <= 1 || radiances.empty())
        return;

    int original_bin_count = (ilen(radiances) + (bin_size - 1)) / bin_size;
    float average_weight = 0.0f;
    for (int i = 0, ie = ilen(radiances); i < ie; ++i) {
        average_weight += radiances[i];
    }
    average_weight /= float(radiances.size());

    struct BinnedRadiances {
        float radiance;
        int source_idx;
        int split_count;
    };
    std::vector<BinnedRadiances> bins;
    bins.reserve(2 * radiances.size());
    for (int i = 0, ie = ilen(radiances); i < ie; ++i) {
        float weight = radiances[i];
        int initial_clones = (int) max((unsigned) min(weight / average_weight, float(original_bin_count)), 1u);
        //initial_clones = 1;
        for (int j = 0; j < initial_clones; ++j)
            bins.push_back( BinnedRadiances{ radiances[i] / float(initial_clones), i, initial_clones } );
    }

    std::vector<BinnedRadiances> shuffled_bins;
    auto reshuffle_bins = [&]() {
        //std::random_shuffle(bins.begin(), bins.end());
        //return;
        shuffled_bins.resize(uint_bound(bins.size()));
        for (int index = 0, element_count = (int) bins.size(); index < element_count; ++index) {
            int source_idx = (int) unsigned(halton2((unsigned) index) * float((unsigned) element_count));
            do {
                // keep looking for indices that are not taken yet
                if (source_idx >= element_count)
                    source_idx = 0;
                if (bins[source_idx].source_idx == ~0)
                    ++source_idx;
                else
                    break;
            } while (true);
            shuffled_bins[index] = bins[source_idx];
            bins[source_idx].source_idx = ~0;
        }
        bins = std::move(shuffled_bins);
    };
    reshuffle_bins();

    auto measure_equality = [bin_size](std::vector<BinnedRadiances> const& bins) {
        float min_total = 2.0e32f, max_total = 0.0f;

        for (int i = 0, ie = ilen(bins); i < ie; ) {
            float bin_total = 0.0f;
            for (int j = 0; j < bin_size && i < ie; ++i, ++j) {
                bin_total += bins[i].radiance;
            }
            min_total = std::min(bin_total, min_total);
            max_total = std::max(bin_total, max_total);
        }

        return min(min_total / max_total, 1.0f);
    };
    float initial_equality = measure_equality(bins);
    float equality = initial_equality;

    int retries = 0;
    std::vector<BinnedRadiances> postfix_bins;
    for (; equality < 0.6f && retries < 2; ++retries) {
        postfix_bins.resize(bins.size());
        std::partial_sum(bins.begin(), bins.end(), postfix_bins.begin(), [](BinnedRadiances acc, BinnedRadiances it) -> BinnedRadiances {
            return { acc.radiance + it.radiance, it.source_idx, 1 };
        });
        postfix_bins.front().split_count = 1;
        float radiance_sum = postfix_bins.back().radiance;
        for (auto& it : postfix_bins)
            it.radiance /= radiance_sum;

        int prev_elements = ilen(bins);
        int prev_bin_count = (prev_elements + (bin_size - 1)) / bin_size;
        int padded_elements = (prev_bin_count + 1) * bin_size;

        unsigned halton_index = 0;
        while (ilen(bins) < padded_elements) {
            float u = halton2(halton_index++);
            auto it = std::upper_bound(postfix_bins.begin(), postfix_bins.end(), u, [](float bound, BinnedRadiances const& b) { return bound < b.radiance; });
            if (it == postfix_bins.end())
                it = postfix_bins.end() - 1;
            
            // note: use for counting clones
            ++it->split_count;
            // note: new clones still reference their source instead of the emitter, until the radiances are updated
            bins.push_back({ it->radiance, int(it - postfix_bins.begin()), 0 });
        }

        // fix up originals and clones
        for (int i = prev_elements; i < padded_elements; ++i) {
            auto& clone = bins[i];
            auto& original = bins[clone.source_idx];
            auto& clone_counter = postfix_bins[clone.source_idx].split_count;
            // fix cloned original, if not done yet
            if (clone_counter > 1) {
                original.radiance /= float(clone_counter);
                original.split_count *= clone_counter;
                clone_counter = 1; // mark as updated
            }
            // fix clone radiance to account for number of clones
            clone.radiance = original.radiance;
            clone.source_idx = original.source_idx;
            clone.split_count = original.split_count;
        }

        reshuffle_bins();
        equality = measure_equality(bins);
    }

    printf("Re-binned in %d retries, reached %.2f%% equality, from %.2f%% equality, with %d lights from %d\n"
        , retries
        , 100.0f * equality, 100.0f * initial_equality
        , (int) bins.size(), (int) emitters.size());

    std::vector<TriLight> reordered_emitters(bins.size());
    radiances.resize(bins.size());
    for (int i = 0, ie = ilen(bins); i < ie; ++i) {
        radiances[i] = bins[i].radiance;
        reordered_emitters[i] = emitters[bins[i].source_idx];
        reordered_emitters[i].radiance /= float(bins[i].split_count);
    }
    emitters = std::move(reordered_emitters);
}

int BinnedLightSampling::bin_count() const {
    return int(emitters.size() + (params.bin_size - 1)) / params.bin_size;
}
