#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include "platform/windows/ExplorerMenuSnapshot.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl.h>

class ExplorerCommandTests final : public QObject
{
    Q_OBJECT
private slots:
    void nativeComLifetimeAndSelectionContract();
    void enumeratesOnlyConfiguredShellsAndRejectsMalformedSnapshots();
};

void ExplorerCommandTests::nativeComLifetimeAndSelectionContract()
{
    using Microsoft::WRL::ComPtr;
    QVERIFY(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    const auto file =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ztermy-explorer-command.dll"));
    const auto module = LoadLibraryExW(reinterpret_cast<LPCWSTR>(file.utf16()), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    QVERIFY(module != nullptr);
    using GetClass = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
    using CanUnload = HRESULT(WINAPI *)();
    const auto getClass = reinterpret_cast<GetClass>(GetProcAddress(module, "DllGetClassObject"));
    const auto canUnload = reinterpret_cast<CanUnload>(GetProcAddress(module, "DllCanUnloadNow"));
    QVERIFY(getClass != nullptr);
    QVERIFY(canUnload != nullptr);
    QCOMPARE(canUnload(), S_OK);
    constexpr CLSID id{.Data1 = 0x9c0c02c2,
                       .Data2 = 0x441d,
                       .Data3 = 0x44e9,
                       .Data4 = {0xb8, 0x80, 0x68, 0xd6, 0x64, 0x7b, 0x2b, 0x31}};
    ComPtr<IClassFactory> factory;
    QCOMPARE(getClass(CLSID_NULL, IID_PPV_ARGS(&factory)), CLASS_E_CLASSNOTAVAILABLE);
    QCOMPARE(getClass(id, IID_PPV_ARGS(&factory)), S_OK);
    QCOMPARE(canUnload(), S_FALSE);
    QCOMPARE(factory->LockServer(TRUE), S_OK);
    ComPtr<IExplorerCommand> command;
    QCOMPARE(factory->CreateInstance(nullptr, IID_PPV_ARGS(&command)), S_OK);
    EXPCMDSTATE state{};
    QCOMPARE(command->GetState(nullptr, FALSE, &state), S_OK);
    QCOMPARE(state, ECS_HIDDEN); // No selection/site must not fall back to CWD.
    QCOMPARE(command->GetState(nullptr, FALSE, nullptr), E_POINTER);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = QDir::toNativeSeparators(directory.path());
    ComPtr<IShellItem> item;
    QCOMPARE(SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, IID_PPV_ARGS(&item)), S_OK);
    ComPtr<IShellItemArray> items;
    QCOMPARE(SHCreateShellItemArrayFromShellItem(item.Get(), IID_PPV_ARGS(&items)), S_OK);
    QCOMPARE(command->GetState(items.Get(), FALSE, &state), S_OK);
    QCOMPARE(state, ECS_ENABLED);
    QFile regular(directory.filePath(QStringLiteral("ordinary.txt")));
    QVERIFY(regular.open(QIODevice::WriteOnly));
    regular.close();
    item.Reset();
    items.Reset();
    const auto regularPath = QDir::toNativeSeparators(regular.fileName());
    QCOMPARE(SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(regularPath.utf16()), nullptr, IID_PPV_ARGS(&item)),
             S_OK);
    QCOMPARE(SHCreateShellItemArrayFromShellItem(item.Get(), IID_PPV_ARGS(&items)), S_OK);
    QCOMPARE(command->GetState(items.Get(), FALSE, &state), S_OK);
    QCOMPARE(state, ECS_HIDDEN);
    PWSTR title = nullptr;
    QCOMPARE(command->GetTitle(nullptr, &title), S_OK);
    QVERIFY(title != nullptr && *title != L'\0');
    CoTaskMemFree(title);
    GUID canonical{};
    QCOMPARE(command->GetCanonicalName(&canonical), S_OK);
    QVERIFY(canonical == id);
    command.Reset();
    factory->LockServer(FALSE);
    factory.Reset();
    QCOMPARE(canUnload(), S_OK);
    items.Reset();
    item.Reset();
    QVERIFY(FreeLibrary(module));
    CoUninitialize();
}

