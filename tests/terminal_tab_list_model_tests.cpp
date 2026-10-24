#include "ui/TerminalTabListModel.h"

#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QtTest>

class TerminalTabListModelTests : public QObject
{
    Q_OBJECT
private slots:
    void keepsRowsAcrossTitleAndProgressUpdates()
    {
        ztermy::ui::TerminalTabListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVariantMap first{{QStringLiteral("id"), QStringLiteral("a")},
                          {QStringLiteral("title"), QStringLiteral("first")}};
        QVariantMap second{{QStringLiteral("id"), QStringLiteral("b")}};
        model.setTabs({first, second});
        QPersistentModelIndex persistent(model.index(0));
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        for (int i = 0; i < 100; ++i)
        {
            first[QStringLiteral("title")] = QString::number(i);
            first[QStringLiteral("progressState")] = i % 4;
            model.setTabs({first, second});
        }
        QCOMPARE(reset.count(), 0);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(changed.count(), 100);
        QVERIFY(persistent.isValid());
        QCOMPARE(persistent.row(), 0);
        QCOMPARE(model.tabs().first().toMap(), first);
        model.setTabs({second, first});
        QCOMPARE(persistent.row(), 1);
        model.setTabs({first});
        QCOMPARE(persistent.row(), 0);
        model.setTabs({first, first});
        QCOMPARE(model.rowCount(), 1);
        model.setTabs({});
        QVERIFY(!persistent.isValid());
        QVERIFY(!model.data(model.index(0), ztermy::ui::TerminalTabListModel::ModelData).isValid());
    }
};
QTEST_GUILESS_MAIN(TerminalTabListModelTests)
#include "terminal_tab_list_model_tests.moc"
