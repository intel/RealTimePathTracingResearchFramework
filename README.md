PROJECT NOT UNDER ACTIVE MANAGEMENT

This project will no longer be maintained by Intel.

Intel has ceased development and contributions including, but not limited to, maintenance, bug fixes, new releases, or updates, to this project.  

Intel no longer accepts patches to this project.

If you have an ongoing need to use this project, are interested in independently developing it, or would like to maintain patches for the open source software community, please create your own fork of this project.  

Contact: webadmin@linux.intel.com
# Real-time Path Tracing Research Framework

Note: This is a public pre-release limited to the core components
required for reproducing some of our research papers.

<!---
-->

The Real-time Path Tracing Research Framework is a fork of
[Will Usher's ChameleonRT](https://github.com/Twinklebear/ChameleonRT). While the
original project maintains many backends (Embree, DXR, Optix, Vulkan, Metal, OSPRay),
our version focuses on an efficient implementation in Vulkan. The other backends
are not maintained. For the currently released feature set, refer to [the changelog](CHANGELOG.md).

## Getting the code

The first option is to clone this repository using git. If you choose this option,
please perform a recursive clone (`git clone --recursive`) to obtain submodules.

The second option is to head over to our [releases](releases) page and download one of the source archives.

<!---
-->

## Building

Before building, you will need to install [cmake](https://cmake.org) v3.21 or higher
and a current [Vulkan SDK](https://vulkan.lunarg.com/).

For a convenient build, you may use our CMake presets. In the repository root,
the following commands will configure the CMake environment and launch a build
that contains the Vulkan backend:

<!---
-->
## Building the base framework

```sh
  $ cmake --preset vulkan
  $ cmake --build --preset vulkan
```

Please note that the first step downloads a set of external dependencies using cmake.
You can prevent this by placing the required dependencies in appropriate locations
under [ext](ext). Please inspect [ext/CMakeLists.txt](ext/CMakeLists.txt)
and [ext/libvkr/ext/CMakeLists.txt](ext/libvkr/ext/CMakeLists.txt) for more detail.

You should now find the main executable at `build/vulkan/rptr`. We also
provide additional presets, such as debug mode builds.
validation layers.


<!---
-->
## Controls

The camera is an free-flight camera that moves around the camera's focal point.

- Use W, A, S, D, Space and Q keys to move
- Speed up/down using the mousewheel. (Two-finger scroll motion on MacBook Pro etc. trackpad.)
- Click and drag to rotate.

Keys while the application window is in focus:

- Print the current camera position, center point, up vector and field of view (FOV) to the terminal by pressing the `p` key.
- Save image by pressing the `s` key.

## Command-line Options

For a comprehensive overview of command line options, please run

```shell
$ rptr --help

usage: rptr <scene_file> [<scene_file>...] [options]
Options:
	--img <x> <y>                Specify the window dimensions. Defaults to 1920x1080.
	--eye <x> <y> <z>            Set the camera position
	--center <x> <y> <z>         Set the camera focus point
	--up <x> <y> <z>             Set the camera up vector
	--fov <fovy>                 Specify the camera field of view (in degrees)
	--camera <n>                 If the scene contains multiple cameras, specify which
	                             should be used. Defaults to the first camera
	                             and overrides any config files.
	--config <file>              Load the given .ini file as an additional config file.
	--frame [<length>:]<file>    Append the given .ini file as an additional keyframe, hold 
	                             for <length> s (default 1 s) if given config is static.
	--vulkan-device <device>     Override device selection with the given device.
	--disable-ui                 Do not draw the user interface on startup.
	                             Press '.' to enable the user interface again.
	-h, --help                   Show this information and exit.

Backends:
	--backend <backend>          Use the given backend. The last one specified wins.
	                             vulkan: Render with Vulkan Ray Tracing

Validation mode:
	--validation <prefix>        Enable validation mode. Render only time 0
	                             for a fixed number of samples per pixel. Store the
	                             framebuffer in prefix.pfm, then exit.
	                             Cannot be used with profiling mode.
	--validation-spp <n>         Render this many samples per pixel before exiting.
	                             If this is set to a value less than 1, the render will
	                             continue indefinitely and store a new image prefix_<spp>.pfm
	                             after every sample per pixel.
	                             Defaults to -1. Ignored unless in validation mode.

Profiling mode:

By default, profiling mode runs for one logical second (on the animation timeline).
The number of frames renderered per logical second in profiling mode are specified
by means of --profiling-fps, otherwise it defaults to 60. Therefore, by default,
the total number of frames rendered equals the one given by --profiling-fps.
If keyframes are given on the command line, by default they are run for one logical
second each.

	--profiling <prefix>         Enable profiling mode. Render all keyframes with a
	                             fixed, non-realtime framerate. Store stats in prefix.csv,
	                             then exit.
	                             Cannot be used with validation mode.
	--profiling-fps <fps>        Profile with the given frames per second.
	                             Defaults to 60. Ignored unless in profiling mode.
	--profiling-img <prefix>     Also store the framebuffer after each keyframe in
	                             prefix_<keyframe>.pfm. Ignored unless in profiling mode.

Example for running 3 frames of a given config in profiling mode:
	./rptr path/to/scene.vks --profiling example_prefix --profiling-fps 3 --config path/to/example_config.ini

Example for running 7 frames for each of 3 given configs:
	./rptr path/to/scene.vks --profiling example_prefix --profiling-fps 7 --frame example_config1.ini --frame example_config2.ini --frame example_config3.ini
```

## Contributors

Tobias Zirr \
Johannes Meng \
Christoph Peters \
Anis Benyoub \
Will Usher

The project is originally based on ChameleonRT created by Will Usher.

Additional contributions and support by Alexander Rath,
Gábor Liktor, Jiawei Shao, Philippe Weier, Eric Heitz,
Sebastian Herholz, Lorenzo Tessari, and Anton Sochenov.

```bibtex
@Misc{irptr23,
   author = {Tobias Zirr and Johannes Meng and Christoph Peters and Anis Benyoub and Will Usher},
   title =  {The {Intel Real-time Path Tracing Research Framework}},
   year =   {2023},
   month =  {10},
   url =    {https://github.com/intel/RealTimePathTracingResearchFramework},
   note =   {\url{https://github.com/intel/RealTimePathTracingResearchFramework}}
}
```

