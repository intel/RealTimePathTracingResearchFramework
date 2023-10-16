// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <filesystem>
#include <array>
#ifdef _WIN32
#include <intrin.h>
#elif not defined(__aarch64__)
#include <cpuid.h>
#endif
#include "error_io.h"
#include "util.h"
#include "types.h"
#include "sha1_bytes.h"
#include <glm/ext.hpp>


std::string pretty_print_count(const double count)
{
    std::string result;
    pretty_print_count(result, count);
    return result;
}
void pretty_print_count(std::string& dest, const double count) {
    const double giga = 1000000000;
    const double mega = 1000000;
    const double kilo = 1000;
    if (count > giga) {
        stringf(dest, "%.2f G", count / giga);
    } else if (count > mega) {
        stringf(dest, "%.2f M", count / mega);
    } else if (count > kilo) {
        stringf(dest, "%.2f K", count / kilo);
    } else {
        stringf(dest, "%.2f", count);
    }
}

uint64_t align_to(uint64_t val, uint64_t align)
{
    return ((val + align - 1) / align) * align;
}

void ortho_basis(glm::vec3 &v_x, glm::vec3 &v_y, const glm::vec3 &n)
{
    v_y = glm::vec3(0);

    if (n.x < 0.6f && n.x > -0.6f) {
        v_y.x = 1.f;
    } else if (n.y < 0.6f && n.y > -0.6f) {
        v_y.y = 1.f;
    } else if (n.z < 0.6f && n.z > -0.6f) {
        v_y.z = 1.f;
    } else {
        v_y.x = 1.f;
    }
    v_x = glm::normalize(glm::cross(v_y, n));
    v_y = glm::normalize(glm::cross(n, v_x));
}

void canonicalize_path(std::string &path, std::string const &base)
{
    std::filesystem::path result(path);
    if (!base.empty() && result.is_relative())
        result = base / result;
    result = std::filesystem::weakly_canonical(result);
    path = result.string();
    canonicalize_path_separator(path);
}
void canonicalize_path_separator(std::string &path) {
    std::replace(path.begin(), path.end(), '\\', '/');
}

char path_separator()
{
    return std::filesystem::path::preferred_separator;
}

bool file_exists(const std::string &fname)
{
    return std::filesystem::is_regular_file(fname);
}

bool directory_exists(const std::string &directory)
{
    return std::filesystem::is_directory(directory);
}

void get_all_files_in_directory(const std::string &directory, std::vector<std::string> &outputFiles)
{
    std::filesystem::path dirPath(directory);
    // Loop through all the files in the vignette folder
    for (const auto &dirEntry : std::filesystem::directory_iterator(dirPath))
    {
        const std::string &pathToFile = dirEntry.path().string();
        outputFiles.push_back(pathToFile);
    }
}

static std::string executable_path;

#ifdef _WIN32
#include <windows.h>
#endif

void set_executable_path(char const* binary) {
    assert(binary);
    assert(executable_path.empty());
    std::error_code ec;
    auto absolute_binary = std::filesystem::canonical(binary, ec);
#ifdef _WIN32
    // when launching from WSL, binary name in $0 does not contain path :-/
    if (ec) {
        char binary_buffer[MAX_PATH+1];
        GetModuleFileNameA(NULL, binary_buffer, MAX_PATH+1);
        absolute_binary = std::filesystem::canonical(binary_buffer, ec);
    }
#endif
    if (!absolute_binary.empty()) {
        assert(status(absolute_binary).type() == std::filesystem::file_type::regular);
        executable_path = absolute_binary.string();
    }
    else {
        warning("Executable path \"%s\" could not be resolved", binary);
        executable_path = binary;
    }
}
char const* get_executable_path() {
    return executable_path.c_str();
}
std::string binary_path(const std::string &relative_to_binary_dir) {
    auto absolute_binary_file = std::filesystem::path(executable_path).parent_path() / std::filesystem::path(relative_to_binary_dir);
    absolute_binary_file = std::filesystem::weakly_canonical(absolute_binary_file);
    return absolute_binary_file.string();
}