void ExplorerCommandTests::enumeratesOnlyConfiguredShellsAndRejectsMalformedSnapshots()
{
    using Microsoft::WRL::ComPtr;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto previousLocal = qgetenv("LOCALAPPDATA");
    qputenv("LOCALAPPDATA", directory.path().toUtf8());
    const auto restoreEnvironment = qScopeGuard([&] {
        qputenv("LOCALAPPDATA", previousLocal);
    });
    QVERIFY(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    const auto uninitialize = qScopeGuard([] {
        CoUninitialize();
    });
    const auto file =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ztermy-explorer-command.dll"));
    const auto executable = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ztermy.exe"));
    const auto path = QString::fromStdWString(ztermy::explorer::snapshotPath(executable.toStdWString()));
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    const auto writeSnapshot = [&](const QByteArray &bytes) {
        QFile snapshot(path);
        return snapshot.open(QIODevice::WriteOnly) && snapshot.write(bytes) == bytes.size();
    };
    const auto module = LoadLibraryExW(reinterpret_cast<LPCWSTR>(file.utf16()), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    QVERIFY(module);
    const auto unload = qScopeGuard([&] {
        FreeLibrary(module);
    });
    using GetClass = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
    const auto getClass = reinterpret_cast<GetClass>(GetProcAddress(module, "DllGetClassObject"));
    QVERIFY(getClass);
    constexpr CLSID id{.Data1 = 0x9c0c02c2,
                       .Data2 = 0x441d,
                       .Data3 = 0x44e9,
                       .Data4 = {0xb8, 0x80, 0x68, 0xd6, 0x64, 0x7b, 0x2b, 0x31}};
    ComPtr<IClassFactory> factory;
    QCOMPARE(getClass(id, IID_PPV_ARGS(&factory)), S_OK);
    auto bytes = ztermy::explorer::defaultSnapshot;
    bytes[8] = 1;
    bytes[10] = (1U << 2) | (1U << 3); // Exactly Windows PowerShell and Command Prompt.
    QVERIFY(writeSnapshot(QByteArray(reinterpret_cast<const char *>(bytes.data()), bytes.size())));
    ComPtr<IExplorerCommand> root;
    QCOMPARE(factory->CreateInstance(nullptr, IID_PPV_ARGS(&root)), S_OK);
    EXPCMDFLAGS flags{};
    QCOMPARE(root->GetFlags(&flags), S_OK);
    QCOMPARE(flags, ECF_HASSUBCOMMANDS);
    PWSTR icon = nullptr;
    QCOMPARE(root->GetIcon(nullptr, &icon), S_OK);
    QVERIFY(QString::fromWCharArray(icon).endsWith(QStringLiteral("ztermy.exe\",0")));
    CoTaskMemFree(icon);
    ComPtr<IEnumExplorerCommand> children;
    QCOMPARE(root->EnumSubCommands(&children), S_OK);
    ULONG fetched = 0;
    ComPtr<IExplorerCommand> child;
    QCOMPARE(children->Next(1, &child, &fetched), S_OK);
    QCOMPARE(fetched, 1UL);
    PWSTR title = nullptr;
    QCOMPARE(child->GetTitle(nullptr, &title), S_OK);
    QCOMPARE(QString::fromWCharArray(title), QStringLiteral("Windows PowerShell"));
    CoTaskMemFree(title);
    QCOMPARE(child->GetIcon(nullptr, &icon), S_OK);
    QVERIFY(QString::fromWCharArray(icon).endsWith(QStringLiteral("ztermy-explorer-command.dll\",-103")));
    CoTaskMemFree(icon);
    HICON nativeIcon = nullptr;
    QCOMPARE(ExtractIconExW(reinterpret_cast<LPCWSTR>(file.utf16()), -103, nullptr, &nativeIcon, 1), 1U);
    QVERIFY(nativeIcon != nullptr);
    QVERIFY(DestroyIcon(nativeIcon));
    QCOMPARE(child->GetFlags(&flags), S_OK);
    QCOMPARE(flags, ECF_DEFAULT);
    ComPtr<IEnumExplorerCommand> nested;
    QCOMPARE(child->EnumSubCommands(&nested), E_NOTIMPL);
    child.Reset();
    ComPtr<IEnumExplorerCommand> clone;
    QCOMPARE(children->Clone(&clone), S_OK);
    QCOMPARE(clone->Next(1, &child, &fetched), S_OK);
    QCOMPARE(child->GetTitle(nullptr, &title), S_OK);
    QCOMPARE(QString::fromWCharArray(title), QStringLiteral("Command Prompt"));
    CoTaskMemFree(title);
    QCOMPARE(child->GetIcon(nullptr, &icon), S_OK);
    QVERIFY(QString::fromWCharArray(icon).endsWith(QStringLiteral("ztermy-explorer-command.dll\",-104")));
    CoTaskMemFree(icon);
    for (int resource = 101; resource <= 107; ++resource)
    {
        nativeIcon = nullptr;
        QCOMPARE(ExtractIconExW(reinterpret_cast<LPCWSTR>(file.utf16()), -resource, nullptr, &nativeIcon, 1), 1U);
        QVERIFY(nativeIcon);
        QVERIFY(DestroyIcon(nativeIcon));
    }
    child.Reset();
    QCOMPARE(clone->Next(1, &child, &fetched), S_FALSE);
    QCOMPARE(fetched, 0UL);
    QCOMPARE(children->Reset(), S_OK);
    QCOMPARE(children->Skip(2), S_OK);
    QCOMPARE(children->Next(1, &child, &fetched), S_FALSE);
    root.Reset();
    for (const auto &malformed : {QByteArray("bad"), QByteArray(17, 'x'), QByteArray(16, '\0')})
    {
        QVERIFY(writeSnapshot(malformed));
        QCOMPARE(factory->CreateInstance(nullptr, IID_PPV_ARGS(&root)), S_OK);
        QCOMPARE(root->GetFlags(&flags), S_OK);
        QCOMPARE(flags, ECF_DEFAULT);
        QCOMPARE(root->EnumSubCommands(&nested), E_NOTIMPL);
        root.Reset();
    }
}

QTEST_GUILESS_MAIN(ExplorerCommandTests)
#include "explorer_command_tests.moc"
