# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

import bpy
import math

# adds and returns a new group input (source) slot for the given name.
def make_group_input(group, inNode, typeID, name, default_value=None):
    input = group.inputs.new(typeID, name)
    if default_value is not None:
        input.default_value = default_value
    data_in = inNode.outputs[name]
    return data_in

# adds a new group input and a new group output slot of the same name,
# connecting the two to pass any data unchanged. returns the new input
# (source) slot.
def make_group_passthrough(group, inNode, outNode, typeID, name, default_value=None, outTypeID=None):
    if outTypeID is None:
        outTypeID = typeID
    data_in = make_group_input(group, inNode, typeID, name, default_value=default_value)
    group.outputs.new(outTypeID, name)
    group.links.new(outNode.inputs[name], data_in)
    return data_in

# makes a hidden processing node of the given type, connecting any given
# input (source) slots to inputs of the new node in their given order.
# returns the selected output (source) slot. optionally selects an operation.
def make_hidden_node(group, typeID, inputs, operation=None, output_idx=0):
    layer = group.nodes.new(typeID)
    if operation is not None:
        layer.operation = operation
    for i, c in enumerate(inputs):
        if isinstance(c, bpy.types.NodeSocket):
            group.links.new(layer.inputs[i], c)
        else:
            layer.inputs[i].default_value = c
    return layer.outputs[output_idx]

# forms a multi-channel color layer by concatenating the given inputs.
# optionally adds an output (sink) slot of the given name, receiving
# the data of the formed layer. returns the layer output (source) slot.
def make_group_layer(group, inputs, outNode=None, name=None):
    if len(inputs) > 1:
        inputs = make_hidden_node(group, 'ShaderNodeCombineRGB', inputs)
    else:
        inputs = inputs[0]
    if name is not None:
        group.outputs.new('NodeSocketColor', name)
        group.links.new(outNode.inputs[name], inputs)
    return inputs

# makes a group input for normals, checks for normal validity and remaps the data to the [0, 1] color range.
def remap_normal_input(group, inNode, typeID, name, default_normal):
    normal = make_group_input(group, inNode, typeID, name)
    # note: this is a bit horrifyingly complex, but for now there does not seem to be a better way
    length = make_hidden_node(group, 'ShaderNodeVectorMath', (normal, ), operation='LENGTH', output_idx='Value') # note: has multiple hidden output nodes :/
    invalid = make_hidden_node(group, 'ShaderNodeMath', (0, length), operation='COMPARE')
    normal = make_hidden_node(group, 'ShaderNodeMixRGB', (invalid, normal, default_normal)) # no other mixing node that works :/
    # now we got a valid world-space normal
    normal = make_hidden_node(group, 'ShaderNodeVectorMath', (normal, [0.5, 0.5, 0.5]), operation='MULTIPLY')
    normal = make_hidden_node(group, 'ShaderNodeVectorMath', (normal, [0.5, 0.5, 0.5]), operation='ADD')
    return normal

# makes two group inputs for roughness and principled roughness, respectively.
# principled material roughness is squared before being passed on.
def remap_roughness_input(group, inNode, typeID, name):
    roughness = make_group_input(group, inNode, typeID, name)
    proughness = make_group_input(group, inNode, typeID, 'Principled %s' % name)
    return proughness

# adds a group input for index of refraction ad remaps it to the [0, 1] range.
def remap_ior_input(group, inNode, typeID, name):
    ior = make_group_input(group, inNode, typeID, name)
    iorLower = make_hidden_node(group, 'ShaderNodeMath', (ior, ior), operation='MULTIPLY') # min(x^2, 1)
    iorLower.node.use_clamp = True
    iorUpper = make_hidden_node(group, 'ShaderNodeMath', (ior, -2), operation='POWER') # min(x^-2, 1)
    iorUpper.node.use_clamp = True
    ior = make_hidden_node(group, 'ShaderNodeMath', (iorLower, iorUpper), operation='SUBTRACT')
    ior = make_hidden_node(group, 'ShaderNodeMath', (0.5, ior, 0.5), operation='MULTIPLY_ADD')
    return ior

