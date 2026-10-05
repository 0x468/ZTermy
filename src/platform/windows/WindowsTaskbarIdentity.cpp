#include "platform/windows/WindowsTaskbarIdentity.h"

#include <shellapi.h>
#include <shobjidl.h>

// The SDK requires PROPERTYKEY declarations before its property-key constants.
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace ztermy::windowing
{
bool initializeTaskbarIdentity() noexcept
{
    return SUCCEEDED(SetCurrentProcessExplicitAppUserModelID(taskbarApplicationId));
}

bool configureTaskbarIdentity(const HWND window)
{
    if (!window)
        return false;
    Microsoft::WRL::ComPtr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store))))
        return false;
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return false;
    const std::wstring executable(buffer.data(), length);
    const auto setString = [&store](const PROPERTYKEY &key, const wchar_t *text) {
        PROPVARIANT value{};
        const HRESULT initialized = InitPropVariantFromString(text, &value);
        if (FAILED(initialized))
            return false;
        const HRESULT result = store->SetValue(key, value);
        PropVariantClear(&value);
        return SUCCEEDED(result);
    };
    // Explorer's packaged COM surrogate passes its sparse-package identity to
    // CreateProcess descendants. Its hidden application is not our taskbar app.
    // Set relaunch metadata before the window ID: Explorer consumes it on ID change.
    // Never retain launch arguments (SSH credentials or paths) in taskbar metadata.
    const std::wstring command = L"\"" + executable + L"\"";
    const std::wstring icon = command + L",0";
    return setString(PKEY_AppUserModel_RelaunchCommand, command.c_str())
           && setString(PKEY_AppUserModel_RelaunchIconResource, icon.c_str())
           && setString(PKEY_AppUserModel_RelaunchDisplayNameResource, L"Ztermy")
           && setString(PKEY_AppUserModel_ID, taskbarApplicationId);
}

bool hasTaskbarIdentity(const HWND window)
{
    if (!window)
        return false;
    Microsoft::WRL::ComPtr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store))))
        return false;
    PROPVARIANT value{};
    if (FAILED(store->GetValue(PKEY_AppUserModel_ID, &value)))
        return false;
    const bool matches =
        value.vt == VT_LPWSTR && value.pwszVal && std::wstring_view(value.pwszVal) == taskbarApplicationId;
    PropVariantClear(&value);
    return matches;
}
} // namespace ztermy::windowing
