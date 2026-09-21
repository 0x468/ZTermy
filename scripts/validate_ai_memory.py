"""Check the synthetic three-turn AI memory regression using its native lifecycle trace."""
import argparse
import json
from pathlib import Path


def total(counts, prefix, suffix):
    return sum(value for key, value in counts.items() if key.startswith(prefix) and key.endswith(suffix))


def check(folder, expected_failure):
    result = json.loads((folder / "result.json").read_text(encoding="utf-8-sig"))
    lines = (folder / "data/ai-objects.jsonl").read_text(encoding="utf-8").splitlines()
    records = []
    for index, line in enumerate(lines):
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            if not expected_failure or index != len(lines) - 1:
                raise
    assert records, f"{folder}: missing lifecycle samples"
    assert not result["survivors"], f"{folder}: owned process survived"
    assert not any(r.get("writeFailed") or r.get("droppedSnapshots", 0) for r in records), "Invalid trace collection"
    last = records[-1]["counts"]
    summary = {
        "run": str(folder), "expectedFailure": expected_failure,
        "peakPrivateMiB": result["peakPrivateBytes"] / 1048576,
        "delegateCreated": total(last, "delegate:", ".created"),
        "delegateDestroyed": total(last, "delegate:", ".destroyed"),
        "delegateLive": total(last, "delegate:", ".live"),
        "peakDelegateLive": max(total(r["counts"], "delegate:", ".live") for r in records),
        "documentCreated": total(last, "document-", ".created"),
        "documentDestroyed": total(last, "document-", ".destroyed"),
        "documentLive": total(last, "document-", ".live"),
        "peakDocumentLive": max(total(r["counts"], "document-", ".live") for r in records),
    }
    if expected_failure:
        assert result["forcedTermination"] and result["exitCode"] != 0, "Original bug did not reproduce"
        assert summary["delegateCreated"] >= 32 and summary["peakDelegateLive"] >= 16, "Missing delegate churn"
        assert last.get("source-update:3.length") == 24720, "Unexpected synthetic input"
    else:
        assert not result["forcedTermination"] and result["exitCode"] == 0, "Benchmark failed"
        stages = json.loads((folder / "data/memory-stages.json").read_text(encoding="utf-8-sig"))
        assert len(stages) == 12 and stages[-1]["name"] == "tab-closed", "Incomplete workload"
        assert stages[-1]["terminalItems"] == 0, "Terminal viewport retained after close"
        assert summary["delegateCreated"] <= 24, "Unexpected repeated delegate creation"
        assert summary["delegateLive"] == 0 and summary["documentLive"] == 0, "AI objects retained after clear"
        assert last.get("source-update:5.length") == 24720, "Third response missing/truncated"
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", type=Path, nargs="+")
    parser.add_argument("--expect-failure", action="store_true")
    args = parser.parse_args()
    summaries = [check(run, args.expect_failure) for directory in args.directories
                 for run in sorted(directory.glob("run-*"))]
    assert summaries, "No completed runs found"
    print(json.dumps(summaries, indent=2))
