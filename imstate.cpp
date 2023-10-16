// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "imstate.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "util.h"
#include "util/error_io.h"

#include <memory>
#include <cstring>
#include <unordered_map>

// to follow the established stateful ImGui API, this file
// maintains the following global state:
// 
// CurrentMode:
// ------------
// stores whether state module is in serialization or deserialization mode
//
// state_context:
// --------------
// temporary LUT storage for settings that were loaded or are to be written to file
//
// application_settings_context:
// -----------------------------
// intermediate storage for settings that were loaded or are to be written to file; auto save for persistent state

namespace ImState {

static char const* DefaultObjectTargetId = "<default>";
 
struct StringHash {
    std::size_t operator()(char const* str) const {
        return ImHashStr(str);
    }
};
struct StringEqual {
    bool operator()(char const* a, char const* b) const {
        return std::strcmp(a, b) == 0;
    }
};

static char* allocate_storage(struct ObjectStorage& storage, size_t size);

template <class T = void>
struct ChunkAllocator {
    typedef T value_type;

    ObjectStorage* storage = nullptr;

    ChunkAllocator() = default;
    explicit ChunkAllocator(ObjectStorage* storage) : storage(storage) { }
    template <class U> ChunkAllocator(const ChunkAllocator<U>& right) noexcept : storage(right.storage) { }

    T* allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_array_new_length();
        return (T*) allocate_storage(*storage, sizeof(T) * n);
    }
    // ignore frees, only done when full state is destroyed
    void deallocate(T* p, std::size_t n) noexcept { }
};
template <class T, class U>
bool operator==(const ChunkAllocator<T>& left, const ChunkAllocator<U>& right) { return left.storage == right.storage; }
template <class T, class U>
bool operator!=(const ChunkAllocator<T>& left, const ChunkAllocator<U>& right) { return left.storage != right.storage; }

struct Object;
typedef std::unordered_map< char const*, Object, StringHash, StringEqual, ChunkAllocator<std::pair<char const* const, Object>> > ObjectMap;

struct Object {
    // POD fields for simple values
    char const* value_or_id = nullptr;

    // full-object properties
    struct LazyStorage;
    LazyStorage* lazy = nullptr;
};
struct Object::LazyStorage {
    ObjectMap attributes;

    LazyStorage(ChunkAllocator<> allocator)
        : attributes(allocator) { }
};

struct MemoryBlock {
    static size_t const MIN_SIZE = 1024 * 4;
    size_t size;
    size_t cursor;
    std::unique_ptr<MemoryBlock> prev;
    // data begins here
};

static char* allocate_chunk(std::unique_ptr<MemoryBlock>& chain, size_t size) {
    size_t current_offset = 16; // make overfull block to force allocation by default
    size_t current_size = 0;
    if (chain) {
        current_offset = align_to(chain->cursor, 8);
        current_size = chain->size;
    }
    if (current_offset + size > current_size) {
        size_t next_size = std::max(size, (size_t) MemoryBlock::MIN_SIZE);
        {
            std::unique_ptr<MemoryBlock> next( new( new char[sizeof(MemoryBlock) + next_size] ) MemoryBlock{next_size, 0, nullptr} );
            next->prev = std::move(chain);
            chain = std::move(next);
        }
        current_offset = 0;
        current_size = next_size;
    }
    chain->cursor = current_offset + size;
    return (char*) chain.get() + sizeof(*chain) + current_offset;
}

struct ObjectStorage {
    std::unique_ptr<MemoryBlock> memory_chain;
    std::vector<Object::LazyStorage*> destructors;

    ObjectStorage() = default;
    ObjectStorage(ObjectStorage const&) = delete;
    ObjectStorage& operator=(ObjectStorage const&) = delete;
    ObjectStorage(ObjectStorage&&) = default;
    // working around broken C++ default move order:
    // we need to make sure that the store survives long enough for
    // all derived objects to be move-assigned before destruction.
    // We swap all storage into the source object, post-poning dealloc
    // until that is destructed (there default order is reverse).
    ObjectStorage& operator =(ObjectStorage&& right) noexcept {
        ObjectStorage keep_alive{ std::move(*this) };
        this->~ObjectStorage(); // no-op
        new(this) ObjectStorage(std::move(right));
        right.~ObjectStorage(); // no-op
        new(&right) ObjectStorage(std::move(keep_alive));
        return *this;
    }
    ~ObjectStorage() {
        for (auto* d : destructors)
            d->~LazyStorage();
    }
};

