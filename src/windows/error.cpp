#include "windows/error.h"
#include <vector>

namespace runner {
WindowsError::WindowsError(const std::wstring& operation, DWORD code) : std::runtime_error("Windows operation failed") {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    message_ = operation + L" (Windows error " + std::to_wstring(code) + L")";
    if (count && buffer) { message_ += L": "; message_.append(buffer, count); }
    if (buffer) LocalFree(buffer);
    while (!message_.empty() && (message_.back() == L'\r' || message_.back() == L'\n')) message_.pop_back();
}

static bool usable(HANDLE handle) {
    return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}
void write_message(DWORD stream, const std::wstring& message) {
    // Preserve inherited pipe/file handles across AttachConsole, which can replace standard handles.
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE handle = stream == STD_OUTPUT_HANDLE ? output : error;
    if (!usable(handle)) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        if (usable(output)) SetStdHandle(STD_OUTPUT_HANDLE, output);
        if (usable(error)) SetStdHandle(STD_ERROR_HANDLE, error);
        handle = GetStdHandle(stream);
    }
    if (!usable(handle)) return;
    DWORD mode = 0, written = 0;
    if (GetConsoleMode(handle, &mode)) {
        WriteConsoleW(handle, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    } else {
        const int size = WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) return;
        std::string bytes(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), bytes.data(), size, nullptr, nullptr);
        size_t offset = 0;
        while (offset < bytes.size()) {
            if (!WriteFile(handle, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &written, nullptr) || !written) break;
            offset += written;
        }
    }
}
}
