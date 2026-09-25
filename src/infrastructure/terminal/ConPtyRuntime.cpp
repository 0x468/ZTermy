#include "infrastructure/terminal/ConPtyRuntime.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QResource>
#include <QSaveFile>
#include <QStandardPaths>

#include <array>

static void initializeConPtyResources()
{
    Q_INIT_RESOURCE(ztermy_conpty_runtime);
}

namespace ztermy::terminal
{
namespace
{
constexpr auto runtimeVersion = "1.24.260710001-x64";

bool installFile(const QString &directory, const QString &name)
{
    QFile resource(QStringLiteral(":/ztermy/conpty/") + name);
    if (!resource.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = resource.readAll();
    const QByteArray expectedHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    QFile existing(directory + u'/' + name);
    if (existing.open(QIODevice::ReadOnly))
    {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (existing.size() == bytes.size() && hash.addData(&existing) && hash.result() == expectedHash)
            return true;
        existing.close();
    }
    QSaveFile output(existing.fileName());
    output.setDirectWriteFallback(false);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}

struct Runtime final
{
    ConPtyApi api;
    HMODULE module = nullptr;
    std::array<HANDLE, 2> pinnedFiles{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};

    Runtime()
    {
        initializeConPtyResources();
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
        if (cache.isEmpty())
        {
            api.error = std::make_error_code(std::errc::no_such_file_or_directory);
            return;
        }
        const QString directory = cache + QStringLiteral("/ztermy/conpty/") + QString::fromLatin1(runtimeVersion);
        if (!QDir().mkpath(directory))
        {
            api.error = std::make_error_code(std::errc::permission_denied);
            return;
        }
        QLockFile lock(directory + QStringLiteral("/extract.lock"));
        if (!lock.tryLock(5'000))
        {
            api.error = std::make_error_code(std::errc::resource_unavailable_try_again);
            return;
        }
        const std::array names{QStringLiteral("conpty.dll"), QStringLiteral("OpenConsole.exe")};
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (!installFile(directory, names[i]))
            {
                api.error = std::make_error_code(std::errc::io_error);
                return;
            }
            const auto path = QDir::toNativeSeparators(directory + u'/' + names[i]).toStdWString();
            // Keep executable contents immutable while this process uses them.
            pinnedFiles[i] = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
            if (pinnedFiles[i] == INVALID_HANDLE_VALUE)
            {
                api.error = {static_cast<int>(GetLastError()), std::system_category()};
                return;
            }
        }
        if (!installFile(directory, QStringLiteral("NOTICE.txt")))
        {
            api.error = std::make_error_code(std::errc::io_error);
            return;
        }
        const auto dll = QDir::toNativeSeparators(directory + QStringLiteral("/conpty.dll")).toStdWString();
        module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module)
        {
            api.error = {static_cast<int>(GetLastError()), std::system_category()};
            return;
        }
        api.create = reinterpret_cast<ConPtyApi::Create>(GetProcAddress(module, "ConptyCreatePseudoConsole"));
        api.resize = reinterpret_cast<ConPtyApi::Resize>(GetProcAddress(module, "ConptyResizePseudoConsole"));
        api.close = reinterpret_cast<ConPtyApi::Close>(GetProcAddress(module, "ConptyClosePseudoConsole"));
        if (!api.create || !api.resize || !api.close)
            api.error = std::make_error_code(std::errc::function_not_supported);
    }

    ~Runtime()
    {
        if (module)
            FreeLibrary(module);
        for (const HANDLE file : pinnedFiles)
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
    }
};
} // namespace

const ConPtyApi &conPtyApi()
{
    static const Runtime runtime;
    return runtime.api;
}
} // namespace ztermy::terminal