static char* allocate_storage(ObjectStorage& storage, size_t size) {
    return allocate_chunk(storage.memory_chain, size);
}

static Object::LazyStorage* construct_full_object(ObjectStorage& storage, Object& object) {
    if (Object::LazyStorage* lazy = object.lazy)
        return lazy;
    char* memory = allocate_chunk(storage.memory_chain, sizeof(Object::LazyStorage));
    Object::LazyStorage* lazy = new(memory) Object::LazyStorage( ChunkAllocator<>(&storage) );
    storage.destructors.push_back(lazy);

    object.lazy = lazy;
    return lazy;
}

static char* new_string(ObjectStorage& storage, char const* begin, char const* end = nullptr) {
    if (!end && begin)
        end = begin + strlen(begin);
    char* buf = allocate_chunk(storage.memory_chain, end - begin + 1);
    if (begin)
        memcpy(buf, begin, end - begin);
    buf[end - begin] = 0;
    return buf;
}

static char* new_string_or_none(ObjectStorage& storage, char const* begin, char const* end = nullptr) {
    if (!begin || !begin[0])
        return nullptr;
    return new_string(storage, begin, end);
}

static Object& get_or_add_object(ObjectStorage& storage, ObjectMap& objects, char const* name, bool* out_is_new = nullptr) {
    auto it = objects.find(name);
    if (it != objects.end()) {
        if (out_is_new) *out_is_new = false;
        return it->second;
    }
    name = new_string(storage, name);
    if (out_is_new) *out_is_new = true;
    return objects[name];
}

struct StateContext {
    // input
    ObjectMap* object_settings = nullptr;
    // output
    ImGuiTextBuffer* output_textbuf = nullptr;

    // state
    int current_level = 0;
    std::vector<ObjectMap*> next_attributes;
    std::string tmp_string;
} state_context;
// state context mode
ImMode CurrentMode = ImMode::None;

void BeginRead() {
    CurrentMode = ImMode::Deserialize;
    state_context.current_level = 0;
}
void EndRead() {
    assert(CurrentMode == ImMode::Deserialize);
    CurrentMode = ImMode::None;
}

void BeginWrite(ImGuiTextBuffer* output_buffer) {
    if (output_buffer)
        state_context.output_textbuf = output_buffer;
    state_context.output_textbuf->clear();
    CurrentMode = ImMode::Serialize;
    state_context.current_level = 0;
}
void EndWrite() {
    assert(CurrentMode == ImMode::Serialize);
    CurrentMode = ImMode::None;
}

static char const* find_qualifier(char const* name) {
    char const* qualifier_begin = name ? std::strchr(name, '#') : nullptr;
    return qualifier_begin && qualifier_begin[1] == '#' ? qualifier_begin : nullptr;
}

bool Open(const char* target_name) {
    if (CurrentMode == ImMode::Serialize) {
        state_context.output_textbuf->appendf("\n[Application][%s]\n", target_name ? target_name : "");
        state_context.current_level = 0;
        return true;
    } else if (CurrentMode != ImMode::Deserialize)
        return false;

    state_context.current_level = 0;
    state_context.next_attributes.clear();

    auto* settings_map = state_context.object_settings;
    if (!settings_map)
        return false;

    auto it = settings_map->find(target_name ? target_name : DefaultObjectTargetId);
    // fallback for single-key maps
    if (!target_name && settings_map->size() == 1)
        it = settings_map->begin();
    // fallback to unqualified defaults
    if (target_name && it == settings_map->end()) {
        if (char const* qualifier_begin = find_qualifier(target_name)) {
            state_context.tmp_string.assign(target_name, qualifier_begin);
            it = settings_map->find(state_context.tmp_string.c_str());
        }
    }
    if (it == settings_map->end())
        return false;

    state_context.next_attributes.push_back(&it->second.lazy->attributes);
    return true;
}

