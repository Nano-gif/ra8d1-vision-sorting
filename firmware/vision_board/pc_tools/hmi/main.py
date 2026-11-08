"""Command-line entry for the Vision Board HMI."""

from __future__ import annotations

import argparse
import sys

from vision_board_client import DEFAULT_BOARD_IP, DEFAULT_UDP_HEARTBEAT_TIMEOUT, DEFAULT_UDP_PORT


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Vision Board PC HMI prototype.")
    parser.add_argument("--board-ip", default=DEFAULT_BOARD_IP, help="default Vision Board IP")
    parser.add_argument("--listen-host", default="0.0.0.0", help="local UDP listen host")
    parser.add_argument("--udp-port", type=int, default=DEFAULT_UDP_PORT, help="local UDP listen port")
    parser.add_argument(
        "--heartbeat-timeout",
        type=float,
        default=DEFAULT_UDP_HEARTBEAT_TIMEOUT,
        help="seconds before UDP status is treated as offline",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        from PySide6.QtWidgets import QApplication

        from .window import MainWindow
    except ImportError as exc:
        print("PySide6 is not installed. Install it first:")
        print("  python -m pip install -r pc_tools\\requirements.txt")
        print(f"Import error: {exc}")
        return 1

    app = QApplication(sys.argv)
    window = MainWindow(args)
    window.show()
    return app.exec()
