// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <libapp/shell.h>
#include <string>
#include <vector>

namespace command_line {

  struct Keyframe {
    double hold = 1.0;
    std::string configuration_ini;
  };

  struct ProgramArgs {
    std::string display_frontend;
    std::vector<std::string> configuration_inis;
    std::vector<Keyframe> added_frames;
    bool have_upscale_factor { false };
    bool have_window_size { false };
    int render_upscale_factor { 1 };
    int window_width { 1920 };
    int window_height { 1080 };
    std::string device_override;
  };

  ProgramArgs parse(Shell::DefaultArgs* shell_args, const std::vector<std::string> &vargs);

} // namespace command_line