bool Begin(char const* name, void const* object, bool force_open_level, bool force_new_object) {
    if (CurrentMode == ImMode::Serialize) {
        if (force_new_object)
            state_context.output_textbuf->appendf("[.][*%s]\n", name);
        else
            state_context.output_textbuf->appendf("[.][%s]\n", name);
        ++state_context.current_level;
        return true;
    } else if (CurrentMode != ImMode::Deserialize)
        return false;
    
    assert(state_context.next_attributes.size() == (size_t) state_context.current_level + 1);

    auto& settings_map = *state_context.next_attributes.back();
    auto it = settings_map.find(name);
    if (it == settings_map.end() || !it->second.lazy) {
        // current imgui standard: some end calls always match begin regardless of return value
        if (force_open_level)
            ++state_context.current_level;
        return false; // not a subobject, ignore group
    }

    state_context.next_attributes.push_back(&it->second.lazy->attributes);
    ++state_context.current_level;
    return true;
}
void End(void const* object) {
    if (CurrentMode == ImMode::Serialize) {
        state_context.output_textbuf->append("..\n");
        --state_context.current_level;
        return;
    } else if (CurrentMode != ImMode::Deserialize)
        return;
    
    assert(state_context.current_level > 0);
    if (state_context.next_attributes.size() == (size_t) state_context.current_level + 1)
        state_context.next_attributes.pop_back();
    else
        // if opened with force_open_level, allow one additional level to be skipped
        assert(state_context.next_attributes.size() + 1 == (size_t) state_context.current_level + 1);
    --state_context.current_level;
}

static inline char const* get_current_level_attribute_value(char const* name) {
    assert(state_context.next_attributes.size() == (size_t) state_context.current_level + 1);

    auto& current_attributes = *state_context.next_attributes.back();
    auto it = current_attributes.find(name);
    if (it == current_attributes.end())
        return nullptr;
    return it->second.value_or_id;
}

bool Attribute(char const* name, char* buf, size_t N) {
    if (CurrentMode == ImMode::Serialize) {
        // todo: fix up newlines!
        state_context.output_textbuf->appendf("%s=%s\n", name, buf);
        return false;
    } else if (CurrentMode != ImMode::Deserialize)
        return false;

    char const* value = get_current_level_attribute_value(name);
    if (!value)
        return false;

    strncpy(buf, value, N);
    return true;
}

bool Attribute(char const* name, float* values, int N) {
    if (CurrentMode == ImMode::Serialize) {
        state_context.output_textbuf->appendf("%s=", name);
        for (int i = 0; i < N; ++i)
            state_context.output_textbuf->appendf(" %e", values[i]);
        state_context.output_textbuf->append("\n");
        return false;
    } else if (CurrentMode != ImMode::Deserialize)
        return false;

    char const* value = get_current_level_attribute_value(name);
    if (!value)
        return false;

    for (int i = 0; i < N; ++i) {
        char* end_value = (char*) value;
        values[i] = strtof(value, &end_value);
        if (value == end_value)
            break;
        value = end_value;
    }
    return true;
}
bool Attribute(char const* name, int* values, int N) {
    if (CurrentMode == ImMode::Serialize) {
        state_context.output_textbuf->appendf("%s=", name);
        for (int i = 0; i < N; ++i)
            state_context.output_textbuf->appendf(" %d", values[i]);
        state_context.output_textbuf->append("\n");
        return false;
    } else if (CurrentMode != ImMode::Deserialize)
        return false;

    char const* value = get_current_level_attribute_value(name);
    if (!value)
        return false;

    for (int i = 0; i < N; ++i) {
        char* end_value = (char*) value;
        values[i] = (int) strtol(value, &end_value, 10);
        if (value == end_value)
            break;
        value = end_value;
    }
    return true;
}
bool Attribute(char const* name, bool* values, int N) {
    if (CurrentMode == ImMode::Serialize) {
        state_context.output_textbuf->appendf("%s=", name);
        for (int i = 0; i < N; ++i)
            state_context.output_textbuf->appendf(" %d", values[i]);
        state_context.output_textbuf->append("\n");
        return false;
    } else if (CurrentMode != ImMode::Deserialize)
        return false;

    char const* value = get_current_level_attribute_value(name);
    if (!value)
        return false;

    for (int i = 0; i < N; ++i) {
        char* end_value = (char*) value;
        values[i] = (bool) strtol(value, &end_value, 10);
        if (value == end_value)
            break;
        value = end_value;
    }
    return true;
}