def remap_emission_intensity_input(group, inNode, typeID, name):
    import numpy as np
    intensity = make_group_input(group, inNode, typeID, name)
    # atan(value) / (pi/2) maps [0, inf) to [0, 1)
    intensity = make_hidden_node(group, 'ShaderNodeMath', (intensity,), operation='ARCTANGENT')
    intensity = make_hidden_node(group, 'ShaderNodeMath', (2.0/np.pi, intensity), operation='MULTIPLY')
    return intensity

class MaterialExportNode(bpy.types.ShaderNodeCustomGroup):
    bl_name = 'MaterialExportNode'
    shader_group_name = "MaterialExportNode.group"
    bl_label ='PBR Material Export'

    @staticmethod
    def get_group():
        group = bpy.data.node_groups.get(MaterialExportNode.shader_group_name, None)
        if group is not None:
            return group
        """
        if hasattr(group, 'is_hidden'):
            group.is_hidden=True
        """
        print("Initializing PBR Export Group")
        group = bpy.data.node_groups.new(name=MaterialExportNode.shader_group_name, type='ShaderNodeTree')
        inputs = group.nodes.new('NodeGroupInput')
        outputs = group.nodes.new('NodeGroupOutput')
        inputs.name = 'AOV Inputs'
        outputs.name = 'AOV Outputs'
        geometry_node = group.nodes.new("ShaderNodeNewGeometry")
        geometry_node.name = 'Default Geometry'
        make_group_passthrough(group, inputs, outputs, 'NodeSocketShader', 'Color')
        base_color = make_group_passthrough(group, inputs, outputs, 'NodeSocketColor', 'Base Color', default_value=[1.0, 1.0, 1.0, 1.0], outTypeID='NodeSocketShader') # shader out type to support transparency
        alpha0_color = make_hidden_node(group, 'ShaderNodeBsdfTransparent', ([1.0, 1.0, 1.0, 0.0],))
        alpha_color = make_hidden_node(group, 'ShaderNodeMixShader', (make_group_input(group, inputs, 'NodeSocketFloat', 'Alpha', default_value=1.0), alpha0_color, base_color))
        group.links.new(outputs.inputs['Base Color'], alpha_color)
        make_group_layer(group, (
            remap_normal_input(group, inputs, 'NodeSocketVector', 'Normal', geometry_node.outputs['Normal']),
        ), outputs, 'Normal Layer')
        make_group_layer(group, (
            make_group_input(group, inputs, 'NodeSocketFloat', 'Sheen'),
            remap_roughness_input(group, inputs, 'NodeSocketFloat', 'Roughness'),
            make_group_input(group, inputs, 'NodeSocketFloat', 'Metallic'),
        ), outputs, 'Reflection Layer')
        make_group_layer(group, (
            make_group_input(group, inputs, 'NodeSocketFloat', 'Translucency'),
            remap_ior_input(group, inputs, 'NodeSocketFloat', 'IOR'),
            make_group_input(group, inputs, 'NodeSocketFloat', 'Transmission'),
        ), outputs, 'Transmission Layer')
        # shader out type to support transparency
        emission_color = make_group_passthrough(group, inputs, outputs, 'NodeSocketColor',
            'Emission', default_value=[0.0, 0.0, 0.0, 1.0], outTypeID='NodeSocketShader')
        emission_intensity = remap_emission_intensity_input(group, inputs, 'NodeSocketFloat', 'Emission Intensity')
        alpha_emission = make_hidden_node(group, 'ShaderNodeMixShader',
            (emission_intensity, alpha0_color, emission_color))
        group.links.new(outputs.inputs['Emission'], alpha_emission)
        return group
    @staticmethod
    def group_inputs(group):
        return group.nodes["AOV Inputs"].outputs
    @staticmethod
    def group_outputs(group):
        return group.nodes["AOV Outputs"].inputs
    @staticmethod
    def default_geometry(group):
        return group.nodes["Default Geometry"].outputs

    @staticmethod
    def reroute(node_tree, in_socket, to_socket):
        if not isinstance(in_socket, bpy.types.NodeSocket):
            to_socket.default_value = in_socket
            return
        if in_socket.is_output:
            node_tree.links.new(to_socket, in_socket)
            return
        if in_socket.is_linked:
            for l in in_socket.links:
                node_tree.links.new(to_socket, l.from_socket)
        else:
            # todo: might require constant conversion
            to_socket.default_value = in_socket.default_value
    @staticmethod
    def reroute_reverse(node_tree, out_socket, from_socket):
        if out_socket.is_linked:
            for l in out_socket.links:
                if l.to_node != from_socket.node:
                    node_tree.links.new(l.to_socket, from_socket)
        else:
            # todo: might require constant conversion
            out_socket.default_value = from_socket.default_value

    @staticmethod
    def reroute_color(group):
        group.links.new(MaterialExportNode.group_outputs(group)["Color"], MaterialExportNode.group_inputs(group)["Color"])
    @staticmethod
    def group_output_basecolor(group):
        outputs = MaterialExportNode.group_outputs(group)
        MaterialExportNode.reroute(group, outputs["Base Color"], outputs["Color"])
    @staticmethod
    def group_output_normal(group):
        outputs = MaterialExportNode.group_outputs(group)
        MaterialExportNode.reroute(group, outputs["Normal Layer"], outputs["Color"])
    @staticmethod
    def group_output_reflection(group):
        outputs = MaterialExportNode.group_outputs(group)
        MaterialExportNode.reroute(group, outputs["Reflection Layer"], outputs["Color"])
    @staticmethod
    def group_output_transmission(group):
        outputs = MaterialExportNode.group_outputs(group)
        MaterialExportNode.reroute(group, outputs["Transmission Layer"], outputs["Color"])
    @staticmethod
    def group_output_emission(group):
        outputs = MaterialExportNode.group_outputs(group)
        MaterialExportNode.reroute(group, outputs["Emission"], outputs["Color"])

    aov_routes = [
        ('None', reroute_color.__func__, 'Propagate BSDF results'),
        ('Color', group_output_basecolor.__func__, 'BSDF base color channels'),
        ('Normal', group_output_normal.__func__, 'Color-mapped normal layer'),
        ('Reflection', group_output_reflection.__func__, 'Sheen/roughness/metal channels'),
        ('Transmission', group_output_transmission.__func__, 'Transmittance/ior/scatter-through channels'),
        ('Emission', group_output_emission.__func__, 'Emission RGB/intensity channels'),
    ]

    # note: this could change long-term, if blender architecture changes?
    def get_node_tree(self):
        return self.id_data

    def set_target_bsdf(self, bsdf, bsdf_output=0):
        node_tree = self.get_node_tree()
        node_tree.links.new(bsdf.outputs[bsdf_output], self.inputs['Color'])
    def get_target_bsdf(self):
        for l in self.inputs['Color'].links:
            return l.from_socket.node
        return None

    alternative_sockets = {
        'Base Color': ['Base Color', 'Color']
    }
    prinicpled_alternative_sockets = {
        'Principled Roughness': ['Roughness'],
        'Roughness': [],
        'Transmission': ['Transmission', lambda export, bsdf: 1.0 if bsdf.bl_idname == 'ShaderNodeBsdfGlass' else None ],
        'Emission Intensity': ['Emission Strength'],
        **alternative_sockets
    }
    principled_bsdfs = [ 'ShaderNodeBsdfGlass', 'ShaderNodeBsdfPrincipled', 'ShaderNodeBsdfGlossy', 'ShaderNodeBsdfAnisotropic', 'ShaderNodeBsdfRefraction']

    def attach_to_bsdf(self, bsdf):
        node_tree = self.get_node_tree()
        self.reroute_reverse(node_tree, bsdf.outputs[0], self.outputs[0])
        for n, s in self.inputs.items():
            if s.is_linked:
                continue # todo: allow clearing old links?
            
            alternative_names = self.typed_alternative_sockets.get(bsdf.bl_idname, self.alternative_sockets).get(n, None)
            if alternative_names is not None:
                for an in alternative_names:
                    if isinstance(an, str):
                        bsdf_s = bsdf.inputs.get(an, None)
                    else:
                        bsdf_s = an(self, bsdf)
                    if bsdf_s is not None:
                        break
            else:
                bsdf_s = bsdf.inputs.get(n, None)
            if bsdf_s is None:
                continue
            
            self.reroute(node_tree, bsdf_s, s)

    def init(self, context):
        self.node_tree = self.get_group()

    def change_aov(self, aov_index):
        MaterialExportNode.aov_routes[aov_index][1](self.node_tree)

    aov_type_enum = [ (n.upper(), n, d) for n, _, d in aov_routes ]
    aov_type: bpy.props.EnumProperty(items=aov_type_enum, set=change_aov, options={'SKIP_SAVE'})

    def draw_buttons(self, context, layout):
        layout.prop(self, 'aov_type', text='Output')

