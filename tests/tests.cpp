#include "cli/arguments.h"
#include "process/launcher.h"
#include "windows/error.h"
#include <windows.h>
#include <sddl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <chrono>
#include <cstdint>

using namespace runner;
namespace fs = std::filesystem;
static int checks = 0;
static void check(bool value, const char* description) {
    ++checks;
    if (!value) throw std::runtime_error(description);
}
template<class Error, class F> static void rejects(F action, const char* description) {
    bool rejected = false;
    try { action(); } catch (const Error&) { rejected = true; }
    check(rejected, description);
}
static void wait_file(const fs::path& path) {
    for (int i = 0; i < 200 && !fs::exists(path); ++i) Sleep(25);
    check(fs::exists(path), "Timed out waiting for child report");
}
static std::vector<std::wstring> report(const fs::path& path) {
    wait_file(path);
    std::ifstream in(path, std::ios::binary);
    std::vector<std::wstring> values;
    uint32_t size;
    while (in.read(reinterpret_cast<char*>(&size), sizeof(size))) {
        check(size < 32768, "Invalid probe field");
        std::wstring value(size, L'\0');
        in.read(reinterpret_cast<char*>(value.data()), size * sizeof(wchar_t));
        check(static_cast<bool>(in), "Truncated probe report");
        values.push_back(value);
    }
    return values;
}
static void utf8_file(const fs::path& path, const std::wstring& content, bool bom = false) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.size()), bytes.data(), count, nullptr, nullptr);
    std::ofstream out(path, std::ios::binary);
    if (bom) out << "\xef\xbb\xbf";
    out << bytes;
}
static std::string read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static void set_test_execute_access(const fs::path& path, bool deny) {
    HANDLE raw_token = nullptr;
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token) != FALSE, "Open test token");
    Handle token(raw_token);
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> bytes(size);
    check(GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size) != FALSE, "Read test identity");
    wchar_t* raw_sid = nullptr;
    check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &raw_sid) != FALSE, "Convert test SID");
    std::wstring sid(raw_sid);
    LocalFree(raw_sid);
    const std::wstring sddl = (deny ? L"D:P(D;;0x20;;;" + sid + L")" : std::wstring(L"D:P")) + L"(A;;FA;;;" + sid + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    check(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) != FALSE, "Build test ACL");
    const BOOL applied = SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
    LocalFree(descriptor);
    check(applied != FALSE, "Apply execute-denial to owned test file");
}
struct Result { DWORD code; std::string output; std::string error; };
static Result run_cli(const fs::path& exe, const std::vector<std::wstring>& arguments, const fs::path& folder) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    const auto out_path = folder / L"stdout.txt", err_path = folder / L"stderr.txt";
    Handle out(CreateFileW(out_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, 0, nullptr));
    Handle err(CreateFileW(err_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, 0, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.get(); startup.hStdOutput = out.get(); startup.hStdError = err.get();
    std::wstring command = quote_argument(exe.wstring());
    for (const auto& arg : arguments) command += L" " + quote_argument(arg);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE, "Create CLI process");
    Handle process_handle(process.hProcess), thread_handle(process.hThread);
    check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "CLI did not return promptly");
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    return {code, read_bytes(out_path), read_bytes(err_path)};
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const auto folder = fs::temp_directory_path() / (L"silent-runner-tests-" + std::to_wstring(GetCurrentProcessId()));
    try {
        fs::create_directories(folder / L"工作 目录");
        const auto probe = folder / L"测试 helper.exe";
        fs::copy_file(argv[1], probe);
        const fs::path cli = argv[2];
        SetEnvironmentVariableW(L"SILENT_RUNNER_TEST", L"inherited-value");
        check(parse_arguments({L"--help"}).action == Action::help, "help parser");
        check(parse_arguments({L"--version"}).action == Action::version, "version parser");
        check(parse_arguments({L"a.exe", L"--", L"--cwd", L""}).parameters.size() == 2, "delimiter parser");
        check(parse_arguments({L"a.exe", L"--cwd", L"work", L"--", L"--cwd"}).parameters == std::vector<std::wstring>{L"--cwd"}, "options after delimiter are target arguments");
        for (const auto& args : std::vector<std::vector<std::wstring>>{{}, {L"--cwd"}, {L"--cwd", L"x", L"a.exe"}, {L"--wait"}, {L"a.exe", L"arg"}, {L"a.exe", L"--cwd"}, {L"a.exe", L"--cwd", L"--"}, {L"a.exe", L"--cwd", L"x", L"--cwd", L"y"}, {L"--help", L"a.exe"}})
            rejects<ArgumentError>([&] { parse_arguments(args); }, "Invalid CLI accepted");

        auto result = run_cli(cli, {L"--help"}, folder);
        check(result.code == 0 && result.output.find("Usage:") != std::string::npos && result.error.empty(), "help redirection");
        result = run_cli(cli, {L"--version"}, folder);
        check(result.code == 0 && result.output == "SilentRunner 0.1.0\r\n", "version output");
        result = run_cli(cli, {}, folder);
        check(result.code == 2 && !result.error.empty() && result.output.empty(), "missing target error");
        result = run_cli(cli, {(folder / L"不存在.exe").wstring()}, folder);
        check(result.code == 1 && !result.error.empty(), "missing target error code");
        result = run_cli(cli, {probe.wstring(), L"--cwd", (folder / L"missing").wstring()}, folder);
        check(result.code == 1, "invalid cwd error code");
        utf8_file(folder / L"invalid.exe", L"not an executable");
        result = run_cli(cli, {(folder / L"invalid.exe").wstring()}, folder);
        check(result.code == 1, "invalid binary error code");
        const auto denied = folder / L"denied.exe";
        fs::copy_file(probe, denied);
        set_test_execute_access(denied, true);
        result = run_cli(cli, {denied.wstring()}, folder);
        set_test_execute_access(denied, false);
        check(result.code == 1 && result.error.find("Windows error 5") != std::string::npos, "access denied error code");
        utf8_file(folder / L"invalid.txt", L"text");
        result = run_cli(cli, {(folder / L"invalid.txt").wstring()}, folder);
        check(result.code == 2, "unsupported extension");

        const std::vector<std::wstring> tricky{L"", L"hello world", L"中文参数", L"a\"b", L"C:\\space dir\\", L"\\\\\"x", L"--cwd", L"a&b|c%PATH%!d", L"line\nbreak"};
        auto destination = folder / L"exe.bin";
        std::vector<std::wstring> args{probe.wstring(), L"--cwd", (folder / L"工作 目录").wstring(), L"--", destination.wstring()};
        args.insert(args.end(), tricky.begin(), tricky.end());
        result = run_cli(cli, args, folder);
        check(result.code == 0 && result.output.empty() && result.error.empty(), "EXE launch");
        auto values = report(destination);
        check(values.size() == tricky.size() + 4, "EXE argc");
        check(values[0] == (folder / L"工作 目录").wstring(), "working directory");
        check(values[1] == L"no-console" && values[2] == L"inherited-value" && values[3] == L"eof", "console/environment/stdin");
        check(std::vector<std::wstring>(values.begin() + 4, values.end()) == tricky, "EXE argv round trip");
        check(read_bytes(folder / L"stdout.txt").empty() && read_bytes(folder / L"stderr.txt").empty(), "child output must not leak into launcher redirection");

        destination = folder / L"default-cwd.bin";
        result = run_cli(cli, {probe.wstring(), L"--", destination.wstring()}, folder);
        check(result.code == 0 && report(destination)[0] == folder.wstring(), "EXE defaults to its own directory");

        destination = folder / L"delay.bin";
        result = run_cli(cli, {probe.wstring(), L"--", destination.wstring(), L"delay"}, folder);
        check(result.code == 0 && !fs::exists(destination), "launcher must return before child");
        values = report(destination);
        check(values[4] == L"delay", "child survives launcher exit");

        const auto old_cwd = fs::current_path();
        fs::current_path(folder);
        Arguments resolve;
        resolve.target = L"测试 helper";
        resolve.cwd = (folder / L"工作 目录").wstring();
        check(prepare_launch(resolve).application == probe.wstring(), "current directory before cwd and exe suffix");
        resolve.cwd.reset();
        check(prepare_launch(resolve).cwd == folder.wstring(), "resolved EXE directory is default cwd");
        resolve.target = L".\\测试 helper.exe";
        check(prepare_launch(resolve).application == probe.wstring(), "relative target resolution");
        resolve.cwd = (folder / L"工作 目录").wstring();
        const auto cwd_probe = folder / L"工作 目录" / L"only-in-cwd.exe";
        fs::copy_file(probe, cwd_probe);
        resolve.target = L"only-in-cwd";
        check(prepare_launch(resolve).application == cwd_probe.wstring(), "bare target resolves in --cwd");
        fs::current_path(old_cwd);
        wchar_t* saved_path = nullptr;
        size_t saved_size = 0;
        _wdupenv_s(&saved_path, &saved_size, L"PATH");
        SetEnvironmentVariableW(L"PATH", folder.c_str());
        resolve.target = L"测试 helper.exe";
        check(prepare_launch(resolve).application == probe.wstring(), "PATH search");
        SetEnvironmentVariableW(L"PATH", saved_path);
        free(saved_path);

        for (const auto& extension : {L".bat", L".cmd"}) {
            const auto script = folder / L"工作 目录" / (std::wstring(L"测试 script") + extension);
            // chcp ensures UTF-8 script literals on every system locale.
            utf8_file(script, L"@echo off\r\nchcp 65001 >nul\r\n\"" + probe.wstring() + L"\" %*\r\n");
            destination = folder / (std::wstring(extension) + L".bin");
            result = run_cli(cli, {script.wstring(), L"--", destination.wstring(), L"中文 参数", L"", L"C:\\tail"}, folder);
            check(result.code == 0, "batch launch");
            values = report(destination);
            check(values.size() == 7 && values[4] == L"中文 参数" && values[5].empty() && values[6] == L"C:\\tail", "batch arguments");
            check(values[0] == script.parent_path().wstring() && values[1] == L"no-console", "batch script directory and hidden console");
            check(prepare_launch(parse_arguments({script.wstring()})).cwd == script.parent_path().wstring(), "batch spec cwd");
            destination = folder / (std::wstring(extension) + L"-override.bin");
            result = run_cli(cli, {script.wstring(), L"--cwd", folder.wstring(), L"--", destination.wstring()}, folder);
            check(result.code == 0 && report(destination)[0] == folder.wstring(), "batch explicit cwd override");
            for (const auto& unsafe : {L"a\"b", L"%PATH%", L"!x!", L"a&whoami", L"x|y", L"a^b", L"x>y", L"x<y", L"(x)", L"x\ny", L"x\ry"}) {
                result = run_cli(cli, {script.wstring(), L"--", unsafe}, folder);
                check(result.code == 2, "unsafe batch argument accepted");
            }
            auto unsafe_path = folder / L"bad&name.cmd";
            fs::copy_file(script, unsafe_path, fs::copy_options::overwrite_existing);
            result = run_cli(cli, {unsafe_path.wstring()}, folder);
            check(result.code == 2, "unsafe batch path accepted");
        }

        const auto ps_script = folder / L"工作 目录" / L"测试 script.ps1";
        // PowerShell writes a marker directly; -File argument behavior belongs to that interpreter.
        utf8_file(ps_script, L"param([string]$OutputFile,[string]$Value)\r\n[IO.File]::WriteAllText($OutputFile, $Value + '|' + (Get-Location).Path, [Text.Encoding]::UTF8)\r\n", true);
        destination = folder / L"ps.txt";
        // Process-scope policy is inherited, never changed by SilentRunner. Machine policy still wins.
        wchar_t policy[256]{};
        GetEnvironmentVariableW(L"PSExecutionPolicyPreference", policy, 256);
        SetEnvironmentVariableW(L"PSExecutionPolicyPreference", L"RemoteSigned");
        result = run_cli(cli, {ps_script.wstring(), L"--", destination.wstring(), L"hello world"}, folder);
        check(result.code == 0, "PowerShell launch");
        wait_file(destination);
        check(read_bytes(destination).find("hello world") != std::string::npos, "PowerShell script arguments");
        std::ifstream ps_report(destination, std::ios::binary);
        std::string ps_content((std::istreambuf_iterator<char>(ps_report)), std::istreambuf_iterator<char>());
        ps_report.close();
        const auto expected_cwd = ps_script.parent_path().wstring();
        const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, expected_cwd.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string expected_utf8(static_cast<size_t>(utf8_size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, expected_cwd.c_str(), -1, expected_utf8.data(), utf8_size, nullptr, nullptr);
        expected_utf8.pop_back();
        check(ps_content.find(expected_utf8) != std::string::npos, "PowerShell runtime cwd");
        check(prepare_launch(parse_arguments({ps_script.wstring()})).cwd == ps_script.parent_path().wstring(), "PowerShell script directory is default cwd");
        const auto ps_override = prepare_launch(parse_arguments({ps_script.wstring(), L"--cwd", folder.wstring()}));
        check(ps_override.cwd == folder.wstring(), "PowerShell explicit cwd override");
        SetEnvironmentVariableW(L"PSExecutionPolicyPreference", L"Restricted");
        const auto blocked = folder / L"blocked.txt";
        result = run_cli(cli, {ps_script.wstring(), L"--", blocked.wstring(), L"blocked"}, folder);
        check(result.code == 0, "policy failure is asynchronous");
        const auto ps_spec = prepare_launch(parse_arguments({ps_script.wstring()}));
        result = run_cli(ps_spec.application, {L"-NoLogo", L"-NoProfile", L"-NonInteractive", L"-File", ps_script.wstring(), blocked.wstring(), L"blocked"}, folder);
        check(result.code != 0 && !result.error.empty(), "Restricted policy should reject the test script");
        check(!fs::exists(blocked), "launcher must not bypass PowerShell policy");
        SetEnvironmentVariableW(L"PSExecutionPolicyPreference", policy[0] ? policy : nullptr);

        const auto resolved_folder = fs::weakly_canonical(folder);
        check(resolved_folder.parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
            resolved_folder.filename() == L"silent-runner-tests-" + std::to_wstring(GetCurrentProcessId()), "cleanup must stay in the test directory");
        std::cout << "Passed " << checks << " checks\n";
        fs::remove_all(folder);
        return 0;
    } catch (const WindowsError& error) {
        std::wcerr << error.message() << L"\n";
    } catch (const ArgumentError& error) {
        std::wcerr << error.message() << L"\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
    }
    std::wcerr << L"Test artifacts retained: " << folder.wstring() << L"\n";
    return 1;
}