bool Attribute(char const* name, bool active) {
    if (CurrentMode == ImMode::Serialize) {
        if (active)
            Attribute(name, &active);
        return false;
    } else if (CurrentMode != ImMode::Deserialize)
        return false;

    bool set = Attribute(name, &active);
    return set && active;
}

struct InlineStateHandler {
    ObjectStorage storage;
    std::string tmp_string;

    struct State {
        std::vector<Object*> stack;
        // temporary generic override state
        std::string intermediate_target;
        Object intermediate_object;
    } state;
};
struct ApplicationStateHandler : InlineStateHandler {
    // input
    struct Settings {
        double timeline_constraint = 0.0f;
        ObjectMap objects;
        std::string source_file;

        Settings(ObjectStorage& storage)
            : objects(ChunkAllocator<>(&storage)) { }
        // force moves for map etc.
        Settings(Settings const&) = delete;
        Settings& operator =(Settings const&) = delete;
        Settings(Settings&&) = default;
        Settings& operator =(Settings&&) = default;
    };
    std::vector<Settings> settings;
    // output
    ImGuiTextBuffer serialization_buffer;

    std::string current_source_path;
};

static void apply_settings(ObjectStorage& storage, Object& target, Object const& source, bool override = true) {
    if (target.value_or_id != source.value_or_id) {
        if (override || !target.value_or_id)
            target = source;
        return;
    }
    if (!source.lazy)
        return;
    if (!target.lazy)
        construct_full_object(storage, target);
    auto& target_attributes = target.lazy->attributes;
    auto& source_attributes = source.lazy->attributes;

    for (auto sit = source_attributes.begin(), sit_end = source_attributes.end(); sit != sit_end; ++sit) {
        auto& target_att = target_attributes[sit->first];
        auto const& source_att = sit->second;
        if (source_att.lazy)
            apply_settings(storage, target_att, source_att, override);
        else if (override || !target_att.value_or_id) {
            if (char const* value_or_id = source_att.value_or_id)
                target_att.value_or_id = value_or_id;
        }
    }
}

static void apply_intermediate_state(ApplicationStateHandler& handler) {
    if (handler.state.intermediate_target.empty())
        return;
    auto& settings = handler.settings.back();
    char const *intermediate_target = handler.state.intermediate_target.c_str();
    bool is_new = true;

    // apply intermediate set of any new generic settings where applicable
    for (auto it = settings.objects.begin(), it_end = settings.objects.end(); it != it_end; ++it) {
        char const* name = it->first;
        // override settings of matching pre-existing qualified objects
        if (char const* qualifier_begin = find_qualifier(name)) {
            size_t target_len = qualifier_begin-name;
            if (!(std::strncmp(name, intermediate_target, target_len) == 0 && !intermediate_target[target_len]))
                continue;
        }
        // also merge with matching pre-existing generic defaults
        else if (std::strcmp(name, intermediate_target) == 0)
            is_new = false;
        else
            continue;

        apply_settings(handler.storage, it->second, handler.state.intermediate_object);
    }

    // introduce intermediate set as default values for later merging and retrieval, if no defaults yet
    if (is_new) {
        bool was_new = false;
        get_or_add_object(handler.storage, settings.objects, intermediate_target, &was_new) = handler.state.intermediate_object;
        assert(was_new);
    }

    handler.state.intermediate_target.clear();
    handler.state.intermediate_object = Object{};
}

static void ApplicationSettingsHandler_ClearAll(ImGuiContext* ctx, ImGuiSettingsHandler* hdl) {
    ApplicationStateHandler& handler = *(ApplicationStateHandler*) hdl->UserData;
    handler = ApplicationStateHandler();
}

