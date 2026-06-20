#!/usr/bin/env sh
set -eu

bin="${1:-./frailbox}"
success_json="$(mktemp)"
failure_json="$(mktemp)"
trap 'rm -f "$success_json" "$failure_json"' EXIT

"$bin" --self-test --self-test-format json >"$success_json"

python3 - "$success_json" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    payload = json.load(fh)

summary = payload["summary"]
tests = payload["tests"]
assert summary["status"] == "pass", summary
assert summary["total"] == len(tests), summary
assert summary["passed"] == len(tests), summary
assert summary["failed"] == 0, summary
assert summary["duration_ms"] >= 0, summary

for item in tests:
    assert item["name"], item
    assert item["status"] == "pass", item
    assert item["duration_ms"] >= 0, item
    assert "failure_reason" not in item, item
PY

if "$bin" --self-test --self-test-format json \
    --self-test-inject-failure arena_allocator >"$failure_json"; then
    echo "expected injected self-test failure to return non-zero" >&2
    exit 1
fi

python3 - "$failure_json" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    payload = json.load(fh)

summary = payload["summary"]
tests = payload["tests"]
assert summary["status"] == "fail", summary
assert summary["failed"] == 1, summary
assert summary["passed"] == summary["total"] - 1, summary

failed = [item for item in tests if item["status"] == "fail"]
assert len(failed) == 1, tests
assert failed[0]["name"] == "arena_allocator", failed[0]
assert failed[0]["failure_reason"], failed[0]
PY
