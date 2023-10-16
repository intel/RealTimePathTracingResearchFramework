#!/usr/bin/env python3
# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT


import pyvkr
import sys
import pprint

if len(sys.argv) < 2:
    print(f'usage: {sys.argv[0]} FILE')
    sys.exit(0)

scene = pyvkr.open_scene(sys.argv[1])
pprint.pprint(scene)