static void* ApplicationSettingsHandler_ReadOpen(ImGuiContext*, ImGuiSettingsHandler* hdl, const char* name) {
    ApplicationStateHandler& handler = *(ApplicationStateHandler*) hdl->UserData;
    // restart recording
    apply_intermediate_state(handler);
    handler.state = ApplicationStateHandler::State();

    if (handler.settings.empty())
        handler.settings.emplace_back(handler.storage);
    auto& settings = handler.settings.back();
    // update to most recent source file
    settings.source_file = handler.current_source_path;

    char const* target_id = name && name[0] ? name : DefaultObjectTargetId;
    bool is_generic = !find_qualifier(name);

    Object* object;
    bool is_new;
    // generic settings must be separated to only apply the new set
    if (is_generic) {
        handler.state.intermediate_target = target_id;
        handler.state.intermediate_object = Object{};
        object = &handler.state.intermediate_object;
        is_new = true;
    } else
        object = &get_or_add_object(handler.storage, settings.objects, target_id, &is_new);
    if (is_new) {
        auto* storage = construct_full_object(handler.storage, *object);
        storage->attributes.reserve(64);  // avoid excessive rehashing
    }

    handler.state.stack.push_back(object);
    return object;
}

static void* ObjectSettingsHandler_ReadOpen(ImGuiContext*, ImGuiSettingsHandler* hdl, const char* name) {
    InlineStateHandler& handler = *(InlineStateHandler*) hdl->UserData;

    assert(name);
    bool make_new = name[0] == '*';
    if (make_new) ++name;

    auto* parent = handler.state.stack.back();
    assert(parent->lazy);
    auto& parent_attributes = parent->lazy->attributes;
    bool is_new = false;
    Object* subobject = &get_or_add_object(handler.storage, parent_attributes, name, &is_new);
    if (!is_new && make_new) {
        *subobject = Object();
        is_new = true;
    }
    if (make_new)
        // make a unique id to avoid later merging of defaults
        subobject->value_or_id = new_string(handler.storage, "");
    if (is_new) {
        auto* storage = construct_full_object(handler.storage, *subobject);
        storage->attributes.reserve(16); // avoid excessive rehashing
    }

    handler.state.stack.push_back(subobject);
    return (void*) subobject;
}

static void pop_subobject(InlineStateHandler& handler) {
    if (handler.state.stack.empty())
        return;
    handler.state.stack.pop_back();
}

static void ApplicationSettingsHandler_ReadLine(ImGuiContext*, ImGuiSettingsHandler* hdl, void*, const char* line) {
    InlineStateHandler& handler = *(InlineStateHandler*) hdl->UserData;
    if (line[0] == '.' && line[1] == '.') {
        pop_subobject(handler);
        return;
    }

    char const* key = line, *keyEnd = line;
    while (*keyEnd && *keyEnd != '=') ++keyEnd;

    handler.tmp_string.assign(key, keyEnd);
    char const* name = handler.tmp_string.c_str();

    auto* parent = handler.state.stack.back();
    assert(parent->lazy);
    auto& parent_attributes = parent->lazy->attributes;
    Object& attribute = get_or_add_object(handler.storage, parent_attributes, name);

    attribute.value_or_id = new_string(handler.storage, keyEnd+1);
}

static void apply_application_settings(ApplicationStateHandler& handler, int settings_idx, bool override_all = true) {
    if ((size_t) settings_idx < handler.settings.size()) {
        state_context.object_settings = &handler.settings[settings_idx].objects;
    // optionally reset imstate context (compatibility: old behavior was to keep settings after done reading)
    } else if (override_all) {
        state_context.object_settings = nullptr;
    }
}

// Apply to existing windows (if any)
static void ApplicationSettingsHandler_ApplyAll(ImGuiContext* ctx, ImGuiSettingsHandler* hdl) {
    ApplicationStateHandler& handler = *(ApplicationStateHandler*) hdl->UserData;
    // end recording
    apply_intermediate_state(handler);
    handler.state = ApplicationStateHandler::State();
    // apply any missing generic defaults (no overrides here, that happened during reading)
    for (auto& settings : handler.settings)
        for (auto it = settings.objects.begin(), it_end = settings.objects.end(); it != it_end; ++it) {
            char const* name = it->first;
            char const* qualifier_begin = find_qualifier(name);
            if (!qualifier_begin)
                continue;
            // parent defaults
            handler.tmp_string.assign(name, qualifier_begin);
            auto parent_it = settings.objects.find(handler.tmp_string.c_str());
            if (parent_it != it_end)
                apply_settings(handler.storage, it->second, parent_it->second, false);
        }
    // apply new settings / refresh pointers
    SwitchSettings();
}

