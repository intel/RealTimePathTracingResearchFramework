// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>
#include <glm/glm.hpp>

struct Sphere {
    glm::vec3 origin;
    float radius;

    Sphere() = default;
    Sphere(const glm::vec3 &_origin, float _radius);

    const Sphere &operator+=(const Sphere &other);
    Sphere operator+(const Sphere &other)const;

    // Computes a bounding sphere for a set of points
    static Sphere boundPoints(const glm::vec3 *positions, int num_positions);
};

