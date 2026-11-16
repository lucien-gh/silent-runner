#pragma once
#include <windows.h>
#include <stdexcept>
#include <string>

namespace runner {
class WindowsError : public std::runtime_error {
public:
    WindowsError(const std::wstring& operation, DWORD code);
    const std::wstring& message() const { return message_; }
private:
    std::wstring message_;
};
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (valid()) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};
void write_message(DWORD stream, const std::wstring& message);
}
