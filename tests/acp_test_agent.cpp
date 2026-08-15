#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>
#include <optional>
#include <string>

namespace
{

void writeObject(QJsonObject object)
{
    object.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    const QByteArray encoded = QJsonDocument(object).toJson(QJsonDocument::Compact);
    std::cout << encoded.constData() << '\n' << std::flush;
}

void writeResponse(const QJsonValue &id, const QJsonValue &result)
{
    writeObject(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
}

void writeUpdate(const QString &sessionId, QJsonObject update)
{
    writeObject(QJsonObject{{QStringLiteral("method"), QStringLiteral("session/update")},
                            {QStringLiteral("params"), QJsonObject{{QStringLiteral("sessionId"), sessionId},
                                                                   {QStringLiteral("update"), std::move(update)}}}});
}

[[nodiscard]] bool hasArgument(const QStringList &arguments, const QString &argument)
{
    return arguments.contains(argument);
}

} // namespace

int run(int &argc, char **argv)
{
    const QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const bool cancelMode = hasArgument(arguments, QStringLiteral("--cancel"));
    const bool foreignUpdate = hasArgument(arguments, QStringLiteral("--foreign-update"));
    const bool duplicateRequest = hasArgument(arguments, QStringLiteral("--duplicate-request"));
    QString sessionId = QStringLiteral("session-ztermy");
    std::optional<QJsonValue> promptId;
    int pendingToolResponses = 0;

    std::string line;
    while (std::getline(std::cin, line))
    {
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(line));
        if (!document.isObject())
        {
            return 2;
        }
        const QJsonObject message = document.object();
        const QJsonValue id = message.value(QStringLiteral("id"));
        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();

        if (method == QStringLiteral("initialize"))
        {
            writeResponse(id, QJsonObject{{QStringLiteral("protocolVersion"), 1},
                                          {QStringLiteral("agentCapabilities"),
                                           QJsonObject{{QStringLiteral("loadSession"), true},
                                                       {QStringLiteral("sessionCapabilities"),
                                                        QJsonObject{{QStringLiteral("resume"), true},
                                                                    {QStringLiteral("close"), true}}},
                                                       {QStringLiteral("promptCapabilities"), QJsonObject{}}}},
                                          {QStringLiteral("agentInfo"),
                                           QJsonObject{{QStringLiteral("name"), QStringLiteral("ztermy-test-agent")},
                                                       {QStringLiteral("version"), QStringLiteral("1.0")}}}});
            continue;
        }
        if (method == QStringLiteral("session/new"))
        {
            writeResponse(id, QJsonObject{{QStringLiteral("sessionId"), sessionId},
                                          {QStringLiteral("configOptions"),
                                           QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("model")}}}},
                                          {QStringLiteral("availableCommands"), QJsonArray{}}});
            continue;
        }
        if (method == QStringLiteral("session/resume"))
        {
            sessionId = params.value(QStringLiteral("sessionId")).toString();
            writeResponse(id, QJsonObject{});
            continue;
        }
        if (method == QStringLiteral("session/prompt"))
        {
            promptId = id;
            if (foreignUpdate)
            {
                writeUpdate(QStringLiteral("foreign-session"),
                            QJsonObject{{QStringLiteral("sessionUpdate"), QStringLiteral("agent_message_chunk")},
                                        {QStringLiteral("content"),
                                         QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                     {QStringLiteral("text"), QStringLiteral("wrong")}}}});
                continue;
            }
            if (cancelMode)
            {
                writeUpdate(sessionId,
                            QJsonObject{{QStringLiteral("sessionUpdate"), QStringLiteral("agent_message_chunk")},
                                        {QStringLiteral("content"),
                                         QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                     {QStringLiteral("text"), QStringLiteral("Waiting")}}}});
                continue;
            }

            writeUpdate(sessionId, QJsonObject{{QStringLiteral("sessionUpdate"), QStringLiteral("agent_thought_chunk")},
                                               {QStringLiteral("content"),
                                                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                            {QStringLiteral("text"), QStringLiteral("Inspecting")}}}});
            writeUpdate(sessionId, QJsonObject{{QStringLiteral("sessionUpdate"), QStringLiteral("agent_message_chunk")},
                                               {QStringLiteral("content"),
                                                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                            {QStringLiteral("text"), QStringLiteral("Done")}}}});
            writeUpdate(sessionId, QJsonObject{{QStringLiteral("sessionUpdate"), QStringLiteral("tool_call")},
                                               {QStringLiteral("toolCallId"), QStringLiteral("tool-1")},
                                               {QStringLiteral("title"), QStringLiteral("Inspect current terminal")},
                                               {QStringLiteral("kind"), QStringLiteral("execute")},
                                               {QStringLiteral("status"), QStringLiteral("in_progress")},
                                               {QStringLiteral("rawInput"),
                                                QJsonObject{{QStringLiteral("command"), QStringLiteral("pwd")}}}});
            const QJsonObject request{
                {QStringLiteral("id"), QStringLiteral("terminal-request-1")},
                {QStringLiteral("method"), QStringLiteral("terminal/create")},
                {QStringLiteral("params"), QJsonObject{{QStringLiteral("sessionId"), sessionId},
                                                       {QStringLiteral("command"), QStringLiteral("pwd")}}}};
            writeObject(request);
            if (duplicateRequest)
            {
                writeObject(request);
            }
            pendingToolResponses = duplicateRequest ? 2 : 1;
            continue;
        }
        if (method == QStringLiteral("session/cancel"))
        {
            if (promptId.has_value())
            {
                writeResponse(*promptId, QJsonObject{{QStringLiteral("stopReason"), QStringLiteral("cancelled")}});
            }
            continue;
        }
        if (method == QStringLiteral("session/close"))
        {
            writeResponse(id, QJsonObject{});
            return 0;
        }
        if (method.isEmpty() && id.toString() == QStringLiteral("terminal-request-1"))
        {
            --pendingToolResponses;
            if (pendingToolResponses == 0 && promptId.has_value())
            {
                writeUpdate(sessionId,
                            QJsonObject{{QStringLiteral("sessionUpdate"), QStringLiteral("tool_call_update")},
                                        {QStringLiteral("toolCallId"), QStringLiteral("tool-1")},
                                        {QStringLiteral("status"), QStringLiteral("completed")},
                                        {QStringLiteral("rawOutput"), QJsonObject{{QStringLiteral("exitCode"), 0}}}});
                writeUpdate(sessionId, QJsonObject{{QStringLiteral("sessionUpdate"), QStringLiteral("usage_update")},
                                                   {QStringLiteral("used"), 100},
                                                   {QStringLiteral("size"), 1000},
                                                   {QStringLiteral("cost"),
                                                    QJsonObject{{QStringLiteral("amount"), 0.01},
                                                                {QStringLiteral("currency"), QStringLiteral("USD")}}}});
                writeResponse(*promptId, QJsonObject{{QStringLiteral("stopReason"), QStringLiteral("end_turn")}});
            }
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
        return 3;
    }
}
