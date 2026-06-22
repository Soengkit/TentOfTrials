# -*- coding: utf-8 -*-
"""Tests for required production secret validation in config_generator.py."""
import os
import sys
import unittest
from unittest import mock

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "tools"))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import config_generator as cg


def _valid_secrets():
    """Override dict that satisfies production secret validation."""
    return {
        "database": {"password": "9f2a7c4e1b3d5860"},
        "redis": {"password": "r3d1s-cred-7q2z-9988"},
        "auth": {"jwt_secret": "jwt-s3cr3t-0a1b2c3d4e5f"},
    }


class ValidateRequiredSecretsTests(unittest.TestCase):
    def test_passes_when_all_secrets_set(self):
        config = cg.generate_config("production", overrides=_valid_secrets())
        self.assertEqual(cg.validate_required_secrets(config, "production"), [])

    def test_fails_when_a_secret_is_empty(self):
        config = cg.generate_config("production", overrides=_valid_secrets())
        config["database"]["password"] = ""
        errors = cg.validate_required_secrets(config, "production")
        self.assertTrue(any("database.password" in e for e in errors))

    def test_fails_when_a_secret_is_placeholder_like(self):
        for bad in ("changeme123", "PLACEHOLDER", "<set-me>", "todo-later"):
            config = cg.generate_config("production", overrides=_valid_secrets())
            config["redis"]["password"] = bad
            errors = cg.validate_required_secrets(config, "production")
            self.assertTrue(
                any("redis.password" in e for e in errors),
                f"expected placeholder '{bad}' to be flagged",
            )

    def test_fails_when_a_secret_key_is_missing(self):
        config = cg.generate_config("production", overrides=_valid_secrets())
        del config["auth"]["jwt_secret"]
        errors = cg.validate_required_secrets(config, "production")
        self.assertTrue(any("jwt_secret" in e and "missing" in e for e in errors))

    def test_short_secret_is_treated_as_placeholder(self):
        config = cg.generate_config("production", overrides=_valid_secrets())
        config["database"]["password"] = "short"
        errors = cg.validate_required_secrets(config, "production")
        self.assertTrue(any("database.password" in e for e in errors))

    def test_non_production_environments_skip_validation(self):
        for env in ("development", "staging"):
            # Defaults have empty secrets but non-prod must still validate clean.
            config = cg.generate_config(env)
            self.assertEqual(cg.validate_required_secrets(config, env), [])

    def test_error_messages_do_not_leak_secret_values(self):
        config = cg.generate_config("production", overrides=_valid_secrets())
        leak = "changeme-UNIQUELEAK123"
        config["auth"]["jwt_secret"] = leak
        errors = cg.validate_required_secrets(config, "production")
        joined = " ".join(errors)
        self.assertTrue(any("jwt_secret" in e for e in errors))
        self.assertNotIn(leak, joined)
        self.assertNotIn("UNIQUELEAK123", joined)


class GenerateConfigValidationTests(unittest.TestCase):
    def test_production_raises_on_empty_default_secrets(self):
        with self.assertRaises(cg.SecretValidationError) as ctx:
            cg.generate_config("production")
        self.assertEqual(len(ctx.exception.errors), 3)

    def test_production_succeeds_when_secrets_provided(self):
        config = cg.generate_config("production", overrides=_valid_secrets())
        self.assertEqual(config["app"]["environment"], "production")
        self.assertEqual(config["database"]["password"], "9f2a7c4e1b3d5860")

    def test_development_remains_compatible(self):
        config = cg.generate_config("development")
        self.assertEqual(config["app"]["environment"], "development")
        self.assertEqual(config["database"]["password"], "")

    def test_staging_remains_compatible(self):
        config = cg.generate_config("staging")
        self.assertEqual(config["app"]["environment"], "staging")


class LoadSecretOverridesTests(unittest.TestCase):
    def test_reads_env_vars_into_nested_overrides(self):
        env = {
            "TOT_DATABASE_PASSWORD": "env-db-pwd-12345678",
            "TOT_REDIS_PASSWORD": "env-redis-pwd-12345",
            "TOT_JWT_SECRET": "env-jwt-cred-abcdef",
        }
        with mock.patch.dict(os.environ, env, clear=False):
            overrides = cg.load_secret_overrides()
        self.assertEqual(overrides["database"]["password"], "env-db-pwd-12345678")
        self.assertEqual(overrides["redis"]["password"], "env-redis-pwd-12345")
        self.assertEqual(overrides["auth"]["jwt_secret"], "env-jwt-cred-abcdef")

    def test_unset_env_vars_are_skipped(self):
        clean = {
            k: v for k, v in os.environ.items()
            if k not in cg.SECRET_ENV_VARS.values()
        }
        with mock.patch.dict(os.environ, clean, clear=True):
            self.assertEqual(cg.load_secret_overrides(), {})

    def test_env_vars_make_production_generation_pass(self):
        env = {
            "TOT_DATABASE_PASSWORD": "env-db-pwd-12345678",
            "TOT_REDIS_PASSWORD": "env-redis-pwd-12345",
            "TOT_JWT_SECRET": "env-jwt-cred-abcdef",
        }
        with mock.patch.dict(os.environ, env, clear=False):
            config = cg.generate_config("production", overrides=cg.load_secret_overrides())
        self.assertEqual(config["database"]["password"], "env-db-pwd-12345678")


class PlaceholderDetectionTests(unittest.TestCase):
    def test_none_and_non_string_are_placeholder(self):
        self.assertTrue(cg._is_placeholder_like(None))
        self.assertTrue(cg._is_placeholder_like(12345))

    def test_real_value_is_not_placeholder(self):
        self.assertFalse(cg._is_placeholder_like("9f2a7c4e1b3d5860"))


if __name__ == "__main__":
    unittest.main()
