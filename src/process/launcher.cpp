#include "process/launcher.h"
#include "windows/error.h"
#include <algorithm>
#include <cwctype>
#include <vector>

namespace runner {
namespace fs = std::filesystem;

std::wstring quote_argument(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

static bool regular_file(const fs::path& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}
static std::wstring environment(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::vector<wchar_t> value(size);
    DWORD count = GetEnvironmentVariableW(name, value.data(), size);
    if (!count || count >= size) throw WindowsError(L"Read environment", ERROR_ENVVAR_NOT_FOUND);
    return {value.data(), count};
}
static fs::path resolve_target(const std::wstring& target, const std::optional<std::wstring>& cwd) {
    fs::path candidate(target);
    if (!candidate.has_extension()) candidate += L".exe";
    if (candidate.has_parent_path() || candidate.has_root_name()) {
        auto full = fs::absolute(candidate).lexically_normal();
        if (!regular_file(full)) throw WindowsError(L"Target not found: " + full.wstring(), ERROR_FILE_NOT_FOUND);
        return full;
    }
    if (cwd) {
        auto in_working_directory = (fs::path(*cwd) / candidate).lexically_normal();
        if (regular_file(in_working_directory)) return in_working_directory;
    }
    auto full = fs::absolute(candidate).lexically_normal();
    if (regular_file(full)) return full;
    const auto path = environment(L"PATH");
    for (size_t begin = 0; begin <= path.size();) {
        size_t end = path.find(L';', begin);
        if (end == std::wstring::npos) end = path.size();
        auto directory = path.substr(begin, end - begin);
        if (directory.size() >= 2 && directory.front() == L'"' && directory.back() == L'"') directory = directory.substr(1, directory.size() - 2);
        if (!directory.empty()) {
            full = fs::absolute(fs::path(directory) / candidate).lexically_normal();
            if (regular_file(full)) return full;
        }
        if (end == path.size()) break;
        begin = end + 1;
    }
    throw WindowsError(L"Target not found: " + target, ERROR_FILE_NOT_FOUND);
}
static fs::path system_directory() {
    std::vector<wchar_t> buffer(32768);
    const UINT size = GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (!size || size >= buffer.size()) throw WindowsError(L"GetSystemDirectoryW", GetLastError());
    return fs::path(std::wstring(buffer.data(), size));
}
static std::wstring batch_argument(const std::wstring& value) {
    // Deliberately conservative: CMD expansion is not the CRT argv quoting language.
    // Reject ambiguous syntax before invoking CMD rather than executing altered input.
    if (value.find_first_of(L"\"%!^&|<>()\r\n") != std::wstring::npos)
        throw ArgumentError(L"Batch paths/arguments cannot contain quotes, %, !, ^, &, |, <, >, parentheses or line breaks.");
    return L"\"" + value + L"\"";
}
LaunchSpec prepare_launch(const Arguments& args) {
    LaunchSpec spec;
    if (args.cwd) {
        fs::path cwd = fs::absolute(*args.cwd).lexically_normal();
        const DWORD attrs = GetFileAttributesW(cwd.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            throw WindowsError(L"Invalid working directory: " + cwd.wstring(), ERROR_DIRECTORY);
        spec.cwd = cwd.wstring();
    }
    const auto target = resolve_target(args.target, spec.cwd);
    if (!spec.cwd) spec.cwd = target.parent_path().wstring();
    auto extension = target.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    if (extension == L".exe") {
        spec.application = target.wstring();
        spec.command_line = quote_argument(spec.application);
        for (const auto& arg : args.parameters) spec.command_line += L" " + quote_argument(arg);
    } else if (extension == L".bat" || extension == L".cmd") {
        spec.application = (system_directory() / L"cmd.exe").wstring();
        std::wstring command = batch_argument(target.wstring());
        for (const auto& arg : args.parameters) command += L" " + batch_argument(arg);
        spec.command_line = quote_argument(spec.application) + L" /d /s /v:off /c \"" + command + L"\"";
        if (spec.command_line.size() >= 8191) throw ArgumentError(L"Batch command exceeds CMD's supported command-line length.");
    } else if (extension == L".ps1") {
        spec.application = (system_directory() / L"WindowsPowerShell/v1.0/powershell.exe").wstring();
        spec.command_line = quote_argument(spec.application) + L" -NoLogo -NoProfile -NonInteractive -File " + quote_argument(target.wstring());
        for (const auto& arg : args.parameters) spec.command_line += L" " + quote_argument(arg);
    } else {
        throw ArgumentError(L"Unsupported target extension. Expected .exe, .bat, .cmd or .ps1.");
    }
    if (spec.command_line.size() >= 32767) throw ArgumentError(L"Command line exceeds the Windows limit.");
    return spec;
}
void launch(const LaunchSpec& spec) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    Handle output(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    if (!input.valid() || !output.valid()) throw WindowsError(L"Open NUL", GetLastError());
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) throw WindowsError(L"Initialize process attributes", GetLastError());
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST list; ~Cleanup() { DeleteProcThreadAttributeList(list); } } cleanup{list};
    HANDLE handles[] = {input.get(), output.get()};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr))
        throw WindowsError(L"Set inherited handles", GetLastError());
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = output.get();
    startup.StartupInfo.hStdError = output.get();
    startup.lpAttributeList = list;
    PROCESS_INFORMATION process{};
    auto command = spec.command_line;
    if (!CreateProcessW(spec.application.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, spec.cwd ? spec.cwd->c_str() : nullptr,
        &startup.StartupInfo, &process)) throw WindowsError(L"Cannot start " + spec.application, GetLastError());
    Handle process_handle(process.hProcess), thread_handle(process.hThread);
}
}
