#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdint>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    if (argc > 2 && std::wstring(argv[2]) == L"delay") Sleep(1500);
    for (DWORD stream : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        DWORD written = 0;
        const auto handle = GetStdHandle(stream);
        if (GetFileType(handle) != FILE_TYPE_CHAR || !WriteFile(handle, "discarded output", 16, &written, nullptr)) return 4;
    }
    const std::filesystem::path report(argv[1]);
    auto temporary = report;
    temporary += L".tmp";
    std::ofstream out(temporary, std::ios::binary);
    auto field = [&](const std::wstring& value) {
        const auto size = static_cast<uint32_t>(value.size());
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        out.write(reinterpret_cast<const char*>(value.data()), size * sizeof(wchar_t));
    };
    field(std::filesystem::current_path().wstring());
    field(GetConsoleWindow() ? L"console" : L"no-console");
    wchar_t environment[256]{};
    GetEnvironmentVariableW(L"SILENT_RUNNER_TEST", environment, 256);
    field(environment);
    DWORD count = 123;
    char input = 0;
    const BOOL read = ReadFile(GetStdHandle(STD_INPUT_HANDLE), &input, 1, &count, nullptr);
    field(read && count == 0 ? L"eof" : L"not-eof");
    for (int i = 2; i < argc; ++i) field(argv[i]);
    out.close();
    if (!out) return 3;
    std::filesystem::rename(temporary, report);
    return 0;
}
