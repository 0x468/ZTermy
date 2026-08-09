#include "domain/sftp/TransferPlanner.h"

#include <QTest>

#include <stop_token>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

class MemoryTransferSource final : public ztermy::sftp::TransferSourceTree
{
public:
    std::unordered_map<std::string, ztermy::sftp::TransferSourceNode> nodes;
    std::unordered_map<std::string, std::vector<ztermy::sftp::TransferSourceNode>> children;

    std::expected<ztermy::sftp::TransferSourceNode, ztermy::sftp::TransferSourceError>
    stat(const std::string_view sourcePath, const std::stop_token &stopToken) override
    {
        if (stopToken.stop_requested())
        {
            return std::unexpected(ztermy::sftp::TransferSourceError{.code = "cancelled"});
        }
        const auto found = nodes.find(std::string(sourcePath));
        if (found == nodes.end())
        {
            return std::unexpected(ztermy::sftp::TransferSourceError{.code = "not-found"});
        }
        return found->second;
    }

    std::expected<std::vector<ztermy::sftp::TransferSourceNode>, ztermy::sftp::TransferSourceError>
    list(const std::string_view sourcePath, const std::stop_token &stopToken) override
    {
        if (stopToken.stop_requested())
        {
            return std::unexpected(ztermy::sftp::TransferSourceError{.code = "cancelled"});
        }
        const auto found = children.find(std::string(sourcePath));
        if (found == children.end())
        {
            return std::vector<ztermy::sftp::TransferSourceNode>{};
        }
        return found->second;
    }
};

[[nodiscard]] ztermy::sftp::TransferPlanRequest request(std::vector<std::string> roots = {"/srv/project"})
{
    return {.batchId = "batch-1",
            .endpointId = "profile-1",
            .displayName = "Download selection",
            .destinationRoot = R"(C:\Downloads)",
            .sourceRoots = std::move(roots),
            .direction = ztermy::sftp::TransferBatchDirection::Download};
}

} // namespace

class TransferPlannerTests final : public QObject
{
    Q_OBJECT

private slots:
    void plansStableParentFirstTreeAndSkipsLinks();
    void rejectsDuplicateRootNamesAndInvalidChildren();
    void honorsCancellationAndDepthLimit();
};

void TransferPlannerTests::plansStableParentFirstTreeAndSkipsLinks()
{
    using namespace ztermy::sftp;
    MemoryTransferSource source;
    source.nodes.emplace(
        "/srv/project",
        TransferSourceNode{.sourcePath = "/srv/project", .name = "project", .type = EntryType::Directory});
    source.children["/srv/project"] = {
        {.sourcePath = "/srv/project/z.txt", .name = "z.txt", .type = EntryType::RegularFile, .size = 7},
        {.sourcePath = "/srv/project/empty", .name = "empty", .type = EntryType::Directory},
        {.sourcePath = "/srv/project/latest", .name = "latest", .type = EntryType::SymbolicLink},
        {.sourcePath = "/srv/project/a.txt",
         .name = "a.txt",
         .type = EntryType::RegularFile,
         .size = 5,
         .modifiedUtcSeconds = 123},
    };

    auto planned = planTransferTree(request(), source);
    QVERIFY(planned.has_value());
    QCOMPARE(planned->status, TransferBatchStatus::Ready);
    QCOMPARE(planned->entries.size(), 5U);
    QCOMPARE(planned->entries.at(0).relativePath, "project");
    QCOMPARE(planned->entries.at(1).relativePath, "project/a.txt");
    QCOMPARE(planned->entries.at(2).relativePath, "project/empty");
    QCOMPARE(planned->entries.at(3).relativePath, "project/latest");
    QCOMPARE(planned->entries.at(4).relativePath, "project/z.txt");
    QCOMPARE(planned->entries.at(1).sourceModifiedUtcSeconds, std::optional<std::int64_t>{123});
    QCOMPARE(planned->entries.at(3).status, TransferPlanEntryStatus::Skipped);
    QCOMPARE(summarizeTransferBatch(*planned).totalBytes, 12U);
    QVERIFY(validTransferBatch(*planned));
}

void TransferPlannerTests::rejectsDuplicateRootNamesAndInvalidChildren()
{
    using namespace ztermy::sftp;
    MemoryTransferSource duplicate;
    duplicate.nodes.emplace(
        "/one/data", TransferSourceNode{.sourcePath = "/one/data", .name = "data", .type = EntryType::Directory});
    duplicate.nodes.emplace(
        "/two/data", TransferSourceNode{.sourcePath = "/two/data", .name = "data", .type = EntryType::Directory});
    auto duplicatePlan = planTransferTree(request({"/one/data", "/two/data"}), duplicate);
    QVERIFY(!duplicatePlan.has_value());
    QCOMPARE(duplicatePlan.error(), TransferPlanningError::DuplicateDestination);

    MemoryTransferSource invalid;
    invalid.nodes.emplace(
        "/srv/project",
        TransferSourceNode{.sourcePath = "/srv/project", .name = "project", .type = EntryType::Directory});
    invalid.children["/srv/project"] = {
        {.sourcePath = "/srv/escape", .name = "../escape", .type = EntryType::RegularFile, .size = 1}};
    auto invalidPlan = planTransferTree(request(), invalid);
    QVERIFY(!invalidPlan.has_value());
    QCOMPARE(invalidPlan.error(), TransferPlanningError::InvalidSource);
}

void TransferPlannerTests::honorsCancellationAndDepthLimit()
{
    using namespace ztermy::sftp;
    MemoryTransferSource source;
    source.nodes.emplace("/root",
                         TransferSourceNode{.sourcePath = "/root", .name = "root", .type = EntryType::Directory});
    std::string parent = "/root";
    for (std::uint32_t depth = 1; depth <= maximumTransferTreeDepth + 1; ++depth)
    {
        const std::string name = "d" + std::to_string(depth);
        std::string path = parent;
        path += '/';
        path += name;
        source.children[parent] = {{.sourcePath = path, .name = name, .type = EntryType::Directory}};
        parent = path;
    }
    auto deep = planTransferTree(request({"/root"}), source);
    QVERIFY(!deep.has_value());
    QCOMPARE(deep.error(), TransferPlanningError::DepthLimit);

    std::stop_source stopped;
    stopped.request_stop();
    auto cancelled = planTransferTree(request({"/root"}), source, stopped.get_token());
    QVERIFY(!cancelled.has_value());
    QCOMPARE(cancelled.error(), TransferPlanningError::Cancelled);
}

QTEST_GUILESS_MAIN(TransferPlannerTests)

#include "transfer_planner_tests.moc"
