# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

import bpy
from mathutils import Matrix, Vector
from math import radians, factorial

import numpy as np
import copy
import struct
import re

from collections import defaultdict

def matmul(m, v):
    return np.matmul(m, v[:,:,np.newaxis])[:,:,0]
def matmulh(m, v):
    v = np.hstack( (v, np.ones((v.shape[0], 1))) )
    v = matmul(m, v)
    return v[:,:3]

# https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
def inplace_part_1_by_2(x):
  x &= 0x000003ff                   # x = ---- ---- ---- ---- ---- --98 7654 3210
  #x = (x ^ (x << 16)) & 0xff0000ff # x = ---- --98 ---- ---- ---- ---- 7654 3210
  x ^= (x << 16)
  x &= 0xff0000ff
  #x = (x ^ (x <<  8)) & 0x0300f00f # x = ---- --98 ---- ---- 7654 ---- ---- 3210
  x ^= (x << 8)
  x &= 0x0300f00f
  #x = (x ^ (x <<  4)) & 0x030c30c3 # x = ---- --98 ---- 76-- --54 ---- 32-- --10
  x ^= (x << 4)
  x &= 0x030c30c3
  #x = (x ^ (x <<  2)) & 0x09249249 # x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
  x ^= (x << 2)
  x &= 0x09249249

def morton_codes_destructive(intvecs):
    inplace_part_1_by_2(intvecs)
    out_view = intvecs[:,0] << 1
    out_view += intvecs[:,1]
    out_view <<= 1
    out_view += intvecs[:,2]
    return out_view


