#include "cli/arguments.h"
#include "process/launcher.h"
#include "windows/error.h"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace runner;
    // Do not display OS critical-error dialogs from this launcher.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    try {
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) throw WindowsError(L"Read command line", GetLastError());
        struct Cleanup { wchar_t** argv; ~Cleanup() { LocalFree(argv); } } cleanup{argv};
        std::vector<std::wstring> values;
        for (int i = 1; i < argc; ++i) values.emplace_back(argv[i]);
        const auto args = parse_arguments(values);
        if (args.action == Action::help) {
            write_message(STD_OUTPUT_HANDLE,
                L"SilentRunner - hidden process launcher\r\n"
                L"Usage: SilentRunner.exe <target> [--cwd <directory>] [-- arguments...]\r\n"
                L"       SilentRunner.exe --help | --version\r\n"
                L"Targets: .exe, .bat, .cmd, .ps1. Scripts require their extension.\r\n"
                L"Default working directory: target file's directory.\r\n"
                L"Returns after process creation; child input/output use NUL.\r\n"
                L"GUI windows are hidden on a best-effort basis only.\r\n"
                L"Batch paths/arguments reject CMD metacharacters; see docs/usage.md.\r\n"
                L"Exit codes: 0 created/help/version, 1 launch failure, 2 invalid arguments.\r\n");
        } else if (args.action == Action::version) {
            write_message(STD_OUTPUT_HANDLE, std::wstring(L"SilentRunner ") + RUNNER_VERSION + L"\r\n");
        } else {
            launch(prepare_launch(args));
        }
        return 0;
    } catch (const ArgumentError& error) {
        write_message(STD_ERROR_HANDLE, L"SilentRunner: " + error.message() + L"\r\n");
        return 2;
    } catch (const WindowsError& error) {
        write_message(STD_ERROR_HANDLE, L"SilentRunner: " + error.message() + L"\r\n");
        return 1;
    } catch (...) {
        write_message(STD_ERROR_HANDLE, L"SilentRunner: unexpected internal error.\r\n");
        return 1;
    }
}
