#include "cli/arguments.h"

namespace runner {
Arguments parse_arguments(const std::vector<std::wstring>& args) {
    Arguments result;
    if (args.size() == 1 && args[0] == L"--help") { result.action = Action::help; return result; }
    if (args.size() == 1 && args[0] == L"--version") { result.action = Action::version; return result; }
    if (args.empty()) throw ArgumentError(L"Missing target. Use --help for usage.");
    if (args[0].empty() || args[0].front() == L'-')
        throw ArgumentError(L"Expected target first: " + args[0]);
    result.target = args[0];
    for (size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == L"--") {
            result.parameters.assign(args.begin() + i + 1, args.end());
            return result;
        }
        if (arg == L"--cwd") {
            if (result.cwd || i + 1 == args.size() || args[i + 1].empty() || args[i + 1] == L"--")
                throw ArgumentError(L"--cwd requires one nonempty directory and may appear only once.");
            result.cwd = args[++i];
        } else {
            throw ArgumentError(L"Unknown option or target arguments must follow --: " + arg);
        }
    }
    return result;
}
}
