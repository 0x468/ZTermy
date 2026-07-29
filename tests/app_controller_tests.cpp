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
    void rejectsIncompleteConnections();
};

void AppControllerTests::savesUpdatesReloadsAndDeletesProfiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));

    ztermy::AppController controller(path);
    QSignalSpy changes(&controller, &ztermy::AppController::hostProfilesChanged);
    QVERIFY(controller.hostProfiles().isEmpty());

    QVERIFY(controller.saveHostProfile({}, QStringLiteral("Lab"), QStringLiteral("server.example.test"), 22,
                                       QStringLiteral("developer"), QStringLiteral("private-key"),
                                       QStringLiteral(R"(C:\keys\id_ed25519)"), true, QStringLiteral("Development")));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(controller.hostProfiles().size(), 1);

    const QVariantMap saved = controller.hostProfiles().constFirst().toMap();
    const QString id = saved.value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());
    QCOMPARE(saved.value(QStringLiteral("name")).toString(), QStringLiteral("Lab"));
    QCOMPARE(saved.value(QStringLiteral("group")).toString(), QStringLiteral("Development"));
    QCOMPARE(saved.value(QStringLiteral("host")).toString(), QStringLiteral("server.example.test"));
    QCOMPARE(saved.value(QStringLiteral("port")).toInt(), 22);
    QCOMPARE(saved.value(QStringLiteral("authentication")).toString(), QStringLiteral("private-key"));
    QCOMPARE(saved.value(QStringLiteral("privateKeyPassphraseRequired")).toBool(), true);

    QVERIFY(controller.saveHostProfile(id, QStringLiteral("Updated"), QStringLiteral("new.example.test"), 2222,
                                       QStringLiteral("operator"), QStringLiteral("password"), {}, false,
                                       QStringLiteral("Production")));
    QCOMPARE(changes.count(), 2);
    QCOMPARE(controller.hostProfiles().size(), 1);
    QCOMPARE(controller.hostProfiles().constFirst().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Updated"));
    QCOMPARE(controller.hostProfiles().constFirst().toMap().value(QStringLiteral("authentication")).toString(),
             QStringLiteral("password"));
    QVERIFY(
        controller.hostProfiles().constFirst().toMap().value(QStringLiteral("privateKeyPath")).toString().isEmpty());
    QCOMPARE(controller.hostProfiles().constFirst().toMap().value(QStringLiteral("group")).toString(),
             QStringLiteral("Production"));

    QVERIFY(controller.duplicateHostProfile(id));
    QCOMPARE(changes.count(), 3);
    QCOMPARE(controller.hostProfiles().size(), 2);
    const QVariantMap duplicate = controller.hostProfiles().constLast().toMap();
    QVERIFY(duplicate.value(QStringLiteral("id")).toString() != id);
    QCOMPARE(duplicate.value(QStringLiteral("name")).toString(), QStringLiteral("Updated copy"));
    QCOMPARE(duplicate.value(QStringLiteral("group")).toString(), QStringLiteral("Production"));
    QVERIFY(controller.duplicateHostProfile(id));
    QCOMPARE(controller.hostProfiles().constLast().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Updated copy 2"));
    const QString duplicateId = duplicate.value(QStringLiteral("id")).toString();
    const QString secondDuplicateId =
        controller.hostProfiles().constLast().toMap().value(QStringLiteral("id")).toString();

    ztermy::AppController reloaded(path);
    QCOMPARE(reloaded.hostProfiles(), controller.hostProfiles());

    QVERIFY(reloaded.deleteHostProfile(id));
    QVERIFY(reloaded.deleteHostProfile(duplicateId));
    QVERIFY(reloaded.deleteHostProfile(secondDuplicateId));
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

    QVERIFY(!controller.saveHostProfile({}, {}, QStringLiteral("host"), 22, QStringLiteral("user"),
                                        QStringLiteral("private-key"), QStringLiteral("key"), false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), {}, 22, QStringLiteral("user"),
                                        QStringLiteral("private-key"), QStringLiteral("key"), false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 0, QStringLiteral("user"),
                                        QStringLiteral("private-key"), QStringLiteral("key"), false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 22, {},
                                        QStringLiteral("private-key"), {}, false, {}));
    QVERIFY(!controller.saveHostProfile({}, QStringLiteral("Name"), QStringLiteral("host"), 22, QStringLiteral("user"),
                                        QStringLiteral("agent"), {}, false, {}));
    QVERIFY(!controller.duplicateHostProfile(QStringLiteral("missing")));
    QCOMPARE(changes.count(), 0);
    QVERIFY(controller.hostProfiles().isEmpty());
}

void AppControllerTests::rejectsIncompleteConnections()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::AppController controller(directory.filePath(QStringLiteral("profiles.json")));

    QVERIFY(
        !controller.connectPrivateKey(QStringLiteral("host"), 22, QStringLiteral("   "), QStringLiteral("key"), {}));
    QVERIFY(
        !controller.connectPrivateKey(QStringLiteral("host"), 22, QStringLiteral("user"), QStringLiteral("   "), {}));
    QVERIFY(!controller.connectPassword(QStringLiteral("host"), 22, QStringLiteral("user"), {}));
    QVERIFY(!controller.connectHostProfile(QStringLiteral("missing"), QStringLiteral("unused")));
}

QTEST_GUILESS_MAIN(AppControllerTests)

#include "app_controller_tests.moc"
