#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTest>

#include <windows.h>

#include <cstddef>
#include <vector>

namespace
{

constexpr WORD applicationIconResourceId = 101;

[[nodiscard]] QString executablePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ztermy.exe"));
}

[[nodiscard]] std::vector<std::byte> versionInformation(const QString &path)
{
    DWORD ignoredHandle = 0;
    const DWORD byteCount = GetFileVersionInfoSizeW(reinterpret_cast<LPCWSTR>(path.utf16()), &ignoredHandle);
    std::vector<std::byte> information(byteCount);
    if (byteCount == 0
        || GetFileVersionInfoW(reinterpret_cast<LPCWSTR>(path.utf16()), 0, byteCount, information.data()) == FALSE)
    {
        information.clear();
    }
    return information;
}

[[nodiscard]] QString versionString(const std::vector<std::byte> &information, const wchar_t *name)
{
    const QString query = QStringLiteral("\\StringFileInfo\\040904B0\\%1").arg(QString::fromWCharArray(name));
    void *value = nullptr;
    UINT characterCount = 0;
    if (VerQueryValueW(information.data(), reinterpret_cast<LPCWSTR>(query.utf16()), &value, &characterCount) == FALSE
        || value == nullptr || characterCount == 0)
    {
        return {};
    }
    return QString::fromWCharArray(static_cast<const wchar_t *>(value), static_cast<qsizetype>(characterCount - 1U));
}

} // namespace

class WindowsExecutableMetadataTests final : public QObject
{
    Q_OBJECT

private slots:
    void exposesVersionAndProductIdentity();
    void embedsApplicationIcon();
};

void WindowsExecutableMetadataTests::exposesVersionAndProductIdentity()
{
    const QString path = executablePath();
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    const std::vector<std::byte> information = versionInformation(path);
    QVERIFY(!information.empty());

    void *fixedValue = nullptr;
    UINT fixedByteCount = 0;
    QVERIFY(VerQueryValueW(information.data(), L"\\", &fixedValue, &fixedByteCount) != FALSE);
    QVERIFY(fixedValue != nullptr);
    QVERIFY(fixedByteCount >= sizeof(VS_FIXEDFILEINFO));
    const auto *fixed = static_cast<const VS_FIXEDFILEINFO *>(fixedValue);
    QCOMPARE(fixed->dwSignature, VS_FFI_SIGNATURE);
    QCOMPARE(HIWORD(fixed->dwFileVersionMS), static_cast<WORD>(ZTERMY_VERSION_MAJOR));
    QCOMPARE(LOWORD(fixed->dwFileVersionMS), static_cast<WORD>(ZTERMY_VERSION_MINOR));
    QCOMPARE(HIWORD(fixed->dwFileVersionLS), static_cast<WORD>(ZTERMY_VERSION_PATCH));
    QCOMPARE(LOWORD(fixed->dwFileVersionLS), static_cast<WORD>(0));

    QCOMPARE(versionString(information, L"CompanyName"), QStringLiteral("ztermy"));
    QCOMPARE(versionString(information, L"FileDescription"), QStringLiteral("ztermy native SSH terminal"));
    QCOMPARE(versionString(information, L"FileVersion"), QStringLiteral(ZTERMY_VERSION_STRING));
    QCOMPARE(versionString(information, L"InternalName"), QStringLiteral("ztermy"));
    QCOMPARE(versionString(information, L"OriginalFilename"), QStringLiteral("ztermy.exe"));
    QCOMPARE(versionString(information, L"ProductName"), QStringLiteral("ztermy"));
    QCOMPARE(versionString(information, L"ProductVersion"), QStringLiteral(ZTERMY_VERSION_STRING));
}

void WindowsExecutableMetadataTests::embedsApplicationIcon()
{
    const QString path = executablePath();
    const HMODULE module = LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
                                          LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    QVERIFY(module != nullptr);

    const auto icon = static_cast<HICON>(
        LoadImageW(module, MAKEINTRESOURCEW(applicationIconResourceId), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    QVERIFY(icon != nullptr);

    QVERIFY(DestroyIcon(icon) != FALSE);
    QVERIFY(FreeLibrary(module) != FALSE);
}

QTEST_APPLESS_MAIN(WindowsExecutableMetadataTests)

#include "windows_executable_metadata_tests.moc"
