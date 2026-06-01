/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

namespace lsfgvk::cli::run {

    /// run the run subcommand: launch a program with frame generation enabled
    /// @param argc argument count (argv[0] is "run")
    /// @param argv arguments; options precede "--" and the command follows
    /// @return process exit code; only returns on failure to exec
    int run(int argc, char** argv);

}