MaterialExportNode.typed_alternative_sockets = {
    bsdf: MaterialExportNode.prinicpled_alternative_sockets
    for bsdf in MaterialExportNode.principled_bsdfs
}

def generate_object_instances(selected_objects=None):
    """
    Iterable that generates all object instances.

    This works with geometry network instances, as well.

    If selected_objects is non-empty, then only instances of objects in that list
    are generated.

    Only visible objects are generated.
    """
    deg = bpy.context.evaluated_depsgraph_get()
    view_layer = deg.view_layer_eval

    for i in deg.object_instances:
        eval_obj = i.object

        # Check if this object or one of its original parents is in the selected group
        if selected_objects:
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

        yield eval_obj

def get_active_materials(selected_objects):#, skip_collections=set()):
    active_materials = []
    for o in generate_object_instances(selected_objects):
        for m in getattr(o.data, 'materials', []):
            active_materials.append(m)
    return set(active_materials)

def attach_export_nodes(mat):
    mat.use_nodes = True
    bsdf_nodes = []
    export_nodes = []
    for n in mat.node_tree.nodes:
        if isinstance(n, MaterialExportNode):
            export_nodes.append(n)
        elif 'ShaderNodeBsdf' in n.bl_idname or 'ShaderNodeEmission' in n.bl_idname:
            bsdf_nodes.append(n)
    prev_tracked_bsdfs = set([n.get_target_bsdf() for n in export_nodes])
    for bsdf in bsdf_nodes:
        if bsdf not in prev_tracked_bsdfs:
            n = mat.node_tree.nodes.new('MaterialExportNode')
            n.set_target_bsdf(bsdf)
            export_nodes.append(n)
    for n in export_nodes:
        target = n.get_target_bsdf()
        if target is not None:
            n.attach_to_bsdf(target)
    return export_nodes

