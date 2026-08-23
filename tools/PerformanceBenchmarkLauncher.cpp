#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
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

[[nodiscard]] std::wstring environmentValue(const wchar_t *name)
{
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
    {
        return {};
    }
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required)
    {
        return {};
    }
    value.resize(written);
    return value;
}

[[nodiscard]] std::wstring performanceLabel()
{
    std::wstring backdrop = environmentValue(L"ZTERMY_PERFORMANCE_BACKDROP");
    if (backdrop != L"acrylic" && backdrop != L"mica" && backdrop != L"micaAlt" && backdrop != L"transparent"
        && backdrop != L"opaque")
    {
        backdrop.clear();
    }
    const bool softwareRenderer = environmentValue(L"QSG_RHI_PREFER_SOFTWARE_RENDERER") == L"1";
    if (backdrop.empty() && !softwareRenderer)
    {
        return L"baseline";
    }
    if (backdrop.empty())
    {
        backdrop = L"acrylic";
    }
    if (softwareRenderer)
    {
        backdrop += L"-warp";
    }
    return backdrop;
}

[[nodiscard]] std::wstring performanceRunId()
{
    const std::wstring value = environmentValue(L"ZTERMY_PERFORMANCE_RUN_ID");
    if (value.empty() || !std::ranges::all_of(value, [](const wchar_t character) {
            return character >= L'0' && character <= L'9';
        }))
    {
        return {};
    }
    return L"run-" + value;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int)
{
    const std::filesystem::path directory = executableDirectory();
    const std::filesystem::path applicationPath = directory / L"ztermy.exe";
    const bool uiBenchmark =
        arguments != nullptr && std::wstring_view(arguments).find(L"--ui") != std::wstring_view::npos;
    const std::wstring label = performanceLabel();
    std::filesystem::path dataDirectory =
        directory / L"test-data" / ((uiBenchmark ? L"performance-ui-" : L"performance-") + label);
    const std::wstring runId = performanceRunId();
    if (!runId.empty())
    {
        dataDirectory /= runId;
    }
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

    const std::wstring benchmarkArgument = uiBenchmark ? L"--ui-performance-benchmark" : L"--performance-benchmark";
    std::wstring commandLine = L"\"" + applicationPath.wstring() + L"\" " + benchmarkArgument + L" --data-dir \""
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
