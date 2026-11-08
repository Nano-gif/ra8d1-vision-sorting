#!/usr/bin/env python3
"""UDP receive helper for W5500 board-side tests."""

import argparse
import sys
import time

from vision_board_client import VisionBoardClient, format_status_summary, format_udp_packet


def parse_args():
    parser = argparse.ArgumentParser(
        description="Listen for UDP packets from the W5500 test firmware."
    )
    parser.add_argument(
        "--host",
        default="0.0.0.0",
        help="Local address to bind. Use 0.0.0.0 to listen on all adapters.",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=16000,
        help="Local UDP port to bind.",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=0,
        help="Stop after receiving this many packets. 0 means run until Ctrl+C.",
    )
    parser.add_argument(
        "--bufsize",
        type=int,
        default=2048,
        help="Maximum bytes to receive per packet.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=0.5,
        help="Socket timeout in seconds. This keeps Ctrl+C responsive.",
    )
    parser.add_argument(
        "--idle-print",
        type=float,
        default=2.0,
        help="Print a waiting message every N seconds. 0 disables it.",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    received = 0
    last_idle_print = 0.0

    client = VisionBoardClient(
        udp_listen_host=args.host,
        udp_listen_port=args.port,
        udp_socket_timeout_s=args.timeout,
        udp_bufsize=args.bufsize,
    )

    def on_packet(packet):
        nonlocal received
        received += 1
        print(format_udp_packet(packet, received))
        summary = format_status_summary(packet)
        if summary:
            print(summary)

    try:
        client.start_udp_listener(on_packet=on_packet)
    except OSError as exc:
        print(f"bind failed: {exc}", file=sys.stderr)
        print("Check whether another process is already using this UDP port.", file=sys.stderr)
        return 1

    print(f"udp listen {args.host}:{args.port}")
    print("Press Ctrl+C to stop.")

    try:
        while args.count <= 0 or received < args.count:
            now = time.monotonic()
            if args.idle_print > 0 and now - last_idle_print >= args.idle_print:
                state = client.get_link_state()
                age = "none" if state.last_udp_age_s is None else f"{state.last_udp_age_s:.1f}s"
                print(
                    f"waiting... received={received} "
                    f"udp_alive={state.udp_alive} last_age={age}"
                )
                last_idle_print = now
            time.sleep(min(args.timeout, 0.2))
    except KeyboardInterrupt:
        print()
        print("stopped by Ctrl+C")
    finally:
        client.close()

    print(f"done, received={received}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
