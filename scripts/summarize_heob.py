"""Summarize Heob XML without conflating reachable blocks with lost blocks."""

import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def summarize(path: Path) -> dict:
    categories = {}
    records = []
    events = ET.iterparse(path, events=("start", "end"))
    _, root = next(events)
    if root.tag != "valgrindoutput":
        raise ValueError("Not a Heob report")
    tool = None
    state = None
    for event, error in events:
        if event != "end":
            continue
        if error.tag == "tool":
            tool = error.text
        if error.tag == "status":
            state = error.findtext("state")
        if error.tag != "error":
            continue
        kind = error.findtext("kind")
        if not kind:
            raise ValueError("Diagnostic without a kind")
        size = int(error.findtext("xwhat/leakedbytes", "0"))
        blocks = int(error.findtext("xwhat/leakedblocks", "0"))
        if size < 0 or blocks < 0:
            raise ValueError("Negative allocation counts")
        aggregate = categories.setdefault(kind, {"records": 0, "bytes": 0, "blocks": 0})
        aggregate["records"] += 1
        aggregate["bytes"] += size
        aggregate["blocks"] += blocks
        # Keep reachable totals, but do not duplicate their full stack trees in
        # memory/JSON. Original XML retains every record for later inspection.
        if kind != "Leak_StillReachable":
            records.append({"kind": kind, "bytes": size, "blocks": blocks,
                            "frames": [{child.tag: child.text for child in frame}
                                       for frame in error.findall("stack/frame")]})
        error.clear()
        root.remove(error)
    if tool != "heob":
        raise ValueError("Not a Heob report")
    if state != "FINISHED":
        raise ValueError("Incomplete detector run; cannot call this clean")
    review = any(kind != "Leak_StillReachable" for kind in categories)
    return {"report": str(path.resolve()), "finished": True,
            "assessment": "needs-review" if review else "no-lost-blocks-observed",
            "categories": categories, "records": records}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = summarize(args.report)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "records"}))
