#!/usr/bin/env python3

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

BUILD_LOGD_RE = re.compile(r"^build-([0-9a-fA-F]{8})(?:-part(\d{3}))?\.logd$")
STUB_COMMIT = "00000000"


@dataclass
class DiagnosticIssue:
    path: Path
    severity: str
    message: str
    remediation: str


@dataclass
class BundleResult:
    bundle_id: str
    logd_paths: list[Path]
    metadata_path: Optional[Path] = None
    issues: list[DiagnosticIssue] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not any(issue.severity == "error" for issue in self.issues)


def normalize(path: Path) -> Path:
    return path.expanduser().resolve()


def bundle_id_for_logd(path: Path) -> Optional[str]:
    match = BUILD_LOGD_RE.match(path.name)
    if not match:
        return None
    return match.group(1).lower()


def matching_metadata_path(logd_path: Path, bundle_id: str) -> Path:
    return logd_path.parent / f"build-{bundle_id}.json"


def relpath(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def load_json(path: Path) -> tuple[Optional[dict[str, Any]], Optional[str]]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except Exception as exc:
        return None, str(exc)
    if not isinstance(data, dict):
        return None, "metadata JSON root is not an object"
    return data, None


def paths_from_metadata(data: dict[str, Any]) -> set[str]:
    value = data.get("diagnostic_logd")
    if isinstance(value, str):
        return {value}
    if isinstance(value, list):
        return {item for item in value if isinstance(item, str)}
    return set()


def validate_metadata(
    result: BundleResult,
    metadata_path: Path,
    root: Path,
) -> None:
    data, error = load_json(metadata_path)
    if data is None:
        result.issues.append(
            DiagnosticIssue(
                metadata_path,
                "error",
                f"metadata JSON cannot be read: {error}",
                "Regenerate diagnostics with `python3 build.py` and commit the new JSON metadata.",
            )
        )
        return

    commit = str(data.get("commit", "")).lower()
    if commit != result.bundle_id:
        result.issues.append(
            DiagnosticIssue(
                metadata_path,
                "error",
                f"metadata commit is {commit or '<missing>'}, expected {result.bundle_id}",
                "Use the JSON metadata generated for the same build commit as the .logd file.",
            )
        )

    metadata_logds = paths_from_metadata(data)
    if not metadata_logds:
        result.issues.append(
            DiagnosticIssue(
                metadata_path,
                "error",
                "metadata does not list diagnostic_logd artifacts",
                "Regenerate diagnostics with a current `python3 build.py` run.",
            )
        )
    else:
        supplied = {relpath(path, root) for path in result.logd_paths}
        listed_basenames = {Path(item).name for item in metadata_logds}
        missing = [path for path in result.logd_paths if path.name not in listed_basenames]
        if missing:
            result.issues.append(
                DiagnosticIssue(
                    metadata_path,
                    "error",
                    "metadata does not reference every supplied .logd artifact",
                    "Pass the .logd path named in diagnostic_logd, or regenerate the matching JSON/logd pair.",
                )
            )

        listed_paths = [root / item for item in metadata_logds]
        missing_listed = [path for path in listed_paths if not path.exists()]
        if missing_listed:
            result.issues.append(
                DiagnosticIssue(
                    metadata_path,
                    "error",
                    "metadata references diagnostic log files that are not present",
                    "Commit all listed .logd chunks or rerun `python3 build.py` to create a complete bundle.",
                )
            )

        if supplied and not supplied.intersection(metadata_logds) and missing:
            result.issues.append(
                DiagnosticIssue(
                    metadata_path,
                    "warning",
                    "supplied paths are outside the repository root while metadata uses repository-relative paths",
                    "Run the verifier from the repository root when possible.",
                )
            )

    total = data.get("total_modules")
    passed = data.get("passed")
    failed = data.get("failed")
    if not all(isinstance(value, int) for value in (total, passed, failed)):
        result.issues.append(
            DiagnosticIssue(
                metadata_path,
                "error",
                "metadata is missing integer total_modules, passed, or failed counts",
                "Regenerate diagnostics with the repository build script.",
            )
        )
    elif total <= 0 or passed + failed != total:
        result.issues.append(
            DiagnosticIssue(
                metadata_path,
                "error",
                f"metadata module counts are inconsistent: total={total}, passed={passed}, failed={failed}",
                "Regenerate diagnostics and verify the JSON was not edited by hand.",
            )
        )

    if data.get("diagnostic_logd_error"):
        result.issues.append(
            DiagnosticIssue(
                metadata_path,
                "error",
                f"metadata reports diagnostic log creation error: {data['diagnostic_logd_error']}",
                "Fix the build/encryptly blocker and rerun `python3 build.py`.",
            )
        )


def validate_bundle(
    bundle_id: str,
    logd_paths: list[Path],
    metadata_paths: dict[str, Path],
    root: Path,
) -> BundleResult:
    result = BundleResult(bundle_id=bundle_id, logd_paths=logd_paths)

    if bundle_id == STUB_COMMIT:
        for path in logd_paths:
            result.issues.append(
                DiagnosticIssue(
                    path,
                    "error",
                    "build-00000000.logd is only the tracked stub example",
                    "Run `python3 build.py` and use the generated diagnostic/build-<commit>.logd artifact.",
                )
            )
        return result

    for path in logd_paths:
        if not path.exists():
            result.issues.append(
                DiagnosticIssue(
                    path,
                    "error",
                    ".logd file does not exist",
                    "Pass an existing diagnostic/build-<commit>.logd path.",
                )
            )
        elif path.stat().st_size == 0:
            result.issues.append(
                DiagnosticIssue(
                    path,
                    "error",
                    ".logd file is empty",
                    "Regenerate diagnostics with `python3 build.py`.",
                )
            )

    metadata_path = metadata_paths.get(bundle_id)
    if metadata_path is None:
        discovered = matching_metadata_path(logd_paths[0], bundle_id)
        if discovered.exists():
            metadata_path = discovered

    if metadata_path is None:
        result.issues.append(
            DiagnosticIssue(
                logd_paths[0],
                "error",
                "matching diagnostic metadata JSON is missing",
                f"Commit diagnostic/build-{bundle_id}.json or pass it to the verifier.",
            )
        )
    else:
        result.metadata_path = metadata_path
        validate_metadata(result, metadata_path, root)

    return result


def parse_inputs(paths: list[Path]) -> tuple[dict[str, list[Path]], dict[str, Path], list[DiagnosticIssue]]:
    logds: dict[str, list[Path]] = {}
    metadata: dict[str, Path] = {}
    issues: list[DiagnosticIssue] = []

    for raw_path in paths:
        path = normalize(raw_path)
        if path.suffix == ".logd":
            bundle_id = bundle_id_for_logd(path)
            if bundle_id is None:
                issues.append(
                    DiagnosticIssue(
                        path,
                        "error",
                        "diagnostic log name must match build-<8 hex chars>.logd or build-<8 hex chars>-partNNN.logd",
                        "Pass paths produced by `python3 build.py` from the diagnostic directory.",
                    )
                )
                continue
            logds.setdefault(bundle_id, []).append(path)
        elif path.suffix == ".json" and path.name.startswith("build-"):
            bundle_id = path.stem.replace("build-", "", 1).lower()
            metadata[bundle_id] = path
        else:
            issues.append(
                DiagnosticIssue(
                    path,
                    "error",
                    "unsupported input path",
                    "Pass diagnostic/build-*.logd files and optional matching diagnostic/build-*.json metadata.",
                )
            )

    return logds, metadata, issues


def print_report(results: list[BundleResult], input_issues: list[DiagnosticIssue]) -> None:
    total = len(results)
    passed = sum(1 for result in results if result.ok)
    failed = total - passed + sum(1 for issue in input_issues if issue.severity == "error")

    print("Diagnostic bundle verification")
    print(f"Bundles checked: {total}")
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")
    print()

    for issue in input_issues:
        print(f"[{issue.severity.upper()}] {issue.path}")
        print(f"  {issue.message}")
        print(f"  Remediation: {issue.remediation}")

    for result in results:
        status = "PASS" if result.ok else "FAIL"
        print(f"[{status}] build-{result.bundle_id}")
        for path in result.logd_paths:
            print(f"  logd: {path}")
        if result.metadata_path:
            print(f"  metadata: {result.metadata_path}")
        for issue in result.issues:
            print(f"  [{issue.severity.upper()}] {issue.path}")
            print(f"    {issue.message}")
            print(f"    Remediation: {issue.remediation}")
        print()


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify diagnostic .logd bundles before submitting a PR.",
    )
    parser.add_argument(
        "paths",
        nargs="+",
        type=Path,
        help="diagnostic/build-*.logd files and optional matching build-*.json metadata",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path.cwd(),
        help="repository root used to resolve metadata-relative logd paths",
    )
    args = parser.parse_args(argv)

    logds, metadata, input_issues = parse_inputs(args.paths)
    results = [
        validate_bundle(bundle_id, sorted(paths), metadata, normalize(args.root))
        for bundle_id, paths in sorted(logds.items())
    ]

    if not logds:
        input_issues.append(
            DiagnosticIssue(
                Path("<args>"),
                "error",
                "no diagnostic .logd files were provided",
                "Pass at least one diagnostic/build-<commit>.logd path.",
            )
        )

    print_report(results, input_issues)
    has_error = any(issue.severity == "error" for issue in input_issues)
    has_error = has_error or any(not result.ok for result in results)
    return 1 if has_error else 0


if __name__ == "__main__":
    raise SystemExit(main())