def find_resolution(root=None, mat=None, parents=()):
    if root is None:
        root = mat.node_tree.get_output_node('EEVEE')

    res_x, res_y = (0, 0)
    if isinstance(root, bpy.types.ShaderNodeTexImage):
        i = root.image
        if i is not None:
            if i.source != 'GENERATED':
                res_x, res_y = (i.size[0], i.size[1])
            else:
                res_x, res_y = (i.generated_width, i.generated_height)
            # i.colorspace_settings
    
    to_node = root
    to_socket = None

    def recurse_links(links, parents):
        nonlocal res_x, res_y
        for l in links:
            from_node = l.from_node
            
            # note: isinstance does not seem to work here:
            # assert(not isinstance(from_node, MaterialExportNode) or isinstance(from_node, bpy.types.ShaderNodeCustomGroup))
            child = getattr(from_node, 'node_tree', None)
            if child is not None:
                child_nodes = child.nodes
                child = None
                for n in child_nodes:
                    if isinstance(n, bpy.types.NodeGroupOutput):
                        child = n
                        break
                if child is not None:
                    recurse_links(child.inputs[l.from_socket.name].links, (from_node, *parents))
                    continue # don't follow the parent group node

            if isinstance(from_node, bpy.types.NodeGroupInput):
                parent = parents[0]
                recurse_links(parent.inputs[l.from_socket.name].links, parents[1:])
                continue # don't follow the group output node

            lres_x, lres_y = find_resolution(from_node, parents=parents)
            res_x, res_y = (max(res_x, lres_x), max(res_y, lres_y))

    for i in root.inputs:
        to_socket = i
        recurse_links(i.links, parents)
        
    return (res_x, res_y)

