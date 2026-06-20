#!/usr/bin/env python3
import json
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "config_generator.py"
FIXTURES = ROOT / "tools" / "fixtures"


def run_generator(*args):
    return subprocess.run(
        [sys.executable, str(GENERATOR), *args],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )


class ProductionSecretValidationTest(unittest.TestCase):
    def test_production_rejects_missing_secrets_without_values(self):
        result = run_generator("--env", "production", "--format", "json")

        self.assertEqual(result.returncode, 1)
        self.assertIn("required secret values", result.stderr)
        self.assertIn("database.password", result.stderr)
        self.assertIn("redis.password", result.stderr)
        self.assertIn("auth.jwt_secret", result.stderr)

    def test_production_rejects_placeholder_secrets_without_printing_values(self):
        result = run_generator(
            "--env", "production",
            "--format", "json",
            "--override-json", str(FIXTURES / "config_secrets_placeholder.json"),
        )

        self.assertEqual(result.returncode, 1)
        self.assertIn("database.password", result.stderr)
        self.assertIn("redis.password", result.stderr)
        self.assertIn("auth.jwt_secret", result.stderr)
        self.assertNotIn("change-me", result.stderr)
        self.assertNotIn("<redis-password>", result.stderr)
        self.assertNotIn("your-jwt-secret", result.stderr)

    def test_production_accepts_non_placeholder_secret_overrides(self):
        result = run_generator(
            "--env", "production",
            "--format", "json",
            "--override-json", str(FIXTURES / "config_secrets_valid.json"),
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        config = json.loads(result.stdout)
        self.assertEqual(config["app"]["environment"], "production")
        self.assertEqual(config["database"]["password"], "***REDACTED***")
        self.assertEqual(config["redis"]["password"], "***REDACTED***")
        self.assertEqual(config["auth"]["jwt_secret"], "***REDACTED***")

    def test_non_production_generation_remains_compatible(self):
        result = run_generator("--env", "development", "--format", "json")

        self.assertEqual(result.returncode, 0, result.stderr)
        config = json.loads(result.stdout)
        self.assertEqual(config["app"]["environment"], "development")


if __name__ == "__main__":
    unittest.main()
