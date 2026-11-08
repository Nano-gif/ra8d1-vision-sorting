#!/usr/bin/env python3
"""
Pack an Edge Impulse C++ MCU export into the Vision Board SD-card layout.

This is a small convenience wrapper around vision_dataset_tool.py so the common
workflow stays short:

python pc_tools/ei_model_pack.py --source <ei_export> --output <package_dir> --name <name>
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Sequence

import vision_dataset_tool


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Pack Edge Impulse export for Vision Board")
    parser.add_argument("--source", "-s", required=True, help="Edge Impulse C++ MCU export directory")
    parser.add_argument("--output", "-o", required=True, help="output model package directory")
    parser.add_argument("--name", "-n", required=True, help="model package display name")
    parser.add_argument("--profile", default=vision_dataset_tool.DEFAULT_PROFILE, help="runtime profile tag")
    parser.add_argument("--main-tflite", help="optional explicit main TFLite path")
    parser.add_argument("--anomaly-tflite", help="optional explicit anomaly TFLite path")
    parser.add_argument("--no-auto", action="store_true", help="disable metadata/arena/threshold auto parsing")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    tool_args = [
        "package-model",
        "--source",
        str(Path(args.source)),
        "--output",
        str(Path(args.output)),
        "--name",
        args.name,
        "--profile",
        args.profile,
    ]
    if args.main_tflite:
        tool_args.extend(["--main-tflite", args.main_tflite])
    if args.anomaly_tflite:
        tool_args.extend(["--anomaly-tflite", args.anomaly_tflite])
    if args.no_auto:
        tool_args.extend(["--no-metadata-auto", "--no-threshold-auto", "--no-arena-auto"])

    return vision_dataset_tool.main(tool_args)


if __name__ == "__main__":
    raise SystemExit(main())