class MeshData:
    def __init__(self, mesh=None, global_material_index_map=None, read_blend_attributes=False):
        if mesh is not None:
            self.name = mesh.name
            mesh.calc_loop_triangles()
            triangle_count = len(mesh.loop_triangles)
        else:
            self.name = '<empty mesh>'
            triangle_count = 0

        # expanding to unindexed format
        vertex_count = 3 * triangle_count
        self.vertices = np.zeros((vertex_count,3), dtype=np.float32)
        self.normals = np.zeros((vertex_count,3), dtype=np.float32)
        self.uvs = np.zeros((vertex_count,2), dtype=np.float32)
        self.material_indices = np.zeros((triangle_count,), dtype=np.int32)
        self.original_vertex_indices = np.zeros((triangle_count*3,), dtype=np.uint32)
        self.blend_weight_count = 0
        self.blend_weights = None
        self.blend_indices = None

        # empty mesh is fully constructed at this point
        if vertex_count == 0:
            return

        mesh.loop_triangles.foreach_get('vertices', self.original_vertex_indices)
        shared_vertices = np.zeros(len(mesh.vertices)*3, dtype=np.float32)
        mesh.vertices.foreach_get('co', shared_vertices)
        self.vertices[:] = shared_vertices.reshape((len(mesh.vertices), 3))[self.original_vertex_indices]
        shared_vertices = None

        shared_normals = np.zeros(len(mesh.vertices)*3, dtype=np.float32)
        mesh.vertices.foreach_get('normal', shared_normals)
        self.normals[:] = shared_normals.reshape((len(mesh.vertices), 3))[self.original_vertex_indices]
        shared_normals = None

        if read_blend_attributes:
            # I could not find a way to get these data without loops
            group_counts = np.asarray([len(vertex.groups) for vertex in mesh.vertices])
            self.blend_weight_count = np.max(group_counts)
            shared_weights = np.zeros((len(mesh.vertices), self.blend_weight_count), dtype=np.float32)
            shared_indices = np.ones((len(mesh.vertices), self.blend_weight_count), dtype=np.uint16) * 0xffff
            for i, group_count in enumerate(group_counts):
                mesh.vertices[i].groups.foreach_get('weight', shared_weights[i, :group_count])
                mesh.vertices[i].groups.foreach_get('group', shared_indices[i, :group_count])
            self.blend_weights = np.copy(shared_weights[self.original_vertex_indices])
            self.blend_indices = np.copy(shared_indices[self.original_vertex_indices])
            shared_weights = shared_indices = None

        if len(mesh.uv_layers) > 0:
            active_uv_layer = mesh.uv_layers.active.data

            uv_per_loop = np.zeros((len(active_uv_layer)*2,), dtype=np.float32)
            active_uv_layer.foreach_get('uv', uv_per_loop)

            loop_idcs = np.zeros((len(mesh.loop_triangles)*3,), dtype=np.int32)
            mesh.loop_triangles.foreach_get('loops', loop_idcs)
            self.uvs[:] = uv_per_loop.reshape((len(active_uv_layer),2))[loop_idcs]

            uv_per_loop = None
            loop_idcs = None

        if len(mesh.materials) > 0:
            local_material_indices = np.zeros(len(mesh.loop_triangles), dtype=np.int32)
            mesh.loop_triangles.foreach_get('material_index', local_material_indices)
            self.material_indices[:] = np.array([global_material_index_map[mat] for mat in mesh.materials])[local_material_indices]
            local_material_indices = None

    def clone(self):
        return copy.deepcopy(self)

    def transform(self, tx):
        tx = np.array(tx) # todo: this might be wrong
        txN = np.linalg.inv(tx[:3,:3]).T
        self.vertices[:] = matmulh(tx, self.vertices)
        self.normals[:] = matmul(txN, self.normals)

    def transformed(self, tx):
        clone = self.clone()
        clone.transform(tx)
        return clone

    def remap_blend_indices(self, old_to_new_indices):
        """
        Given a list mapping old bone indices to new bone indices, this
        function updates blend indices in this mesh accordingly.
        """
        if self.blend_weight_count > 0:
            mask = self.blend_indices != 0xffff
            self.blend_indices[mask] = np.asarray(old_to_new_indices)[self.blend_indices[mask]]

    def append(self, other, preliminary=False):
        append_list = getattr(self, 'append_list', [])
        append_list.append(other)
        self.append_list = append_list
        if not preliminary:
            self.finalize_append()
    def finalize_append(self):
        append_list = getattr(self, 'append_list', [])
        if len(append_list) == 0:
            return
        def concat(a):
            return np.concatenate((a(self, 0), *(a(x, 1+i) for i, x in enumerate(append_list))), axis=0)
        self.vertices = concat(lambda x, _: x.vertices)
        self.normals = concat(lambda x, _: x.normals)
        self.uvs = concat(lambda x, _: x.uvs)
        self.material_indices = concat(lambda x, _: x.material_indices)
        # Make all meshes have the same blend weight count
        self.blend_weight_count = max([mesh.blend_weight_count for mesh in append_list])
        if self.blend_weight_count > 0:
            self.blend_weights = np.concatenate([
                np.concatenate(
                    [np.zeros((self.vertices.shape[0], self.blend_weight_count - mesh.blend_weight_count), dtype=np.float32)] +
                    ([mesh.blend_weights] if mesh.blend_weight_count > 0 else []), axis=1)
                for mesh in append_list], axis=0)
            self.blend_indices = np.concatenate([
                np.concatenate(
                    [0xffff * np.ones((self.vertices.shape[0], self.blend_weight_count - mesh.blend_weight_count), dtype=np.uint16)] +
                    ([mesh.blend_indices] if mesh.blend_weight_count > 0 else []), axis=1)
                for mesh in append_list], axis=0)
        # Disambiguate original indices of multiple merged meshes
        index_offsets = np.cumsum(np.array([np.max(self.original_vertex_indices)+1, *(np.max(x.original_vertex_indices)+1 for x in append_list) ], dtype=np.int64))
        self.original_vertex_indices = concat(lambda x, i: x.original_vertex_indices + index_offsets[i-1] if i > 0 else x.original_vertex_indices)
        del self.append_list

    def select(self, selected_triangles, source=None):
        if source is None:
            source = self
        self.vertices = source.vertices.reshape((-1, 3, 3))[selected_triangles].reshape((-1, 3))
        self.normals = source.normals.reshape((-1, 3, 3))[selected_triangles].reshape((-1, 3))
        self.blend_weight_count = source.blend_weight_count
        if source.blend_weight_count > 0:
            self.blend_weights = source.blend_weights.reshape((-1, 3, source.blend_weight_count))[selected_triangles].reshape((-1, source.blend_weight_count))
            self.blend_indices = source.blend_indices.reshape((-1, 3, source.blend_weight_count))[selected_triangles].reshape((-1, source.blend_weight_count))
        self.uvs = source.uvs.reshape((-1, 3, 2))[selected_triangles].reshape((-1, 2))
        self.material_indices = source.material_indices[selected_triangles]
        self.original_vertex_indices = source.original_vertex_indices.reshape(-1, 3)[selected_triangles].reshape(-1)

    def split(self, separate):
        remainder = MeshData()
        remainder.name = self.name + '_split'
        remainder.select(separate, source=self)
        self.select(~separate)
        return remainder

    def enforce_max_diagonal(self, max_diagonal_len):
        self.apply_reordering()
        self.compute_bounds(recompute=False)

        subdiv = np.maximum( np.ceil((self.max - self.min) / max_diagonal_len), 1 )
        cell_ext = (self.max - self.min) / subdiv

        tri_centroids = self.vertices.reshape(-1, 3, 3)
        tri_centroids = np.mean(tri_centroids, axis=1)
        new_mesh_idcs = np.minimum( ((tri_centroids - self.min) / cell_ext).astype(np.int32), subdiv-1)
        new_mesh_idcs = new_mesh_idcs[:,0] + subdiv[0] * new_mesh_idcs[:,1] + subdiv[0] * subdiv[1] * new_mesh_idcs[:,2]

        grid_order = np.argsort(new_mesh_idcs, kind='stable')
        self.reorder_triangles(grid_order)
        new_mesh_idcs = new_mesh_idcs[grid_order]

        mesh_idcs = np.unique(new_mesh_idcs)
        if len(mesh_idcs) <= 1:
            return None

        splits = []
        for i in mesh_idcs:
            split = MeshData()
            split.name = self.name + f'_split{i}'
            # todo: this may be a bit inefficient
            split.select(i == new_mesh_idcs, source=self)
            splits.append(split)
        return splits

    def compute_bounds(self, recompute=True):
        if not recompute and getattr(self, "min", None) is not None:
            return
        self.min = np.amin(self.vertices, axis=0)
        self.max = np.amax(self.vertices, axis=0)
        self.quantization_base = self.min
        self.quantization_extent = np.clip(self.max - self.min, a_min=1e-6, a_max=None)

    def reorder_triangles(self, gather_idcs):
        self.vertices[:] = self.vertices.reshape(-1, 3, 3)[gather_idcs].reshape(-1, 3)
        self.normals[:] = self.normals.reshape(-1, 3, 3)[gather_idcs].reshape(-1, 3)
        self.uvs[:] = self.uvs.reshape(-1, 3, 2)[gather_idcs].reshape(-1, 2)
        self.material_indices[:] = self.material_indices[gather_idcs]
        self.original_vertex_indices[:] = self.original_vertex_indices.reshape(-1, 3)[gather_idcs].reshape(-1)
        if self.blend_weight_count > 0:
            self.blend_weights[:] = self.blend_weights.reshape(-1, 3, self.blend_weight_count)[gather_idcs].reshape(-1, self.blend_weight_count)
            self.blend_indices[:] = self.blend_indices.reshape(-1, 3, self.blend_weight_count)[gather_idcs].reshape(-1, self.blend_weight_count)
        if hasattr(self, 'shared_vertices'):
            del self.shared_vertices

    def compute_spatial_reordering(self):
        pos = self.vertices.reshape(-1, 3, 3)
        bmin = np.amin(pos.reshape(-1,3), axis=0)
        bmax = np.amax(pos.reshape(-1,3), axis=0)
        means = np.mean(pos, axis=1)
        means -= bmin
        means *= 2**10 / (bmax-bmin)
        np.clip(means, a_min=0, a_max=2**10-1, out=means)
        means = means.astype(np.uint32)
        codes = morton_codes_destructive(means)
        self.triangle_reordering = np.argsort(codes)
        return self.triangle_reordering

    def compute_material_reordering(self):
        mat_idcs = self.material_indices
        base_order = getattr(self, 'triangle_reordering', None)
        if base_order is not None:
            mat_idcs = mat_idcs[base_order]
        mat_order = np.argsort(mat_idcs, kind='stable')
        if base_order is not None:
            mat_order[:] = base_order[mat_order]
        self.triangle_reordering = mat_order
        return self.triangle_reordering

    def apply_reordering(self):
        if not hasattr(self, 'triangle_reordering'):
            return
        self.reorder_triangles(self.triangle_reordering)
        del self.triangle_reordering

    def default_segments(self):
        if hasattr(self, 'segment_triangle_counts'):
            return
        self.apply_reordering()
        total_triangle_count = self.material_indices.shape[0]
        self.segment_triangle_counts = np.array([total_triangle_count], dtype=np.int64)
        self.segment_material_ids = np.asarray([self.material_indices.min(initial=0)])

    def segment_by_material(self):
        """
        Splits the mesh into segments with one material per segment.
        """
        self.segment_by_material_range(0)

    def segment_by_material_range(self, max_index=0xff):
        """
        Splits the mesh into segments such that for each segment, the difference
        between largest and smallest material index reaches at most the given
        value.
        """
        material_range_max = self.material_indices.max(initial=0) - self.material_indices.min(initial=0)
        if material_range_max <= max_index:
            self.default_segments()
            return

        if max_index > 0:
            print("Splitting mesh", self.name, "to enforce a maximal material index of", max_index, "(current max is ", material_range_max, ")")

            base_order = getattr(self, 'triangle_reordering', None)
            mat_order = self.compute_material_reordering()
            self.triangle_reordering = base_order

            mat_idcs = self.material_indices[mat_order]
            seg_idcs = np.zeros_like(mat_idcs, dtype=np.int64)

            tri_counts = list()
            material_base_idcs = list()
            seg_idx = 0
            begin = 0
            while begin < len(mat_idcs):
                tri_count = np.count_nonzero(mat_idcs[begin:] <= mat_idcs[begin] + max_index)
                material_base_idcs.append(mat_idcs[begin])
                tri_counts.append(tri_count)
                seg_idcs[begin:begin+tri_count] = seg_idx
                seg_idx += 1
                begin += tri_count
            seg_idcs[mat_order] = seg_idcs.copy()

            if base_order is not None:
                seg_idcs = seg_idcs[base_order]
            seg_order = np.argsort(seg_idcs, kind='stable')
            if base_order is not None:
                seg_order[:] = base_order[seg_order]
            self.triangle_reordering = seg_order

            self.apply_reordering()

            self.segment_triangle_counts = np.asarray(tri_counts, dtype=np.int64)
            self.segment_material_ids = np.asarray(material_base_idcs, dtype=np.int64)
        else:
            print("Splitting mesh", self.name, "to enforce one material per segment.")
            self.compute_material_reordering()
            self.apply_reordering()

            tri_counts = list()
            count_from_beginning = np.arange(1, len(self.material_indices), dtype=np.int64)[self.material_indices[1:] != self.material_indices[:-1]]
            if count_from_beginning.shape[0] > 0:
                tri_counts = [ count_from_beginning[0] ]
                for n in (count_from_beginning[1:] - count_from_beginning[:-1]):
                    tri_counts.append(n)
                tri_counts.append(len(self.material_indices) - count_from_beginning[-1])
            else:
                tri_counts = [ len(self.material_indices) ]

            self.segment_triangle_counts = np.asarray(tri_counts, dtype=np.int64)
            self.segment_material_ids = self.material_indices[np.cumsum(self.segment_triangle_counts) - 1]

        # force shared vertices to be recomputed
        if hasattr(self, 'shared_vertices'):
            del self.shared_vertices

    def recompute_shared_vertices(self, recompute=True):
        if not recompute and getattr(self, "shared_vertices", None) is not None:
            return
        self.default_segments()
        base_triangle = 0
        self.shared_vertices = np.zeros_like(self.original_vertex_indices)
        for segment_tri_count in self.segment_triangle_counts:
            segment_indices = self.original_vertex_indices[3*base_triangle:3*(base_triangle+segment_tri_count)]
            segment_indices = segment_indices - np.min(segment_indices)

            shared_indices = np.zeros(np.max(segment_indices)+1, dtype=self.shared_vertices.dtype)
            shared_indices[segment_indices] = np.arange(3*base_triangle, 3*(base_triangle+segment_tri_count), dtype=shared_indices.dtype)

            self.shared_vertices[3*base_triangle:3*(base_triangle+segment_tri_count)] = shared_indices[segment_indices]

            base_triangle += segment_tri_count

    def cache_optimize(self):
        self.recompute_shared_vertices(recompute=False)

        if not globals().get('have_pyvkr', False):
            print('Error: cache optimization needs pyvkr module')
            return

        tri_idx = self.shared_vertices.reshape(-1, 3)
        reorder_table = np.zeros(len(tri_idx), dtype=tri_idx.dtype)

        tb = 0
        for tc in self.segment_triangle_counts:
            idx = tri_idx[tb:tb + tc]
            reorder = reorder_table[tb:tb + tc]
            assert(idx.dtype == np.int32 or idx.dtype == np.uint32)
            pyvkr.optimize_mesh(idx, np.amin(idx)+tc*3, reorder)
            reorder += tb
            tb += tc

        self.reorder_triangles(reorder_table)

    def normalize_and_sort_blend_weights(self, new_blend_weight_count=None):
        """
        Ensures that all blend weights are non-negative, sum to one and are
        sorted in ascending order. If requested, the blend weight count is
        adjusted to the given count by setting the smallest weights to zero or
        adding zero weights.
        """
        if self.blend_weight_count == 0:
            if new_blend_weight_count is not None and new_blend_weight_count > 0:
                self.blend_weights = np.zeros((self.vertices.shape[0], new_blend_weight_count), dtype=np.float32)
                self.blend_indices = np.zeros((self.vertices.shape[0], new_blend_weight_count), dtype=np.uint16)
            return
        # Sort
        permutation = np.argsort(self.blend_weights, axis=1)
        self.blend_weights = np.take_along_axis(self.blend_weights, permutation, axis=1)
        self.blend_indices = np.take_along_axis(self.blend_indices, permutation, axis=1)
        # Clamp
        self.blend_weights = np.maximum(0.0, self.blend_weights)
        # Adjust the number of weights
        if new_blend_weight_count is not None:
            vertex_count = self.blend_weights.shape[0]
            diff = new_blend_weight_count - self.blend_weight_count
            if diff > 0:
                # Prepend zero weights
                self.blend_weights = np.concatenate([ \
                    np.zeros((vertex_count, diff), dtype=np.float32),
                    self.blend_weights],
                    axis=-1)
                self.blend_indices = np.concatenate([ \
                    0xffff * np.ones((vertex_count, diff), dtype=np.uint16),
                    self.blend_indices],
                    axis=-1)
            elif new_blend_weight_count < self.blend_weight_count:
                # Remove the smallest weights
                self.blend_weights = self.blend_weights[:, -diff:]
                self.blend_indices = self.blend_indices[:, -diff:]
            self.blend_weight_count = new_blend_weight_count
        # Ensure that the sum is not zero
        sums = np.sum(self.blend_weights, axis=1)
        self.blend_weights[:, 0] = np.where(sums == 0.0, np.ones_like(sums), self.blend_weights[:, 0])
        sums = np.sum(self.blend_weights, axis=1)
        # Normalize
        self.blend_weights /= sums[:, np.newaxis]

