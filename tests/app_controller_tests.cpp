#include "application/AppController.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

class AppControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void savesUpdatesReloadsAndDeletesProfiles();
    void rejectsInvalidProfiles();
};

void AppControllerTests::savesUpdatesReloadsAndDeletesProfiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));

    ztermy::AppController controller(path);
    QSignalSpy changes(&controller, &ztermy::AppController::hostProfilesChanged);
    QVERIFY(controller.hostProfiles().isEmpty());

    QVERIFY(controller.savePrivateKeyProfile({}, QStringLiteral("Lab"), QStringLiteral("server.example.test"), 22,
                                             QStringLiteral("developer"), QStringLiteral(R"(C:\keys\id_ed25519)")));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(controller.hostProfiles().size(), 1);

    const QVariantMap saved = controller.hostProfiles().constFirst().toMap();
    const QString id = saved.value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());
    QCOMPARE(saved.value(QStringLiteral("name")).toString(), QStringLiteral("Lab"));
    QCOMPARE(saved.value(QStringLiteral("host")).toString(), QStringLiteral("server.example.test"));
    QCOMPARE(saved.value(QStringLiteral("port")).toInt(), 22);
    QCOMPARE(saved.value(QStringLiteral("authentication")).toString(), QStringLiteral("private-key"));

    QVERIFY(controller.savePrivateKeyProfile(id, QStringLiteral("Updated"), QStringLiteral("new.example.test"), 2222,
                                             QStringLiteral("operator"), QStringLiteral(R"(C:\keys\updated)")));
    QCOMPARE(changes.count(), 2);
    QCOMPARE(controller.hostProfiles().size(), 1);
    QCOMPARE(controller.hostProfiles().constFirst().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Updated"));

    ztermy::AppController reloaded(path);
    QCOMPARE(reloaded.hostProfiles(), controller.hostProfiles());

    QVERIFY(reloaded.deleteHostProfile(id));
    QVERIFY(reloaded.hostProfiles().isEmpty());
    QVERIFY(!reloaded.deleteHostProfile(id));

    ztermy::AppController emptyReload(path);
    QVERIFY(emptyReload.hostProfiles().isEmpty());
}

void AppControllerTests::rejectsInvalidProfiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")));
    QSignalSpy changes(&controller, &ztermy::AppController::hostProfilesChanged);

    QVERIFY(!controller.savePrivateKeyProfile({}, {}, QStringLiteral("host"), 22, QStringLiteral("user"),
                                              QStringLiteral("key")));
    QVERIFY(!controller.savePrivateKeyProfile({}, QStringLiteral("Name"), {}, 22, QStringLiteral("user"),
                                              QStringLiteral("key")));
    QVERIFY(!controller.savePrivateKeyProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 0,
                                              QStringLiteral("user"), QStringLiteral("key")));
    QVERIFY(!controller.savePrivateKeyProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 22, {}, {}));
    QCOMPARE(changes.count(), 0);
    QVERIFY(controller.hostProfiles().isEmpty());
}

QTEST_GUILESS_MAIN(AppControllerTests)

#include "app_controller_tests.moc"
