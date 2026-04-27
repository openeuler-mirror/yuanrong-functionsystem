#!/usr/bin/env python3
"""Probe whether Rust binaries accept official deployment-layer flags.

The C++ --help surface is not the full black-box contract. openYuanrong also
starts functionsystem components from shell scripts and TOML/Jinja launch
templates. This helper extracts those deployment flags from a clean upper-layer
yuanrong tree and probes the packaged Rust binaries with `--flag=dummy --help`
so services do not actually start.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

COMPONENTS = (
    "function_master",
    "function_proxy",
    "function_agent",
    "runtime_manager",
    "domain_scheduler",
    "iam_server",
)

SHELL_FUNCTIONS = {
    "function_proxy": "install_function_proxy",
    "function_agent": "install_function_agent_and_runtime_manager_in_the_same_process",
    "function_master": "install_function_master",
    "iam_server": "install_iam_server",
}

FLAG_RE = re.compile(r"(?<![\w-])--([A-Za-z0-9][A-Za-z0-9_]*)")
TOML_SECTION_RE = re.compile(r"\s*\[([A-Za-z_]+)\.args\]")
TOML_KEY_RE = re.compile(r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*=")


def resolve_layout(root: Path) -> Tuple[Path, Path]:
    candidates = [
        (root / "functionsystem" / "bin", root / "functionsystem" / "lib"),
        (root / "bin", root / "lib"),
        (root / "output" / "functionsystem" / "bin", root / "output" / "functionsystem" / "lib"),
    ]
    for bin_dir, lib_dir in candidates:
        if bin_dir.is_dir():
            return bin_dir, lib_dir
    raise SystemExit(f"cannot find functionsystem bin dir under {root}")


def candidate_files(clean_yuanrong_root: Path) -> List[Path]:
    return [
        clean_yuanrong_root / "functionsystem/yuanrong-functionsystem/output/functionsystem/deploy/function_system/install.sh",
        clean_yuanrong_root / "output/openyuanrong/runtime/service/python/yr/cli/config.toml.jinja",
        clean_yuanrong_root / "deploy/k8s/charts/openyuanrong/templates/common/components-toml-configmap.yaml",
    ]


def shell_function_body(text: str, function_name: str) -> str:
    marker = f"function {function_name}()"
    start = text.find(marker)
    if start < 0:
        return ""
    next_function = text.find("\nfunction ", start + len(marker))
    if next_function < 0:
        return text[start:]
    return text[start:next_function]


def extract_shell_flags(path: Path) -> Dict[str, List[str]]:
    flags = {component: set() for component in COMPONENTS}
    if not path.exists():
        return {component: [] for component in COMPONENTS}
    text = path.read_text(errors="ignore")
    for component, function_name in SHELL_FUNCTIONS.items():
        flags[component].update(FLAG_RE.findall(shell_function_body(text, function_name)))
    return {component: sorted(values) for component, values in flags.items()}


def extract_toml_args(path: Path) -> Dict[str, List[str]]:
    flags = {component: set() for component in COMPONENTS}
    if not path.exists():
        return {component: [] for component in COMPONENTS}
    current = None
    for line in path.read_text(errors="ignore").splitlines():
        section = TOML_SECTION_RE.match(line)
        if section:
            name = section.group(1)
            current = name if name in flags else None
            continue
        if re.match(r"\s*\[", line):
            current = None
            continue
        if current:
            key = TOML_KEY_RE.match(line)
            if key:
                flags[current].add(key.group(1))
    return {component: sorted(values) for component, values in flags.items()}


def merge_flags(*sources: Dict[str, Iterable[str]]) -> Dict[str, List[str]]:
    merged = {component: set() for component in COMPONENTS}
    for source in sources:
        for component, values in source.items():
            if component in merged:
                merged[component].update(values)
    return {component: sorted(values) for component, values in merged.items()}


def run_flag_probe(binary: Path, lib_dir: Path, flag: str, timeout: float) -> Dict[str, object]:
    env = os.environ.copy()
    ld_parts = [str(lib_dir)] if lib_dir.is_dir() else []
    if env.get("LD_LIBRARY_PATH"):
        ld_parts.append(env["LD_LIBRARY_PATH"])
    env["LD_LIBRARY_PATH"] = ":".join(ld_parts)
    try:
        proc = subprocess.run(
            [str(binary), f"--{flag}=dummy", "--help"],
            text=True,
            capture_output=True,
            timeout=timeout,
            env=env,
        )
        output = (proc.stdout or "") + (proc.stderr or "")
        lower = output.lower()
        unexpected = "unexpected argument" in lower or "found argument" in lower
        return {
            "accepted": not unexpected,
            "returncode": proc.returncode,
            "classification": "accepted" if not unexpected else "rejected_unexpected_argument",
            "error": output[-1000:] if unexpected else "",
        }
    except subprocess.TimeoutExpired:
        return {
            "accepted": False,
            "returncode": "timeout",
            "classification": "timeout",
            "error": "timeout",
        }


def probe(clean_yuanrong_root: Path, rust_root: Path, timeout: float) -> Dict[str, object]:
    files = candidate_files(clean_yuanrong_root)
    sources = []
    per_file = {}
    for path in files:
        if path.name == "install.sh":
            extracted = extract_shell_flags(path)
        else:
            extracted = extract_toml_args(path)
        per_file[str(path)] = extracted
        sources.append(extracted)

    flags = merge_flags(*sources)
    bin_dir, lib_dir = resolve_layout(rust_root)
    binaries = {}
    for component, component_flags in flags.items():
        binary = bin_dir / component
        if not binary.exists():
            binaries[component] = {
                "exists": False,
                "flags": component_flags,
                "accepted_flags": [],
                "rejected_flags": component_flags,
                "probes": {},
            }
            continue
        probes = {
            flag: run_flag_probe(binary, lib_dir, flag, timeout)
            for flag in component_flags
        }
        binaries[component] = {
            "exists": True,
            "flags": component_flags,
            "accepted_flags": sorted(flag for flag, data in probes.items() if data["accepted"]),
            "rejected_flags": sorted(flag for flag, data in probes.items() if not data["accepted"]),
            "probes": probes,
        }
    return {
        "clean_yuanrong_root": str(clean_yuanrong_root),
        "rust_root": str(rust_root),
        "rust_bin_dir": str(bin_dir),
        "source_files": [str(path) for path in files if path.exists()],
        "per_file": per_file,
        "binaries": binaries,
    }


def write_markdown(report: Dict[str, object], path: Path) -> None:
    lines = [
        "# Deployment Flag Acceptance Probe",
        "",
        f"clean yuanrong root: `{report['clean_yuanrong_root']}`",
        f"Rust root: `{report['rust_root']}`",
        "",
        "Source files:",
    ]
    lines.extend(f"- `{source}`" for source in report["source_files"])
    lines.extend([
        "",
        "| Binary | Extracted deployment flags | Accepted | Rejected |",
        "| --- | ---: | ---: | ---: |",
    ])
    for name, data in sorted(report["binaries"].items()):
        lines.append(
            f"| `{name}` | {len(data['flags'])} | {len(data['accepted_flags'])} | {len(data['rejected_flags'])} |"
        )
    lines.append("")
    for name, data in sorted(report["binaries"].items()):
        lines.extend([f"## {name}", ""])
        if data["rejected_flags"]:
            lines.append("Rejected deployment flags:")
            lines.extend(f"- `--{flag}`" for flag in data["rejected_flags"])
        else:
            lines.append("Rejected deployment flags: none")
        lines.append("")
    path.write_text("\n".join(lines) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--clean-yuanrong-root", required=True, type=Path)
    parser.add_argument("--rust-root", required=True, type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--md", type=Path)
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()
    report = probe(args.clean_yuanrong_root, args.rust_root, args.timeout)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if args.md:
        write_markdown(report, args.md)
    print(json.dumps({
        name: {
            "flags": len(data["flags"]),
            "accepted": len(data["accepted_flags"]),
            "rejected": len(data["rejected_flags"]),
        }
        for name, data in sorted(report["binaries"].items())
    }, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
