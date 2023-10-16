// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <stdexcept>

enum struct LogLevel {
    CRITICAL = 0,
    WARNING,
    INFORMATION,
    VERBOSE,
    DEVTEST
};
typedef LogLevel CLL;

void print(LogLevel level, char const* message, ...);
void println(LogLevel level, char const* message, ...);

void test_print(char const* message, ...); // level = DEVTEST
void test_println(char const* message, ...); // level = DEVTEST

void warning(char const* message, ...); // level = WARNING
void warning(LogLevel level, char const* message, ...);

struct logged_exception : std::runtime_error { using runtime_error::runtime_error; };

void throw_error(char const* message, ...);
void print_error(char const* message, ...);
std::string sprint_error(char const* message, ...);

void stringf(std::string& dest, char const* format, ...);
std::string to_stringf(char const* format, ...);
