// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "file_mapping.h"
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

FileMapping::FileMapping(const std::string &fname) : mapping(nullptr), num_bytes(0)
{
#ifdef _WIN32
    file_handle = (void*) CreateFile(fname.c_str(),
                                     GENERIC_READ,
                                     FILE_SHARE_READ,
                                     nullptr,
                                     OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL,
                                     nullptr);
    if ((HANDLE) file_handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to open file " + fname);
    }

    LARGE_INTEGER file_size;
    GetFileSizeEx((HANDLE) file_handle, &file_size);
    if (file_size.QuadPart == 0) {
        throw std::runtime_error("Cannot map 0 size file");
    }

    mapping_handle = (void*) CreateFileMapping((HANDLE) file_handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if ((HANDLE) mapping_handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to create file mapping for " + fname);
    }

    num_bytes = file_size.QuadPart;
    mapping = MapViewOfFile((HANDLE) mapping_handle, FILE_MAP_READ, 0, 0, 0);
    if (!mapping) {
        throw std::runtime_error("Failed to create mapped view of file " + fname);
    }
#else
    file = open(fname.c_str(), O_RDONLY);
    if (file == -1) {
        perror("failed opening file");
        fflush(0);
        throw std::runtime_error("Failed to open file " + fname);
    }

    struct stat stat_buf;
    fstat(file, &stat_buf);
    num_bytes = stat_buf.st_size;

    mapping = mmap(NULL, num_bytes, PROT_READ, MAP_SHARED, file, 0);
    if (!mapping) {
        throw std::runtime_error("Failed to map file!");
    }
#endif
}

void FileMapping::release_resources() {
    if (mapping) {
#ifdef _WIN32
        UnmapViewOfFile(mapping);
        CloseHandle((HANDLE) mapping_handle);
        CloseHandle((HANDLE) file_handle);
#else
        munmap(mapping, num_bytes);
        close(file);
#endif
        mapping = nullptr;
    }
}

FileMapping::~FileMapping()
{
    discard_reference();
}

const uint8_t *FileMapping::data() const
{
    return static_cast<uint8_t *>(mapping);
}

size_t FileMapping::nbytes() const
{
    return num_bytes;
}
