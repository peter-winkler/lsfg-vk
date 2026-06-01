/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <string>
#include <vector>

namespace lsfgvk::cli::doctor {

    /// run the doctor subcommand: diagnose the lsfg-vk setup
    /// @param args arguments following "doctor" (optionally a game exe/process name)
    /// @return process exit code (0 if no problems found)
    int run(const std::vector<std::string>& args);

}
