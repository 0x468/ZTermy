#include "platform/windows/WindowsTaskbarIdentity.h"

#include <QCoreApplication>
#include <QDir>
#include <QScopeGuard>
#include <QTest>

#include <shellapi.h>
#include <shobjidl.h>

// The SDK requires PROPERTYKEY declarations before its property-key constants.
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

class WindowsTaskbarIdentityTests final : public QObject
{
    Q_OBJECT
private slots:
    void overridesLauncherIdentityWithoutRetainingArguments()
    {
        QVERIFY(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
        const auto uninitialize = qScopeGuard([] {
            CoUninitialize();
        });
        QVERIFY(ztermy::windowing::initializeTaskbarIdentity());
        PWSTR id = nullptr;
        QVERIFY(SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&id)));
        QCOMPARE(QString::fromWCharArray(id), QString::fromWCharArray(ztermy::windowing::taskbarApplicationId));
        CoTaskMemFree(id);
        QVERIFY(!ztermy::windowing::configureTaskbarIdentity(nullptr));
        for (int index = 0; index < 2; ++index)
        {
            const HWND window = CreateWindowExW(0, L"STATIC", L"Taskbar identity regression", WS_OVERLAPPED, 0, 0, 100,
                                                100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            QVERIFY(window);
            const auto destroy = qScopeGuard([window] {
                DestroyWindow(window);
            });
            Microsoft::WRL::ComPtr<IPropertyStore> store;
            QVERIFY(SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store))));
            PROPVARIANT launcher{};
            QVERIFY(SUCCEEDED(InitPropVariantFromString(L"ZSeries.Ztermy.Explorer!Ztermy", &launcher)));
            QVERIFY(SUCCEEDED(store->SetValue(PKEY_AppUserModel_ID, launcher)));
            PropVariantClear(&launcher);
            QVERIFY(ztermy::windowing::configureTaskbarIdentity(window));
            QVERIFY(ztermy::windowing::hasTaskbarIdentity(window));
            const auto read = [&store](const PROPERTYKEY &key) {
                PROPVARIANT value{};
                if (FAILED(store->GetValue(key, &value)))
                    return QString{};
                const auto result = value.vt == VT_LPWSTR ? QString::fromWCharArray(value.pwszVal) : QString{};
                PropVariantClear(&value);
                return result;
            };
            const auto command = QStringLiteral("\"")
                                 + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
                                 + QStringLiteral("\"");
            QCOMPARE(read(PKEY_AppUserModel_ID), QStringLiteral("ZSeries.Ztermy"));
            QCOMPARE(read(PKEY_AppUserModel_RelaunchCommand), command);
            QCOMPARE(read(PKEY_AppUserModel_RelaunchIconResource), command + QStringLiteral(",0"));
            QCOMPARE(read(PKEY_AppUserModel_RelaunchDisplayNameResource), QStringLiteral("Ztermy"));
        }
    }
};

QTEST_GUILESS_MAIN(WindowsTaskbarIdentityTests)
#include "windows_taskbar_identity_tests.moc"
