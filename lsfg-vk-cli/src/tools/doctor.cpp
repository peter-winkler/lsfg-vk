/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "doctor.hpp"
#include "lsfg-vk-common/configuration/config.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace lsfgvk::cli;

namespace {

    enum class Status { Ok, Warn, Fail };
    int g_problems = 0;

    void report(Status status, const std::string& label, const std::string& detail,
            const std::string& hint = "") {
        const char* tag = status == Status::Ok ? "  ok  "
                        : status == Status::Warn ? " warn "
                        : " FAIL ";
        if (status != Status::Ok) g_problems++;
        std::cout << "  [" << tag << "] " << label << "  " << detail << '\n';
        if (status != Status::Ok && !hint.empty())
            std::cout << "            -> " << hint << '\n';
    }

    std::vector<fs::path> implicitLayerDirs() {
        std::vector<fs::path> dirs;
        const auto add = [&](const char* env, const char* fallback) {
            if (const char* val = std::getenv(env); val != nullptr && *val != '\0')
                dirs.emplace_back(val);
            else if (fallback != nullptr)
                dirs.emplace_back(fallback);
        };
        add("VK_IMPLICIT_LAYER_PATH", nullptr);
        add("VK_ADD_IMPLICIT_LAYER_PATH", nullptr);
        if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0')
            dirs.emplace_back(fs::path(xdg) / "vulkan/implicit_layer.d");
        else if (const char* home = std::getenv("HOME"); home != nullptr)
            dirs.emplace_back(fs::path(home) / ".local/share/vulkan/implicit_layer.d");
        dirs.emplace_back("/usr/local/share/vulkan/implicit_layer.d");
        dirs.emplace_back("/usr/share/vulkan/implicit_layer.d");
        dirs.emplace_back("/etc/vulkan/implicit_layer.d");
        return dirs;
    }

