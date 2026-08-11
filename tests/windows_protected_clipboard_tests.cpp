#include "platform/windows/WindowsProtectedClipboard.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTest>

#include <Windows.h>

#include <chrono>
#include <cstring>
#include <limits>
#include <memory>

using namespace std::chrono_literals;

namespace
{

constexpr auto excludeFromMonitorFormat = L"ExcludeClipboardContentFromMonitorProcessing";
constexpr auto excludeFromHistoryFormat = L"CanIncludeInClipboardHistory";
constexpr auto excludeFromCloudFormat = L"CanUploadToCloudClipboard";

[[nodiscard]] std::unique_ptr<QMimeData> cloneClipboardMimeData()
{
    auto clone = std::make_unique<QMimeData>();
    const QMimeData *source = QGuiApplication::clipboard()->mimeData(QClipboard::Clipboard);
    if (source == nullptr)
    {
        return clone;
    }
    for (const QString &format : source->formats())
    {
        clone->setData(format, source->data(format));
    }
    return clone;
}

[[nodiscard]] DWORD nativeClipboardFlag(const UINT format)
{
    if (OpenClipboard(nullptr) == FALSE)
    {
        return std::numeric_limits<DWORD>::max();
    }

    DWORD value = std::numeric_limits<DWORD>::max();
    if (const HANDLE data = GetClipboardData(format); data != nullptr)
    {
        if (const void *bytes = GlobalLock(data); bytes != nullptr)
        {
            std::memcpy(&value, bytes, sizeof(value));
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return value;
}

class WindowsProtectedClipboardTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void publishesWindowsProtectionFormats();
    void autoClearRemovesOnlyItsOwnClipboardItem();
    void autoClearPreservesNewerClipboardItem();

private:
    std::unique_ptr<QMimeData> m_originalClipboard;
};

void WindowsProtectedClipboardTests::initTestCase()
{
    m_originalClipboard = cloneClipboardMimeData();
}

void WindowsProtectedClipboardTests::cleanupTestCase()
{
    QGuiApplication::clipboard()->setMimeData(m_originalClipboard.release(), QClipboard::Clipboard);
}

void WindowsProtectedClipboardTests::publishesWindowsProtectionFormats()
{
    ztermy::windowing::WindowsProtectedClipboard clipboard;
    QVERIFY(clipboard.setProtectedText(QStringLiteral("protected assistant response")));
    QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::clipboard()->text(QClipboard::Clipboard),
                              QStringLiteral("protected assistant response"), 1000);

    const UINT monitorFormat = RegisterClipboardFormatW(excludeFromMonitorFormat);
    const UINT historyFormat = RegisterClipboardFormatW(excludeFromHistoryFormat);
    const UINT cloudFormat = RegisterClipboardFormatW(excludeFromCloudFormat);
    QVERIFY(monitorFormat != 0);
    QVERIFY(historyFormat != 0);
    QVERIFY(cloudFormat != 0);
    QTRY_VERIFY(IsClipboardFormatAvailable(monitorFormat) != FALSE);
    QTRY_VERIFY(IsClipboardFormatAvailable(historyFormat) != FALSE);
    QTRY_VERIFY(IsClipboardFormatAvailable(cloudFormat) != FALSE);
    QCOMPARE(nativeClipboardFlag(historyFormat), DWORD{0});
    QCOMPARE(nativeClipboardFlag(cloudFormat), DWORD{0});
}

void WindowsProtectedClipboardTests::autoClearRemovesOnlyItsOwnClipboardItem()
{
    ztermy::windowing::WindowsProtectedClipboard clipboard;
    QVERIFY(clipboard.setProtectedText(QStringLiteral("short lived"), 120ms));
    QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::clipboard()->text(QClipboard::Clipboard), QStringLiteral("short lived"),
                              1000);
    QTRY_VERIFY_WITH_TIMEOUT(QGuiApplication::clipboard()->text(QClipboard::Clipboard).isEmpty(), 1000);
}

void WindowsProtectedClipboardTests::autoClearPreservesNewerClipboardItem()
{
    ztermy::windowing::WindowsProtectedClipboard clipboard;
    QVERIFY(clipboard.setProtectedText(QStringLiteral("older protected value"), 180ms));
    QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::clipboard()->text(QClipboard::Clipboard),
                              QStringLiteral("older protected value"), 1000);
    QGuiApplication::clipboard()->setText(QStringLiteral("newer user value"), QClipboard::Clipboard);
    QTest::qWait(260);
    QCOMPARE(QGuiApplication::clipboard()->text(QClipboard::Clipboard), QStringLiteral("newer user value"));
}

} // namespace

QTEST_MAIN(WindowsProtectedClipboardTests)

#include "windows_protected_clipboard_tests.moc"