static void ApplicationSettingsHandler_WriteAll(ImGuiContext* ctx, ImGuiSettingsHandler* hdl, ImGuiTextBuffer* buf) {
    ApplicationStateHandler& handler = *(ApplicationStateHandler*) hdl->UserData;
    buf->append(handler.serialization_buffer.begin(), handler.serialization_buffer.end());
}

static ApplicationStateHandler::Settings& add_settings_frame(ApplicationStateHandler& handler, double timecode_constraint) {
    handler.settings.emplace_back(handler.storage);
    auto& settings = handler.settings.back();
    settings.timeline_constraint = timecode_constraint;
    settings.source_file = handler.current_source_path;
    return settings;
}

static void* SequencedSettingsHandler_ReadOpen(ImGuiContext*, ImGuiSettingsHandler* hdl, const char* name) {
    ApplicationStateHandler& handler = *(ApplicationStateHandler*) hdl->UserData;
    // restart recording
    apply_intermediate_state(handler);
    handler.state = ApplicationStateHandler::State();


    double timecode_constraint = 0.0f;
    if (!handler.settings.empty())
        timecode_constraint = handler.settings.back().timeline_constraint;

    double timecode = 0.0f;
    if (name && name[0] && sscanf(name, "%lf", &timecode)) {
        if (name[0] == '+' || name[0] == '-')
            timecode_constraint += timecode;
        else
            timecode_constraint = timecode;
    }

    auto& settings = add_settings_frame(handler, timecode_constraint);
    return &settings;
}

static void* IncludeSettingsHandler_ReadOpen(ImGuiContext* context, ImGuiSettingsHandler* hdl, const char* name) {
    ApplicationStateHandler& handler = *(ApplicationStateHandler*) hdl->UserData;
    const char* filepath = name;

    std::string rebased;
    if (!handler.current_source_path.empty()) {
        rebased = get_file_basepath(handler.current_source_path);
        if (!rebased.empty()) {
            rebased.reserve(rebased.size() + 1 + strlen(name));
            rebased += '/';
            rebased += name;
            filepath = rebased.c_str();
        }
    }

    {
        struct IniLoaderStateBackup {
            ImGuiContext* g;
            ApplicationStateHandler* handler;
            ImVector<char> buffer;
            std::string current_source_path;
            IniLoaderStateBackup(ImGuiContext* g, ApplicationStateHandler* handler)
                : g(g)
                , handler(handler) {
                g->SettingsIniData.Buf.swap(buffer);
                handler->current_source_path.swap(current_source_path);
            }
            IniLoaderStateBackup(IniLoaderStateBackup const&) = delete;
            IniLoaderStateBackup& operator=(IniLoaderStateBackup const&) = delete;
            ~IniLoaderStateBackup() {
                g->SettingsIniData.Buf.swap(buffer);
                handler->current_source_path.swap(current_source_path);
            }
        } state_backup{context, &handler};
        handler.current_source_path = filepath;
        ImGui::LoadIniSettingsFromDisk(filepath);
    }

    return (void*) name;
}

static void TransparentSettingsHandler_WriteAll(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
    // nop, full tree written by application settings handler
}

static void TransparentSettingsHandler_ReadLine(ImGuiContext*, ImGuiSettingsHandler* hdl, void*, const char* line) {
    if (line && line[0])
        printf("No unscoped attributes supported after [;] sequence:\n   %s\n", line);
}

struct ApplicationStateContext {
    ApplicationStateHandler state;
    int next_settings_index = 0;

    ImGuiContext* guiCtx = nullptr;

    std::string autoSaveIniFile;
};

ApplicationStateContext* application_settings_context;

void SetApplicationSettingsContext(ApplicationStateContext* ctx) {
    application_settings_context = ctx;
    // link
    apply_application_settings(ctx->state, ctx->next_settings_index);
    state_context.output_textbuf = &ctx->state.serialization_buffer;
}

