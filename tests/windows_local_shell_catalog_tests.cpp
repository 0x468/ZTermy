#include "application/terminal/WindowsLocalShellCatalog.h"

#include <QtTest/QTest>

class WindowsLocalShellCatalogTests final : public QObject
{
    Q_OBJECT

private slots:
    void resolvesAutomaticInStableOrder();
    void fallsBackWithoutOverwritingPreference();
    void createsLaunchSpec();
    void resolvesNushellAndWslProfiles();
    void detectsStableCatalogEntriesWithoutDuplicateIds();
};

void WindowsLocalShellCatalogTests::resolvesAutomaticInStableOrder()
{
    const QList<ztermy::terminal::LocalShellProfile> profiles{
        {.id = QStringLiteral("powerShellCore"), .name = QStringLiteral("PowerShell 7"), .available = false},
        {.id = QStringLiteral("windowsPowerShell"),
         .name = QStringLiteral("Windows PowerShell"),
         .executable = QStringLiteral("C:/Windows/powershell.exe"),
         .available = true},
        {.id = QStringLiteral("commandPrompt"),
         .name = QStringLiteral("Command Prompt"),
         .executable = QStringLiteral("C:/Windows/cmd.exe"),
         .available = true},
    };
    const auto resolved = ztermy::terminal::WindowsLocalShellCatalog::resolve(profiles, QStringLiteral("automatic"));
    if (!resolved)
    {
        QTest::qFail("Automatic shell resolution returned no profile", __FILE__, __LINE__);
        return;
    }
    QCOMPARE(resolved->id, QStringLiteral("windowsPowerShell"));
}

void WindowsLocalShellCatalogTests::fallsBackWithoutOverwritingPreference()
{
    const QList<ztermy::terminal::LocalShellProfile> profiles{
        {.id = QStringLiteral("powerShellCore"),
         .name = QStringLiteral("PowerShell 7"),
         .executable = QStringLiteral("C:/PowerShell/pwsh.exe"),
         .available = true},
        {.id = QStringLiteral("gitBash"), .name = QStringLiteral("Git Bash"), .available = false},
    };
    const auto resolved = ztermy::terminal::WindowsLocalShellCatalog::resolve(profiles, QStringLiteral("gitBash"));
    if (!resolved)
    {
        QTest::qFail("Unavailable shell fallback returned no profile", __FILE__, __LINE__);
        return;
    }
    QCOMPARE(resolved->id, QStringLiteral("powerShellCore"));
}

void WindowsLocalShellCatalogTests::createsLaunchSpec()
{
    const ztermy::terminal::LocalShellProfile profile{
        .id = QStringLiteral("gitBash"),
        .name = QStringLiteral("Git Bash"),
        .executable = QStringLiteral("C:/Program Files/Git/bin/bash.exe"),
        .arguments = {QStringLiteral("--login"), QStringLiteral("-i")},
        .available = true,
    };
    const auto spec =
        ztermy::terminal::WindowsLocalShellCatalog::launchSpec(profile, QStringLiteral("D:/Repo/Qt/ztermy"));
    QCOMPARE(spec.id, profile.id);
    QCOMPARE(spec.executable, profile.executable);
    QCOMPARE(spec.arguments, profile.arguments);
    QCOMPARE(spec.workingDirectory, QStringLiteral("D:/Repo/Qt/ztermy"));
    QVERIFY(!spec.powerShellIntegration);
}

void WindowsLocalShellCatalogTests::resolvesNushellAndWslProfiles()
{
    const QList<ztermy::terminal::LocalShellProfile> profiles{
        {.id = QStringLiteral("nushell"),
         .name = QStringLiteral("Nushell"),
         .executable = QStringLiteral("C:/Tools/nu.exe"),
         .available = true},
        {.id = QStringLiteral("wsl"),
         .name = QStringLiteral("WSL · Debian"),
         .executable = QStringLiteral("C:/Windows/System32/wsl.exe"),
         .arguments = {QStringLiteral("~")},
         .available = true},
    };
    const auto nushell = ztermy::terminal::WindowsLocalShellCatalog::resolve(profiles, QStringLiteral("nushell"));
    QVERIFY(nushell);
    if (!nushell)
        return;
    QCOMPARE(nushell->id, QStringLiteral("nushell"));
    const auto wsl = ztermy::terminal::WindowsLocalShellCatalog::resolve(profiles, QStringLiteral("wsl"));
    QVERIFY(wsl);
    if (!wsl)
        return;
    QCOMPARE(wsl->arguments, QStringList{QStringLiteral("~")});
}

void WindowsLocalShellCatalogTests::detectsStableCatalogEntriesWithoutDuplicateIds()
{
    const QList<ztermy::terminal::LocalShellProfile> profiles = ztermy::terminal::WindowsLocalShellCatalog::detect();
    QCOMPARE(profiles.size(), 6);
    QSet<QString> ids;
    for (const ztermy::terminal::LocalShellProfile &profile : profiles)
    {
        QVERIFY(!profile.id.isEmpty());
        QVERIFY(!profile.name.isEmpty());
        QVERIFY(!ids.contains(profile.id));
        ids.insert(profile.id);
    }
    QVERIFY(ids.contains(QStringLiteral("nushell")));
    QVERIFY(ids.contains(QStringLiteral("wsl")));
}

QTEST_GUILESS_MAIN(WindowsLocalShellCatalogTests)

#include "windows_local_shell_catalog_tests.moc"
