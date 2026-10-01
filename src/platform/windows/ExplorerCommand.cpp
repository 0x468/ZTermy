// Explorer's surrogate loads this DLL, not Qt or the terminal application.
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl.h>

#include <atomic>
#include <new>
#include <string>

namespace
{
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
constexpr CLSID commandId{
    .Data1 = 0x9c0c02c2, .Data2 = 0x441d, .Data3 = 0x44e9, .Data4 = {0xb8, 0x80, 0x68, 0xd6, 0x64, 0x7b, 0x2b, 0x31}};
HMODULE moduleHandle = nullptr;
std::atomic<long> objects{0};
std::atomic<long> locks{0};

class Lifetime
{
public:
    Lifetime() { ++objects; }
    ~Lifetime() { --objects; }
    Lifetime(const Lifetime &) = delete;
    Lifetime &operator=(const Lifetime &) = delete;
};

HRESULT executablePath(std::wstring &path)
{
    path.resize(32768);
    const DWORD length = GetModuleFileNameW(moduleHandle, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
    path.resize(length);
    const auto separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return E_FAIL;
    path.resize(separator + 1);
    path += L"ztermy.exe";
    return S_OK;
}

class OpenHereCommand final
    : public RuntimeClass<RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IExplorerCommand, IObjectWithSite>
{
public:
    IFACEMETHODIMP GetTitle(IShellItemArray *, PWSTR *title) override
    {
        if (title == nullptr)
            return E_POINTER;
        return SHStrDupW(PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? L"在此处打开 Ztermy"
                                                                                   : L"Open in Ztermy",
                         title);
    }
    IFACEMETHODIMP GetIcon(IShellItemArray *, PWSTR *icon) override
    {
        if (icon == nullptr)
            return E_POINTER;
        *icon = nullptr;
        try
        {
            std::wstring path;
            const HRESULT result = executablePath(path);
            if (FAILED(result))
                return result;
            path = L"\"" + path + L"\",0";
            return SHStrDupW(path.c_str(), icon);
        }
        catch (const std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray *, PWSTR *tip) override
    {
        if (tip == nullptr)
            return E_POINTER;
        *tip = nullptr;
        return E_NOTIMPL;
    }
    IFACEMETHODIMP GetCanonicalName(GUID *name) override
    {
        if (name == nullptr)
            return E_POINTER;
        *name = commandId;
        return S_OK;
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS *flags) override
    {
        if (flags == nullptr)
            return E_POINTER;
        *flags = ECF_DEFAULT;
        return S_OK;
    }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand **commands) override
    {
        if (commands == nullptr)
            return E_POINTER;
        *commands = nullptr;
        return E_NOTIMPL;
    }
    IFACEMETHODIMP SetSite(IUnknown *site) override
    {
        m_site = site;
        return S_OK;
    }
    IFACEMETHODIMP GetSite(REFIID iid, void **site) override
    {
        if (site == nullptr)
            return E_POINTER;
        *site = nullptr;
        return m_site ? m_site->QueryInterface(iid, site) : E_FAIL;
    }
    IFACEMETHODIMP GetState(IShellItemArray *items, BOOL, EXPCMDSTATE *state) override
    {
        if (state == nullptr)
            return E_POINTER;
        *state = ECS_HIDDEN;
        ComPtr<IShellItem> item;
        if (SUCCEEDED(directoryItem(items, item)))
            *state = ECS_ENABLED;
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray *items, IBindCtx *) override
    {
        ComPtr<IShellItem> item;
        HRESULT result = directoryItem(items, item);
        if (FAILED(result))
            return result;
        PWSTR rawPath = nullptr;
        result = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
        if (FAILED(result))
            return result;
        // Shell filesystem paths cannot contain quotes. Do not invoke a shell:
        // pass the directory as one literal argv value to the fixed sibling EXE.
        try
        {
            std::wstring directory(rawPath);
            CoTaskMemFree(rawPath);
            rawPath = nullptr;
            if (directory.find_first_of(L"\"\r\n") != std::wstring::npos)
                return E_INVALIDARG;
            std::wstring executable;
            result = executablePath(executable);
            if (FAILED(result))
                return result;
            // The suffix also prevents a drive-root backslash escaping a quote.
            std::wstring command = L"\"" + executable + L"\" --open-directory \"" + directory + L"\\.\"";
            STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                                &startup, &process))
                return HRESULT_FROM_WIN32(GetLastError());
            AllowSetForegroundWindow(process.dwProcessId);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return S_OK;
        }
        catch (const std::bad_alloc &)
        {
            CoTaskMemFree(rawPath);
            return E_OUTOFMEMORY;
        }
    }

private:
    HRESULT directoryItem(IShellItemArray *items, ComPtr<IShellItem> &item)
    {
        if (items != nullptr)
        {
            DWORD count = 0;
            HRESULT result = items->GetCount(&count);
            if (FAILED(result) || count != 1)
                return E_INVALIDARG;
            result = items->GetItemAt(0, &item);
            if (FAILED(result))
                return result;
        }
        else
        {
            if (!m_site)
                return E_FAIL;
            // Folder-background commands have no selection. Resolve Explorer's
            // current view from the COM site, never the surrogate's working dir.
            ComPtr<IServiceProvider> services;
            HRESULT result = m_site.As(&services);
            if (FAILED(result))
                return result;
            ComPtr<IFolderView> view;
            result = services->QueryService(SID_SFolderView, IID_PPV_ARGS(&view));
            if (FAILED(result))
                return result;
            ComPtr<IPersistFolder2> folder;
            result = view->GetFolder(IID_PPV_ARGS(&folder));
            if (FAILED(result))
                return result;
            PIDLIST_ABSOLUTE id = nullptr;
            result = folder->GetCurFolder(&id);
            if (FAILED(result))
                return result;
            result = SHCreateItemFromIDList(id, IID_PPV_ARGS(&item));
            CoTaskMemFree(id);
            if (FAILED(result))
                return result;
        }
        SFGAOF attributes = 0;
        const HRESULT result = item->GetAttributes(SFGAO_FOLDER | SFGAO_FILESYSTEM, &attributes);
        return SUCCEEDED(result)
                       && (attributes & (SFGAO_FOLDER | SFGAO_FILESYSTEM)) == (SFGAO_FOLDER | SFGAO_FILESYSTEM)
                   ? S_OK
                   : E_INVALIDARG;
    }
    Lifetime m_lifetime;
    ComPtr<IUnknown> m_site;
};

class CommandFactory final : public RuntimeClass<RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IClassFactory>
{
public:
    IFACEMETHODIMP CreateInstance(IUnknown *outer, REFIID iid, void **object) override
    {
        if (object == nullptr)
            return E_POINTER;
        *object = nullptr;
        if (outer != nullptr)
            return CLASS_E_NOAGGREGATION;
        auto command = Make<OpenHereCommand>();
        return command ? command->QueryInterface(iid, object) : E_OUTOFMEMORY;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        if (lock)
            ++locks;
        else
            --locks;
        return S_OK;
    }

private:
    Lifetime m_lifetime;
};
} // namespace

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **object)
{
    if (object == nullptr)
        return E_POINTER;
    *object = nullptr;
    if (clsid != commandId)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = Make<CommandFactory>();
    return factory ? factory->QueryInterface(iid, object) : E_OUTOFMEMORY;
}

STDAPI DllCanUnloadNow()
{
    return objects.load() == 0 && locks.load() == 0 ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        moduleHandle = module;
    return TRUE;
}
