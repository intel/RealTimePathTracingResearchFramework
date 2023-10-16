# libvkr scripts

## Preparation

To run the python scripts, you need to set `PYTHONPATH` to point to
the built pyvkr module. If you use the release preset to build, this can be
done using

```shell
export PYTHONPATH=$(pwd)/build/release:${PYTHONPATH}
```

The module will only build if cmake can find Python!

## vkrinfo.py

Print metadata loaded from a .vks file.

## vkr2obj.py

Convert .vks to .obj.

## vktconvert.py

Convert textures to .vkt.

## vktinfo.py

Print metadata loaded from a .vkt file.

## Blender addon

If Python, NumPy and Blender can be found, then cmake will also install the
`blender_vkr` addon. This addon facilitates exporting to the .vks scene format.

Note that you need to build with NumPy that is old enough
for your Blender version. You can check the NumPy version used in Blender with

```sh
  $ blender --background --python-expr "import numpy; print(numpy.__version__)"
```
