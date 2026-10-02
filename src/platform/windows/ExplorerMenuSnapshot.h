#pragma once

#include "core/config/ExplorerMenuContract.h"

#include <windows.h>
#include <string>
#include <utility>

namespace ztermy::explorer
{
// Installation-specific and per-user: a development/portable build cannot replace an installed menu's snapshot.
[[nodiscard]] inline std::wstring snapshotPath(std::wstring executable)
{
    const auto separator = executable.find_last_of(L"/\\");
    if (separator == std::wstring::npos)
        return {};
    executable.resize(separator);
    for (auto &character : executable)
        if (character == L'/')
            character = L'\\';
    CharLowerBuffW(executable.data(), static_cast<DWORD>(executable.size()));
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto character : executable)
    {
        hash = (hash ^ static_cast<std::uint16_t>(character)) * 1099511628211ULL;
    }
    std::wstring local(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (length == 0 || length >= local.size())
        return {};
    local.resize(length);
    return local + L"\\Ztermy\\Explorer\\" + std::to_wstring(hash) + L".bin";
}

[[nodiscard]] inline MenuSnapshot readSnapshot(const std::wstring &executable)
{
    const auto path = snapshotPath(executable);
    if (path.empty())
        return defaultSnapshot;
    const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return defaultSnapshot;
    MenuSnapshot value{};
    DWORD read = 0;
    LARGE_INTEGER size{};
    const bool loaded = GetFileSizeEx(file, &size) && std::cmp_equal(size.QuadPart, value.size())
                        && ReadFile(file, value.data(), static_cast<DWORD>(value.size()), &read, nullptr)
                        && read == value.size();
    CloseHandle(file);
    return loaded && validSnapshot(value) ? value : defaultSnapshot;
}
} // namespace ztermy::explorer
