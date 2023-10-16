// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

// format the count as #G, #M, #K, depending on its magnitude
std::string pretty_print_count(const double count);
void pretty_print_count(std::string& dest, const double count);

uint64_t align_to(uint64_t val, uint64_t align);

// the executable path is the canonical path to the binary of the running program
void set_executable_path(char const* binary);
char const* get_executable_path();
// the root path is where the program resources, caches etc. are located
void set_root_path(char const* root);
char const* get_root_path();
// searches for the given file in the directory of the current executable
// first, if not found there, checks existence in the working directory
void detect_root_path(char const* look_for_file);

// completes a path relative to the folder containing the binary of the running program
std::string binary_path(const std::string &relative_to_binary_dir);
// complete a path relative to the root path containing the program resources, caches etc.
std::string rooted_path(const std::string &relative_to_root_dir);

// returns a normalized path representation of the given path
void canonicalize_path(std::string &path, std::string const &base = {});
void canonicalize_path_separator(std::string &path);
// OS-specific preferred path separator
char path_separator();
bool directory_exists(const std::string &directory);
bool file_exists(const std::string &fname);
std::string read_text_file(const std::string &fname);
void write_text_file(const std::string &fname, char const* text);

// function that returns all the files inside a directory
void get_all_files_in_directory(const std::string &directory, std::vector<std::string>& outputFiles);

// returns only the directory of fname.
std::string get_file_basepath(const std::string &fname);
// strips the directory from fname.
std::string get_file_name(const std::string &fname);
// strips the directory and extension from fname.
std::string get_file_basename(const std::string &fname);
// returns only the file extension of fname (including a leading dot).
std::string get_file_extension(const std::string &fname);
// replaces the file extension by the given extension (should include a dot).
std::string file_replace_extension(const std::string &fname, const std::string &new_extension);

// returns a 40-char hex string representation of the SHA1 hash of the given data.
std::string sha1_hash(char const* data, size_t data_len);

// returns an integer representation of the last modification time, 0 if unreadable.
unsigned long long get_last_modified(char const* fname);

bool launch_sibling_process(const std::vector<std::string>& args);
void send_launch_signal(int i);
void wait_for_signal(int i);

void chrono_sleep(int milliseconds);

std::string get_cpu_brand();
