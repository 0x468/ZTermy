import tempfile
import unittest
from pathlib import Path

from summarize_heob import summarize


class HeobSummaryTests(unittest.TestCase):
    def parse(self, contents):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.xml"
            path.write_text(contents, encoding="utf-8")
            return summarize(path)

    def report(self, diagnostics="", finished=True):
        state = "FINISHED" if finished else "RUNNING"
        return ("<valgrindoutput><tool>heob</tool>" + diagnostics
                + f"<status><state>{state}</state></status></valgrindoutput>")

    def test_unfinished_empty_report_is_not_clean(self):
        with self.assertRaises(ValueError):
            self.parse(self.report(finished=False))

    def test_reachable_and_lost_are_not_combined(self):
        errors = "".join(f"<error><kind>{kind}</kind><xwhat><leakedbytes>{size}</leakedbytes>"
                         "<leakedblocks>1</leakedblocks></xwhat></error>"
                         for kind, size in (("Leak_StillReachable", 9000), ("Leak_DefinitelyLost", 4096)))
        result = self.parse(self.report(errors))
        self.assertEqual(result["assessment"], "needs-review")
        self.assertEqual(result["categories"]["Leak_DefinitelyLost"]["bytes"], 4096)

    def test_invalid_access_with_zero_leak_bytes_requires_review(self):
        result = self.parse(self.report("<error><kind>InvalidRead</kind></error>"))
        self.assertEqual(result["assessment"], "needs-review")

    def test_unknown_diagnostic_is_not_silently_discarded(self):
        result = self.parse(self.report("<error><kind>FutureError</kind></error>"))
        self.assertEqual(result["assessment"], "needs-review")

    def test_complete_empty_report_is_bounded_observation(self):
        result = self.parse(self.report())
        self.assertEqual(result["assessment"], "no-lost-blocks-observed")


if __name__ == "__main__":
    unittest.main()
