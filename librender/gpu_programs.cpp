// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "gpu_programs.h"
#include "render_params.glsl.h"
#include "util.h"
#include "error_io.h"
#include "types.h"
#include <stdlib.h>
#include <initializer_list>
#include <algorithm>

bool gpu_program_binary_changed(GpuProgram const* program, RenderBackendOptions const& options, char const* compiler_options) {
    for (int i = 0; program->modules[i]; ++i) {
        auto m = program->modules[i];
        for (int j = 0; m->units[j]; ++j) {
            if (gpu_shader_binary_changed(m->units[j], options, compiler_options))
                return true;
        }
    }
    return false;
}

void make_gpu_program_binaries(GpuProgram const* program, RenderBackendOptions const& options, char const* compiler_options) {
    // todo: make parallel
    for (int i = 0; program->modules[i]; ++i) {
        auto m = program->modules[i];
        for (int j = 0; m->units[j]; ++j) {
            gpu_shader_binary_file(m->units[j], options, compiler_options);
        }
    }
}

static std::string make_gpu_shader_binary_filename(GpuModuleUnit const* shader, RenderBackendOptions /*const&*/ options
    , char const* compiler_options
    , std::string& cmd_line, std::string& dep_file) {
    cmd_line = shader->cmdpath;
    cmd_line += " \"";
    cmd_line += shader->srcpath;
    cmd_line += '\"';
    size_t unrooted_cmdline_end = cmd_line.size();
    for (int i = 0; shader->defines[i].name; ++i) {
        auto d = shader->defines[i];
        cmd_line += " -D";
        cmd_line += d.name;
        if (d.value) {
            cmd_line += '=';
            cmd_line += d.value;
        }
    }

    // todo: needs to depend on backend state
    options.enable_rayqueries = false;

    // do not require RBO_*_DEFAULT defines for boolean options
    #undef RENDER_BACKEND_OPTION_FMT_bool
    #define RENDER_BACKEND_OPTION_FMT_bool(option, val) (assert(false), "1") // note: should not be set to an actual value

    #define add_backend_option(type, option, default, flags) #option, 
    char const* option_names[] = { RENDER_BACKEND_OPTIONS(add_backend_option) };
    #undef add_backend_option
    std::string option_value_store[sizeof(option_names) / sizeof(option_names[0])];
    int option_value_count = 0;

    // first collect list of set option definitions
    #define add_backend_option(type, option, default, flags) \
    if (~(flags) & RBO_STAGES_CPU_ONLY) { /* omit host-side / CPU-only options */ \
        if (typeid(type) == typeid(bool)) { \
            if (options.option) { \
                option_value_store[option_value_count++] = " -D" RENDER_BACKEND_OPTION_DEFINE(option); \
            } \
        } else { \
            auto v = to_stringf(RENDER_BACKEND_OPTION_FMT_##type(option, options.option)); \
            auto d = to_stringf(RENDER_BACKEND_OPTION_FMT_##type(option, RENDER_BACKEND_OPTION(option##_DEFAULT))); \
            if (v != d) { \
                option_value_store[option_value_count++]  = " -D" RENDER_BACKEND_OPTION_DEFINE(option) "=" + v; \
            } \
        } \
    }
    RENDER_BACKEND_OPTIONS(add_backend_option)
    #undef add_backend_option
    #undef RENDER_BACKEND_OPTION_FMT_bool

    // append collected options in a sorted order
    std::sort(&option_value_store[0], &option_value_store[0] + option_value_count);
    for (int i = 0; i < option_value_count; ++i)
        cmd_line += option_value_store[i];

    // todo: pull out -Defines and sort together with other options
    if (compiler_options) {
        cmd_line += ' ';
        cmd_line += compiler_options;
    }

    auto sha1 = sha1_hash(cmd_line.data(), cmd_line.size());

    std::string binary_dir = binary_path(".");
    std::string source_dir = rooted_path(".");
    canonicalize_path_separator(source_dir); // clean deps file output
    {
        std::string rooted_cmd_line = "cd \"" + binary_dir + "\" && ";
        if (shader->cmdpath[0] == '"')
            rooted_cmd_line += '"';
        rooted_cmd_line += '.';
        rooted_cmd_line += path_separator();
        rooted_cmd_line += &shader->cmdpath[(shader->cmdpath[0] == '"') ? 1 : 0];
        rooted_cmd_line += " \"";
        rooted_cmd_line += source_dir;
        rooted_cmd_line += '/';
        rooted_cmd_line += shader->srcpath;
        rooted_cmd_line += '\"';
        cmd_line.replace(0, unrooted_cmdline_end, rooted_cmd_line);
    }

    std::string cache_path = shader->cachepath;
    cache_path += '/';
    cache_path += sha1;
    std::string cache_file = cache_path + ".spv";
    dep_file = cache_path + ".dep";

    static const std::string SOURCE_DIR_MARKER = "${SOURCE_DIR}";
    static const std::string DEP_FILE_MARKER = "${DEP_FILE}";
    for (size_t cursor = 0; (cursor = cmd_line.find(SOURCE_DIR_MARKER, cursor)) != cmd_line.npos; )
        cmd_line.replace(cursor, SOURCE_DIR_MARKER.size(), source_dir);
    for (size_t cursor = 0; (cursor = cmd_line.find(DEP_FILE_MARKER, cursor)) != cmd_line.npos; )
        cmd_line.replace(cursor, DEP_FILE_MARKER.size(), dep_file);

    cmd_line += " -o \"";
    cmd_line += cache_file;
    cmd_line += '"';

    dep_file = binary_path(dep_file);
    return binary_path(cache_file);
}

static bool gpu_shader_cache_needs_build(std::string const& cache_file, GpuModuleUnit const* program, std::string const& dep_file) {
    auto binary_update_timestap = get_last_modified(cache_file.c_str());
    // no binary created yet
    if (binary_update_timestap == 0)
        return true;

    auto source_update_timestamp = get_last_modified(rooted_path(program->srcpath).c_str());
    // source is missing, fall back to shipped binaries
    if (source_update_timestamp == 0) {
        static bool noted_missing_sources = false;
        if (!noted_missing_sources) {
            println(CLL::INFORMATION, "This release does not include full shader sources, noted for \"%s\"", program->srcpath);
            noted_missing_sources = true;
        }
        return false;
    }

    bool needs_update = false;
    if (file_exists(dep_file)) {
        bool encountered_error = false;
        std::string dep_text = read_text_file(dep_file);
        size_t src_offset = dep_text.find(": ");
        if (src_offset != dep_text.npos)
            ++src_offset;
        else {
            warning("Ill-formated depfile \"%s\", missing \": \" character sequence", dep_file.c_str());
            encountered_error = true;
            src_offset = dep_text.size();
        }
        char const* c_str = dep_text.c_str();
        for (size_t offset = src_offset, offset_end = dep_text.size() + 1; offset < offset_end; ++offset) {
            // skip ahead to next space
            if (c_str[offset] && !std::isspace(c_str[offset]))
                continue;
            // current offset 0 or space delimiter
            if (src_offset != offset) {
                std::string dependency = dep_text.substr(src_offset, offset - src_offset);
                size_t escape_colon_pos = dependency.find("\\:");
                if (escape_colon_pos != dependency.npos)
                    dependency.replace(escape_colon_pos, 2, 1, ':');
                canonicalize_path(dependency, program->source_to_build_path);
                auto dependency_time_stamp = get_last_modified(rooted_path(dependency).c_str());
                if (dependency_time_stamp == 0) {
                    warning("Could not resolve dependency \"%s\" in update checking", dependency.c_str());
                    encountered_error = true;
                }
                needs_update |= (binary_update_timestap < dependency_time_stamp);
            }
            src_offset = offset + 1;
        }

        if (!encountered_error)
            return needs_update;
    }

    warning("Error reading depfile \"%s\", falling back to primary source \"%s\" for update checking"
        , dep_file.c_str()
        , program->srcpath);
    return needs_update || binary_update_timestap < source_update_timestamp;
}

bool gpu_shader_binary_changed(GpuModuleUnit const* shader, RenderBackendOptions const& options, char const* compiler_options) {
    std::string cmd_line, dep_file;
    std::string filename = make_gpu_shader_binary_filename(shader, options, compiler_options, cmd_line, dep_file);

    return gpu_shader_cache_needs_build(filename, shader, dep_file);
}

std::string gpu_shader_binary_file(GpuModuleUnit const* shader, RenderBackendOptions const& options, char const* compiler_options) {
    std::string cmd_line, dep_file;
    std::string filename = make_gpu_shader_binary_filename(shader, options, compiler_options, cmd_line, dep_file);
    if (!gpu_shader_cache_needs_build(filename, shader, dep_file))
        return filename;

    print(CLL::VERBOSE, "Building \"%s\" to \"%s\":\n$ %s\n", shader->srcpath, filename.c_str(), cmd_line.c_str());

    // only needed for depfile fixup
    std::string old_dep_text;
    try {
        if (file_exists(dep_file))
            old_dep_text = read_text_file(dep_file);
    } catch (...) { }

    int result = system(cmd_line.c_str());
    if (result != 0)
        throw_error("Failed to compile shader binary:\n$ %s\nreturned %d\n", cmd_line.c_str(), result);

    // fix up target binary path of depfile to match the one used in the original build system
    if (!old_dep_text.empty() && file_exists(dep_file)) {
        std::string new_dep_text = read_text_file(dep_file);
        size_t old_lead_offset = old_dep_text.find(": ");
        size_t new_lead_offset = new_dep_text.find(": ");
        if (old_lead_offset != old_dep_text.npos && new_lead_offset != old_dep_text.npos) {
            new_dep_text.replace(new_dep_text.begin(), new_dep_text.begin() + new_lead_offset,
                old_dep_text.begin(), old_dep_text.begin() + old_lead_offset);
        }
        write_text_file(dep_file, new_dep_text.c_str());
    }

    return filename;
}

std::vector<char> read_gpu_shader_binary(GpuModuleUnit const* shader, RenderBackendOptions const& options, char const* compiler_options) {
    std::vector<char> binary;
    auto filename = gpu_shader_binary_file(shader, options, compiler_options);
    FILE* f = fopen(filename.c_str(), "rb");
    if (!f)
        throw_error("Failed to open shader binary file \"%s\"", filename.c_str());
    fseek(f, 0, SEEK_END);
    binary.resize(uint_bound(ftell(f)));
    fseek(f, 0, SEEK_SET);
    bool failure = fread(binary.data(), 1, binary.size(), f) != binary.size();
    fclose(f);
    if (failure)
        throw_error("Failed to read shader binary file \"%s\"", filename.c_str());
    return binary;
}

GpuModuleUnit const* gpu_module_single_unit(GpuProgram const* program, char const* module_name, bool optional) {
    GpuModuleUnit const* single_unit = nullptr;
    for (int module_idx = 0; program->modules[module_idx]; ++module_idx) {
        auto module = program->modules[module_idx];
        if (!module->units[0] || strcmp(module->name, module_name) != 0)
            continue;
        if (single_unit)
            throw_error("Module \"%s\" in program \"%s\" is ambiguous", module_name, program->name);
        if (module->units[1])
            throw_error("Module \"%s\" in program \"%s\" has multiple units attached, single unit requested", module_name, program->name);

        single_unit = module->units[0];
    }
    if (!single_unit && !optional)
        throw_error("Failed to find a unit for module \"%s\" in program \"%s\"", module_name, program->name);
    return single_unit;
}

GpuModuleUnit const* gpu_module_single_unit_typed(GpuModule const* module, char const* unit_type, bool optional) {
    GpuModuleUnit const* single_unit = nullptr;
    for (int unit_idx = 0; module->units[unit_idx]; ++unit_idx) {
        auto unit = module->units[unit_idx];
        if (strcmp(unit->type, unit_type) != 0)
            continue;
        if (single_unit)
            throw_error("Multiple units of type \"%s\" attached to module #%s", unit_type, module->id);
        single_unit = unit;
    }
    if (!single_unit && !optional)
        throw_error("Failed to find a unit of type \"%s\" for module #%s", unit_type, module->id);
    return single_unit;
}

std::vector<GpuProgram> subprograms_from_program(GpuProgram const* program) {
    static std::vector<std::unique_ptr<GpuModule const*[]>> static_module_list_store;
    std::vector<GpuProgram> subprograms;
    int module_count = 0;
    while (program->modules[module_count])
        ++module_count;
    subprograms.resize(module_count);
    std::unique_ptr<GpuModule const*[]> module_list{ new GpuModule const*[module_count * 2] };
    for (int i = 0; GpuModule const* module = program->modules[i]; ++i) {
        GpuProgram& sp = subprograms[i];
        memset(&sp, 0, sizeof(sp));
        sp.id = module->id;
        sp.name = module->name;
        sp.type = program->type;
        sp.feature_flags = module->feature_flags;
        module_list[2 * i] = module;
        module_list[2 * i + 1] = nullptr;
        sp.modules = &module_list[2 * i];
    }
    static_module_list_store.push_back(std::move(module_list));
    return subprograms;
}

// todo: remove
std::vector<char const*> merge_to_old_defines(GpuModuleDefine const* defines, std::vector<std::string>& string_store) {
    std::vector<char const*> old_defines;
    for (int i = 0; defines[i].name; ++i) {
        old_defines.push_back(defines[i].name);
        string_store.push_back(defines[i].name);
    }
    old_defines.push_back(nullptr);
    for (int i = 0; defines[i].name; ++i) {
        if (!defines[i].value)
            continue;
        auto& s = string_store[i];
        s += '=';
        s += defines[i].value;
        old_defines[i] = s.data();
    }
    return old_defines;
}

#include <unordered_map>

template <class T>
size_t hash_bits(T val) {
    static_assert(sizeof(T) <= sizeof(uint64_t));
    union {
        uint64_t b;
        T v;
    } u = { .v = val };
    return std::hash<uint64_t>()(u.b);
}

struct GenericGpuProgramCache::shared_data {
    struct Key {
        GpuProgram const* program;
        RenderBackendOptions options;
    };
    struct KeyHash {
        std::size_t operator()(Key const& a) const {
            return hash_bits(a.program)
            #define hash_backend_option(type, option, default, flags) \
                ^ hash_bits(a.options.option)
            RENDER_BACKEND_OPTIONS(hash_backend_option)
            #undef hash_backend_option
                ;
        }
    };
    struct KeyComp {
        bool operator()(Key const& a, Key const& b) const {
            return a.program == b.program
            #define hash_backend_option(type, option, default, flags) \
                && a.options.option == b.options.option
            RENDER_BACKEND_OPTIONS(hash_backend_option)
            #undef hash_backend_option
                ;
        }
    };
    std::unordered_map<Key, void*, KeyHash, KeyComp> cache;
};

GenericGpuProgramCache::GenericGpuProgramCache() = default;
GenericGpuProgramCache::~GenericGpuProgramCache() {
    this->discard_reference(nullptr);
}
void GenericGpuProgramCache::discard_reference(release_resources_t release_resources) noexcept {
    ref_counted::discard_reference(release_resources);
}

GenericGpuProgramCache::GenericGpuProgramCache(GenericGpuProgramCache const& right) noexcept = default;
GenericGpuProgramCache& GenericGpuProgramCache::operator =(GenericGpuProgramCache const& right) noexcept = default;

void* GenericGpuProgramCache::find(GpuProgram const* program, RenderBackendOptions const& options) {
    auto key = GenericGpuProgramCache::shared_data::Key{program, options};
    auto it = ref_data->cache.find(key);
    return (it != ref_data->cache.end()) ? it->second : nullptr;
}

void GenericGpuProgramCache::add(void* compiled, GpuProgram const* program, RenderBackendOptions const& options) {
    auto key = GenericGpuProgramCache::shared_data::Key{program, options};
    // detect spilling of CPU-only options
    {
    #define nrm_backend_option(type, option, default, flags) \
        if ((flags) == RBO_STAGES_CPU_ONLY && options.option != (default)) \
            warning("CPU-only option %s found in GPU program cache, did you forget to normalize for a specific stage?", #option);
        RENDER_BACKEND_OPTIONS(nrm_backend_option)
    #undef nrm_backend_option
    }
    auto it_inserted = ref_data->cache.insert({ key, compiled });
    if (!it_inserted.second)
        throw_error("Cache entry for program \"%s\" and given options already exists", program->name);
}

void* GenericGpuProgramCache::remove(GpuProgram const* program, RenderBackendOptions const& options) {
    void* removed = nullptr;
    auto key = GenericGpuProgramCache::shared_data::Key{program, options};
    auto it = ref_data->cache.find(key);
    if (it != ref_data->cache.end()) {
        removed = it->second;
        ref_data->cache.erase(it);
    }
    return removed;
}

void* GenericGpuProgramCache::prune_next(int max_count) {
    if (max_count == 0) {
        void* removed = nullptr;
        auto it = ref_data->cache.begin();
        if (it != ref_data->cache.end()) {
            removed = it->second;
            ref_data->cache.erase(it);
        }
        return removed;
    }
    // todo: add a cache pruning strategy?
    return nullptr;
}
