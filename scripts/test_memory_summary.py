"""Small, dependency-free checks for the memory evidence summarizer."""
from summarize_memory import summarize, timestamp

result = summarize([
    {"privateBytes": "1048576", "cpuOneCorePercent": "0", "gpuDedicatedBytes": ""},
    {"privateBytes": "3145728", "cpuOneCorePercent": "100", "gpuDedicatedBytes": ""},
])
assert result["privateBytes"] == {"median": 2.0, "min": 1.0, "max": 3.0, "samples": 2}
assert result["cpuOneCorePercent"]["median"] == 50
assert "gpuDedicatedBytes" not in result  # unavailable must not become zero
assert summarize([]) == {"samples": 0}
assert timestamp("2026-09-21T01:00:00Z") == timestamp("2026-09-21T09:00:00+08:00")
print("Memory evidence summary checks passed.")