void RegisterApplicationSettings(ImGuiContext& g) {
    static ApplicationStateContext application_settings;
    assert(!application_settings.guiCtx);
    application_settings.guiCtx = &g;
    {
        ImGuiSettingsHandler ini_handler;
        ini_handler.TypeName = "Application";
        ini_handler.TypeHash = ImHashStr(ini_handler.TypeName);
        ini_handler.ClearAllFn = ApplicationSettingsHandler_ClearAll;
        ini_handler.ReadOpenFn = ApplicationSettingsHandler_ReadOpen;
        ini_handler.ReadLineFn = ApplicationSettingsHandler_ReadLine;
        ini_handler.ApplyAllFn = ApplicationSettingsHandler_ApplyAll;
        ini_handler.WriteAllFn = ApplicationSettingsHandler_WriteAll;
        ini_handler.UserData = &application_settings.state;
        g.SettingsHandlers.push_back(ini_handler);
    }
    {
        ImGuiSettingsHandler ini_handler;
        ini_handler.TypeName = ".";
        ini_handler.TypeHash = ImHashStr(ini_handler.TypeName);
        ini_handler.ReadOpenFn = ObjectSettingsHandler_ReadOpen;
        ini_handler.ReadLineFn = ApplicationSettingsHandler_ReadLine;
        ini_handler.WriteAllFn = TransparentSettingsHandler_WriteAll;
        ini_handler.UserData = &application_settings.state;
        g.SettingsHandlers.push_back(ini_handler);
    }
    {
        ImGuiSettingsHandler ini_handler;
        ini_handler.TypeName = ";";
        ini_handler.TypeHash = ImHashStr(ini_handler.TypeName);
        ini_handler.ReadOpenFn = SequencedSettingsHandler_ReadOpen;
        ini_handler.ReadLineFn = TransparentSettingsHandler_ReadLine;
        ini_handler.WriteAllFn = TransparentSettingsHandler_WriteAll;
        ini_handler.UserData = &application_settings.state;
        g.SettingsHandlers.push_back(ini_handler);
    }
    {
        ImGuiSettingsHandler ini_handler;
        ini_handler.TypeName = "Include";
        ini_handler.TypeHash = ImHashStr(ini_handler.TypeName);
        ini_handler.ReadOpenFn = IncludeSettingsHandler_ReadOpen;
        ini_handler.ReadLineFn = TransparentSettingsHandler_ReadLine;
        ini_handler.WriteAllFn = TransparentSettingsHandler_WriteAll;
        ini_handler.UserData = &application_settings.state;
        g.SettingsHandlers.push_back(ini_handler);
    }
    SetApplicationSettingsContext(&application_settings);
    // take over auto save
    if (g.IO.IniFilename)
        application_settings.autoSaveIniFile = g.IO.IniFilename;
    // needs to go through ImState infrastructure, call NeedSettingsUpdate() and UpdateSettings()
    g.IO.IniFilename = nullptr;
}

void SetApplicationIniFile(char const* file) {
    auto& context = *application_settings_context;
    if (!file)
        context.autoSaveIniFile.clear();
    else
        context.autoSaveIniFile = file;
}

