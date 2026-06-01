/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "config.hpp"
#include "lsfg-vk-common/configuration/config.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace lsfgvk::cli;

namespace {

    std::string pacingToString(ls::Pacing pacing) {
        switch (pacing) {
            case ls::Pacing::None: return "none";
            case ls::Pacing::CPU:  return "cpu";
        }
        return "none";
    }
    std::optional<ls::Pacing> pacingFromString(const std::string& str) {
        if (str == "none") return ls::Pacing::None;
        if (str == "cpu")  return ls::Pacing::CPU;
        return std::nullopt;
    }
    bool boolFromString(const std::string& str) {
        return str == "1" || str == "true" || str == "yes" || str == "on";
    }
    std::string join(const std::vector<std::string>& items, const std::string& sep) {
        std::string out;
        for (size_t i = 0; i < items.size(); i++) {
            if (i != 0) out += sep;
            out += items[i];
        }
        return out;
    }
    std::string modeSummary(const ls::GameConf& conf) {
        std::ostringstream os;
        os << std::fixed << std::setprecision(2);
        if (conf.target_fps > 0.0F)
            os << "adaptive " << static_cast<int>(conf.target_fps) << "fps";
        else
            os << conf.multiplier << "x";
        return os.str();
    }
    ls::GameConf* findProfile(ls::ConfigFile& config, const std::string& name) {
        auto& profiles = config.profiles();
        auto it = std::ranges::find_if(profiles,
            [&](const ls::GameConf& conf) { return conf.name == name; });
        return it == profiles.end() ? nullptr : &*it;
    }

