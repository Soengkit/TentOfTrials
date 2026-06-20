import json
import unittest

from tools.ai_reviewer import (
    FileReviewResult,
    ProjectReviewReport,
    ReviewCategory,
    ReviewFinding,
    ReviewSeverity,
    build_sarif_summary,
    redact_secret_values,
)


class AiReviewerSarifTests(unittest.TestCase):
    def build_report(self):
        return ProjectReviewReport(
            timestamp="2026-06-20T17:55:00",
            project_path="sample",
            total_files=2,
            reviewed_files=2,
            total_findings=1,
            critical_findings=1,
            errors=0,
            warnings=0,
            info_findings=0,
            suggestions=0,
            file_results=[
                FileReviewResult(
                    file_path="sample/clean.py",
                    language="py",
                    line_count=1,
                    findings=[],
                ),
                FileReviewResult(
                    file_path="sample/failing.py",
                    language="py",
                    line_count=3,
                    findings=[
                        ReviewFinding(
                            id="SEC-HARDCODED-KEY-2-test",
                            severity=ReviewSeverity.CRITICAL,
                            category=ReviewCategory.SECURITY,
                            message="Hardcoded secret detected: api_key='abcd1234SECRET'",
                            file_path="sample/failing.py",
                            line_number=2,
                            suggestion="Move api_key='abcd1234SECRET' to a secret manager.",
                            code_snippet="api_key='abcd1234SECRET'",
                            rules=["SEC-HARDCODED-KEY"],
                        )
                    ],
                ),
            ],
            summary="one finding",
        )

    def test_secret_redaction_preserves_key_names(self):
        redacted = redact_secret_values("password='supersecretvalue' token: abcdefghijklmnop")

        self.assertEqual(redacted, "password=[REDACTED] token: [REDACTED]")

    def test_sarif_summary_includes_clean_and_failing_files(self):
        sarif = build_sarif_summary(self.build_report())
        encoded = json.dumps(sarif)

        self.assertEqual(sarif["version"], "2.1.0")
        self.assertIn("SEC-HARDCODED-KEY", encoded)
        self.assertIn("sample/failing.py", encoded)
        self.assertNotIn("abcd1234SECRET", encoded)

        run = sarif["runs"][0]
        self.assertEqual(run["invocations"][0]["properties"]["reviewedFiles"], 2)
        self.assertEqual(run["invocations"][0]["properties"]["totalFindings"], 1)
        self.assertEqual(len(run["results"]), 1)
        self.assertEqual(run["results"][0]["level"], "error")
        self.assertEqual(
            run["results"][0]["locations"][0]["physicalLocation"]["region"]["startLine"],
            2,
        )


if __name__ == "__main__":
    unittest.main()
