#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{

[[nodiscard]] std::filesystem::path executableDirectory()
{
    std::vector<wchar_t> buffer(1024);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return {};
        }
        if (length < buffer.size() - 1U)
        {
            return std::filesystem::path(std::wstring_view(buffer.data(), length)).parent_path();
        }
        buffer.resize(buffer.size() * 2U);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const std::filesystem::path directory = executableDirectory();
    const std::filesystem::path applicationPath = directory / L"ztermy.exe";
    const std::filesystem::path dataDirectory = directory / L"test-data" / L"performance-baseline";
    if (directory.empty() || !std::filesystem::is_regular_file(applicationPath))
    {
        return ERROR_FILE_NOT_FOUND;
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(dataDirectory, cleanupError);
    if (cleanupError)
    {
        return static_cast<int>(cleanupError.value());
    }

    std::wstring commandLine = L"\"" + applicationPath.wstring() + L"\" --performance-benchmark --data-dir \""
                               + dataDirectory.wstring() + L"\"";
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    if (CreateProcessW(applicationPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       directory.c_str(), &startupInfo, &processInfo)
        == FALSE)
    {
        return static_cast<int>(GetLastError());
    }

    CloseHandle(processInfo.hThread);
    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = EXIT_FAILURE;
    if (waitResult == WAIT_OBJECT_0)
    {
        static_cast<void>(GetExitCodeProcess(processInfo.hProcess, &exitCode));
    }
    CloseHandle(processInfo.hProcess);
    return waitResult == WAIT_OBJECT_0 ? static_cast<int>(exitCode) : static_cast<int>(waitResult);
}
