"""Summarize measure_process_memory.ps1 evidence; Python standard library only."""
import argparse
import csv
import datetime as dt
import json
import statistics as stats
from pathlib import Path


def timestamp(value):
    return dt.datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


def summarize(rows):
    result = {"samples": len(rows)}
    for key in ("privateBytes", "workingSetBytes", "commitBytes", "vaReservedBytes",
                "gpuDedicatedBytes", "gpuSharedBytes", "threads", "handles",
                "gdiHandles", "userHandles", "cpuOneCorePercent", "slowProbeMilliseconds"):
        values = [float(row[key]) for row in rows if row.get(key)]
        if values:
            scale = 1048576 if key.endswith("Bytes") else 1
            result[key] = {"median": stats.median(values) / scale,
                           "min": min(values) / scale, "max": max(values) / scale,
                           "samples": len(values)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    runs = []
    for folder in sorted(args.directory.glob("run-*")):
        if not (folder / "result.json").exists():
            continue
        result = json.loads((folder / "result.json").read_text(encoding="utf-8-sig"))
        with (folder / "samples.csv").open(encoding="utf-8-sig", newline="") as stream:
            rows = list(csv.DictReader(stream))
        item = {"run": folder.name, "validExit": result["exitCode"] == 0 and not result["forcedTermination"],
                "samplingUsable": len(rows) >= 10,
                "all": summarize(rows), "stages": {}}
        if rows:
            last_time = float(rows[-1]["elapsedSeconds"])
            item["tail5s"] = summarize([r for r in rows if float(r["elapsedSeconds"]) >= last_time - 5])
        stages_file = folder / "data" / "memory-stages.json"
        if stages_file.exists():
            for stage in json.loads(stages_file.read_text(encoding="utf-8-sig")):
                begin, end = timestamp(stage["utc"]), timestamp(stage["endUtc"])
                selected = [r for r in rows if max(begin, end - 5) <= timestamp(r["utc"]) <= end]
                item["stages"][stage["name"]] = {**stage, "tail5s": summarize(selected)}
        runs.append(item)
    by_stage = {}
    for item in runs:
        for name, stage in item["stages"].items():
            if "privateBytes" in stage["tail5s"]:
                by_stage.setdefault(name, []).append(stage["tail5s"]["privateBytes"]["median"])
    aggregate = {name: {"n": len(v), "medianMiB": stats.median(v), "minMiB": min(v), "maxMiB": max(v),
                        "sampleStdDevMiB": stats.stdev(v) if len(v) > 1 else None}
                 for name, v in by_stage.items()}
    output = {"units": "Memory in MiB; tail5s is a window median, not proof of steady state.",
              "runs": runs, "privateBytesByStage": aggregate}
    (args.directory / "summary.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
    print(json.dumps(aggregate or [{"run": r["run"], "validExit": r["validExit"],
                                  "tail5s": r.get("tail5s")} for r in runs], indent=2))


if __name__ == "__main__":
    main()
