#!/usr/bin/env python3
"""Compare dictionary preparation metrics to detect regressions."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, Any


def load_metrics(path: Path) -> Dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as fh:
            return json.load(fh)
    except FileNotFoundError:
        raise SystemExit(f"Metrics file not found: {path}") from None
    except json.JSONDecodeError as exc:
        raise SystemExit(f"Failed to parse JSON metrics {path}: {exc}") from exc


def flatten(prefix: str, data: Dict[str, Any], out: Dict[str, float]) -> None:
    for key, value in data.items():
        if isinstance(value, (int, float)):
            out[f"{prefix}{key}"] = float(value)
        elif isinstance(value, dict):
            flatten(f"{prefix}{key}.", value, out)


def compare_metrics(base: Dict[str, Any], target: Dict[str, Any]) -> Dict[str, float]:
    base_flat: Dict[str, float] = {}
    target_flat: Dict[str, float] = {}
    flatten("", base, base_flat)
    flatten("", target, target_flat)

    keys = set(base_flat.keys()) | set(target_flat.keys())
    deltas: Dict[str, float] = {}
    for key in sorted(keys):
        deltas[key] = target_flat.get(key, 0.0) - base_flat.get(key, 0.0)
    return deltas


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Compare dictionary metrics JSON files and print deltas."
    )
    parser.add_argument("base", type=Path, help="Older metrics JSON file.")
    parser.add_argument("target", type=Path, help="Newer metrics JSON file.")
    parser.add_argument(
        "--threshold",
        type=float,
        default=0.0,
        help="Optional absolute threshold; exit with 1 if exceeded.",
    )
    args = parser.parse_args(argv)

    base_metrics = load_metrics(args.base)
    target_metrics = load_metrics(args.target)
    deltas = compare_metrics(base_metrics, target_metrics)

    exceeded = False
    for key, delta in deltas.items():
        print(f"{key}: {delta:+.6f}")
        if args.threshold and abs(delta) > args.threshold:
            exceeded = True

    if exceeded:
        print(
            f"Threshold {args.threshold} exceeded for at least one metric.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

