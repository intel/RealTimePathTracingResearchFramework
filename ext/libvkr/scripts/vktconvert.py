#!/usr/bin/env python3
# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT


import pyvkr
import sys

if len(sys.argv) < 4:
    print(f'usage: {sys.argv[0]} INPUT OUTPUT FORMAT')
    sys.exit(0)

inputFile = sys.argv[1]
outputFile = sys.argv[2]
outputFormat = int(sys.argv[3])

print(f"Converting {inputFile} to {outputFile} ...")
pyvkr.convert_texture(inputFile, outputFile, outputFormat)

