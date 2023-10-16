// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#define SHA1_HASH_SIZE 20

int sha1_bytes(
    unsigned char hash[SHA1_HASH_SIZE],
    const unsigned char *data,
    size_t len);
