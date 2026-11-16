#pragma once
#include "cli/arguments.h"
#include <filesystem>

namespace runner {
struct LaunchSpec {
    std::wstring application;
    std::wstring command_line;
    std::optional<std::wstring> cwd;
};
std::wstring quote_argument(const std::wstring& value);
LaunchSpec prepare_launch(const Arguments& args);
void launch(const LaunchSpec& spec);
}