class LodGroup:
    def __init__(self):
        self.mesh_ids = []
        self.detail_reduction = []

    def add(self, mesh_id, detail_reduction):
        self.mesh_ids.append(mesh_id)
        self.detail_reduction.append(detail_reduction / 100.0)

    def sort_by_detail(self):
        sorted_pairs = sorted(list(zip(self.mesh_ids, self.detail_reduction)), key=lambda p: p[1])
        self.mesh_ids = [ p[0] for p in sorted_pairs ]
        self.detail_reduction = [ p[1] for p in sorted_pairs ]

class ProceduralMesh:
    def __init__(self, name, source_mesh=None):
        self.name = name
        self.source_mesh = source_mesh

class SceneData:

    class MeshInstances:
        """
        A description of all instances of a single Blender mesh object.
        """
        lod_pattern = re.compile("(.*)_lod_([0-9]+)")

        def __init__(self, mesh_bobject, mesh, armature=None):
            """
            Initializes with zero instances.
            :param mesh_bobject: The bpy.types.Object for the mesh.
            :param mesh: The bpy.types.Mesh for the mesh.
            :param armature: If this mesh is affected by an armature, this is
                the bpy.types.Object for the armature.
            """
            self.mesh_bobject = mesh_bobject
            self.mesh = mesh
            # The MeshData once it has been constructed
            self.mesh_data = None
            self.armature = armature
            self.mesh_to_pre_skinning_space = Matrix.Identity(4)
            if self.armature is not None:
                # If the mesh is affected by an armature, it must be transformed
                # to a common space for all meshes affected by this armature
                # before skinning is applied. We call this space pre-skinning
                # space and it is the local frame of the armature. This
                # transform could potentially be animated, but it is not a
                # common use case and we choose to assume that it is constant.
                self.mesh_to_pre_skinning_space = \
                    self.armature.matrix_world.inverted() @ self.mesh_bobject.matrix_world

            # Map instance ids to the transform index.
            self.transform_index = dict()

            # Map instance ids (as defined by the random_id on the depsgraph object
            # instance) to transforms for frames.
            self.instance_to_world = None

            # Figure out if this mesh belongs to an LoD group. Only meshes
            # that conform to the lod_pattern are considered levels of detail.
            # This means that even for the base level, the _lod_[0-9]+ suffix
            # has to be in the name.
            self.lod_group_name = None
            self.lod = -1
            self.lod_group = 0 # This has to be updated before exporting.
            match = self.lod_pattern.match(mesh_bobject.name)
            if match:
                self.lod_group_name = match.group(1)
                self.lod = int(match.group(2))

        def allocate_transforms(self, num_frames):
            """
            Allocate memory for transformations on all instances and frames.
            """
            num_instances = len(self.transform_index)
            self.instance_to_world = np.zeros((num_instances, num_frames, 3, 4),
                dtype=np.float32)

        def set_transform(self, instance_id, frame_id, matrix_world):
            """
            Set the transformation for the given instance and frame.
            """
            # TODO: The old code checked the armature here - do we still need
            # that?
            """
            if self.armature is not None:
                instance_to_world_chain = collection_to_world_chain.append(self.armature)
            else:
                instance_to_world_chain = collection_to_world_chain.append(self.mesh_bobject)
            self.instance_to_world_chains.append(instance_to_world_chain)
            """
            if instance_id in self.transform_index:
                idx = self.transform_index[instance_id]
                self.instance_to_world[idx][frame_id] = np.asarray(matrix_world)[:3]

        def get_transform(self, instance_id, frame_id):
            if instance_id in self.transform_index:
                idx = self.transform_index[instance_id]
                return self.instance_to_world[idx][frame_id]

        def add_instance(self, instance_id):
            """
            Add an instance with the unique identifier instance_id.
            This invalidates instance_to_world completely.
            """
            new_index = len(self.transform_index)
            self.transform_index[instance_id] = new_index
            self.instance_to_world = None

        def is_animated(self, instance_id):
            """:return: True iff the transformation for instance_id varies across time."""
            idx = self.transform_index[instance_id]
            smin = self.instance_to_world[idx].min(axis=0)
            smax = self.instance_to_world[idx].max(axis=0)
            return np.linalg.norm((smax-smin) > 1.0e-6)

        def any_animated(self):
            return any([ self.is_animated(i) for i in self.transform_index ])

        def num_instances(self):
            return len(self.transform_index)

    def allocate_instance_transforms(self, num_frames):
        """
        Allocate memory for transformations on all instances and frames.
        """
        for mi in self.mesh_instances.values():
            mi.allocate_transforms(num_frames)

    def record_instance_transforms(self, frame_index):
        """
        Stores the transformation for all instances and the current frame.
        NOTE: you must use allocate_instance_transforms before calling this.
        """
        deg = bpy.context.evaluated_depsgraph_get()
        for i in deg.object_instances:
            if i.object.data is None or i.object.data.name not in self.mesh_instances:
                continue

            mi = self.mesh_instances[i.object.data.name]
            mi.set_transform(instance_id  = i.random_id,
                             frame_id     = frame_index,
                             matrix_world = i.matrix_world)

    def collect_mesh_instances(self, selected_objects):
        """
        Collect all meshes we want to export
        (but none of the instance transforms at this point).

        Note that we iterate over deg.object_instances here. This must only be
        used as an iterator - do not keep references to these objects.

        The objects are also evaluated. Use object.original to get to the 
        original object.
        """
        print("Evaluating scene graph ...")
        deg = bpy.context.evaluated_depsgraph_get()
        view_layer = deg.view_layer_eval

        if len(selected_objects) == 0:
            selected_objects = None

        print("Collecting mesh instances ...")
        for i in deg.object_instances:
            eval_obj = i.object

            # Check if this object or one of its original parents is in the selected group
            if selected_objects is not None:
                selected_object_or_parent = (i.parent if i.is_instance else eval_obj).original
                while selected_object_or_parent is not None and selected_object_or_parent not in selected_objects:
                    selected_object_or_parent = selected_object_or_parent.parent
                if selected_object_or_parent is None:
                    continue
            elif not (i.parent if i.is_instance else eval_obj).visible_get(view_layer=view_layer):
                continue

            # If there is no data then this object certainly cannot be exported.
            # Otherwise, we use the data name as our identifier.
            if eval_obj.data is None:
                continue
            mesh_name = eval_obj.data.name

            if mesh_name not in self.mesh_instances:
                if eval_obj.type == 'MESH':
                    mesh = eval_obj.data
                else:
                    try:
                        mesh = bpy.data.meshes.new_from_object(eval_obj,
                            preserve_all_data_layers=True,
                            depsgraph=deg)
                        print("Converted object ", eval_obj.name, " to mesh")
                    except RuntimeError:
                        # Conversion is expected to fail for object types like
                        # cameras and lights.
                        if eval_obj.type not in ['GPENCIL', 'ARMATURE', 'LIGHT', 'LIGHT_PROBE', 'CAMERA', 'SPEAKER']:
                            print("Error converting object ", eval_obj.name, " to mesh")
                        continue

                # This can happen on geometry networks that generate instances,
                # where the main object remains visible but has no geometry.
                if len(mesh.vertices) == 0:
                    continue

                if eval_obj.name in self.armature_modifiers:
                    armature = self.armature_modifiers[eval_obj.name].object
                else:
                    armature = None

                self.mesh_instances[mesh_name] = SceneData.MeshInstances(
                    i.object, mesh, armature)

            # random_id is specific to this instance (and will be 0 if this is
            # not an instance).
            self.mesh_instances[mesh_name].add_instance(i.random_id)

        # By default (if we do not export animation), we set all instance
        # transforms to the current frame.
        self.allocate_instance_transforms(1)
        self.record_instance_transforms(0)

    def collect_materials(self):
        """Fills self.materials."""
        for instances in self.mesh_instances.values():
            for mat in instances.mesh.materials:
                if mat not in self.materials:
                    next_material_id = len(self.materials)
                    self.materials[mat] = next_material_id

    def collect_bones(self):
        """Fills self.bones."""
        for armature in self.armatures:
            if armature.type == "ARMATURE" and armature.data is not None:
                for bone in armature.data.bones:
                    if (armature, bone) not in self.bones:
                        next_bone_id = len(self.bones)
                        self.bones[armature, bone] = next_bone_id

    def record_animation(self, scene):
        """
        Plays back the animation of the scene using frames from self.frames,
        records object animation into each TransformChain and bone animation
        into self.bone_transforms.
        """
        print(f"Recording {len(self.frames)} frames of transformation data ...")
        # Remember what frame is currently being displayed
        prev_frame, prev_subframe = scene.frame_current, scene.frame_subframe
        self.allocate_instance_transforms(self.frames.size)
        self.bone_transforms = np.zeros((self.frames.size, len(self.bones), 3, 4), dtype=np.float32)

        wm = bpy.context.window_manager
        wm.progress_begin(0, len(self.frames))
        # Record all frames
        for i, frame in enumerate(self.frames):
            scene.frame_set(int(np.floor(frame)), subframe=frame - np.floor(frame))
            # Record bone to armature space transforms
            for key, j in self.bones.items():
                armature, bone = key
                # The bone is a dummy bone or not animated
                if armature is None or bone.name not in armature.pose.bones:
                    self.bone_transforms[i, j] = np.eye(3, 4)
                    continue
                pose_bone = armature.pose.bones[bone.name]
                self.bone_transforms[i, j] = np.asarray(pose_bone.matrix)[:3]

            self.record_instance_transforms(i)
            wm.progress_update(i)
        wm.progress_end()

        # The bone transforms are currently bone to armature space transforms
        # but are supposed to be pre-skinning to armature space transforms
        for key, j in self.bones.items():
            armature, bone = key
            # The bone is a dummy bone or not animated, so the identity
            # transform is correct
            if armature is None or bone.name not in armature.pose.bones:
                continue
            bone_to_pre_skinning_space = bone.matrix_local
            pre_skinning_to_bone_space = np.linalg.inv(bone_to_pre_skinning_space)
            self.bone_transforms[:, j] = np.einsum("...ij, jk -> ...ik", self.bone_transforms[:, j], pre_skinning_to_bone_space)
        # Restore the previously displayed frame. Note that this does not
        # necessarily mean that the scene is back in its original state.
        scene.frame_set(prev_frame, subframe=prev_subframe)



    def __init__(self, context, scene, objects, export_animation, frame_step,
                 split_sharp_edges, sharp_edge_angle):

        self.export_animation = export_animation
        self.frame_start = scene.frame_start
        self.frame_step = frame_step
        self.frame_rate = scene.render.fps
        self.frames = np.arange(self.frame_start, scene.frame_end + 1.0e-4 * self.frame_step, self.frame_step)

        object_preprocessors = []

        if split_sharp_edges:
            """
            Split sharp edges on all objects if the user requests it.
            """
            class PreprocSplitEdge:
                def __init__(self):
                    self.modifiers = dict()

                def apply(self, bobject):
                    try:
                        mod = bobject.modifiers.new('SharpEdgeSplit', 'EDGE_SPLIT')
                        mod.split_angle = sharp_edge_angle
                        mod.use_edge_angle = True
                        mod.use_edge_sharp = True
                        self.modifiers[bobject.name] = mod
                    except:
                        pass

                def remove(self, bobject):
                    if bobject.name in self.modifiers:
                        bobject.modifiers.remove(self.modifiers[bobject.name])

            object_preprocessors = object_preprocessors + [PreprocSplitEdge()]

        self.armature_modifiers = dict()
        if self.export_animation:
            """
            We temporarily disable all armature modifiers and keep track of
            which ones were disabled and which object they were responsible
            for so that we can reenable them later.
            """
            class PreprocDisableArmature:
                def __init__(self, modifiers):
                    self.modifiers = modifiers

                def apply(self, bobject):
                    modifiers = [mod for mod in bobject.modifiers \
                                  if mod.type == "ARMATURE" \
                                  and mod.show_viewport \
                                  and mod.object is not None]

                    if len(modifiers) > 0:
                        modifiers[0].show_viewport = False
                        self.modifiers[bobject.name] = modifiers[0]

                def remove(self, bobject):
                    for modifier in self.modifiers.values():
                        modifier.show_viewport = True

            object_preprocessors = object_preprocessors + [PreprocDisableArmature(self.armature_modifiers)]

        print("Applying preprocessors ...")
        wm = bpy.context.window_manager
        wm.progress_begin(0, 100)
        for i, obj in enumerate(bpy.data.objects):
            for p in object_preprocessors:
                p.apply(obj)
            wm.progress_update(100 * i // len(bpy.data.objects))
        wm.progress_end()

        # Maps bpy.types.object to SceneData.MeshInstances
        self.mesh_instances = dict()
        # Maps bpy.types.Material to unique indices
        self.materials = dict()
        # Maps pairs of bpy.types.Object for an armature and
        # bpy.types.Bone to unique indices
        self.bones = dict()
        # This is a dummy bone with identity bone to armature space
        # transform
        self.bones[None, None] = 0
        # This array will eventually hold animated pre-skinning to instance
        # space transforms for each bone. The first dim corresponds to
        # self.frames.size, the second to len(self.bones) and the last two
        # to 3x4 matrices.
        self.bone_transforms = np.eye(3, 4, dtype=np.float32)[np.newaxis, np.newaxis, :, :]
        # We record the transform for the current frame by default. This will
        # be replaced if the users chooses to export animation.
        current_frame = bpy.context.scene.frame_current

        self.collect_mesh_instances(objects)

        self.armatures = frozenset([mesh_instances.armature for mesh_instances in self.mesh_instances.values() if mesh_instances.armature is not None])
        self.collect_materials()
        self.collect_bones()
        #print(self.meshes)

        print("Removing preprocessors ...")
        for obj in bpy.data.objects:
            for p in reversed(object_preprocessors):
                p.remove(obj)

        # May get replaced by MeshData for a static mesh that accounts for
        # multiple mesh objects
        self.flat_mesh = None

    def triangulate(self, context):
        print(f"Triangulating {len(self.mesh_instances)} meshes ...")

        wm = bpy.context.window_manager
        wm.progress_begin(0, len(self.mesh_instances))
        for i, instances in enumerate(self.mesh_instances.values()):
            mesh = instances.mesh
            mesh_data = MeshData(mesh, self.materials, self.export_animation and instances.armature is not None)
            # Remap blend indices
            if mesh_data.blend_weight_count > 0:
                bone_indices = dict([(key[1].name, index) for key, index in self.bones.items() if key[0] == instances.armature])
                group_bone_indices = [bone_indices.get(group.name, 0) for group in instances.mesh_bobject.vertex_groups]
                mesh_data.remap_blend_indices(group_bone_indices)
            # empty meshes break things ...
            if len(mesh_data.original_vertex_indices) > 0:
                instances.mesh_data = mesh_data
            wm.progress_update(i)
        wm.progress_end()
        # Delete instances without mesh data
        self.mesh_instances = dict([item for item in self.mesh_instances.items() if item[1].mesh_data is not None])

    def flatten(self, keep_instances=False):
        """
        Creates a single MeshData that combines many meshes from the scene and
        removes these meshes from self. Meshes with an animated transform or
        meshes affected by armatures are always excluded. The flattened mesh (if
        any) is stored in self.flat_mesh and will be returned by
        enumerate_mesh_data().
        :param keep_instances: Pass True, to avoid flattening meshes that have
            at least two instances.
        """
        def merge_mesh(instances):
            no_armature = instances.armature is None
            mergable_instance_count = (len(instances.transform_index) == 1 or not keep_instances)
            no_animation = not instances.any_animated()
            no_lod = instances.lod < 0
            return no_lod and no_armature and mergable_instance_count and no_animation

        print('Flattening instances ...')
        meshes_to_merge = [instances for instances in self.mesh_instances.values() if merge_mesh(instances)]
        for instances in meshes_to_merge:
            print(f"   {instances.mesh_bobject.name}")
            for idx in range(instances.num_instances()):
                mesh_to_world_space = instances.get_transform(instance_id=idx, frame_id=0)
                transformed_mesh = instances.mesh_data.transformed(mesh_to_world_space)
                if self.flat_mesh is None:
                    self.flat_mesh = transformed_mesh
                else:
                    self.flat_mesh.append(transformed_mesh, preliminary=True)
        if self.flat_mesh is not None:
            self.flat_mesh.finalize_append()
        meshes_to_merge = frozenset(meshes_to_merge)
        self.mesh_instances = dict([item for item in self.mesh_instances.items() if item[1] not in meshes_to_merge])

    def optimize_materials(self):
        print("Optimizing materials ...")
        counted_meshes = [m.mesh_data for m in self.mesh_instances.values()]
        unique_materials_for_meshes = []
        material_counts = np.zeros((len(counted_meshes)), dtype=np.int32)
        for mesh_idx, mesh in enumerate(counted_meshes):
            unique_materials = np.unique(mesh.material_indices)
            unique_materials_for_meshes.append(unique_materials)
            material_counts[mesh_idx] = unique_materials.size

        material_reorder_table = np.full((len(self.materials)), -1, dtype=np.int32)
        new_material_idx = 0
        for mesh_idx in np.argsort(material_counts):
            mesh = counted_meshes[mesh_idx]
            unique_materials = unique_materials_for_meshes[mesh_idx]
            print('Counted ', material_counts[mesh_idx], " material for ", mesh.name)
            for old_material_idx in unique_materials:
                if material_reorder_table[old_material_idx] == -1:
                    material_reorder_table[old_material_idx] = new_material_idx
                    new_material_idx += 1

        for material, old_material_idx in self.materials.items():
            self.materials[material] = material_reorder_table[old_material_idx]

        for mesh in counted_meshes:
            mesh.material_indices = material_reorder_table[mesh.material_indices]

    def enumerate_meshes(self):
        """
        Return a list without duplicates of pairs (mesh_data, mesh_instances)
        representing the SceneData. mesh_data is always a valid MeshData.
        mesh_instance is either a MeshInstances object holding mesh_data or None
        for the flattened mesh (if any). It starts with the flat mesh (if any),
        followed by the entries of self.mesh_instances.
        """
        result = [] if self.flat_mesh is None else [(self.flat_mesh, None)]
        result.extend([(instances.mesh_data, instances) for instances in self.mesh_instances.values()])
        return result

    def enumerate_mesh_data(self):
        """
        Return a list without duplicates of all MeshData representing the
        SceneData. It starts with the flat mesh (if any), followed by the
        entries of self.mesh_instances.
        """
        result = [] if self.flat_mesh is None else [self.flat_mesh]
        result += [instances.mesh_data for instances in self.mesh_instances.values()]
        return result

    def enforce_max_diagonal(self, max_diagonal_len):
        print(f"Splitting to max diagonal length {max_diagonal_len} ...")
        wm = bpy.context.window_manager
        wm.progress_begin(0, len(self.mesh_instances))
        split_instances = []
        split_mesh_names = []
        for i,(mesh_name, instances) in enumerate(self.mesh_instances.items()):
            local_max_len = max_diagonal_len / np.amax(instances.instance_to_world @ np.asarray([1.0, 1.0, 1.0, 0.0], dtype=np.float32))
            print(f"Splitting to local max diagonal length {local_max_len} ...")
            splits = instances.mesh_data.enforce_max_diagonal(local_max_len)
            if splits is not None:
                split_mesh_names.append(mesh_name)
                instances.mesh_data = None # will be unlinked
                for i, mesh in enumerate(splits):
                    split_i = copy.copy(instances)
                    split_i.mesh =  ProceduralMesh(mesh_name + f"_diagonal_split{i}", instances.mesh)
                    split_i.mesh_data = mesh
                    split_instances.append((mesh_name, split_i))
            wm.progress_update(i)
        print(f"Split meshes: {split_mesh_names}")
        for mesh_name in split_mesh_names:
            self.mesh_instances.pop(mesh_name)
        for mesh_name,instances in split_instances:
            self.mesh_instances[instances.mesh.name] = instances
        wm.progress_end()

    def initialize_spatial_ordering(self):
        print("Computing spatial initial mesh ordering ...")
        wm = bpy.context.window_manager
        wm.progress_begin(0, len(self.enumerate_mesh_data()))
        for i,mesh in enumerate(self.enumerate_mesh_data()):
            mesh.compute_spatial_reordering()
            wm.progress_update(i)
        wm.progress_end()

    def optimize(self, use_meshoptimizer=False):
        print("Optimizing meshes ...")
        wm = bpy.context.window_manager
        wm.progress_begin(0, len(self.enumerate_mesh_data()))
        for i,mesh in enumerate(self.enumerate_mesh_data()):
            if use_meshoptimizer:
                mesh.cache_optimize()
            wm.progress_update(i)
        wm.progress_end()

    def enforce_material_range(self, max_index=0xff):
        print("Enforcing material range ...")
        for mesh in self.enumerate_mesh_data():
            mesh.segment_by_material_range(max_index)

    def default_segments(self):
        print("Creating default segments ...")
        for mesh in self.enumerate_mesh_data():
            mesh.default_segments()

    def segment_by_material(self):
        print(f"Segmenting meshes by material ...")
        for mesh in self.enumerate_mesh_data():
            mesh.segment_by_material()

def quantize_vertices(vertices, quantization_base, quantization_extent):
    quantization_scaling = 0x200000 / quantization_extent
    vertices = vertices * quantization_scaling
    vertices -= quantization_base * quantization_scaling
    np.clip(vertices, a_min=0, a_max=0x1FFFFF, out=vertices)
    vertices = vertices.astype(np.uint32)
    quantized_vertices = (vertices[:,2] << np.array([21], dtype=np.uint64))
    quantized_vertices += vertices[:,1]
    quantized_vertices <<= 21
    quantized_vertices += vertices[:,0]
    return quantized_vertices

def dequantization_scaling_offsets(quantization_base, quantization_extent):
    quantization_scaling = quantization_extent / 0x200000
    return (quantization_scaling, quantization_base + 0.5 * quantization_scaling)

# represent 0, -1 and 1 precisely by integers
def quantize_normals(normals):
    nl1 = np.abs(normals[:,0])
    nl1 += np.abs(normals[:,1])
    nl1 += np.abs(normals[:,2])
    pn = normals[:,:2] / nl1[:,np.newaxis]
    nl1 = None

    pnN = np.abs(pn[:,::-1])
    pnN -= 1.0
    pnN[pn >= 0.0] *= -1.0
    np.copyto(pn, pnN, where=normals[:,2:3] <= 0.0)
    pnN = None

    pn *= 0x8000
    pn = pn.astype(np.int32)
    np.clip(pn, a_min=-0x7FFF, a_max=0x7FFF, out=pn)
    pn += 0x8000
    pn = pn.astype(np.uint32, copy=False)

    qn = pn[:,1] << 16
    qn += pn[:,0]
    return qn

# tile cleanly by snapping boundaries to integers (wastes 0.5 step on each side)
def quantize_uvs(uv):
    # Avoid broken UV conversion by shifting each face into the positive domain.
    # We can do this because the domain is repeated.
    uv3 = uv.reshape((-1,3,2))
    uv3Org = np.floor(uv3.min(axis=1))
    uv3 -= uv3Org[:, np.newaxis, :]
    uv = uv3.reshape((-1,2))

    # Actual quantization (note that inputs should be < 8).
    uv = uv * 0xFFFF / 8.0
    uv += 0.5
    uv = uv.astype(np.int32)
    uv &= 0xFFFF
    uv = uv.astype(np.uint32, copy=False)

    quv = uv[:,1] << 16
    quv += uv[:,0]
    return quv


def encode_ints(ranges, values):
    """
    Encodes multiple integers within a fixed range into a single bigger integer.
    :param ranges: Array of shape value_count. The product must be less than
        2^64. The first entry is not actually used for anything.
    :param values: Array of shape (..., value_count) providing values ranging
        from 0 to ranges - 1.
    :return: Array of shape (...) with values ranging from 0 to
        np.prod(ranges) - 1.
    """
    prods = np.concatenate([(np.cumprod(ranges[1:][::-1]))[::-1], [1]])
    return np.sum(values * prods, axis=-1)


def lehmer_to_permutation(lehmer, entry_count):
    """
    Turns an integer into a permutation. The produced sequence of permutations
    is lexicographically ordered. This is algorithm 1 here:
    https://doi.org/10.1145/3522607
    :param lehmer: Array of shape (...) providing integers from 0 to
        factorial(entry_count) - 1.
    :param entry_count: The number of elements, which get shuffled by the
        permutation.
    :return: The permutation as array of shape (..., entry_count).
    """
    permutation = np.zeros(list(lehmer.shape) + [entry_count], dtype=np.int64)
    quotient = np.asarray(lehmer, dtype=np.int64)
    for i in range(2, entry_count + 1):
        quotient, remainder = np.divmod(quotient, i)
        permutation[..., entry_count - i] = remainder
        permutation[..., entry_count + 1 - i:] += np.where(permutation[..., entry_count + 1 - i:] >= remainder[..., np.newaxis], 1, 0)
    return permutation


class BlendAttributeCodec:
    """
    Holds parameters defining how blend weights are encoded into an integer and
    provides that functionality.
    """

    def __init__(self, weight_value_count=32, extra_value_counts=(1, 1, 1, 2, 4), payload_value_count_over_factorial=127):
        """
        Sets up this codec with the given parameters.
        :param weight_value_count: Per weight, there is an integer from 0 to
            weight_value_count - 1 to store it. To invest b bits per weight, set
            this to 2^b. Must be less than 2^16=65536.
        :param extra_value_counts: A multiplier for the precision of individual
            weights or 1 to stick to the precision of weight_value_count. The
            length + 1 gives the maximal number of bone influences.
        :param payload_value_count_over_factorial: Product of all
            extra_value_counts, multiplied by the number of representable tuple
            indices (must be at most 2^32) and divided by the factorial of
            len(extra_value_counts).
        """
        self.weight_value_count = weight_value_count
        self.extra_value_counts = np.asarray(extra_value_counts)
        self.payload_value_count_over_factorial = payload_value_count_over_factorial
        self.entry_count = self.extra_value_counts.size

    def compress(self, weights, tuple_index):
        """
        Encodes blend weights and a tuple index for the table of bone indices
        into up to 64 bits using this codec.
        :param weights: Non-negative blend weights sorted in ascending order.
            Their sum must be one. Shape (..., self.entry_count).
        :param tuple_index: An index into a table of blend index tuples. Shape
            (...).
        :return: The code for the weights and the tuple index. Shape (...) and
            scalar type uint64. Between 0 and self.get_max_code() - 1.
        """
        # Transform weights so that they fill out the unit cube
        prefixes = np.cumsum(weights, axis=-1) - weights
        cube_weights = (self.entry_count + 1 - np.arange(self.entry_count)) * weights[..., :-1] + prefixes[..., :-1]
        value_counts = (self.weight_value_count - self.entry_count) * self.extra_value_counts
        # Quantize into one part that has the same range for each weight and one
        # that has different range per weight
        offsets = (np.arange(self.entry_count) + 1) * self.extra_value_counts - 1
        combined = np.asarray(np.floor(value_counts * cube_weights + (offsets + 0.5)), dtype=np.uint32)
        quantized = combined // self.extra_value_counts
        extra = combined - self.extra_value_counts * quantized
        # Encode relevant values into the payload
        payload = encode_ints(
            np.concatenate([[-1], self.extra_value_counts]),
            np.concatenate([tuple_index[..., np.newaxis], extra], axis=-1))
        # Take apart the payload
        payload_quotient, lehmer = np.divmod(payload, factorial(self.entry_count))
        # Turn the Lehmer code into a permutation
        permutation = lehmer_to_permutation(lehmer, self.entry_count)
        # Shuffle the quantized values using the inverse permutation
        shuffled = quantized + 2**28 * permutation
        shuffled = np.sort(shuffled, axis=-1)
        shuffled = shuffled & 0x0fffffff
        # Encode everything
        return encode_ints(
            np.asarray([-1] + self.entry_count * [self.weight_value_count]),
            np.concatenate([payload_quotient[..., np.newaxis], shuffled], axis=-1))

    def get_max_code(self):
        """Returns an upper bound for return values of self.compress()."""
        return self.weight_value_count**self.entry_count * self.payload_value_count_over_factorial


def matrix_to_quaternion(matrix):
    """
    Given a rotation matrix (shape (..., 3, 3)), this function returns a
    normalized quaternion (shape (..., 4)) describing the same rotation. It is
    based on http://www.j3d.org/matrix_faq/matrfaq_latest.html (Q55).
    """
    # There are four different ways to go about it, which are stable in
    # different cases. Different from the FAQ, we multiply everything by S and
    # normalize in the end.
    solution_t = np.stack([
        matrix[..., 2, 1] - matrix[..., 1, 2],
        matrix[..., 0, 2] - matrix[..., 2, 0],
        matrix[..., 1, 0] - matrix[..., 0, 1],
        1.0 + matrix[..., 0, 0] + matrix[..., 1, 1] + matrix[..., 2, 2],
    ], axis=-1)
    solution_0 = np.stack([
        1.0 + matrix[..., 0, 0] - matrix[..., 1, 1] - matrix[..., 2, 2],
        matrix[..., 1, 0] + matrix[..., 0, 1],
        matrix[..., 0, 2] + matrix[..., 2, 0],
        matrix[..., 2, 1] - matrix[..., 1, 2],
    ], axis=-1)
    solution_1 = np.stack([
        matrix[..., 1, 0] + matrix[..., 0, 1],
        1.0 + matrix[..., 1, 1] - matrix[..., 0, 0] - matrix[..., 2, 2],
        matrix[..., 2, 1] + matrix[..., 1, 2],
        matrix[..., 0, 2] - matrix[..., 2, 0],
    ], axis=-1)
    solution_2 = np.stack([
        matrix[..., 0, 2] + matrix[..., 2, 0],
        matrix[..., 2, 1] + matrix[..., 1, 2],
        1.0 + matrix[..., 2, 2] - matrix[..., 0, 0] - matrix[..., 1, 1],
        matrix[..., 1, 0] - matrix[..., 0, 1],
    ], axis=-1)
    # Pick a solution that looks like it should be stable
    choice = np.argmax(np.diagonal(matrix, axis1=-2, axis2=-1), axis=-1)
    choice = np.where(solution_t[..., 3] > 0.1, 3, choice)
    solution = np.choose(choice[..., np.newaxis].repeat(4, -1), [solution_0, solution_1, solution_2, solution_t])
    # Normalize
    solution /= np.linalg.norm(solution, axis=-1)[..., np.newaxis]
    return solution


def quantize_transforms(matrix):
    """
    Takes apart a transformation matrix into rotation, scaling and translation.
    Rotations are converted to 16-bit fixed-point quaternions, scaling and
    translation to floats. Orientation reversing transforms are supported.
    :param matrix: An array of shape (..., 3, 4) providing transformation
        matrices without shear.
    :return: A tuple (translation_and_scaling, rotation). The return values are
        arrays of shape (..., 4) using 32-bit floats and 16-bit uint,
        respectively.
    """
    translation = matrix[..., :, 3]
    scaling = np.linalg.norm(matrix[..., 0], axis=-1)
    scaling *= np.sign(np.linalg.det(matrix[..., :3, :3]))
    rotation = matrix[..., :3, :3] / scaling[..., np.newaxis, np.newaxis]
    # Represent the rotation as quaternion
    quaternion = matrix_to_quaternion(rotation)
    quantized = np.asarray(np.floor((quaternion * 0.5 + 0.5) * (2.0**16.0 - 1.0) - 0.5), dtype=np.uint16)
    return np.concatenate([translation, scaling[..., np.newaxis]], axis=-1, dtype=np.float32), quantized


def tabulate_index_tuples(blend_indices):
    """
    Eliminates redundant tuples of blend indices.
    :param blend_indices: An array of shape (vertex_count, blend_weight_count)
        providing indices. Use 0xffff for irrelevant indices and put them at the
        start.
    :return: A pair (tuple_indices, tuple_table). tuple_indices has shape
        vertex_count and provides an index into tuple_table for each vertex
        such that the tuple in the table matches the relevant indices. For
        singleton tuples, tuple_indices simply holds the single index.
        tuple_table has shape (table_size, blend_weight_count) and the same
        scalar type as blend_indices.
    """
    # Sort the tuples lexicographically
    weight_count = blend_indices.shape[-1]
    permutation = np.lexsort([blend_indices[:, i] for i in range(weight_count)])
    sorted_indices = blend_indices[permutation]
    # Figure out which tuples cannot reuse the previous tuple
    prev_sorted_indices = np.roll(sorted_indices, 1, axis=0)
    prev_sorted_indices[0, :] = 0xffff
    mismatch = np.any(np.logical_and(sorted_indices != prev_sorted_indices, sorted_indices != 0xffff), axis=1)
    # Build a table from only these tuples
    tuple_table = sorted_indices[mismatch]
    # Figure out where each tuple went
    sorted_tuple_indices = np.cumsum(np.where(mismatch, 1, 0), axis=0) - 1
    tuple_indices = np.zeros_like(sorted_tuple_indices, dtype=np.uint16)
    tuple_indices[permutation] = sorted_tuple_indices
    # Singletons get special treatment
    singleton = np.count_nonzero(blend_indices == 0xffff, axis=1) == (weight_count - 1)
    tuple_indices[singleton] = blend_indices[singleton, -1]
    return tuple_indices, tuple_table


def write_string_to_file(f, string):
    ecs = string.encode('utf-8')
    f.write(struct.pack('q', len(ecs)))
    f.write(ecs)
    f.write(struct.pack('b', 0))


def write_scene_data(context, filepath, scene_data, format_version, store_index_buffer=False):
    print(f'Writing scene data to {filepath} ...')
    f = open(filepath, 'wb') # , encoding='utf-8'

    mesh_datas = scene_data.enumerate_mesh_data()
    total_triangle_count = sum([mesh_data.vertices.shape[0] // 3 for mesh_data in mesh_datas])
    total_instance_count = sum([instances.num_instances() for instances in scene_data.mesh_instances.values()])
    if scene_data.flat_mesh is not None:
        total_instance_count += 1
    total_material_count = len(scene_data.materials)

    file_flags = 0

    f.write(struct.pack('II', 0x00abcabc, format_version))
    if format_version >= 3:
        f.write(struct.pack('Q', file_flags))
        header_stride_fpos = f.tell()
        f.write(struct.pack('q', 0))
        data_offset_fpos = f.tell()
        f.write(struct.pack('q', 0))
    if format_version >= 2:
        f.write(struct.pack('qq', len(mesh_datas), total_instance_count))
    f.write(struct.pack('qq', total_material_count, total_triangle_count))
    if format_version >= 3:
        f.write(struct.pack('q', len(mesh_datas))) # currently, instance group count == mesh count

    # Build LoD groups.
    # Note: all of the below code assumes that the order of dict.values() is stable.
    # This is guaranteed in Python 3.7+, where iteration order is insertion order.
    print("Building LoD groups ...")
    lod_groups = []
    if format_version >= 4:
        lod_group_index = dict()
        lod_groups.append(LodGroup()) # The catch all lod group.

        for mesh_id, (mesh, instances) in enumerate(scene_data.enumerate_meshes()):
            if instances is None or instances.lod < 0:
                continue
            lod_group_name = instances.lod_group_name
            if lod_group_name in lod_group_index:
                group_index = lod_group_index[lod_group_name]
            else:
                group_index = len(lod_groups)
                lod_groups.append(LodGroup())
                lod_group_index[lod_group_name] = group_index
            lod_groups[group_index].add(mesh_id, instances.lod)
            instances.lod_group = group_index

        for lod_group in lod_groups:
            lod_group.sort_by_detail()

    if format_version >= 4:
        f.write(struct.pack('q', len(lod_groups)))
        lod_group_offset_fpos = f.tell()
        f.write(struct.pack('q', 0))

    print("Building transform table ...")
    if scene_data.export_animation:
        # Build a big table of all animated transforms
        transform_table = np.concatenate(
            # Dummy transform for a flattened mesh
            [np.eye(3, 4, dtype=np.float32)[np.newaxis, np.newaxis].repeat(scene_data.frames.size, 0)] +
            # Instance to world space transforms
            [instances.instance_to_world[i,:,np.newaxis] \
                for instances in scene_data.mesh_instances.values() \
                for i in instances.transform_index.values()] +
            # Pre-skinning to instance space transforms (i.e. bone transforms)
            [scene_data.bone_transforms],
            axis=1, dtype=np.float32)
        # Keep track of where each transform landed in that table
        mesh_transform_offsets = np.cumsum([1] + [instances.num_instances() for instances in scene_data.mesh_instances.values()])
        bone_transform_offset = mesh_transform_offsets[-1]
        transform_count = transform_table.shape[1]
        # Identify which transforms are completely static across all frames
        is_animated = np.linalg.norm(transform_table.max(axis=0) - transform_table.min(axis=0), axis=(1, 2)) > 1.0e-6
        # Split the table into a static and a dynamic part
        static_table = transform_table[0, ~is_animated, :, :]
        animated_table = transform_table[:, is_animated, :, :]
        transform_table = None
        # Keep track of the index mapping (static transforms come first)
        transform_index_mapping = np.zeros(transform_count, dtype=int)
        static_cumsum = np.cumsum(np.where(is_animated, 0, 1))
        static_count = static_cumsum[-1]
        animated_count = transform_count - static_count
        transform_index_mapping[~is_animated] = static_cumsum[~is_animated] - 1
        transform_index_mapping[is_animated] = np.cumsum(np.where(is_animated, 1, 0))[is_animated] + (static_count - 1)
    else:
        # Just stack up all the static transforms
        static_table = np.stack(
            # Dummy transform for a flattened mesh
            [np.eye(3, 4, dtype=np.float32)] +
            # Instance to world space transforms
            [instances.instance_to_world[i,0] \
                for instances in scene_data.mesh_instances.values() \
                for i in instances.transform_index.values()])
        transform_index_mapping = np.arange(static_table.shape[0])
        static_count = static_table.shape[0]
        animated_count = 0
        animated_table = np.zeros((1, 0, 3, 4))

    if scene_data.export_animation:
        print("Preparing blend weights ...")
        # Ensure that each skinned mesh uses six weights per vertex and gather
        # blend indices
        blend_indices = list()
        blend_index_offsets = list()
        blend_index_offset = 0
        for mesh in scene_data.enumerate_mesh_data():
            blend_index_offsets.append(blend_index_offset)
            if mesh.blend_weight_count > 0:
                mesh.normalize_and_sort_blend_weights(6)
                remapped = transform_index_mapping[mesh.blend_indices + bone_transform_offset]
                blend_indices.append(remapped)
                blend_index_offset += mesh.blend_indices.shape[0]
        if len(blend_indices) > 0:
            blend_indices = np.concatenate(blend_indices, axis=0)
            # Build the table of bone index tuples
            tuple_indices, tuple_table = tabulate_index_tuples(blend_indices)
        else:
            tuple_indices = np.zeros(0, dtype=np.uint16)
            tuple_table = np.zeros((0, 6), dtype=np.uint16)
    else:
        tuple_indices = np.zeros(0, dtype=np.uint16)
        tuple_table = np.zeros((0, 6), dtype=np.uint16)

    if format_version >= 4:
        # Write the size of the tuple table and a byte offset to the file
        f.write(struct.pack('q', tuple_table.shape[0]))
        tuple_table_offset_fpos = f.tell()
        f.write(struct.pack('q', 0))
        # Write animation metadata to the header
        f.write(struct.pack('ff', scene_data.frame_start / scene_data.frame_rate, scene_data.frame_step / scene_data.frame_rate))
        f.write(struct.pack('qqq', scene_data.frames.size, static_count, animated_count))
        animation_offset_fpos = f.tell()
        f.write(struct.pack('q', 0))

    if format_version >= 3:
        fpos = f.tell()
        f.seek(header_stride_fpos)
        f.write(struct.pack('q', fpos))
        f.seek(fpos)

    # Maps a mesh_index to a list of transform indices. They are already
    # remapped.
    instance_transforms = []
    mesh_index = 0
    transform_offset = 1
    MESH_FLAGS_INDICES = 0x1
    MESH_FLAGS_BLEND_ATTRIBUTES = 0x2
    for mesh, instances in scene_data.enumerate_meshes():
        mesh.compute_bounds(recompute=False)

        dqs, dqb = dequantization_scaling_offsets(mesh.quantization_base, mesh.quantization_extent)
        f.write(struct.pack('fff', *dqs.flat))
        f.write(struct.pack('fff', *dqb.flat))

        if format_version < 2:
            mesh_index += 1
            continue
        assert format_version >= 3 # v2 is deprecated

        mesh_flags = 0
        if store_index_buffer:
            mesh_flags |= MESH_FLAGS_INDICES
        if mesh.blend_weight_count > 0:
            mesh_flags |= MESH_FLAGS_BLEND_ATTRIBUTES
        f.write(struct.pack('Q', mesh_flags))
        mesh_header_stride_fpos = f.tell()
        f.write(struct.pack('q', 0))  # headerEnd (deprecated)
        mesh.data_offset_fpos = f.tell()  # vertexBufferOffset (deprecated)
        f.write(struct.pack('q', 0))

        mesh.default_segments()
        f.write(struct.pack('qq', mesh.segment_triangle_counts.shape[0],
            mesh.original_vertex_indices.shape[0] // 3))

        material_base = mesh.material_indices.min()
        material_count = mesh.material_indices.max() + 1 - material_base
        f.write(struct.pack('iI', material_base, material_count))

        if format_version >= 4:
            f.write(struct.pack('q', instances.lod_group if instances is not None else 0))
            f.write(struct.pack('qqqq', 0, 0, 0, 0)) # reserved
        else:
            f.write(struct.pack('qqqqq', 0, 0, 0, 0, 0)) # reserved

        stc = mesh.segment_triangle_counts.astype(np.int64, copy=False)
        stc.tofile(f)
        stc = None
        stm = mesh.segment_material_ids.astype(np.int32, copy=False)
        stm.tofile(f)
        stm = None

        num_instances = 1 if instances is None else instances.num_instances()
        print(f"Writing mesh {mesh.name} with {num_instances} instance{'s' if num_instances > 1 else ''} ...")
        write_string_to_file(f, mesh.name)

        fpos = f.tell()
        f.seek(mesh_header_stride_fpos)
        f.write(struct.pack('q', fpos))
        f.seek(fpos)

        if instances is not None:
            transform_indices = transform_offset + np.arange(len(instances.transform_index))
            transform_indices = transform_index_mapping[transform_indices]
            instance_transforms.append(transform_indices)
            transform_offset += transform_indices.size
        else:
            instance_transforms.append([0])
        mesh_index += 1
    
    written_mesh_count = mesh_index

    if format_version >= 2:
        for mesh_index in range(written_mesh_count):
            assert format_version >= 3 # v2 is deprecated

            instances = instance_transforms[mesh_index]
            instance_flags = 0
            f.write(struct.pack('Ii', instance_flags, mesh_index))

            instance_header_stride_fpos = f.tell()
            f.write(struct.pack('q', 0))
            instance_data_offset_fpos = f.tell()
            f.write(struct.pack('q', 0))

            f.write(struct.pack('q', len(instances)))

            write_string_to_file(f, 'Instance N/A') # todo

            fpos = f.tell()
            f.seek(instance_data_offset_fpos)
            f.write(struct.pack('q', fpos))
            f.seek(fpos)

            if format_version >= 4:
                # Write the indices of the transforms (which may be animated)
                f.write(struct.pack(len(instances) * 'I', * instances))
            else:
                # Write the transformation matrices themselves
                for tx_index in instances:
                    tx = static_table[tx_index].T
                    f.write(struct.pack(4 * 'fff', *tx.flat))

            fpos = f.tell()
            f.seek(instance_header_stride_fpos)
            f.write(struct.pack('q', fpos))
            f.seek(fpos)

    if format_version >= 4:
        fpos = f.tell()
        f.seek(lod_group_offset_fpos)
        f.write(struct.pack('q', fpos))
        f.seek(fpos)
        for lod_group in lod_groups:
            f.write(struct.pack('q', len(lod_group.mesh_ids)))
            if lod_group.mesh_ids:
                np.array(lod_group.mesh_ids, dtype=np.int64).tofile(f)
                np.array(lod_group.detail_reduction, dtype=np.float32).tofile(f)

    if format_version >= 3:
        fpos = f.tell()
        f.seek(data_offset_fpos)
        f.write(struct.pack('q', fpos))
        f.seek(fpos)

    material_table = [''] * len(scene_data.materials)
    for mat, idx in scene_data.materials.items():
        material_table[idx] = mat.name
    for matname in material_table:
        write_string_to_file(f, matname)

    blend_attribute_codec = BlendAttributeCodec()

    mesh_index = 0
    for mesh, instances in scene_data.enumerate_meshes():
        if mesh.vertices.size == 0:
            print("Empty mesh ", mesh.name)
            continue

        # Apply the mesh to pre-skinning space transform (if necessary)
        mesh_to_pre_skinning = np.eye(4)
        if instances is not None:
            mesh_to_pre_skinning = np.asarray(instances.mesh_to_pre_skinning_space)
        if not np.allclose(mesh_to_pre_skinning, np.eye(4)):
            mesh = mesh.transformed(mesh_to_pre_skinning)

        if format_version >= 2:
            fpos = f.tell()
            f.seek(mesh.data_offset_fpos)
            f.write(struct.pack('q', fpos))
            f.seek(fpos)

        qpos = quantize_vertices(mesh.vertices, mesh.quantization_base, mesh.quantization_extent)
        #print(qpos.dtype)
        qpos.tofile(f)
        #print(qpos.shape, qpos.size * 8 / 1024 / 1024)
        qpos = None

        qnrm = quantize_normals(mesh.normals)
        #print(qnrm.dtype)
        #qnrm.tofile(f)
        #print(qnrm.shape, qnrm.size * 4 / 1024 / 1024)
        #qnrm = None

        quvs = quantize_uvs(mesh.uvs)
        #print(quvs.dtype)
        #quvs.tofile(f)
        np.dstack((qnrm, quvs)).tofile(f)
        #print(quvs.shape, quvs.size * 4 / 1024 / 1024)
        quvs = None

        if mesh.blend_weight_count > 0:
            offset = blend_index_offsets[mesh_index]
            mesh_tuple_indices = tuple_indices[offset:offset + mesh.blend_weights.shape[0]]
            blend_attributes = blend_attribute_codec.compress(mesh.blend_weights, mesh_tuple_indices)
            blend_attributes = np.asarray(blend_attributes, dtype=np.uint32)
            blend_attributes.tofile(f)

        mat_ids = mesh.material_indices - mesh.material_indices.min()
        mat_ids = mat_ids.astype(np.uint8, copy=False)
        #print(mat_ids.dtype)
        mat_ids.tofile(f)
        #print(mat_ids.shape, mat_ids.size / 1024 / 1024)
        mat_ids = None

        if store_index_buffer:
            mesh.recompute_shared_vertices(recompute=False)
            index_buffer = mesh.shared_vertices
            assert(index_buffer.shape[0] == mesh.vertices.shape[0])
            assert(np.min(index_buffer) >= 0)
            assert(np.max(index_buffer) < mesh.vertices.shape[0])
            index_buffer.tofile(f)
            index_buffer = None

        #print(f.tell())

        #print(instances, mesh)

        mesh_index += 1

    if format_version >= 4:
        fpos = f.tell()
        f.seek(tuple_table_offset_fpos)
        f.write(struct.pack('q', fpos))
        f.seek(fpos)
        # Write the tuple index table
        tuple_indices.tofile(f)

    if format_version >= 4:
        fpos = f.tell()
        f.seek(animation_offset_fpos)
        f.write(struct.pack('q', fpos))
        f.seek(fpos)
        # Flatten static and animated transforms into one long array
        transforms = np.concatenate([static_table, animated_table.reshape(-1, 3, 4)], axis=0)
        # Quantize the transforms
        translation_and_scaling, rotation = quantize_transforms(transforms)
        transforms = None
        # Pack each transform into 24 consecutive bytes
        packed = np.zeros((rotation.shape[0], 6), dtype=np.uint32)
        packed[:, 0:4] = translation_and_scaling.view(dtype=np.uint32)
        packed[:, 4:] = rotation[:, 1::2]
        packed[:, 4:] *= 2**16
        packed[:, 4:] += rotation[:, 0::2]
        packed.tofile(f)

    print("Export finished.")
    return {'FINISHED'}


# ExportHelper is a helper class, defines filename and
# invoke() function which calls the file selector.
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty, FloatProperty, EnumProperty
from bpy.types import Operator


class ExportVulkanRendererScene(Operator, ExportHelper):
    """Export Vulkan Renderer scene format"""
    bl_idname = "export.vulkan_renderer_scene"  # important since its how bpy.ops.import_test.some_data is constructed
    bl_label = "Export Vulkan Renderer scene"

    # ExportHelper mixin class uses this
    filename_ext = ".vks"

    filter_glob: StringProperty(
        default="*.vks",
        options={'HIDDEN'},
        maxlen=255,  # Max internal buffer length, longer would be clamped.
    )

    # List of operator properties, the attributes will be assigned
    # to the class instance from the operator settings before calling.
    selection_only: BoolProperty(
        name="Selection Only",
        description="Only export selected objects (otherwise, exports all visible objects)",
        default=False,
    )
    split_sharp_edges: BoolProperty(
        name="Split Sharp Edges",
        description="Adds a split edge modifier, if required (changes the scene permanently!)",
        default=True,
    )
    sharp_edge_angle: FloatProperty(
        name="Sharp Edge Angle",
        description="Maximum angle for edges to be shaded smoothly (degrees)",
        default=35.0,
    )
    flatten_static_meshes: BoolProperty(
        name="Flatten Static Meshes",
        description="Combines all static meshes",
        default=True,
    )
    split_max_diagonal: FloatProperty(
        name="Splitting Maximum Diagonal",
        description="Maximum diagonal length before instance is split",
        default=0.0,
    )
    keep_instances: BoolProperty(
        name="Keep Instances",
        description="Keep instanced objects as separate instanced meshes",
        default=True,
    )
    segment_by_material: BoolProperty(
        name="Segment by Material",
        description="Write segmented meshes that group geometry by material",
        default=True,
    )
    use_spatial_ordering: BoolProperty(
        name="Use spatial (morton) ordering for mesh geometry",
        description="Turn this on to initialize triangle order by morton order",
        default=True,
    )
    use_meshoptimizer: BoolProperty(
        name="Use meshoptimizer",
        description="Turn this on to use the meshoptimizer library instead of our own optimization",
        default=True,
    )
    store_index_buffer: BoolProperty(
        name="Store Index Buffer",
        description="Store an index buffer with the mesh. If this is off, expand into a flat vertex buffer.",
        default=True,
    )
    export_animation: BoolProperty(
        name="Export animation",
        description="Export animated object transforms and the effect of armature modifiers (temporarily changes the displayed frame)",
        default=False,
    )
    frame_step: FloatProperty(
        name="Frame step",
        description="Number of frames between two successive samples for the animation state of the model. In other words, the frame rate gets divided by this number.",
        default=0.5,
    )
    format_version: EnumProperty(
        items=[ 
            ("4", "Version 4", "Introduces support for animation and levels of detail"),
            ("3", "Version 3", "Introduces instances, segmented meshes and future-proofs the format with header size and data offsets"),
        ],
        name="Target format version",
        description="The format version to write. Choosing an earlier version may disable features. Note that version 2 is deprecated. and cannot be written"
    )

    def execute(self, context):
        format_version = int(self.format_version)
        assert(format_version >= 3)
        print(f"Exporting format version {format_version}.")

        object_filter = set()
        if self.selection_only:
            object_filter |= set(context.selected_objects)
            try:
                object_filter |= set(id for id in context.selected_ids if isinstance(id, bpy.types.Object))
            except Exception as e:
                print(f'Selected outliner IDs not available: {e}')
            if len(object_filter) == 0:
                print('No objects selected')
                return {'FINISHED'}
        object_filter = frozenset(object_filter)

        scene_data = SceneData(
            context, context.scene,
            object_filter,
            self.export_animation, self.frame_step,
            self.split_sharp_edges, np.radians(self.sharp_edge_angle))
        
        scene_data.triangulate(context)

        if self.export_animation:
            scene_data.record_animation(context.scene)

        if self.flatten_static_meshes:
            if self.split_max_diagonal > 0.0:
                print('Warning: Flattening currently unsupported with max diagonal splitting')
            else:
                scene_data.flatten(self.keep_instances)

        if self.split_max_diagonal > 0.0:
            scene_data.enforce_max_diagonal(self.split_max_diagonal)

        if self.use_spatial_ordering:
            scene_data.initialize_spatial_ordering()

        if self.segment_by_material:
            scene_data.segment_by_material()
        else:
            scene_data.optimize_materials()
            scene_data.enforce_material_range()
            scene_data.default_segments()

        scene_data.optimize(use_meshoptimizer=self.use_meshoptimizer)

        return write_scene_data(context, self.filepath, scene_data,
            format_version, store_index_buffer=self.store_index_buffer)


# Only needed if you want to add into a dynamic menu
def menu_func_export(self, context):
    self.layout.operator(ExportVulkanRendererScene.bl_idname, text="Vulkan Renderer Export")

def register():
    bpy.utils.register_class(ExportVulkanRendererScene)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)

def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(ExportVulkanRendererScene)

