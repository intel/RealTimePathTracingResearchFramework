// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef GPU_PROGRAMS
#define GPU_PROGRAMS

#include "../rendering/gpu_programs.h"
#include "ref_counted.h"
#include <string>
#include <vector>
#include <type_traits>

struct RenderBackendOptions;

bool gpu_program_binary_changed(GpuProgram const* program, RenderBackendOptions const& options, char const* compiler_options = nullptr);
void make_gpu_program_binaries(GpuProgram const* program, RenderBackendOptions const& options, char const* compiler_options = nullptr);

bool gpu_shader_binary_changed(GpuModuleUnit const* unit, RenderBackendOptions const& options, char const* compiler_options = nullptr);
std::string gpu_shader_binary_file(GpuModuleUnit const* unit, RenderBackendOptions const& options, char const* compiler_options = nullptr);
std::vector<char> read_gpu_shader_binary(GpuModuleUnit const* unit, RenderBackendOptions const& options, char const* compiler_options = nullptr);

GpuModuleUnit const* gpu_module_single_unit(GpuProgram const* program, char const* module_name, bool optional = false);
GpuModuleUnit const* gpu_module_single_unit_typed(GpuModule const* module, char const* unit_type, bool optional = false);

// meant to be called only once per program around program startup
std::vector<GpuProgram> subprograms_from_program(GpuProgram const* program);

// todo: remove
std::vector<char const*> merge_to_old_defines(GpuModuleDefine const* defines, std::vector<std::string>& string_store);

struct GenericGpuProgramCache : ref_counted<GenericGpuProgramCache> {
    struct shared_data;

    GenericGpuProgramCache();
    ~GenericGpuProgramCache();

    GenericGpuProgramCache(GenericGpuProgramCache const& right) noexcept;
    GenericGpuProgramCache& operator =(GenericGpuProgramCache const& right) noexcept;

    using ref_counted::discard_reference;
    void discard_reference(release_resources_t release_resources) noexcept;
    // only destructor releases resources to retain correct ownership handling
    void release_resources() { }

    void* find(GpuProgram const* program, RenderBackendOptions const& options);
    void add(void* compiled, GpuProgram const* program, RenderBackendOptions const& options);
    void* remove(GpuProgram const* program, RenderBackendOptions const& options);
    void* prune_next(int max_count = -1);
};

template <class T, class TransferPointer = std::unique_ptr<T>>
struct GpuProgramCache : GenericGpuProgramCache {
    struct shared_data;

    ~GpuProgramCache() {
        this->discard_reference<GpuProgramCache>();
    }
    void release_resources() {
        if constexpr (!std::is_pointer<TransferPointer>::value) {
            while (auto tp = prune_next(0)) {
                // tp destructor may release resources
            }
        }
    }

    template <class MakeType, class Backend, class Options, class... OptionalArgs>
    T* find_or_make(Backend const& backend, GpuProgram const* program, Options const& options
        , const OptionalArgs&... optional_args) {
        T* cached = find(program, options);
        if (!cached) {
            TransferPointer compiled{ new MakeType(backend, program, options, optional_args...) };
            cached = add((TransferPointer&&) compiled, program, options);
        }
        return cached;
    }

    T* find(GpuProgram const* program, RenderBackendOptions const& options) {
        return static_cast<T*>( GenericGpuProgramCache::find(program, options) );
    }
    T* add(TransferPointer compiled, GpuProgram const* program, RenderBackendOptions const& options) {
        T* added;
        if constexpr (!std::is_pointer<TransferPointer>::value)
            added = compiled.get();
        else
            added = compiled;
        GenericGpuProgramCache::add(added, program, options);
        if constexpr (!std::is_pointer<TransferPointer>::value)
            compiled.release(); // now owned by cache
        return added;
    }
    TransferPointer remove(GpuProgram const* program, RenderBackendOptions const& options) {
        return TransferPointer( static_cast<T*>( GenericGpuProgramCache::remove(program, options) ) );
    }
    TransferPointer prune_next(int max_count = -1) {
        return TransferPointer( static_cast<T*>( GenericGpuProgramCache::prune_next(max_count) ) );
    }
};

#define RENDER_BACKEND_OPTION_DEFINE(option) "RBO_" #option
#define RENDER_BACKEND_OPTION_ENUM_NAME(option, index) \
    ([](int i, std::initializer_list<char const*> n) -> char const* { \
        return *(n.begin() + i); \
    })(index, { RENDER_BACKEND_OPTION(option##_NAMES) })

#define RENDER_BACKEND_OPTION_FMT_bool(option, val) "%d", int(val)
#define RENDER_BACKEND_OPTION_FMT_int(option, val) "%d", int(val)
#define RENDER_BACKEND_OPTION_FMT_float(option, val) "%f", float(val)
#define RENDER_BACKEND_OPTION_FMT_RBO_enum_t(option, val) "%s%s", RENDER_BACKEND_OPTION(option##_NAMES_PREFIX), RENDER_BACKEND_OPTION_ENUM_NAME(option, val)

#endif // GPU_PROGRAMS
