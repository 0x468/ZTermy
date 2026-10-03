#pragma once

#include <Windows.h>

#include <system_error>

namespace ztermy::terminal
{
struct ConPtyApi final
{
    using Create = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
    using Resize = HRESULT(WINAPI *)(HPCON, COORD);
    using Close = void(WINAPI *)(HPCON);
    using Release = HRESULT(WINAPI *)(HPCON);
    Create create = nullptr;
    Resize resize = nullptr;
    Close close = nullptr;
    Release release = nullptr;
    std::error_code error;
};

// Initialize before constructing the GUI application. Subsequent session starts
// only read the cached API; extraction and hashing never run during GUI events.
[[nodiscard]] const ConPtyApi &conPtyApi();
} // namespace ztermy::terminal
