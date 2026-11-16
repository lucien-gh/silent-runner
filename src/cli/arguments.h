#pragma once
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>

namespace runner {
class ArgumentError : public std::runtime_error {
public:
    explicit ArgumentError(std::wstring message) : std::runtime_error("Invalid arguments"), message_(std::move(message)) {}
    const std::wstring& message() const { return message_; }
private:
    std::wstring message_;
};
enum class Action { launch, help, version };
struct Arguments {
    Action action = Action::launch;
    std::wstring target;
    std::optional<std::wstring> cwd;
    std::vector<std::wstring> parameters;
};
Arguments parse_arguments(const std::vector<std::wstring>& args);
}
