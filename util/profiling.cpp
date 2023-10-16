// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "profiling.h"
#include <chrono>
#include <cassert>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include "error_io.h"
#include <mutex>
#include <atomic>

ProfilingScopeRecord::ProfilingScopeRecord(char const* name)
    : name(name) {
    static_assert(sizeof(std::atomic<decltype(nanoseconds)>) == sizeof(nanoseconds));
    (void) new(&nanoseconds) std::atomic<decltype(nanoseconds)>(~0);
}

struct ProfilingRecord {
    static const int MAX_NAME_LEN = 31;
    unsigned long long nanoseconds = 0;
    unsigned long long const* persistent_nanoseconds = nullptr;
    char name[MAX_NAME_LEN + 1];
    int scope_level;
};

struct ProfilingTable {
    static const size_t MAX_RECORDS = 1024;
    std::vector<ProfilingRecord> records;

    std::mutex mutex;

    int logging_watermark = 0;

    ProfilingTable() {
        records.reserve(MAX_RECORDS);
    }
} profiling_table;

thread_local int current_scope_level = 0;

void BasicProfilingScope::begin() {
    if (persistent_record)
        persistent_record->scope_level = current_scope_level++;

    begin_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

void BasicProfilingScope::end() {
    end_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    if (persistent_record) {
        --current_scope_level;
        assert(persistent_record->scope_level == current_scope_level);

        auto elapsed_ns = end_timestamp - begin_timestamp;

        auto& atomic_nanoseconds = *reinterpret_cast<std::atomic<decltype(persistent_record->nanoseconds)>*>(&persistent_record->nanoseconds);
        unsigned long long last_ns = decltype(persistent_record->nanoseconds)(~0);
        bool needs_registration = atomic_nanoseconds.compare_exchange_strong(last_ns, elapsed_ns);

        if (!needs_registration)
            atomic_nanoseconds += elapsed_ns;
        else
            register_profiling_time(persistent_record->scope_level, persistent_record->name, &persistent_record->nanoseconds);

        persistent_record = nullptr;
    }
}

double BasicProfilingScope::elapsedMS() const {
    return (end_timestamp - begin_timestamp) * 1.0e-6;
}

unsigned long long BasicProfilingScope::elapsedNS() const {
    return (end_timestamp - begin_timestamp);
}

void register_profiling_time(int scope_level, char const* name, unsigned long long nanoseconds) {
    if (profiling_table.records.capacity() == profiling_table.records.size())
        warning("Profiling table required resizing, consider increasing the initial capacity");
    if (scope_level < 0)
        scope_level = current_scope_level;
    std::lock_guard<std::mutex> g(profiling_table.mutex);
    profiling_table.records.emplace_back();
    auto& record = profiling_table.records.back();
    record.nanoseconds = nanoseconds;
    strncpy(record.name, name, ProfilingRecord::MAX_NAME_LEN);
    record.name[ProfilingRecord::MAX_NAME_LEN] = 0;
    record.scope_level = scope_level;
}

void register_profiling_time(int scope_level, char const* name, unsigned long long const* nanoseconds) {
    if (profiling_table.records.capacity() == profiling_table.records.size())
        warning("Profiling table required resizing, consider increasing the initial capacity");
    if (scope_level < 0)
        scope_level = current_scope_level;
    std::lock_guard<std::mutex> g(profiling_table.mutex);
    profiling_table.records.emplace_back();
    auto& record = profiling_table.records.back();
    record.persistent_nanoseconds = nanoseconds;
    strncpy(record.name, name, ProfilingRecord::MAX_NAME_LEN);
    record.name[ProfilingRecord::MAX_NAME_LEN] = 0;
    record.scope_level = scope_level;
}

void log_profiling_times(bool start_at_watermark) {
    int watermark = start_at_watermark ? profiling_table.logging_watermark : 0;
    println(CLL::INFORMATION, "Timings");
    std::atomic_thread_fence(std::memory_order_acquire);
    while (size_t(watermark) < profiling_table.records.size()) {
        auto const& record = profiling_table.records[watermark++];

        auto total_ns = record.persistent_nanoseconds ? *record.persistent_nanoseconds : record.nanoseconds;
        double time = double(total_ns);
        char const* unit = "ns";
        if (total_ns >= 1000000000) {
            unit = "s ";
            time = double(total_ns / 1000) * 1.e-6;
        } else if (total_ns >= 1000000) {
            unit = "ms";
            time *= 1.e-6;
        } else if (total_ns >= 1000) {
            unit = "us";
            time *= 1.e-3;
        }

        int indent = 1 + record.scope_level;
        char indent_buffer[32];
        memset(indent_buffer, '-', indent);
        indent_buffer[indent] = 0;

        println(CLL::INFORMATION, "|%s %-*s%16.2f %s", indent_buffer, ProfilingRecord::MAX_NAME_LEN, record.name, time, unit);
    }
    profiling_table.logging_watermark = watermark;
}