def setup_baking_image(scene, res_x, res_y):
    render = scene.render
    render.resolution_x = res_x
    render.resolution_y = res_y
    render.pixel_aspect_x = max(1, res_y / res_x)
    render.pixel_aspect_y = max(1, res_x / res_y)

baking_file_format = 'png'

def setup_baking_scene(context, uvmaps):
    scene = bpy.data.scenes.new(name='PBRExportScene')
    context = context.copy()
    context['scene'] = scene
    # create orthographic camera
    cam_data = bpy.data.cameras.new(name='PBRMaterialRecorder')
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = 2.0
    cam_obj = bpy.data.objects.new(name='PBRMaterialRecorder', object_data=cam_data)
    cam_obj.delta_location = [0, 0, 1]
    scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj
    # create quad
    plane_data = bpy.data.meshes.new(name='PBRMaterialScreen')
    plane_data.from_pydata([ [-1, -1, 0], [1, -1, 0], [-1, 1, 0], [1, 1, 0] ], [], [ (0, 1, 3, 2) ])
    plane_obj = bpy.data.objects.new(name='PBRMaterialScreen', object_data=plane_data)
    scene.collection.objects.link(plane_obj)
    context['active_object'] = plane_obj
    # add trivial uv map for each name referenced in the node graph.
    if not uvmaps:
        uvmaps = ["uv"]
    uvs = [(0,0), (0, 1), (1, 1), (1,0)]
    for map_name in uvmaps:
        uvmap = plane_obj.data.uv_layers.new(name="st")
        for loop in plane_obj.data.loops:
            uv = plane_data.vertices[loop.vertex_index].co[:2]
            uv = [ v * 0.5 + 0.5 for v in uv ]
            uvmap.data[loop.index].uv = uv[:]

    # set up 1 spp eevee
    scene.view_settings.view_transform = 'Standard'
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.bake_samples = 1
    scene.render.use_high_quality_normals = True
    scene.render.use_motion_blur = False
    scene.render.use_overwrite = True
    scene.render.filter_size = 0 # todo: does this work?
    scene.render.film_transparent = True
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.file_format = baking_file_format.upper()
    scene.render.image_settings.color_depth = '16'
    scene.render.image_settings.view_settings.view_transform = 'Standard'
    scene.eevee.sss_samples = 1
    scene.eevee.taa_render_samples = 1
    return context, scene

import os
import os.path

import re
def make_filename(s):
    return re.sub(r'[^a-zA-Z0-9_. -]', '_-_', s)

class BakedTexturePath:
    def __init__(self, output_dir, material_name, texture_name, extension):
        self.output_dir = output_dir
        self.material_name = material_name
        self.texture_name = texture_name
        self.extension = extension

    def get_path(self, output_dir=None, material_name=None, texture_name=None, extension=None):
        output_dir = self.output_dir if output_dir is None else output_dir
        material_name = self.material_name if material_name is None else material_name
        texture_name = self.texture_name if texture_name is None else texture_name
        extension = self.extension if extension is None else extension
        filename = make_filename(f"{material_name}_{texture_name}") + f".{extension}"
        return os.path.join(output_dir, filename)

    def __str__(self, output_dir=None, material_name=None, texture_name=None, extension=None):
        return self.get_path()

