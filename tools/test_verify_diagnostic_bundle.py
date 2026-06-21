#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path

from verify_diagnostic_bundle import main


def write_metadata(root: Path, commit: str, logd_value, *, passed=1, failed=0):
    path = root / "diagnostic" / f"build-{commit}.json"
    path.write_text(
        json.dumps(
            {
                "generated_at": "2026-01-01T00:00:00Z",
                "commit": commit,
                "diagnostic_logd": logd_value,
                "diagnostic_logd_error": None,
                "total_modules": passed + failed,
                "passed": passed,
                "failed": failed,
                "modules": [],
            }
        ),
        encoding="utf-8",
    )
    return path


class VerifyDiagnosticBundleTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        (self.root / "diagnostic").mkdir()

    def tearDown(self):
        self.tempdir.cleanup()

    def write_logd(self, name: str, content: bytes = b"logd"):
        path = self.root / "diagnostic" / name
        path.write_bytes(content)
        return path

    def run_tool(self, *paths):
        return main([*(str(path) for path in paths), "--root", str(self.root)])

    def test_accepts_matching_logd_and_metadata(self):
        logd = self.write_logd("build-1234abcd.logd")
        write_metadata(self.root, "1234abcd", "diagnostic/build-1234abcd.logd")

        self.assertEqual(self.run_tool(logd), 0)

    def test_rejects_stub_logd(self):
        logd = self.write_logd("build-00000000.logd")
        write_metadata(self.root, "00000000", "diagnostic/build-00000000.logd")

        self.assertEqual(self.run_tool(logd), 1)

    def test_rejects_missing_metadata(self):
        logd = self.write_logd("build-1234abcd.logd")

        self.assertEqual(self.run_tool(logd), 1)

    def test_rejects_mismatched_metadata_reference(self):
        logd = self.write_logd("build-1234abcd.logd")
        write_metadata(self.root, "1234abcd", "diagnostic/build-deadbeef.logd")

        self.assertEqual(self.run_tool(logd), 1)

    def test_accepts_chunked_bundle_when_metadata_lists_all_parts(self):
        part1 = self.write_logd("build-1234abcd-part001.logd")
        part2 = self.write_logd("build-1234abcd-part002.logd")
        write_metadata(
            self.root,
            "1234abcd",
            [
                "diagnostic/build-1234abcd-part001.logd",
                "diagnostic/build-1234abcd-part002.logd",
            ],
        )

        self.assertEqual(self.run_tool(part1, part2), 0)

    def test_rejects_missing_listed_chunk(self):
        part1 = self.write_logd("build-1234abcd-part001.logd")
        write_metadata(
            self.root,
            "1234abcd",
            [
                "diagnostic/build-1234abcd-part001.logd",
                "diagnostic/build-1234abcd-part002.logd",
            ],
        )

        self.assertEqual(self.run_tool(part1), 1)


if __name__ == "__main__":
    unittest.main()
