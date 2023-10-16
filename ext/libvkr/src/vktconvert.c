// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "vkr.h"
#include <stdio.h>
#include <stdlib.h>

void errorHandler(VkrResult result, const char *msg)
{
  printf("error: %s\n", msg);
}

int main(int argc, char **argv)
{
  if (argc < 4) {
    printf("usage: %s INPUT OUTPUT FORMAT [OPAQUE FORMAT]\n", argv[0]);
    return -1;
  }
  printf("converting %s to %s ...\n", argv[1], argv[2]);
  int format = atoi(argv[3]);
  int opaqueFormat = argc > 4 ? atoi(argv[4]) : format;
  vkr_convert_texture(argv[1], argv[2], format, opaqueFormat,
      errorHandler);
  return 0;
}
