/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "run.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

#include <getopt.h>
#include <unistd.h>

using namespace lsfgvk::cli;

namespace {
    void usage() {
        std::cerr <<
R"(Launch a program with frame generation enabled, using an env-based profile.

USAGE:
    lsfg-vk run [OPTIONS] -- <command> [args...]

OPTIONS:
    -m, --multiplier <FLOAT>     Frame generation multiplier (fractional ok)
    -t, --target-fps <FLOAT>     Adaptive target output fps (0 = fixed multiplier)
    -f, --flow-scale <FLOAT>     Flow scale (0.25-1.0)
    -p, --performance-mode       Use the lighter generation model
        --pacing <none|cpu>      Pacing mode
    -g, --gpu <STRING>           GPU to use
    -d, --dll <PATH>             Path to Lossless.dll

EXAMPLES:
    lsfg-vk run -m 1.5 -- vkcube
    lsfg-vk run -t 144 -- %command%        (in Steam launch options)
)";
    }
}

int run::run(int argc, char** argv) {
    setenv("LSFGVK_ENV", "1", 1); // NOLINT (activate via an environment profile)

    const std::array<option, 9> GETOPT {{
        { "multiplier",       required_argument, nullptr, 'm' },
        { "target-fps",       required_argument, nullptr, 't' },
        { "flow-scale",       required_argument, nullptr, 'f' },
        { "performance-mode", no_argument,       nullptr, 'p' },
        { "pacing",           required_argument, nullptr, 'P' },
        { "gpu",              required_argument, nullptr, 'g' },
        { "dll",              required_argument, nullptr, 'd' },
        { "help",             no_argument,       nullptr, 'h' },
        { nullptr,            no_argument,       nullptr,  0  }
    }};

    int c{0};
    // a leading '+' stops option parsing at the first non-option, so flags meant
    // for the launched command are left untouched
    while ((c = getopt_long(argc, argv, "+m:t:f:pP:g:d:h", GETOPT.data(), nullptr)) != -1) {
        switch (c) {
            case 'm': setenv("LSFGVK_MULTIPLIER", optarg, 1); break;       // NOLINT
            case 't': setenv("LSFGVK_TARGET_FPS", optarg, 1); break;       // NOLINT
            case 'f': setenv("LSFGVK_FLOW_SCALE", optarg, 1); break;       // NOLINT
            case 'p': setenv("LSFGVK_PERFORMANCE_MODE", "1", 1); break;    // NOLINT
            case 'P': setenv("LSFGVK_PACING", optarg, 1); break;           // NOLINT
            case 'g': setenv("LSFGVK_GPU", optarg, 1); break;              // NOLINT
            case 'd': setenv("LSFGVK_DLL_PATH", optarg, 1); break;         // NOLINT
            case 'h': usage(); return 0;
            case '?':
            default:  usage(); return 1;
        }
    }

    if (optind >= argc) {
        std::cerr << "No command given.\n\n";
        usage();
        return 1;
    }

    execvp(argv[optind], argv + optind);

    // execvp only returns if it failed
    std::cerr << "lsfg-vk run: failed to execute '" << argv[optind] << "'\n";
    return 127;
}
