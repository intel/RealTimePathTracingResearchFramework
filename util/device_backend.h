// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "ref_counted.h"
#include <glm/glm.hpp>
#include <string>
#include <utility>

// Opaque structure that needs to be defined per API
struct SubmitParameters;

struct CommandStream {
    virtual ~CommandStream() { }

    virtual void begin_record() = 0;
    virtual void end_submit(bool only_manual_wait = false) = 0;
    virtual void end_submit(const SubmitParameters* submit_params) = 0;
    virtual void wait_complete(int cursor = -1) = 0;
};

struct GpuBuffer {
    virtual ~GpuBuffer() { }

    virtual void* map() = 0;
    virtual void unmap() = 0;
    virtual size_t size() const = 0;
};

struct ComputePipeline {
    virtual ~ComputePipeline() { }
    virtual std::string name() = 0;

    virtual int add_buffer(int bindpoint, GpuBuffer* buffer, bool uniform_buffer = false) = 0;
    virtual int add_shader(char const* name) = 0;

    // inherit descriptor sets from another pipeline
    virtual int add_pipeline(int bindpoint, ComputePipeline* pipeline) = 0;

    virtual void finalize_build() = 0;
    virtual void run(CommandStream* stream, int shader_index, glm::uvec2 dispatch_dim) = 0;
};

struct ComputeDevice {
    virtual ~ComputeDevice() { }

    virtual CommandStream* sync_command_stream() = 0;
    virtual std::unique_ptr<GpuBuffer> create_uniform_buffer(size_t size) = 0;
    virtual std::unique_ptr<GpuBuffer> create_buffer(size_t size) = 0;
    virtual std::unique_ptr<ComputePipeline> create_pipeline() = 0;
};

typedef std::unique_ptr<ComputeDevice> (*create_compute_device_function)(const char *device_override);

#ifdef ENABLE_VULKAN
std::unique_ptr<ComputeDevice> create_vulkan_compute_device(const char *device_override = nullptr);
#endif
