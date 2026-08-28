#include "application/terminal/WindowsLocalShellCatalog.h"

#include <QtTest/QTest>

class WindowsLocalShellCatalogTests final : public QObject
{
    Q_OBJECT

private slots:
    void resolvesAutomaticInStableOrder();
    void fallsBackWithoutOverwritingPreference();
    void createsLaunchSpec();
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

QTEST_GUILESS_MAIN(WindowsLocalShellCatalogTests)

#include "windows_local_shell_catalog_tests.moc"
