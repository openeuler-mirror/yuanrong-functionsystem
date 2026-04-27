#!/usr/bin/env python3
"""Compare packaged C++ and Rust functionsystem binary flag surfaces.

This is a black-box helper: it runs real packaged binaries with --help and
extracts advertised long flags. It can also probe whether C++-only flags are
accepted by the Rust binary even when they are hidden from Rust --help output.
It intentionally does not inspect source.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from pathlib import Path
from typing import Dict, List, Tuple

FLAG_RE = re.compile(r"(?<![\w-])--([A-Za-z0-9][A-Za-z0-9_-]*)")


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


def run_help(binary: Path, lib_dir: Path, timeout: float) -> Dict[str, object]:
    env = os.environ.copy()
    ld_parts = [str(lib_dir)] if lib_dir.is_dir() else []
    if env.get("LD_LIBRARY_PATH"):
        ld_parts.append(env["LD_LIBRARY_PATH"])
    env["LD_LIBRARY_PATH"] = ":".join(ld_parts)
    try:
        proc = subprocess.run(
            [str(binary), "--help"],
            text=True,
            capture_output=True,
            timeout=timeout,
            env=env,
        )
        output = (proc.stdout or "") + (proc.stderr or "")
        flags = sorted(set(FLAG_RE.findall(output)))
        return {
            "ok": proc.returncode == 0,
            "returncode": proc.returncode,
            "flags": flags,
            "stdout_lines": len((proc.stdout or "").splitlines()),
            "stderr_lines": len((proc.stderr or "").splitlines()),
            "error": "" if proc.returncode == 0 else output[-1000:],
        }
    except subprocess.TimeoutExpired as exc:
        output = ((exc.stdout or "") if isinstance(exc.stdout, str) else "") + ((exc.stderr or "") if isinstance(exc.stderr, str) else "")
        return {
            "ok": False,
            "returncode": "timeout",
            "flags": sorted(set(FLAG_RE.findall(output))),
            "stdout_lines": 0,
            "stderr_lines": 0,
            "error": "timeout",
        }


def run_flag_probe(binary: Path, lib_dir: Path, flag: str, timeout: float) -> Dict[str, object]:
    """Probe whether a Rust binary accepts a C++ flag name.

    The probe always appends --help so services do not start. Some recognized
    typed flags may reject the dummy value; that is still useful evidence that
    the flag name is no longer an "unexpected argument" startup failure.
    """
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


def compare(cpp_root: Path, rust_root: Path, timeout: float, probe_acceptance: bool) -> Dict[str, object]:
    cpp_bin, cpp_lib = resolve_layout(cpp_root)
    rust_bin, rust_lib = resolve_layout(rust_root)
    cpp_bins = {p.name: p for p in cpp_bin.iterdir() if p.is_file() and os.access(p, os.X_OK)}
    rust_bins = {p.name: p for p in rust_bin.iterdir() if p.is_file() and os.access(p, os.X_OK)}
    common = sorted(set(cpp_bins) & set(rust_bins))
    binaries: Dict[str, object] = {}
    for name in common:
        c = run_help(cpp_bins[name], cpp_lib, timeout)
        r = run_help(rust_bins[name], rust_lib, timeout)
        cflags = set(c["flags"])
        rflags = set(r["flags"])
        missing = sorted(cflags - rflags)
        probes = {}
        if probe_acceptance:
            probes = {
                flag: run_flag_probe(rust_bins[name], rust_lib, flag, timeout)
                for flag in missing
            }
        accepted_hidden = sorted(
            flag for flag, probe in probes.items() if probe.get("accepted")
        )
        rejected_hidden = sorted(
            flag for flag, probe in probes.items() if not probe.get("accepted")
        )
        binaries[name] = {
            "cpp": c,
            "rust": r,
            "missing_in_rust": missing,
            "extra_in_rust": sorted(rflags - cflags),
            "common_flags": sorted(cflags & rflags),
            "hidden_acceptance_probes": probes,
            "accepted_hidden_flags": accepted_hidden,
            "rejected_hidden_flags": rejected_hidden,
        }
    return {
        "cpp_root": str(cpp_root),
        "rust_root": str(rust_root),
        "cpp_bin_dir": str(cpp_bin),
        "rust_bin_dir": str(rust_bin),
        "cpp_only_binaries": sorted(set(cpp_bins) - set(rust_bins)),
        "rust_only_binaries": sorted(set(rust_bins) - set(cpp_bins)),
        "binaries": binaries,
    }


def write_markdown(report: Dict[str, object], path: Path) -> None:
    lines: List[str] = []
    lines.append("# Binary Flag Surface Comparison")
    lines.append("")
    lines.append(f"C++ root: `{report['cpp_root']}`")
    lines.append(f"Rust root: `{report['rust_root']}`")
    lines.append("")
    lines.append(f"C++-only binaries: `{', '.join(report['cpp_only_binaries']) or 'none'}`")
    lines.append(f"Rust-only binaries: `{', '.join(report['rust_only_binaries']) or 'none'}`")
    lines.append("")
    lines.append("| Binary | C++ help | Rust help | C++ flags | Rust flags | Missing in Rust help | Hidden accepted | Hidden rejected | Extra in Rust |")
    lines.append("| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |")
    for name, data in sorted(report["binaries"].items()):
        cpp = data["cpp"]
        rust = data["rust"]
        lines.append(
            f"| `{name}` | {cpp['returncode']} | {rust['returncode']} | {len(cpp['flags'])} | {len(rust['flags'])} | {len(data['missing_in_rust'])} | {len(data['accepted_hidden_flags'])} | {len(data['rejected_hidden_flags'])} | {len(data['extra_in_rust'])} |"
        )
    lines.append("")
    for name, data in sorted(report["binaries"].items()):
        lines.append(f"## {name}")
        lines.append("")
        if data["missing_in_rust"]:
            lines.append("Missing in Rust:")
            lines.extend(f"- `--{flag}`" for flag in data["missing_in_rust"])
        else:
            lines.append("Missing in Rust: none")
        lines.append("")
        if data["accepted_hidden_flags"]:
            lines.append("Accepted by Rust but hidden from Rust help:")
            lines.extend(f"- `--{flag}`" for flag in data["accepted_hidden_flags"])
        else:
            lines.append("Accepted by Rust but hidden from Rust help: none")
        lines.append("")
        if data["rejected_hidden_flags"]:
            lines.append("Rejected by Rust:")
            lines.extend(f"- `--{flag}`" for flag in data["rejected_hidden_flags"])
            lines.append("")
            lines.append("Rejected details:")
            for flag in data["rejected_hidden_flags"]:
                probe = data["hidden_acceptance_probes"][flag]
                lines.append(f"- `--{flag}`: {probe['classification']} rc={probe['returncode']}")
        else:
            lines.append("Rejected by Rust: none")
        lines.append("")
        if data["extra_in_rust"]:
            lines.append("Extra in Rust:")
            lines.extend(f"- `--{flag}`" for flag in data["extra_in_rust"])
        else:
            lines.append("Extra in Rust: none")
        lines.append("")
    path.write_text("\n".join(lines) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp-root", required=True, type=Path)
    parser.add_argument("--rust-root", required=True, type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--md", type=Path)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument(
        "--no-probe-acceptance",
        action="store_true",
        help="Only compare --help surfaces; do not probe hidden C++ flag acceptance.",
    )
    args = parser.parse_args()
    report = compare(args.cpp_root, args.rust_root, args.timeout, not args.no_probe_acceptance)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if args.md:
        write_markdown(report, args.md)
    print(json.dumps({
        "cpp_only_binaries": report["cpp_only_binaries"],
        "rust_only_binaries": report["rust_only_binaries"],
        "binaries": {
            k: {
                "missing_in_rust": len(v["missing_in_rust"]),
                "accepted_hidden": len(v["accepted_hidden_flags"]),
                "rejected_hidden": len(v["rejected_hidden_flags"]),
                "extra_in_rust": len(v["extra_in_rust"]),
                "cpp_rc": v["cpp"]["returncode"],
                "rust_rc": v["rust"]["returncode"],
            } for k, v in sorted(report["binaries"].items())
        }
    }, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
