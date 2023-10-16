# libvkr

This project includes a library and tools designed to deal with the
.vks/.vkt scene format. It is used in the Real-time Path Tracing Research Framework.

You can build standalone by simply calling

```shell
   cmake --preset release
   cmake --build --preset release
```

Note that there is also a debug preset.

Alternatively, you can use this library directly with `add_subdirectory`, which
will give access to the library target `vkr`.

This library also contains a Python wrapper, pyvkr.

# Scripts

The scripts folder contains utilities that use the pyvkr module.
See [scripts/README.md](scripts/README.md) for more detail.