def bake_material_texture(output_dir, output_type, context, materials, default_resolution=4, output_color_transform='None',
                          blend_method='OPAQUE'):
    scene = context['scene']
    active_object = context['active_object']
    scene.display_settings.display_device = output_color_transform
    scene.render.image_settings.display_settings.display_device = output_color_transform
    baked_textures = []
    for mm in materials:
        material_name = mm.name
        m = mm.copy()
        m.blend_method = blend_method
        active_object.data.materials.clear()
        active_object.data.materials.append(m)
        print('Baking texture %s_%s:' % (material_name, output_type))
        res_x, res_y = find_resolution(mat=m)
        if res_x == 0:
            print('Warning: Inferred resolution X == 0, defaulting to %d' % default_resolution)
            res_x = default_resolution
        if res_y == 0:
            print('Warning: Inferred resolution Y == 0, defaulting to %d' % default_resolution)
            res_y = default_resolution
        # TODO: Blender seems to break at 16384^2 - check back in the future to see if this still happens.
        res_x = min(2**math.ceil(math.log(res_x, 2)), 8192)
        res_y = min(2**math.ceil(math.log(res_y, 2)), 8192)
        print('Rendering at resolution %d x %d' % (res_x, res_y))
        setup_baking_image(scene, res_x, res_y)
        output_path = BakedTexturePath(
            output_dir, material_name, output_type, baking_file_format)
        scene.render.filepath = str(output_path)
        bpy.ops.render.render(write_still=True)
        baked_textures.append(output_path)
        active_object.data.materials.clear()
        bpy.data.materials.remove(m)

    return baked_textures

class MaterialTextureResults:
    pass

def find_uv_map_names(material):
    map_names = []
    for node in material.node_tree.nodes:
        if (isinstance(node, bpy.types.ShaderNodeUVMap)):
            map_names.append(node.uv_map)
    return map_names

def bake_material_textures(output_dir, context, materials):
    os.makedirs(output_dir, exist_ok=True)

    export_nodes = []
    uvmaps = []
    for m in materials:
        uvmaps.extend(find_uv_map_names(m))
        export_nodes += attach_export_nodes(m)

    uvmaps = list(set(uvmaps)) # Make unique.

    context, export_scene = setup_baking_scene(context, uvmaps)

    # need to switch context for render to work :/
    prev_context_scene = bpy.context.window.scene
    bpy.context.window.scene = context['scene']
    active_object = context['active_object']
    context = bpy.context.copy()
    context['active_object'] = active_object
    
    results = MaterialTextureResults()

    MaterialExportNode.group_output_basecolor(MaterialExportNode.get_group())
    results.base_color = bake_material_texture(output_dir, 'BaseColor', context, materials, output_color_transform='sRGB')
    
    MaterialExportNode.group_output_normal(MaterialExportNode.get_group())
    results.normal = bake_material_texture(output_dir, 'Normal', context, materials)

    MaterialExportNode.group_output_reflection(MaterialExportNode.get_group())
    results.specular = bake_material_texture(output_dir, 'Specular', context, materials)

    if True:
        MaterialExportNode.group_output_transmission(MaterialExportNode.get_group())
        results.transmission = bake_material_texture(output_dir, 'SpecularTransmission', context, materials)

    MaterialExportNode.group_output_emission(MaterialExportNode.get_group())
    results.emission = bake_material_texture(output_dir, 'Emission', context, materials, blend_method='BLEND')

    # todo: maybe remove export nodes again
    # todo: maybe remove scene again

    MaterialExportNode.reroute_color(MaterialExportNode.get_group())
    bpy.context.window.scene = prev_context_scene

    return results