    std::optional<fs::path> findLayerManifest() {
        for (const auto& dir : implicitLayerDirs()) {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) continue;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                const auto name = entry.path().filename().string();
                if ((name.find("LSFGVK") != std::string::npos || name.find("lsfg") != std::string::npos)
                        && name.ends_with(".json"))
                    return entry.path();
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> manifestLibrary(const fs::path& manifest) {
        std::ifstream ifs(manifest);
        const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        auto key = content.find("\"library_path\"");
        if (key == std::string::npos) return std::nullopt;
        auto colon = content.find(':', key);
        auto open = content.find('"', colon == std::string::npos ? key : colon);
        if (open == std::string::npos) return std::nullopt;
        auto close = content.find('"', open + 1);
        if (close == std::string::npos) return std::nullopt;
        return content.substr(open + 1, close - open - 1);
    }

    std::optional<fs::path> resolveLibrary(const std::string& lib, const fs::path& manifestDir) {
        const fs::path libPath(lib);
        std::error_code ec;
        if (libPath.is_absolute())
            return fs::exists(libPath, ec) ? std::optional(libPath) : std::nullopt;
        if (lib.find('/') != std::string::npos) {
            const auto rel = manifestDir / libPath;
            if (fs::exists(rel, ec)) return rel;
        }
        for (const char* dir : {"/usr/lib", "/usr/lib64", "/usr/lib/x86_64-linux-gnu", "/lib"}) {
            const auto cand = fs::path(dir) / libPath.filename();
            if (fs::exists(cand, ec)) return cand;
        }
        return std::nullopt;
    }

    bool matchesActiveIn(const ls::GameConf& conf, const std::string& game) {
        return std::ranges::any_of(conf.active_in, [&](const std::string& entry) {
            return game == entry || (entry.size() < game.size() && game.ends_with(entry));
        });
    }
}

int doctor::run(const std::vector<std::string>& args) {
    g_problems = 0;
    const std::optional<std::string> game = args.empty() ? std::nullopt : std::optional(args[0]);

    std::cout << "lsfg-vk doctor\n\n";

    // configuration
    const auto confPath = ls::findConfigurationFile();
    std::optional<ls::ConfigFile> config;
    if (!fs::exists(confPath)) {
        report(Status::Warn, "config        ", confPath.string() + " (missing)",
            "run 'lsfg-vk config list' to create a default one");
    } else {
        try {
            config.emplace(confPath);
            report(Status::Ok, "config        ", confPath.string());
        } catch (const std::exception& e) {
            report(Status::Fail, "config        ", std::string("invalid: ") + e.what(),
                "fix or remove the file; 'lsfg-vk validate' shows details");
        }
    }

    // Lossless.dll
    if (config.has_value()) {
        const auto& dll = config->global().dll;
        if (!dll.has_value())
            report(Status::Fail, "Lossless.dll  ", "not configured",
                "lsfg-vk config global dll /path/to/Lossless.dll");
        else if (!fs::exists(*dll))
            report(Status::Fail, "Lossless.dll  ", *dll + " (not found)",
                "point it at your Lossless Scaling install's Lossless.dll");
        else
            report(Status::Ok, "Lossless.dll  ", *dll);
    }

    // layer manifest + library
    const auto manifest = findLayerManifest();
    if (!manifest.has_value()) {
        report(Status::Fail, "layer manifest", "not found in any implicit_layer.d",
            "install lsfg-vk, or set VK_IMPLICIT_LAYER_PATH to the build's manifest");
    } else {
        report(Status::Ok, "layer manifest", manifest->string());
        const auto lib = manifestLibrary(*manifest);
        if (!lib.has_value()) {
            report(Status::Warn, "layer library ", "library_path not found in manifest");
        } else if (const auto resolved = resolveLibrary(*lib, manifest->parent_path()); !resolved.has_value()) {
            report(Status::Fail, "layer library ", *lib + " (cannot resolve)",
                "the manifest points at a missing .so");
        } else {
            report(Status::Ok, "layer library ", resolved->string());
        }
    }

    // vulkan loader
    bool haveLoader = false;
    for (const char* dir : {"/usr/lib", "/usr/lib64", "/usr/lib/x86_64-linux-gnu", "/lib"}) {
        std::error_code ec;
        if (fs::exists(fs::path(dir) / "libvulkan.so.1", ec)) { haveLoader = true; break; }
    }
    report(haveLoader ? Status::Ok : Status::Warn, "vulkan loader ",
        haveLoader ? "libvulkan.so.1 present" : "libvulkan.so.1 not found",
        "install the Vulkan loader (vulkan-icd-loader)");

    // profiles + game match
    if (config.has_value()) {
        const auto& profiles = config->profiles();
        report(Status::Ok, "profiles      ", std::to_string(profiles.size()) + " configured");

        if (game.has_value()) {
            std::cout << '\n';
            std::optional<std::string> matched;
            for (const auto& conf : profiles)
                if (matchesActiveIn(conf, *game)) { matched = conf.name; break; }
            if (matched.has_value())
                report(Status::Ok, "profile match ", "'" + *matched + "' is active in '" + *game + "'");
            else
                report(Status::Warn, "profile match ", "no profile's active_in matches '" + *game + "'",
                    "lsfg-vk config game add <profile> " + *game);
        }
    }

    std::cout << '\n';
    if (g_problems == 0)
        std::cout << "No problems found.\n";
    else
        std::cout << g_problems << " problem(s) found above.\n";

    std::cout <<
R"(
Notes:
  - Frame generation only engages when a profile's active_in matches the game
    (or LSFG_PROCESS / LSFGVK_ENV is set); an empty active_in matches nothing.
  - On Proton, the game must run on a modern Steam Linux Runtime (SLR4). The
    older 'sniper' runtime does not device-chain the layer, so generation is
    silently skipped. Map such games to an SLR4 Proton (e.g. proton-cachyos-slr).
)";

    return g_problems == 0 ? 0 : 1;
}
