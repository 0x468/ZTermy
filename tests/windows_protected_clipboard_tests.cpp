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
#include <optional>

using namespace std::chrono_literals;

namespace
{

constexpr auto excludeFromMonitorFormat = L"ExcludeClipboardContentFromMonitorProcessing";
constexpr auto excludeFromHistoryFormat = L"CanIncludeInClipboardHistory";
constexpr auto excludeFromCloudFormat = L"CanUploadToCloudClipboard";
constexpr int clipboardReadAttempts = 40;
constexpr DWORD clipboardReadRetryDelayMilliseconds = 5;

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
    for (int attempt = 0; attempt < clipboardReadAttempts; ++attempt)
    {
        if (OpenClipboard(nullptr) != FALSE)
        {
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

        if (attempt + 1 < clipboardReadAttempts)
        {
            Sleep(clipboardReadRetryDelayMilliseconds);
        }
    }
    return std::numeric_limits<DWORD>::max();
}

[[nodiscard]] std::optional<QString> nativeClipboardText()
{
    for (int attempt = 0; attempt < clipboardReadAttempts; ++attempt)
    {
        if (OpenClipboard(nullptr) != FALSE)
        {
            QString value;
            if (const HANDLE data = GetClipboardData(CF_UNICODETEXT); data != nullptr)
            {
                if (const auto *text = static_cast<const wchar_t *>(GlobalLock(data)); text != nullptr)
                {
                    value = QString::fromWCharArray(text);
                    GlobalUnlock(data);
                }
            }
            CloseClipboard();
            return value;
        }

        if (attempt + 1 < clipboardReadAttempts)
        {
            Sleep(clipboardReadRetryDelayMilliseconds);
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool nativeClipboardTextEquals(const QString &expected)
{
    const std::optional<QString> actual = nativeClipboardText();
    return actual.has_value() && *actual == expected;
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
    QTRY_VERIFY_WITH_TIMEOUT(nativeClipboardTextEquals(QStringLiteral("protected assistant response")), 2000);

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
    QVERIFY(clipboard.setProtectedText(QStringLiteral("short lived"), 500ms));
    QTRY_VERIFY_WITH_TIMEOUT(nativeClipboardTextEquals(QStringLiteral("short lived")), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(nativeClipboardTextEquals(QString{}), 3000);
}

void WindowsProtectedClipboardTests::autoClearPreservesNewerClipboardItem()
{
    ztermy::windowing::WindowsProtectedClipboard clipboard;
    QVERIFY(clipboard.setProtectedText(QStringLiteral("older protected value"), 500ms));
    QTRY_VERIFY_WITH_TIMEOUT(nativeClipboardTextEquals(QStringLiteral("older protected value")), 2000);
    QGuiApplication::clipboard()->setText(QStringLiteral("newer user value"), QClipboard::Clipboard);
    QTRY_VERIFY_WITH_TIMEOUT(nativeClipboardTextEquals(QStringLiteral("newer user value")), 2000);
    QTest::qWait(700);
    QVERIFY(nativeClipboardTextEquals(QStringLiteral("newer user value")));
}

} // namespace

QTEST_MAIN(WindowsProtectedClipboardTests)

#include "windows_protected_clipboard_tests.moc"
