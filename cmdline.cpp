// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "cmdline.h"
#include "util/error_io.h"
#include "util/util.h"

namespace command_line {

static char const* const s_usage =
    "usage: %s <scene_file> [<scene_file>...] [options]\n"
    "Options:\n"
    "\t--img <x> <y>                Specify the window dimensions. Defaults to 1920x1080.\n"
    "\t--upscale <n>                Specify the render upscale factor. Defaults to 1.\n"
    "\t--eye <x> <y> <z>            Set the camera position\n"
    "\t--center <x> <y> <z>         Set the camera focus point\n"
    "\t--up <x> <y> <z>             Set the camera up vector\n"
    "\t--fov <fovy>                 Specify the camera field of view (in degrees)\n"
    "\t--camera <n>                 If the scene contains multiple cameras, specify which\n"
    "\t                             should be used. Defaults to the first camera\n"
    "\t                             and overrides any config files.\n"
    "\t--config <file>              Load the given .ini file as an additional config file.\n"
    "\t--keyframe [<length>:]<file> Append the given .ini file as an additional keyframe, hold \n"
    "\t                             for <length> s (default 1 s) if given config is static.\n"
    "\t--vulkan-device <device>     Override device selection with the given device.\n"
    "\t--disable-ui                 Do not draw the user interface on startup.\n"
    "\t                             Press '.' to enable the user interface again.\n"
    "\t--freeze-frame               Keep repeating the same fixed frame, until the next keyframe if\n"
    "\t                             multiple (then freezes the first frame for every keyframe).\n"
    "\t--exr                        Use EXR as the output image format. This is the default.\n"
    "\t--pfm                        Use PFM as the output image format instead of the default EXR.\n"
    "\t--png                        Use PNG as the output image format instead of the default EXR.\n"
    "\t-h, --help                   Show this information and exit.\n"
    "\n"
    "Backends:\n"
    "\t--backend <backend>          Use the given backend. The last one specified wins.\n"
#if ENABLE_VULKAN
    "\t                             vulkan: Render with Vulkan Ray Tracing\n"
#endif
    "\n"
    "Validation mode:\n"
    "\t--validation <prefix>        Enable validation mode. Render only time 0\n"
    "\t                             for a fixed number of samples per pixel. Store the\n"
    "\t                             framebuffer in prefix.pfm, then exit.\n"
    "\t                             Cannot be used with profiling mode or data capture mode.\n"
    "\t--validation-spp <n>         Render this many samples per pixel before exiting.\n"
    "\t                             If this is set to a value less than 1, the render will\n"
    "\t                             continue indefinitely and store a new image prefix_<spp>.pfm\n"
    "\t                             after every sample per pixel.\n"
    "\t                             Defaults to -1. Ignored unless in validation mode.\n"
    "\n"
    "Profiling mode:\n"
    "\n"
    "By default, profiling mode runs for one logical second (on the animation timeline).\n"
    "The number of frames renderered per logical second in profiling mode are specified\n"
    "by means of --profiling-fps, otherwise it defaults to 60. Therefore, by default,\n"
    "the total number of frames rendered equals the one given by --profiling-fps.\n"
    "If keyframes are given on the command line, by default they are run for one logical\n"
    "second each.\n"
    "\n"
    "\t--profiling <prefix>         Enable profiling mode. Render all keyframes with a\n"
    "\t                             fixed, non-realtime framerate. Store stats in prefix.csv,\n"
    "\t                             then exit.\n"
    "\t                             Cannot be used with validation mode or data capture mode.\n"
    "\t--profiling-fps <fps>        Profile with the given frames per second.\n"
    "\t                             Defaults to 60. Ignored unless in profiling mode.\n"
    "\t--profiling-img <prefix>     Also store the framebuffer after each keyframe in\n"
    "\t                             prefix_<keyframe>.pfm. Ignored unless in profiling mode.\n"
    "\n"
    "Example for running 3 frames of a given config in profiling mode:\n"
    "\t./rptr path/to/scene.vks --profiling example_prefix --profiling-fps 3 --config path/to/example_config.ini\n"
    "\n"
    "Example for running 7 frames for each of 3 given configs:\n"
    "\t./rptr path/to/scene.vks --profiling example_prefix --profiling-fps 7 --keyframe example_config1.ini --keyframe example_config2.ini --keyframe example_config3.ini\n"
    "\n"
    "Data capture mode:\n"
    "\n"
    "By default, data capture mode runs for one logical second (on the animation timeline).\n"
    "The number of frames renderered per logical second in profiling mode are specified\n"
    "by means of --data-capture-fps, otherwise it defaults to 60. Therefore, by default,\n"
    "the total number of frames rendered equals the one given by --data-capture-fps.\n"
    "If keyframes are given on the command line, by default they are run for one logical\n"
    "second each.\n"
    "\n"
    "\t--data-capture <prefix>           Enable data capture mode. Render all keyframes with a\n"
    "\t                                  fixed, non-realtime framerate. Store AOVs in\n"
    "\t                                  <prefix>_<aov>.exr, then exit.\n"
    "\t                                  Cannot be used with validation mode or profiling mode.\n"
    "\t--data-capture-fps <fps>          Profile with the given frames per second.\n"
    "\t                                  Defaults to 60. Ignored unless in profiling mode.\n"
    "\t--data-capture-spp <n>            Render this many samples per pixel before advancing to\n"
    "\t                                  the next frame.\n"
    "\t                                  Defaults to 1. Ignored unless in data capture mode.\n"
    "\t--data-capture-no-rgba            Do not store the rgba image.\n"
    "\t--data-capture-no-aovs            Do not store any of the aovs.\n"
    "\t--data-capture-albedo-roughness   Store the albedo (RGB) and roughness (A) aovs.\n"
    "\t--data-capture-normal-depth       Store the normal (RGB) and depth (A) aovs.\n"
    "\t--data-capture-motion             Store the motion vector (RGB) aovs\n"
    "\n"
    "\t By default, the rgba buffer and all aovs are stored.\n"
    "\t The order of the arguments on the command line matters. This way, you can render\n"
    "\t individual aovs using, for example,\n"
    "\t    --data-capture-no-aovs --data-capture-albedo-roughness\n"
    "\n";

struct ApiDescriptor {
  const char *backend;
  const char *frontend;
};

static constexpr ApiDescriptor available_apis[] = {
#if ENABLE_VULKAN
  {"vulkan", "vk"},
#endif
  {"unused", "unused"} // We don't count this one, it simply swallows a comma.
};
static constexpr size_t num_available_apis
  = (sizeof(available_apis) / sizeof(available_apis[0])) - 1;
static_assert(num_available_apis > 0,
    "At least one backend must be enabled.");

inline const ApiDescriptor *find_api(const std::string &arg)
{
  const auto *apis = available_apis;
  for (size_t i = 0; i < num_available_apis; ++i) {
    if (arg == apis[i].backend)
      return apis + i;
  }
  return nullptr;
}

template <class T>
inline T convert(const std::string &s);

template <>
inline std::string convert<std::string>(const std::string &s)
{
  return s;
}

template <>
inline int convert<int>(const std::string &s)
{
  return std::stoi(s);
}

template <>
inline float convert<float>(const std::string &s)
{
  return std::stof(s);
}

template <>
inline size_t convert<size_t>(const std::string &s)
{
  return std::stoll(s);
}

template <class T1>
inline void consume(const std::vector<std::string> &args,
                    size_t &i,
                    T1 &v1)
{
  if (i + 1 >= args.size()) {
    print(CLL::CRITICAL, "%s expects an argument\n", args[i].c_str());
    throw -1;
  }

  const size_t oldI = i;

  try {
    v1 = convert<T1>(args[++i]);
  } catch (...) {
    print(CLL::CRITICAL, "invalid argument for %s: %s\n",
        args[oldI].c_str(), args[i].c_str());
    throw -1;
  }
}

template <class T1, class T2>
inline void consume(const std::vector<std::string> &args,
                    size_t &i,
                    T1 &v1,
                    T2 &v2)
{
  if (i + 2 >= args.size()) {
    print(CLL::CRITICAL, "%s expects two arguments\n", args[i].c_str());
    throw -1;
  }

  const size_t oldI = i;

  try {
    v1 = convert<T1>(args[++i]);
    v2 = convert<T2>(args[++i]);
  } catch (...) {
    print(CLL::CRITICAL, "invalid argument for %s: %s\n",
        args[oldI].c_str(), args[i].c_str());
    throw -1;
  }
}

template <class T1, class T2, class T3>
inline void consume(const std::vector<std::string> &args,
                    size_t &i,
                    T1 &v1,
                    T2 &v2,
                    T3 &v3)
{
  if (i + 3 >= args.size()) {
    print(CLL::CRITICAL, "%s expects three arguments\n",
        args[i].c_str());
    throw -1;
  }

  const size_t oldI = i;

  try {
    v1 = convert<T1>(args[++i]);
    v2 = convert<T2>(args[++i]);
    v3 = convert<T3>(args[++i]);
  } catch (...) {
    print(CLL::CRITICAL, "invalid argument for %s: %s\n",
        args[oldI].c_str(), args[i].c_str());
    throw -1;
  }
}

inline void check_old_argument(const std::string &file_name)
{
  static const std::vector<std::string> old_backends = {
    "-vulkan", "-embree", "-dxr", "-optix", "-metal"
  };

  if (std::find(old_backends.begin(), old_backends.end(), file_name)
      != old_backends.end())
  {
    print(CLL::WARNING, "%s used to be a "
        "command line argument that selects a rendering backend. We have "
        "instead introduced the --backend <BACKEND> argument, which is also "
        "optional. "
        "Please run with --help for more information.\n",
      file_name.c_str());
  }

  static const std::vector<std::string> old_args = {
    "-img", "-config", "-validation", "-eye", "-center",
    "-up", "-fov", "-camera", "-spp", "-profiling-frames"
  };

  if (std::find(old_args.begin(), old_args.end(), file_name) != old_args.end())
  {
    print(CLL::WARNING, "%s used to be a command "
      "line argument. We have instead moved to double dashes (-%s) for long "
      "form arguments. "
      "Please run with --help for more information.\n",
      file_name.c_str(), file_name.c_str());
  }
}

inline bool looks_like_argument(const std::string &arg)
{
    if (arg.length() == 0 || arg[0] != '-') {
        return false;
    }
    check_old_argument(arg);
    return true;
}

ProgramArgs parse(Shell::DefaultArgs* optional_shell_args, std::vector<std::string> const &vargs)
{
  auto find_help = std::find_if(vargs.begin(), vargs.end(),
      [](const std::string &a) {
        return a == "-h" || a == "--help";
      }
  );

  if (vargs.size() < 2 || find_help != vargs.end()) {
    print(CLL::CRITICAL, s_usage, vargs[0].c_str());
    throw 1;
  }

  ProgramArgs args;

  std::unique_ptr<Shell::DefaultArgs> dummy_shell_args;
  if (!optional_shell_args) {
    dummy_shell_args.reset(new Shell::DefaultArgs);
    optional_shell_args = dummy_shell_args.get();
  }
  auto& shell = *optional_shell_args;

  bool have_profiling_options = false;
  bool have_backend = false;
  bool have_unknown_args = false;

  for (size_t i = 1 /*ignore program name*/; i < vargs.size(); ++i)
  {
    if (vargs[i] == "--img") {
      consume(vargs, i, args.window_width, args.window_height);
      if (args.window_width < 1)
          args.window_width = 1;
      if (args.window_height < 1)
          args.window_height = 1;
      args.have_window_size = true;
    }
    else if (vargs[i] == "--upscale") {
      consume(vargs, i, args.render_upscale_factor);
      if (args.render_upscale_factor < 1)
        args.render_upscale_factor = 1;
      else
        args.have_upscale_factor = true;
    }
    else if (vargs[i] == "--config") {
      std::string config;
      consume(vargs, i, config);
      canonicalize_path(config);
      args.configuration_inis.push_back(config);
    }
    else if (vargs[i] == "--keyframe" || vargs[i] == "--frame") { // support for old cmd line
      Keyframe frame;
      {
        std::string config;
        consume(vargs, i, config);
        auto sep = config.find(':');
        if (sep != config.npos && sep > 0 && !std::isalpha(config[sep-1])) {
          frame.hold = convert<float>(config.substr(0, sep));
          config = config.substr(sep+1);
        }
        canonicalize_path(config);
        frame.configuration_ini = std::move(config);
      }
      args.added_frames.push_back(frame);
    }
    else if (vargs[i] == "--eye") {
      consume(vargs, i, shell.eye.x, shell.eye.y, shell.eye.z);
      shell.got_camera_args = true;
    }
    else if (vargs[i] == "--center") {
      consume(vargs, i, shell.center.x, shell.center.y, shell.center.z);
      shell.got_camera_args = true;
    }
    else if (vargs[i] == "--up") {
      consume(vargs, i, shell.up.x, shell.up.y, shell.up.z);
      shell.got_camera_args = true;
    }
    else if (vargs[i] == "--fov") {
      consume(vargs, i, shell.fov_y);
      shell.got_camera_args = true;
    }
    else if (vargs[i] == "--camera") {
      consume(vargs, i, shell.camera_id);
    }
    else if (vargs[i] == "--vulkan-device") {
      consume(vargs, i, args.device_override);
    }
    else if (vargs[i] == "--disable-ui") {
        shell.disable_ui = true;
    }
    else if (vargs[i] == "--freeze-frame") {
        shell.freeze_frame = true;
    } else if (vargs[i] == "--deduplicate-scene") {
        shell.deduplicate_scene = true;
    } else if (vargs[i] == "--backend") {
      std::string backend;
      consume(vargs, i, backend);
      const auto *api = find_api(backend);
      if (api) {
        args.display_frontend = std::string(api->frontend);
        shell.renderer = std::string(api->backend);
        have_backend = true;
      }
      else {
        print(CLL::CRITICAL, "unsupported backend: %s\n", vargs[i].c_str());
        throw -1;
      }
    }
    else if (vargs[i] == "--validation")
    {
        consume(vargs, i, shell.validation_img_prefix);
        shell.validation_mode = true;
    }
    else if (vargs[i] == "--validation-spp")
    {
        consume(vargs, i, shell.validation_target_spp);
        if (shell.validation_target_spp < 1)
            shell.validation_target_spp = -1;
    }
    else if (vargs[i] == "--profiling")
    {
        have_profiling_options = true;
        consume(vargs, i, shell.profiling_csv_prefix);
        shell.profiling_mode = true;
    }
    else if (vargs[i] == "--profiling-fps" || vargs[i] == "--profiling-frames") // support for old cmd line
    {
        have_profiling_options = true;
        consume(vargs, i, shell.profiling_fps);
        if (shell.profiling_fps <= 1)
            shell.profiling_fps = 1;
    }
    else if (vargs[i] == "--profiling-img")
    {
        have_profiling_options = true;
        consume(vargs, i, shell.profiling_img_prefix);
    }
    else if (vargs[i] == "--benchmark-file")
    {
      println(CLL::CRITICAL, "--benchmark-file <name>.csv is now --profiling <name>");
      throw -1;
    }
    else if (vargs[i] == "--data-capture")
    {
        shell.data_capture_mode = true;
        consume(vargs, i, shell.data_capture.img_prefix);
    }
    else if (vargs[i] == "--data-capture-fps")
    {
        consume(vargs, i, shell.data_capture.fps);
        if (shell.data_capture.fps <= 1)
            shell.data_capture.fps = 1;
    }
    else if (vargs[i] == "--data-capture-spp")
    {
        consume(vargs, i, shell.data_capture.target_spp);
        if (shell.data_capture.target_spp <= 1)
            shell.data_capture.target_spp = 1;
    }
    else if (vargs[i] == "--data-capture-no-rgba")
    {
        shell.data_capture.rgba = false;
    }
    else if (vargs[i] == "--data-capture-no-aovs")
    {
        shell.data_capture.albedo_roughness = false;
        shell.data_capture.normal_depth = false;
        shell.data_capture.motion = false;
    }
    else if (vargs[i] == "--data-capture-albedo-roughness")
    {
        shell.data_capture.albedo_roughness = true;
    }
    else if (vargs[i] == "--data-capture-normal-depth")
    {
        shell.data_capture.normal_depth = true;
    }
    else if (vargs[i] == "--data-capture-motion")
    {
        shell.data_capture.motion = true;
    }
    else if (vargs[i] == "--exr")
    {
        shell.image_format = OUTPUT_IMAGE_FORMAT_EXR;
    }
    else if (vargs[i] == "--pfm")
    {
        shell.image_format = OUTPUT_IMAGE_FORMAT_PFM;
    }
    else if (vargs[i] == "--png")
    {
        shell.image_format = OUTPUT_IMAGE_FORMAT_PNG;
    } else if (vargs[i] == "--resource-dir") {
        consume(vargs, i, shell.resource_dir);
    }
    else {
      auto arg = vargs[i];
      if (looks_like_argument(arg)) {
          println(CLL::CRITICAL, "Unknown argument: %s", arg.c_str());
          have_unknown_args = true;
      } else {
          canonicalize_path(arg);
          shell.scene_files.push_back(arg);
      }
    }
  }

  if (have_unknown_args)
      throw -1;

  if (int(shell.validation_mode) 
   +  int(shell.profiling_mode)
   +  int(shell.data_capture_mode)  > 1)

  {
      println(CLL::CRITICAL, "validation mode, profiling mode and data capture mode are "
              "mutually exclusive");
      throw -1;
  }
  if (have_profiling_options && !shell.profiling_mode)
  {
      println(CLL::CRITICAL, "got profiling automation options without "
              "profiling mode, enable it using --profiling <stats>");
      throw -1;
  }

  // Make the backend switch optional.
  if (!have_backend) {
    // A static assert in cmdline.h makes sure we don't compile unless there
    // is at least one available API.
    const auto &api = available_apis[0];
    args.display_frontend = api.frontend;
    shell.renderer = api.backend;
  }

  if (shell.scene_files.empty()) {
    print(CLL::CRITICAL, s_usage, vargs[0].c_str());
    throw -1;
  }

  return args;
}

} // namespace command_line
