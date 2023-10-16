// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

struct ProfilingScopeRecord;

struct BasicProfilingScope {
    ProfilingScopeRecord* persistent_record;
    unsigned long long begin_timestamp = 0, end_timestamp = 0;

    BasicProfilingScope(ProfilingScopeRecord* persistent_record, bool auto_start = true)
        : persistent_record(persistent_record) {
        if (auto_start)
            begin();
    }
    BasicProfilingScope(bool auto_start = true)
        : BasicProfilingScope(nullptr, auto_start) { }
    BasicProfilingScope(BasicProfilingScope&& src)
        : persistent_record(src.persistent_record)
        , begin_timestamp(src.begin_timestamp)
        , end_timestamp(src.end_timestamp) {
        src.persistent_record = nullptr;
    }
    BasicProfilingScope& operator=(BasicProfilingScope &&) = delete;
    BasicProfilingScope(BasicProfilingScope const&) = delete;
    BasicProfilingScope& operator=(BasicProfilingScope const&) = delete;
    ~BasicProfilingScope() {
        if (persistent_record)
            end();
    }

    void begin();
    void end();

    double elapsedMS() const;
    unsigned long long elapsedNS() const;
};

struct ProfilingScopeRecord {
    unsigned long long nanoseconds = ~0;
    char const* name = nullptr;
    int scope_level = -1;

    ProfilingScopeRecord(char const* name);
};

// note: needs anonymous namespace to not alias with scopes in other files
namespace {

template <int CounterInFile>
struct LocalizedProfilingScope : BasicProfilingScope {
    ProfilingScopeRecord& my_record(char const* name) {
        static ProfilingScopeRecord record(name);
        return record;
    }
    LocalizedProfilingScope(char const* name, bool auto_start = true)
        : BasicProfilingScope(&my_record(name), auto_start) {
    }
};

#define ProfilingScope LocalizedProfilingScope<__COUNTER__>

} // namespace

void register_profiling_time(int scope_level, char const* name, unsigned long long nanoseconds);
void register_profiling_time(int scope_level, char const* name, unsigned long long const* persistent_nanoseconds);
void log_profiling_times(bool start_at_watermark = true);
