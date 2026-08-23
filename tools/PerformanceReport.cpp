#include "application/diagnostics/PerformanceEvidence.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>
#include <expected>

namespace
{

[[nodiscard]] std::expected<ztermy::diagnostics::PerformanceEvidence, QString> loadEvidence(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::unexpected(QStringLiteral("Could not read %1").arg(QFileInfo(path).absoluteFilePath()));
    }
    return ztermy::diagnostics::PerformanceEvidence::parse(file.readAll());
}

void printIssues(QTextStream &stream, const QStringList &issues)
{
    for (const QString &issue : issues)
    {
        stream << "- " << issue << '\n';
    }
}

[[nodiscard]] int validate(const QString &path, QTextStream &output, QTextStream &error)
{
    const auto evidence = loadEvidence(path);
    if (!evidence)
    {
        error << evidence.error() << '\n';
        return EXIT_FAILURE;
    }
    const QStringList issues = evidence->validationIssues();
    if (!issues.isEmpty())
    {
        error << "Invalid performance evidence:\n";
        printIssues(error, issues);
        return EXIT_FAILURE;
    }
    output << "Valid performance evidence: " << QFileInfo(path).absoluteFilePath() << '\n';
    return EXIT_SUCCESS;
}

[[nodiscard]] int compare(const QString &baselinePath, const QString &candidatePath, const QString &outputPath,
                          QTextStream &output, QTextStream &error)
{
    const auto baseline = loadEvidence(baselinePath);
    const auto candidate = loadEvidence(candidatePath);
    if (!baseline || !candidate)
    {
        error << (!baseline ? baseline.error() : candidate.error()) << '\n';
        return EXIT_FAILURE;
    }
    const QStringList baselineIssues = baseline->validationIssues();
    const QStringList candidateIssues = candidate->validationIssues();
    const QStringList comparisonIssues = baseline->comparabilityIssues(*candidate);
    if (!baselineIssues.isEmpty() || !candidateIssues.isEmpty() || !comparisonIssues.isEmpty())
    {
        if (!baselineIssues.isEmpty())
        {
            error << "Invalid baseline:\n";
            printIssues(error, baselineIssues);
        }
        if (!candidateIssues.isEmpty())
        {
            error << "Invalid candidate:\n";
            printIssues(error, candidateIssues);
        }
        if (!comparisonIssues.isEmpty())
        {
            error << "Reports are not comparable:\n";
            printIssues(error, comparisonIssues);
        }
        return EXIT_FAILURE;
    }

    const QString markdown = baseline->comparisonMarkdown(*candidate);
    if (outputPath.isEmpty())
    {
        output << markdown;
        return EXIT_SUCCESS;
    }
    QSaveFile file(outputPath);
    const QByteArray bytes = markdown.toUtf8();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size() || !file.commit())
    {
        error << "Could not write " << QFileInfo(outputPath).absoluteFilePath() << '\n';
        return EXIT_FAILURE;
    }
    output << "Wrote comparison: " << QFileInfo(outputPath).absoluteFilePath() << '\n';
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    QTextStream error(stderr);
    const QStringList arguments = application.arguments();
    if (arguments.size() == 3 && arguments[1] == QStringLiteral("--validate"))
    {
        return validate(arguments[2], output, error);
    }
    if ((arguments.size() == 4 || arguments.size() == 6) && arguments[1] == QStringLiteral("--compare"))
    {
        QString outputPath;
        if (arguments.size() == 6)
        {
            if (arguments[4] != QStringLiteral("--output"))
            {
                error << "Expected --output before the report path.\n";
                return EXIT_FAILURE;
            }
            outputPath = arguments[5];
        }
        return compare(arguments[2], arguments[3], outputPath, output, error);
    }

    error << "Usage:\n"
          << "  ztermy_performance_report --validate <report.json>\n"
          << "  ztermy_performance_report --compare <baseline.json> <candidate.json> [--output <report.md>]\n";
    return EXIT_FAILURE;
}