static std::string root_path;

void detect_root_path(char const* look_for_file) {
    auto in_binary_tree = binary_path(look_for_file);
    if (std::filesystem::exists(in_binary_tree))
        set_root_path(binary_path(".").c_str());
    else if (!file_exists(look_for_file))
        warning("Requested file in root tree \"%s\" could not be resolved", look_for_file);
}
void set_root_path(char const* root) {
    root_path = root;
}
char const* get_root_path() {
    return root_path.c_str();
}
std::string rooted_path(const std::string &relative_to_root_dir) {
    auto absolute_rooted_file = std::filesystem::path(root_path) / std::filesystem::path(relative_to_root_dir);
    absolute_rooted_file = std::filesystem::weakly_canonical(absolute_rooted_file);
    return absolute_rooted_file.string();
}

std::string read_text_file(const std::string &filename)
{
    std::string text;
    FILE* f = fopen(filename.c_str(), "rb");
    if (!f)
        throw_error("Failed to open text file \"%s\" for reading", filename.c_str());
    fseek(f, 0, SEEK_END);
    text.resize(int_cast<size_t>(ftell(f)));
    fseek(f, 0, SEEK_SET);
    bool failure = fread(text.data(), 1, text.size(), f) != text.size();
    fclose(f);
    if (failure)
        throw_error("Failed to read text file \"%s\" for reading", filename.c_str());
    std::ptrdiff_t bom_bytes = 0;
    // BOM
    {
        char const* c_str = text.c_str();
        if ((c_str[0] == (char) 0xFE && c_str[1] == (char) 0xFF)
         || (c_str[0] == (char) 0xFF && c_str[1] == (char) 0xFE))
            throw_error("UTF-16 text files unsupported! \"%s\"", filename.c_str());
        // optional utf-8 marker
        if (c_str[0] == (char) 0xEF && c_str[1] == (char) 0xBB && c_str[2] == (char) 0xBF)
            bom_bytes += 3;
    }
    // filter line endings
    {
        // Previously, c_str from above was used here. However, the call to .data()
        // just below this comment invalidates that old pointer.
        // So instead, we store bom_bytes and recompute c_str here.
        char* text_data = text.data();
        const char *c_str = text.c_str() + bom_bytes;
        for (char const* c_str_end = text.c_str() + text.size(); c_str != c_str_end; ++c_str)
            if (c_str[0] != '\r' || c_str[1] != '\n')
                *text_data++ = *c_str;
        size_t filtered_size = text_data - text.data();
        if (text.size() != filtered_size)
            text.resize(filtered_size);
    }
    return text;
}
void write_text_file(const std::string &filename, char const* text) {
    FILE* f = fopen(filename.c_str(), "w");
    if (!f)
        throw_error("Failed to open text file \"%s\" for writing", filename.c_str());
    if (text)
        fputs(text, f);
    fclose(f);
}

std::string get_file_basepath(const std::string &fname)
{
    const auto path = std::filesystem::path{fname};
    return path.parent_path().string();
}
std::string get_file_name(const std::string &fname)
{
    const auto path = std::filesystem::path{fname};
    return path.filename().string();
}
std::string get_file_basename(const std::string &fname)
{
    const auto path = std::filesystem::path{fname};
    return path.stem().string();
}
std::string get_file_extension(const std::string &fname)
{
    const auto path = std::filesystem::path{fname};
    return path.extension().string();
}
std::string file_replace_extension(const std::string &fname,
    const std::string &new_extension)
{
    auto path = std::filesystem::path{fname};
    return path.replace_extension(new_extension).string();
}

