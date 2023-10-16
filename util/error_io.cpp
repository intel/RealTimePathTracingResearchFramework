// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include <error_io.h>
#include <cstdio>
#include <cstdarg>
#include <stdexcept>
#include <mutex>
#include "types.h"
#include <cinttypes>

#if defined(_WIN32)
#include <windows.h>

void print_colored_label(char const* label, WORD color) {
    static HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD oldTextAttrib = 0;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(console, &info))
        oldTextAttrib = info.wAttributes;

    const WORD colorMask = FOREGROUND_RED
        | FOREGROUND_GREEN
        | FOREGROUND_BLUE
        | FOREGROUND_INTENSITY
        | BACKGROUND_RED
        | BACKGROUND_GREEN
        | BACKGROUND_BLUE
        | BACKGROUND_INTENSITY;

    SetConsoleTextAttribute(console, (oldTextAttrib & ~colorMask) | color);
    printf("[%s] ", label);
    SetConsoleTextAttribute(console, oldTextAttrib);
}
void print_red_label(char const* label) {
    print_colored_label(label, FOREGROUND_RED);
}
void print_yellow_label(char const* label) {
    print_colored_label(label, FOREGROUND_RED | FOREGROUND_GREEN);
}
void print_green_label(char const* label) {
    print_colored_label(label, FOREGROUND_GREEN);
}
#else // !defined _WIN32
void print_colored_label(char const* label, const char *ansiCode) {
    static const char *reset = "\033[0m";
    printf("%s[%s]%s ", ansiCode, label, reset);
}
void print_red_label(char const* label) {
    print_colored_label(label, "\033[31m");
}
void print_yellow_label(char const* label) {
    print_colored_label(label, "\033[33m");
}
void print_green_label(char const* label) {
    print_colored_label(label, "\033[32m");
}
#endif

inline void internal_vstringf(std::string& result, char const* message, va_list args) noexcept {
    std::va_list args_retry;
    va_copy(args_retry, args);
    result.resize(127);
    int total_size = vsnprintf(const_cast<char*>(result.data()), result.size() + 1, message, args);
    if (total_size < 0)
        result = message;
    else {
        bool retry = size_t(total_size) > result.size();
        if (result.size() != size_t(total_size))
            result.resize(total_size);
        if (retry)
            vsnprintf(const_cast<char*>(result.data()), result.size() + 1, message, args_retry);
    }
    va_end(args_retry);
}

static std::mutex message_mutex;

inline bool internal_vprint(LogLevel level, char const* message, va_list args, bool is_locked = false) {
//    if ((int) level > (int) LogLevel::INFORMATION)
//        return false;

    auto guard = !is_locked ? std::unique_lock<std::mutex>(message_mutex) : std::unique_lock<std::mutex>();

    switch (level) {
    case LogLevel::INFORMATION:
        print_green_label("INFO");
        break;
    case LogLevel::WARNING:   
        print_yellow_label("WARNING");
        break;
    case LogLevel::CRITICAL:   
        print_red_label("CRITICAL");
        break;
    case LogLevel::VERBOSE:
        print_green_label("VERBOSE");
        break;
    default:
        // default prints no label
        break;
    }

    vprintf(message, args);
    return true;
}

inline void internal_vprintln(LogLevel level, char const* message, va_list args) {
    std::lock_guard<std::mutex> guard(message_mutex);
    if (internal_vprint(level, message, args, true))
        putchar('\n');
}

void print(LogLevel level, char const* message, ...) {
    va_list args;
    va_start(args, message);
    internal_vprint(level, message, args);
    va_end(args);
}
void println(LogLevel level, char const* message, ...) {
    va_list args;
    va_start(args, message);
    internal_vprintln(level, message, args);
    va_end(args);
}

void test_print(char const* message, ...) {
    va_list args;
    va_start(args, message);
    internal_vprint(LogLevel::DEVTEST, message, args);
    va_end(args);
}
void test_println(char const* message, ...) {
    va_list args;
    va_start(args, message);
    internal_vprintln(LogLevel::DEVTEST, message, args);
    va_end(args);
}

void warning(LogLevel level, char const* message, ...) {
    va_list args;
    va_start(args, message);
    internal_vprintln(level, message, args);
    va_end(args);
}
void warning(char const* message, ...) {
    LogLevel level = LogLevel::WARNING;
    va_list args;
    va_start(args, message);
    internal_vprintln(level, message, args);
    va_end(args);
}

void throw_error(char const* message, ...) {
    va_list args;
    va_start(args, message);
    internal_vprintln(LogLevel::CRITICAL, message, args);
    va_end(args);
    std::string formatted;
    va_start(args, message);
    internal_vstringf(formatted, message, args);
    va_end(args);
    throw logged_exception(std::move(formatted));
}
void print_error(char const* message, ...) {
    va_list args;
    va_start(args, message);
    internal_vprintln(LogLevel::CRITICAL, message, args);
    va_end(args);
}
std::string sprint_error(char const* message, ...) {
    va_list args;
    va_start(args, message);
    internal_vprintln(LogLevel::CRITICAL, message, args);
    va_end(args);
    std::string formatted;
    va_start(args, message);
    internal_vstringf(formatted, message, args);
    va_end(args);
    return formatted;
}

void stringf(std::string& dest, char const* format, ...) {
    va_list args;
    va_start(args, format);
    internal_vstringf(dest, format, args);
    va_end(args);
}

std::string to_stringf(char const* format, ...) {
    std::string formatted;
    va_list args;
    va_start(args, format);
    internal_vstringf(formatted, format, args);
    va_end(args);
    return formatted;
}

void throw_ilen_overflow(int to, intmax_t from) {
    throw_error("Integer length overflow: %" PRIdMAX " -> %d", from, to);
}
void throw_int_overflow(intmax_t to, intmax_t from) {
    throw_error("Integer length overflow: %" PRIdMAX " -> %" PRIdMAX, from, to);
}
void throw_uint_overflow(unsigned to, intmax_t from) {
    throw_error("(U)Integer length overflow: %" PRIdMAX " -> %u", from, to);
}
