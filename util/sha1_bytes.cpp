// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "sha1_bytes.h"

extern "C" {
#include "sha1.h"
}

static_assert(SHA1_HASH_SIZE == SHA1_BLOCK_SIZE, "SHA hash size is inconsistent");

int sha1_bytes(
    unsigned char *hash,
    const unsigned char *data,
    size_t len)
{
	if (!hash)
		return SHA1_BLOCK_SIZE;
	SHA1_CTX ctx;
	sha1_init(&ctx);
	sha1_update(&ctx, (BYTE*) data, len);
	sha1_final(&ctx, (BYTE*) hash);
	return SHA1_BLOCK_SIZE;
}