std::string get_cpu_brand()
{
#if defined(__APPLE__) && defined(__aarch64__)
    return "Apple M1";
#else
    std::string brand = "Unspecified";
    std::array<int32_t, 4> regs;
#ifdef _WIN32
    __cpuid(regs.data(), 0x80000000);
#else
    __cpuid(0x80000000, regs[0], regs[1], regs[2], regs[3]);
#endif
    if ((unsigned) regs[0] >= 0x80000004) {
        char b[64] = {0};
        for (int i = 0; i < 3; ++i) {
#ifdef _WIN32
            __cpuid(regs.data(), 0x80000000 + i + 2);
#else
            __cpuid(0x80000000 + i + 2, regs[0], regs[1], regs[2], regs[3]);
#endif
            std::memcpy(b + i * sizeof(regs), regs.data(), sizeof(regs));
        }
        brand = b;
    }
    return brand;
#endif
}

std::string sha1_hash(char const* data, size_t data_len) {
    unsigned char hash[SHA1_HASH_SIZE];
    int hash_len = sha1_bytes(hash, reinterpret_cast<const unsigned char *>(data),
        data_len);
    assert(hash_len == sizeof(hash));
    std::string result(hash_len * 2, 0);
    char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
    for (int i = 0; i < hash_len; ++i) {
        result[2*i] = hex_digits[hash[i] >> 4];
        result[2*i+1] = hex_digits[hash[i] & 0xf];
    }
    return result;
}

float srgb_to_linear(float x)
{
    if (x <= 0.04045f) {
        return x / 12.92f;
    }
    return std::pow((x + 0.055f) / 1.055f, 2.4);
}

float linear_to_srgb(float x)
{
    if (x <= 0.0031308f) {
        return 12.92f * x;
    }
    return 1.055f * pow(x, 1.f / 2.4f) - 0.055f;
}

float luminance(const glm::vec3 &c)
{
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

#ifdef _WIN32

#include <sys/types.h>
#include <sys/stat.h>

unsigned long long get_last_modified(char const* fname) {
    struct _stat mstat;
    if (_stat(fname, &mstat))
        return 0;
    return mstat.st_mtime;
}

// todo: automatic app relaunch support?
bool launch_sibling_process(const std::vector<std::string>& args) {
    return false;
}
void send_launch_signal(int i) { }
void wait_for_signal(int i) { }

#else

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

unsigned long long get_last_modified(char const* fname) {
    struct stat mstat;
    if (stat(fname, &mstat))
        return 0;
    return mstat.st_mtime;
}

bool launch_sibling_process(const std::vector<std::string>& args) {
    pid_t original_proc = getpid();
    pid_t sibling_proc = fork();
    if (sibling_proc < 0) {
        printf("Fork failed, cannot launch %s!\n", args[0].c_str());
        return false;
    }
    if (sibling_proc == 0) {
        char buf[128];
        sprintf(buf, "%i", (int) original_proc);
        setenv("hotreload_calling_pid", buf, 1);

        std::vector<char const*> argv;
        for (auto& a : args)
            argv.push_back(a.c_str());
        argv.push_back(nullptr);
        execv(args[0].c_str(), (char* const*) argv.data());

        printf("Launching sibling %s failed!\n", args[0].c_str());
        return false; // if returning, sibling has not been replaced
    }
    printf("Fork successful, watch out for launched %s.\n", args[0].c_str());
    return true;
}

void send_launch_signal(int i) {
    char const* calling_pid_env = getenv("hotreload_calling_pid");
    int calling_pid = 0;
    if (calling_pid_env && 1 == sscanf(calling_pid_env, "%i", &calling_pid)) {
        printf("Sending signal %i to pid %i!\n", i, calling_pid);
        kill((pid_t) calling_pid, SIGUSR1 + i);
    }
}

void wait_for_signal(int i) {
    timespec timeout;
    timeout.tv_nsec = 0; // 500 * 1000000;
    timeout.tv_sec = 10;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1 + i);
    siginfo_t info = { };
    auto last_handler = signal(SIGUSR1 + i, [](int) {});
    int r = sigtimedwait(&set, &info, &timeout);
    signal(SIGUSR1 + i, last_handler);
    if (r >= 0)
        printf("Received signal %i!\n", i);
    else
        printf("Failed to wait for signal %i!\n", i);
}

#endif

bool in_stack_unwind() { return std::uncaught_exceptions() != 0; }

#include <thread>
#include <chrono>

void chrono_sleep(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