def convert_material_textures(output_dir, baked_results):
    def get_output_file(path, extension="vkt", texture_name=None):
        return path.get_path(output_dir=output_dir, extension=extension, texture_name=texture_name)
    for t in baked_results.base_color:
        output_file = get_output_file(t)
        print('Converting to', output_file)
        pyvkr.convert_texture(str(t), output_file, 138, 132)
    for t in baked_results.normal:
        output_file = get_output_file(t)
        print('Converting to', output_file)
        pyvkr.convert_texture(str(t), output_file, 141)
    for t in baked_results.specular:
        output_file = get_output_file(t)
        print('Converting to', output_file)
        pyvkr.convert_texture(str(t), output_file, 131)

    import numpy as np

    for t in baked_results.transmission:
        output_file = get_output_file(t, extension='txt')
        print('Converting to', output_file)
        try:
            i = bpy.data.images.load(str(t), check_existing=True)
            i.colorspace_settings.name = 'Raw'
            values = np.array(i.pixels).reshape(-1, i.channels)
            values = np.mean(values, axis=0)
            bpy.data.images.remove(i)
            i = None

            # todo: why is this offset by 1/255?
            values = np.clip((values * 256 - 1)/255, 0.0, 1.0)
            if values[0] > 0 or values[2] > 0:
                print(values)

                ior = values[1] * 2.0 - 1.0
                if ior > 1.0:
                    ior = 1.0 / np.sqrt(1.0 - ior)
                else:
                    ior = np.sqrt(ior + 1.0)

                with open(output_file, 'w') as f:
                    f.write('%f\n%f\n%f\n%f' % (values[2], ior, 0.0, values[0]))

        except Exception as e:
            print(e)

    for t in baked_results.emission:
        output_file = get_output_file(t, extension='txt', texture_name="EmissionIntensity")
        print('Converting to', output_file)
        try:
            i = bpy.data.images.load(str(t), check_existing=True)
            i.colorspace_settings.name = 'Raw'
            values = np.array(i.pixels).reshape(-1, i.channels)
            values[:,3] = np.tan(values[:,3] * np.pi / 2.0)
            values = np.mean(values, axis=0)
            bpy.data.images.remove(i)
            i = None

            if np.max(values[0:3]) > 0 and values[3] > 0:
                print(f" ... {values}")
                with open(output_file, 'w') as f:
                    f.write('%f\n%f\n%f\n%f' % (values[3], values[0], values[1], values[2]))
            else:
                print(" ... no emission")

        except Exception as e:
            print(e)

# ExportHelper is a helper class, defines filename and
# invoke() function which calls the file selector.
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty, FloatProperty, EnumProperty
from bpy.types import Operator

class ExportPBRTextures(Operator, ExportHelper):
    """Export PBR textures from node graphs"""
    bl_idname = "export.pbr_renderer_textures"  # important since its how bpy.ops.import_test.some_data is constructed
    bl_label = "Export PBR Renderer textures"

    # ExportHelper mixin class uses this
    filename_ext = "" # todo: is this ok?
    # todo: intelligent warn overwrite?

    filter_glob: StringProperty(
        default="*.vks;*/",
        options={'HIDDEN'},
        maxlen=255,  # Max internal buffer length, longer would be clamped.
    )

    selection_only: BoolProperty(
        name="Selection Only",
        description="Only export selected objects (otherwise, exports all visible objects)",
        default=False,
    )
    # Attempt to directly convert to vulkan renderer texture format
    compress_to_vkt: BoolProperty(
        name="Compress to *.vkt",
        description="Converts the resulting PBR textures to the compressed Vulkan renderer texture format",
        default=True,
    )

    def execute(self, context):
        target_path = self.filepath
        if not os.path.isdir(target_path):
            target_path, _ = os.path.splitext(target_path)
            target_path += "_textures"
            os.makedirs(target_path, exist_ok=True)
        print('Baking textures to {target_path}')

        selected_objects = context.selected_objects if self.selection_only else None
        print('Collecting materials ...')
        active_materials = get_active_materials(selected_objects)
        print('Collected', len(active_materials), 'materials')

        baking_target_path = target_path
        if self.compress_to_vkt:
            baking_target_path = os.path.join(target_path, 'src')

        print('Attaching export nodes')
        results = bake_material_textures(baking_target_path, context, active_materials) # bpy.context?

        if self.compress_to_vkt:
            convert_material_textures(target_path, results)

        return {'FINISHED'}

# Only needed if you want to add into a dynamic menu
def menu_func_export(self, context):
    self.layout.operator(ExportPBRTextures.bl_idname, text="PBR Texture Export")

def register():
    bpy.utils.register_class(ExportPBRTextures)
    bpy.utils.register_class(MaterialExportNode)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)

def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(ExportPBRTextures)
    bpy.utils.unregister_class(MaterialExportNode)

