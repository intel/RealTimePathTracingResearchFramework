#!/usr/bin/env python3
# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT


import mmap
import os
import pyvkr
import sys
import numpy as np

if len(sys.argv) < 2:
    print(f'usage: {sys.argv[0]} FILE')
    sys.exit(0)

filename = sys.argv[1]
base,ext = os.path.splitext(filename)
objfile = base + ".obj"

scene = pyvkr.open_scene(filename)

vertex_offset = 0
for idx, inst in enumerate(scene["instances"]):
    name = inst["name"]
    if name == "" or name == "N/A":
        name = f"instance_{idx}"

    meshId = inst["meshId"]
    mesh = scene["meshes"][meshId]

    T = inst["transform"]

    R = T[:3,:3]
    t = T[:3,3]
    RN = np.linalg.inv(R).transpose()

    numTriangles = mesh["numTriangles"]
    vertexScale = mesh["vertexScale"]
    vertexOffset = mesh["vertexOffset"]
    vertexBufferOffset = mesh["vertexBufferOffset"]
    normalUvBufferOffset = mesh["normalUvBufferOffset"]
    indexBufferOffset = mesh["indexBufferOffset"]

    qv = np.memmap(filename, dtype=np.uint64, mode='r',
        offset=vertexBufferOffset, shape=(3*numTriangles))
    vertices = pyvkr.dequantize_vertices(qv, vertexScale, vertexOffset)
    vertices = vertices.dot(R) + t

    qnuv = np.memmap(filename, dtype=np.uint64, mode='r',
        offset=normalUvBufferOffset, shape=(3*numTriangles))
    normals, uv = pyvkr.dequantize_normal_uv(qnuv)
    normals = normals.dot(RN)

    qidcs = None
    if indexBufferOffset > 0:
        qidcs = np.memmap(filename, dtype=np.uint32, mode='r',
            offset=indexBufferOffset, shape=(numTriangles, 3))

    print(f"o {name}")
    for v in vertices:
        print(f"v {v[0]} {v[1]} {v[2]}")
    for n in normals:
        print(f"vn {n[0]} {n[1]} {n[2]}")
    for t in uv:
        print(f"vt {t[0]} {t[1]}")
    if qidcs is not None:
        print("Writing indexed", file=sys.stderr)
        for t in qidcs:
            print(f"f {t[0]+1}/{t[0]+1}/{t[0]+1} {t[1]+1}/{t[1]+1}/{t[1]+1} {t[2]+1}/{t[2]+1}/{t[2]+1}")
    else:
        for t in range(numTriangles):
            i = 3*t + vertex_offset
            print(f"f {i+1}/{i+1}/{i+1} {i+2}/{i+2}/{i+2} {i+3}/{i+3}/{i+3}")

    vertex_offset += len(vertices)
