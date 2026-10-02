#pragma once

#include <QString>
#include <qt_windows.h>

namespace ztermy::windowing
{
// Respect inherited console/pipe output; GUI launches receive a native dialog.
inline void showLaunchFeedback(const QString &text, const bool error = false)
{
    HANDLE output = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE || GetFileType(output) == FILE_TYPE_UNKNOWN)
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            output = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    }
    if (output != nullptr && output != INVALID_HANDLE_VALUE && GetFileType(output) != FILE_TYPE_UNKNOWN)
    {
        const auto bytes = (text + u'\n').toUtf8();
        DWORD written = 0;
        if (WriteFile(output, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr))
            return;
    }
    MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(text.utf16()), L"Ztermy",
                MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}
} // namespace ztermy::windowing
