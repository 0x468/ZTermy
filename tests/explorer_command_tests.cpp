#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <windows.h>
#include <shlobj.h>
#include <wrl.h>

class ExplorerCommandTests final : public QObject
{
    Q_OBJECT
private slots:
    void nativeComLifetimeAndSelectionContract();
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

QTEST_GUILESS_MAIN(ExplorerCommandTests)
#include "explorer_command_tests.moc"
