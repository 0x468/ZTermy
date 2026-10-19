#include "ui/ApplicationInfo.h"
#include "ztermy_version.h"

#include <QRegularExpression>
#include <QSysInfo>
#include <QtTest>

class ApplicationInfoTests : public QObject
{
    Q_OBJECT

private slots:
    void usesRuntimePlatformAndCompiledRevision()
    {
        const ztermy::ui::ApplicationInfo info;
        QVERIFY(info.platformDescription().contains(QSysInfo::prettyProductName()));
        QVERIFY(info.platformDescription().contains(QSysInfo::kernelVersion()));
        QCOMPARE(info.buildCommit(), QStringLiteral(ZTERMY_BUILD_COMMIT));
        QVERIFY(info.buildCommit() == QStringLiteral("unknown")
                || QRegularExpression(QStringLiteral("^[0-9a-f]{7,40}$")).match(info.buildCommit()).hasMatch());
    }
};

QTEST_MAIN(ApplicationInfoTests)
#include "application_info_tests.moc"