void ClearSettings(bool autoReload) {
    ImGui::ClearIniSettings();
    if (autoReload) {
        auto& context = *application_settings_context;
        assert(ImGui::GetCurrentContext() == context.guiCtx);
        context.guiCtx->SettingsLoaded = false;
    }
}
void SwitchSettings(bool rewind) {
    auto& context = *application_settings_context;
    if (rewind)
        context.next_settings_index = 0;
    apply_application_settings(context.state, context.next_settings_index);
}
static void LateLoadSettings() {
    auto& context = *application_settings_context;
    // late load initial settings if not happened yet
    if (!context.guiCtx->SettingsLoaded) {
        assert(ImGui::GetCurrentContext() == context.guiCtx);
        if (!context.autoSaveIniFile.empty()) {
            println(CLL::INFORMATION, "Loading auto save config from %s", context.autoSaveIniFile.c_str());
            ImGui::LoadIniSettingsFromDisk(context.autoSaveIniFile.c_str());
        }
        context.guiCtx->SettingsLoaded = true;
    }
}
bool HaveNewSettings(double timecode) {
    LateLoadSettings();
    auto& context = *application_settings_context;
    if ((size_t) context.next_settings_index >= context.state.settings.size())
        return false;
    if (timecode && timecode < context.state.settings[context.next_settings_index].timeline_constraint)
        return false;
    return true;
}
bool NewSettingsSource(std::string& current_settings_source) {
    auto& context = *application_settings_context;
    if (!ImState::InReadMode() || context.next_settings_index >= (int) context.state.settings.size())
        return false;
    auto& next_source = context.state.settings[context.next_settings_index].source_file;
    if (next_source != current_settings_source) {
        current_settings_source = next_source;
        return true;
    }
    return false;
}
void HandledNewSettings() {
    int next_settings_idx = ++application_settings_context->next_settings_index;
    // switch settings, if more available
    apply_application_settings(application_settings_context->state, next_settings_idx, false);
}

void AppendFrame(double delay) {
    auto& context = *application_settings_context;

    double timeline_constraint = 0.0;
    if (!context.state.settings.empty())
        timeline_constraint = context.state.settings.back().timeline_constraint;
    timeline_constraint += delay;
    add_settings_frame(context.state, timeline_constraint);

    // refresh pointers to variable-size state
    apply_application_settings(context.state, context.next_settings_index);
}
void PadFrames(int minNumAfterStart) {
    auto& context = *application_settings_context;

    int numAfterStart = 0;
    {
        int numFrames = NumKeyframes();
        while (numAfterStart < numFrames && numAfterStart < minNumAfterStart
            && context.state.settings[numFrames-1-numAfterStart].timeline_constraint)
            ++numAfterStart;
    }

    double timeline_constraint = 0.0;
    if (!context.state.settings.empty())
        timeline_constraint = context.state.settings.back().timeline_constraint;
    while (numAfterStart < minNumAfterStart) {
        timeline_constraint += 1.0;
        add_settings_frame(context.state, timeline_constraint);
        ++numAfterStart;
    }

    // refresh pointers to variable-size state
    apply_application_settings(context.state, context.next_settings_index);
}
int NumKeyframes() {
    auto& context = *application_settings_context;
    return (int) context.state.settings.size();
}
int CurrentKeyframe() {
    auto& context = *application_settings_context;
    return context.next_settings_index-1;
}
bool LastKeyframeComingUp(double timecode) {
    auto& context = *application_settings_context;
    const int nextIdx = context.next_settings_index;
    const int lastFrame = NumKeyframes()-1;
    // We don't care if the next keyframe is not the last.
    return (nextIdx > lastFrame) || (nextIdx == lastFrame
    // We don't care if we wouldn't actually move on to the next keyframe.
        && timecode >= context.state.settings[nextIdx].timeline_constraint);
}

bool NeedSettingsUpdate() {
    // check weather application state should be updated
    return application_settings_context->guiCtx->IO.WantSaveIniSettings;
}
bool UpdateSettings() {
    auto& context = *application_settings_context;
    // update initial settings, if necessary
    if (!context.guiCtx->IO.WantSaveIniSettings)
        return false;
    context.guiCtx->IO.WantSaveIniSettings = false;

    return WriteSettings(); // attempt auto save
}

bool LoadSettings(char const* file) {
    if (!file) {
        auto& context = *application_settings_context;
        if (context.autoSaveIniFile.empty())
            return false; // no auto save file set
        assert(ImGui::GetCurrentContext() == context.guiCtx);
        file = context.autoSaveIniFile.c_str();
    }
    application_settings_context->state.current_source_path = file;
    ImGui::LoadIniSettingsFromDisk(file);
    return true;
}

bool WriteSettings(char const* file) {
    if (!file) {
        auto& context = *application_settings_context;
        if (context.autoSaveIniFile.empty())
            return false; // no auto save file set
        assert(ImGui::GetCurrentContext() == context.guiCtx);
        file = context.autoSaveIniFile.c_str();
    }
    ImGui::SaveIniSettingsToDisk(file);
    return true;
}

} // namespace
