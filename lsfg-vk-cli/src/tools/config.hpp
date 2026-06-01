/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <string>
#include <vector>

namespace lsfgvk::cli::config {

    /// run the config subcommand
    /// @param args arguments following "config"
    /// @return process exit code
    int run(const std::vector<std::string>& args);

}