    int cmd_list(ls::ConfigFile& config) {
        const auto& global = config.global();
        std::cout << "Global\n"
                  << "  dll        " << global.dll.value_or("(unset)") << '\n'
                  << "  allow_fp16 " << (global.allow_fp16 ? "true" : "false") << "\n\n"
                  << "Profiles\n";
        if (config.profiles().empty())
            std::cout << "  (none)\n";
        for (const auto& conf : config.profiles())
            std::cout << "  " << std::left << std::setw(24) << conf.name
                      << std::setw(16) << modeSummary(conf)
                      << join(conf.active_in, ", ") << '\n';
        return 0;
    }
    int cmd_show(ls::ConfigFile& config, const std::string& name) {
        const auto* conf = findProfile(config, name);
        if (conf == nullptr) { std::cerr << "No such profile: " << name << '\n'; return 1; }
        std::cout << std::fixed << std::setprecision(2)
                  << "Profile: " << conf->name << '\n'
                  << "  multiplier        " << conf->multiplier << '\n'
                  << "  target_fps        "
                  << (conf->target_fps > 0.0F ? std::to_string(static_cast<int>(conf->target_fps)) : "0 (fixed)") << '\n'
                  << "  flow_scale        " << conf->flow_scale << '\n'
                  << "  performance_mode  " << (conf->performance_mode ? "true" : "false") << '\n'
                  << "  pacing            " << pacingToString(conf->pacing) << '\n'
                  << "  gpu               " << conf->gpu.value_or("(default)") << '\n'
                  << "  active_in         " << join(conf->active_in, ", ") << '\n';
        return 0;
    }
    int cmd_set(ls::ConfigFile& config, const std::filesystem::path& path,
            const std::string& name, const std::string& key, const std::string& value) {
        auto* conf = findProfile(config, name);
        if (conf == nullptr) { std::cerr << "No such profile: " << name << '\n'; return 1; }
        if (key == "multiplier") {
            const float mult = std::stof(value);
            if (mult <= 1.0F) { std::cerr << "multiplier must be greater than 1\n"; return 1; }
            conf->multiplier = mult;
        } else if (key == "target-fps" || key == "target_fps") {
            const float target = std::stof(value);
            if (target < 0.0F) { std::cerr << "target-fps must be >= 0\n"; return 1; }
            conf->target_fps = target;
        } else if (key == "flow-scale" || key == "flow_scale") {
            const float flow = std::stof(value);
            if (flow < 0.25F || flow > 1.0F) { std::cerr << "flow-scale must be between 0.25 and 1.0\n"; return 1; }
            conf->flow_scale = flow;
        } else if (key == "performance-mode" || key == "performance_mode") {
            conf->performance_mode = boolFromString(value);
        } else if (key == "pacing") {
            const auto pacing = pacingFromString(value);
            if (!pacing.has_value()) { std::cerr << "pacing must be 'none' or 'cpu'\n"; return 1; }
            conf->pacing = *pacing;
        } else if (key == "gpu") {
            conf->gpu = (value == "default" || value.empty()) ? std::nullopt : std::optional<std::string>(value);
        } else if (key == "name") {
            conf->name = value;
        } else {
            std::cerr << "Unknown key: " << key << "\n"
                      << "Keys: multiplier, target-fps, flow-scale, performance-mode, pacing, gpu, name\n";
            return 1;
        }
        config.write(path);
        std::cout << "Set " << name << '.' << key << " = " << value << '\n';
        return 0;
    }
    int cmd_create(ls::ConfigFile& config, const std::filesystem::path& path, const std::string& name) {
        if (findProfile(config, name) != nullptr) { std::cerr << "Profile already exists: " << name << '\n'; return 1; }
        ls::GameConf conf;
        conf.name = name;
        config.profiles().push_back(std::move(conf));
        config.write(path);
        std::cout << "Created profile: " << name << '\n';
        return 0;
    }
    int cmd_delete(ls::ConfigFile& config, const std::filesystem::path& path, const std::string& name) {
        auto& profiles = config.profiles();
        const auto count = std::erase_if(profiles, [&](const ls::GameConf& conf) { return conf.name == name; });
        if (count == 0) { std::cerr << "No such profile: " << name << '\n'; return 1; }
        config.write(path);
        std::cout << "Deleted profile: " << name << '\n';
        return 0;
    }
    int cmd_game(ls::ConfigFile& config, const std::filesystem::path& path,
            const std::string& sub, const std::string& name, const std::string& exe) {
        auto* conf = findProfile(config, name);
        if (conf == nullptr) { std::cerr << "No such profile: " << name << '\n'; return 1; }
        if (sub == "add") {
            if (std::ranges::find(conf->active_in, exe) == conf->active_in.end())
                conf->active_in.push_back(exe);
        } else if (sub == "rm" || sub == "remove") {
            std::erase(conf->active_in, exe);
        } else {
            std::cerr << "Usage: config game add|rm <profile> <exe>\n";
            return 1;
        }
        config.write(path);
        std::cout << "active_in for " << name << ": " << join(conf->active_in, ", ") << '\n';
        return 0;
    }
    int cmd_global(ls::ConfigFile& config, const std::filesystem::path& path,
            const std::string& key, const std::string& value) {
        auto& global = config.global();
        if (key == "dll") {
            global.dll = value.empty() ? std::nullopt : std::optional<std::string>(value);
        } else if (key == "allow-fp16" || key == "allow_fp16") {
            global.allow_fp16 = boolFromString(value);
        } else {
            std::cerr << "Unknown global key: " << key << " (dll, allow-fp16)\n";
            return 1;
        }
        config.write(path);
        std::cout << "Set global." << key << " = " << value << '\n';
        return 0;
    }
    void usage() {
        std::cerr <<
R"(Manage the lsfg-vk configuration.

USAGE:
    lsfg-vk config <ACTION> [ARGS]

ACTIONS:
    list                            List profiles and global settings
    show <profile>                  Show a profile's settings
    set <profile> <key> <value>     Set a profile field
    create <name>                   Create a new profile
    delete <profile>                Delete a profile
    game add|rm <profile> <exe>     Add/remove an active_in entry
    global <key> <value>            Set a global setting (dll, allow-fp16)

PROFILE KEYS:
    multiplier <float>        target-fps <float>     flow-scale <0.25-1.0>
    performance-mode <bool>   pacing none|cpu        gpu <name>|default     name <string>
)";
    }
}

int config::run(const std::vector<std::string>& args) {
    if (args.empty()) { usage(); return 1; }
    const std::string& action = args[0];

    const auto path = ls::findConfigurationFile();
    try {
        if (!std::filesystem::exists(path))
            ls::ConfigFile::createDefaultConfigFile(path);
        ls::ConfigFile config{path};

        if (action == "list")                          return cmd_list(config);
        if (action == "show"   && args.size() >= 2)    return cmd_show(config, args[1]);
        if (action == "set"    && args.size() >= 4)    return cmd_set(config, path, args[1], args[2], args[3]);
        if (action == "create" && args.size() >= 2)    return cmd_create(config, path, args[1]);
        if (action == "delete" && args.size() >= 2)    return cmd_delete(config, path, args[1]);
        if (action == "game"   && args.size() >= 4)    return cmd_game(config, path, args[1], args[2], args[3]);
        if (action == "global" && args.size() >= 3)    return cmd_global(config, path, args[1], args[2]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    usage();
    return 1;
}
