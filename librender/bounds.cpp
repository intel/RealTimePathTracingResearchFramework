// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "bounds.h"

Sphere::Sphere(const glm::vec3 &_origin, float _radius)
    : origin(_origin), radius(_radius) {}

const Sphere& Sphere::operator+=(const Sphere& other) {
    const glm::vec3 origin_offset = other.origin - origin;
    const float dist_squared = glm::dot(origin_offset, origin_offset);
    const float radius_delta = other.radius - radius;

    // is one of the spheres already within the other?
    if (dist_squared <= (radius_delta * radius_delta)) {
        // pick the larger sphere
        if (other.radius > radius) {
            *this = other;
        }
    } else {
        const float distance = glm::length(origin_offset);
        Sphere combined_sphere(*this);
        combined_sphere.radius = (radius + other.radius + distance) * 0.5f;
        combined_sphere.origin += (combined_sphere.radius - radius) * origin_offset / distance;
        *this = combined_sphere;
    }
    return *this;
}

Sphere Sphere::operator+(const Sphere& other) const {
    return Sphere(*this)+=other;
}

Sphere Sphere::boundPoints(const glm::vec3* positions, int num_positions) {
    // Implementation based on "AN EFFICIENT BOUNDING SPHERE" by Jack Ritter

    // pick 6 points that span AABB
    int min_indices[3] = {0, 0, 0};
    int max_indices[3] = {0, 0, 0};

    for (int iPos = 1; iPos < num_positions; iPos++) {
        const auto &pos = positions[iPos];
        for (int i = 0; i < 3; i++) {
            if (pos[i] < positions[min_indices[i]][i])
                min_indices[i] = iPos;
            if (pos[i] > positions[max_indices[i]][i])
                max_indices[i] = iPos;
        }
    }

    // initialize Sphere based on the AABB pair with the maximum spatial distance
    float largest_dist_squared = 0;
    int largest_AABB_dimension = 0;
    for (int i = 0; i < 3; i++) {
        const auto &p0 = positions[min_indices[i]];
        const auto &p1 = positions[max_indices[i]];
        const glm::vec3 delta = p1 - p0;
        const float dist_squared = glm::dot(delta, delta);
        if (dist_squared > largest_dist_squared) {
            largest_dist_squared = dist_squared;
            largest_AABB_dimension = i;
        }
    }

    const auto &p0 = positions[min_indices[largest_AABB_dimension]];
    const auto &p1 = positions[max_indices[largest_AABB_dimension]];
    Sphere bounding_sphere(0.5f * (p0 + p1), 0.5f * sqrtf(largest_dist_squared));

    // Update sphere to contain all points
    for (int iPos = 0; iPos < num_positions; iPos++) {
        const glm::vec3 delta = positions[iPos] - bounding_sphere.origin;
        const float dist_squared = glm::dot(delta, delta);
        if (dist_squared > bounding_sphere.radius) {
            const float dist = sqrtf(dist_squared);
            const float radius_new = (bounding_sphere.radius + dist) * 0.5f;
            bounding_sphere.origin += delta * ((radius_new - bounding_sphere.radius) / dist);
            bounding_sphere.radius = radius_new;
        }
    }

    return bounding_sphere;
}

