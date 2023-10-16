// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "scene.h"
#include "libdatacapture/raytrace.h"
#include <glm/glm.hpp>

using rt_datacapture::RayQuery;
using rt_datacapture::RaytraceResults;

struct RaytraceBackend : public rt_datacapture::RaytraceBackend {
    virtual ~RaytraceBackend() { }
    virtual std::string name() const = 0;

    virtual void set_scene(const Scene &scene) = 0;
    virtual int trace_ray(RayQuery* queries, int num_queries, RaytraceResults aux_results) = 0;
};

typedef RaytraceBackend* (*create_raytracer_function)();

