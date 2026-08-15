#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>
#include <string>

namespace
{

void send(const QJsonObject &message)
{
    const QByteArray bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    std::cout.write(bytes.constData(), bytes.size());
    std::cout << '\n' << std::flush;
}

[[nodiscard]] QJsonObject response(const QJsonObject &request, const QJsonObject &result)
{
    return QJsonObject{{QStringLiteral("id"), request.value(QStringLiteral("id"))}, {QStringLiteral("result"), result}};
}

[[nodiscard]] bool writeSchema(const QString &path, const QJsonObject &schema)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(QJsonDocument(schema).toJson(QJsonDocument::Compact)) >= 0;
}

[[nodiscard]] int generateSchema(const QStringList &arguments)
{
    const qsizetype outputFlag = arguments.indexOf(QStringLiteral("--out"));
    if (outputFlag < 0 || outputFlag + 1 >= arguments.size())
    {
        return 4;
    }
    const QString &root = arguments.at(outputFlag + 1);
    if (!QDir().mkpath(root + QStringLiteral("/v2")))
    {
        return 5;
    }
    const QJsonObject thread{
        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("dynamicTools"),
                                                    QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}}}}};
    const QJsonObject call{{QStringLiteral("required"),
                            QJsonArray{QStringLiteral("arguments"), QStringLiteral("callId"),
                                       QStringLiteral("threadId"), QStringLiteral("tool"), QStringLiteral("turnId")}}};
    const QJsonObject toolResponse{
        {QStringLiteral("required"), QJsonArray{QStringLiteral("contentItems"), QStringLiteral("success")}}};
    return writeSchema(root + QStringLiteral("/v2/ThreadStartParams.json"), thread)
                   && writeSchema(root + QStringLiteral("/DynamicToolCallParams.json"), call)
                   && writeSchema(root + QStringLiteral("/DynamicToolCallResponse.json"), toolResponse)
               ? 0
               : 6;
}

} // namespace

int run(int argc, char **argv)
{
    const QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--version")))
    {
        std::cout << "codex-cli 999.0.0-test\n" << std::flush;
        return 0;
    }
    if (arguments.contains(QStringLiteral("generate-json-schema")))
    {
        return generateSchema(arguments);
    }
    const bool noTool = arguments.contains(QStringLiteral("--no-tool"));
    const QString toolName = qEnvironmentVariableIsSet("ZTERMY_TEST_CODEX_SESSION_INFO")
                                 ? QStringLiteral("read_session_info")
                                 : QStringLiteral("read_terminal_frame");
    bool resumedThread = false;
    std::string line;
    while (std::getline(std::cin, line))
    {
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(line));
        if (!document.isObject())
        {
            return 2;
        }
        const QJsonObject request = document.object();
        const QString method = request.value(QStringLiteral("method")).toString();
        if (method == QStringLiteral("initialize"))
        {
            send(response(request, QJsonObject{{QStringLiteral("codexHome"), QStringLiteral("C:/fake-codex")}}));
        }
        else if (method == QStringLiteral("initialized"))
        {
            continue;
        }
        else if (method == QStringLiteral("thread/start") || method == QStringLiteral("thread/resume"))
        {
            resumedThread = method == QStringLiteral("thread/resume");
            const QJsonObject params = request.value(QStringLiteral("params")).toObject();
            if (params.value(QStringLiteral("sandbox")).toString() != QStringLiteral("read-only")
                || params.value(QStringLiteral("approvalPolicy")).toString() != QStringLiteral("never")
                || params.value(QStringLiteral("dynamicTools")).toArray().isEmpty())
            {
                send(QJsonObject{{QStringLiteral("id"), request.value(QStringLiteral("id"))},
                                 {QStringLiteral("error"),
                                  QJsonObject{{QStringLiteral("message"), QStringLiteral("missing tool contract")}}}});
                continue;
            }
            const QString threadId = method == QStringLiteral("thread/resume")
                                         ? params.value(QStringLiteral("threadId")).toString()
                                         : QStringLiteral("thread-ztermy");
            send(response(request,
                          QJsonObject{{QStringLiteral("thread"), QJsonObject{{QStringLiteral("id"), threadId}}}}));
        }
        else if (method == QStringLiteral("turn/start"))
        {
            const QString threadId =
                request.value(QStringLiteral("params")).toObject().value(QStringLiteral("threadId")).toString();
            send(response(request, QJsonObject{{QStringLiteral("turn"),
                                                QJsonObject{{QStringLiteral("id"), QStringLiteral("turn-ztermy")}}}}));
            send(QJsonObject{{QStringLiteral("method"), QStringLiteral("turn/started")},
                             {QStringLiteral("params"),
                              QJsonObject{{QStringLiteral("threadId"), threadId},
                                          {QStringLiteral("turn"),
                                           QJsonObject{{QStringLiteral("id"), QStringLiteral("turn-ztermy")}}}}}});
            send(QJsonObject{
                {QStringLiteral("method"), QStringLiteral("item/agentMessage/delta")},
                {QStringLiteral("params"),
                 QJsonObject{{QStringLiteral("threadId"), threadId},
                             {QStringLiteral("turnId"), QStringLiteral("turn-ztermy")},
                             {QStringLiteral("itemId"), QStringLiteral("message-ztermy")},
                             {QStringLiteral("delta"), resumedThread ? QStringLiteral("Resumed the terminal inspection")
                                                                     : QStringLiteral("Inspecting the terminal")}}}});
            if (noTool)
            {
                continue;
            }
            send(QJsonObject{
                {QStringLiteral("method"), QStringLiteral("item/tool/call")},
                {QStringLiteral("id"), 700},
                {QStringLiteral("params"), QJsonObject{{QStringLiteral("threadId"), threadId},
                                                       {QStringLiteral("turnId"), QStringLiteral("turn-ztermy")},
                                                       {QStringLiteral("callId"), QStringLiteral("call-ztermy")},
                                                       {QStringLiteral("tool"), toolName},
                                                       {QStringLiteral("arguments"), QJsonObject{}}}}});
        }
        else if (method == QStringLiteral("turn/interrupt"))
        {
            const QString threadId =
                request.value(QStringLiteral("params")).toObject().value(QStringLiteral("threadId")).toString();
            send(response(request, QJsonObject{}));
            send(QJsonObject{{QStringLiteral("method"), QStringLiteral("turn/completed")},
                             {QStringLiteral("params"),
                              QJsonObject{{QStringLiteral("threadId"), threadId},
                                          {QStringLiteral("turn"),
                                           QJsonObject{{QStringLiteral("id"), QStringLiteral("turn-ztermy")},
                                                       {QStringLiteral("status"), QStringLiteral("interrupted")}}}}}});
        }
        else if (request.value(QStringLiteral("id")).toInt() == 700)
        {
            const QJsonObject result = request.value(QStringLiteral("result")).toObject();
            if (!result.value(QStringLiteral("success")).toBool())
            {
                return 3;
            }
            send(QJsonObject{{QStringLiteral("method"), QStringLiteral("turn/completed")},
                             {QStringLiteral("params"),
                              QJsonObject{{QStringLiteral("threadId"), QStringLiteral("thread-ztermy")},
                                          {QStringLiteral("turn"),
                                           QJsonObject{{QStringLiteral("id"), QStringLiteral("turn-ztermy")},
                                                       {QStringLiteral("status"), QStringLiteral("completed")}}}}}});
        }
    }
    return 0;
}

int main(int argc, char **argv) noexcept
{
    try
    {
        return run(argc, argv);
    }
    catch (...)
    {
        return 1;
    }
}
