// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

/*
 * This is a simple test program that does nothing but open a .vks file.
 * Uses include crash tests and debugging.
 */

#include "vkr.h"
#include <stdio.h>

void eh(VkrResult result, const char *msg)
{
  printf("error: %s\n", msg);
}

int main(int argc, char **argv)
{
  if (argc <= 1) {
    return -1;
  }

  VkrScene vks = {0};
  return !(vkr_open_scene(argv[1], &vks, eh) == VKR_SUCCESS);
}
