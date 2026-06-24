import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "config_generator.py"
spec = importlib.util.spec_from_file_location("config_generator", MODULE_PATH)
config_generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(config_generator)


class ProductionSecretValidationTest(unittest.TestCase):
    def test_production_config_requires_all_secret_keys(self):
        with patch.dict(os.environ, {}, clear=True):
            config = config_generator.generate_config("production")

        valid, errors = config_generator.validate_production_secrets(config)

        self.assertFalse(valid)
        self.assertEqual(
            errors,
            [
                "database.password is required for production",
                "redis.password is required for production",
                "auth.jwt_secret is required for production",
            ],
        )

    def test_production_config_accepts_environment_secrets(self):
        env = {
            "DATABASE_PASSWORD": "database-secret-value",
            "REDIS_PASSWORD": "redis-secret-value",
            "AUTH_JWT_SECRET": "jwt-secret-value",
        }

        with patch.dict(os.environ, env, clear=True):
            config = config_generator.generate_config("production")

        valid, errors = config_generator.validate_production_secrets(config)

        self.assertTrue(valid)
        self.assertEqual(errors, [])

    def test_placeholder_secret_values_are_rejected_without_leaking_value(self):
        overrides = {
            "database": {"password": "change-me"},
            "redis": {"password": "replace-this"},
            "auth": {"jwt_secret": "your-secret-here"},
        }

        with patch.dict(os.environ, {}, clear=True):
            config = config_generator.generate_config("production", overrides)

        valid, errors = config_generator.validate_production_secrets(config)
        joined_errors = "\n".join(errors)

        self.assertFalse(valid)
        self.assertIn("database.password", joined_errors)
        self.assertIn("redis.password", joined_errors)
        self.assertIn("auth.jwt_secret", joined_errors)
        self.assertNotIn("change-me", joined_errors)
        self.assertNotIn("replace-this", joined_errors)
        self.assertNotIn("your-secret-here", joined_errors)

    def test_non_production_generation_remains_compatible(self):
        with patch.dict(os.environ, {}, clear=True):
            development = config_generator.generate_config("development")
            staging = config_generator.generate_config("staging")

        self.assertEqual(development["app"]["environment"], "development")
        self.assertEqual(staging["app"]["environment"], "staging")
        self.assertEqual(development["database"]["password"], "")
        self.assertEqual(staging["redis"]["password"], "")

    def test_sensitive_key_list_has_no_duplicates(self):
        keys = config_generator.SENSITIVE_KEYS

        self.assertEqual(sorted(keys), sorted(set(keys)))


if __name__ == "__main__":
    unittest.main()
