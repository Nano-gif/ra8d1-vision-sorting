#!/usr/bin/env python3
"""Monitor Vision Board Ethernet link state from the PC side."""

import argparse
import time

from vision_board_client import VisionBoardClient, format_udp_packet


def main():
    parser = argparse.ArgumentParser(description="Monitor Vision Board UDP heartbeat and TCP availability.")
    parser.add_argument("--board-ip", default="192.168.1.88", help="Vision Board IP address.")
    parser.add_argument("--udp-port", type=int, default=16000, help="Local UDP status listen port.")
    parser.add_argument("--tcp-port", type=int, default=5000, help="Board TCP test/image port.")
    parser.add_argument("--heartbeat-timeout", type=float, default=1.0, help="UDP alive timeout in seconds.")
    parser.add_argument("--tcp-check-interval", type=float, default=0.0, help="TCP check interval. 0 disables active TCP check.")
    parser.add_argument("--interval", type=float, default=1.0, help="Status print interval in seconds.")
    args = parser.parse_args()

    client = VisionBoardClient(
        board_ip=args.board_ip,
        udp_listen_port=args.udp_port,
        heartbeat_timeout_s=args.heartbeat_timeout,
    )

    packet_count = 0

    def on_packet(packet):
        nonlocal packet_count
        packet_count += 1
        print(format_udp_packet(packet, packet_count))

    try:
        client.start_udp_listener(on_packet=on_packet)
    except OSError as exc:
        print(f"UDP listen failed on port {args.udp_port}: {exc}")
        print("Check firewall or whether another process is using this UDP port.")
        return 1

    print(f"monitor board={args.board_ip} udp_port={args.udp_port} tcp_port={args.tcp_port}")
    print("Press Ctrl+C to stop.")

    last_tcp_check = 0.0
    tcp_available = None

    try:
        while True:
            now = time.monotonic()
            if args.tcp_check_interval > 0 and now - last_tcp_check >= args.tcp_check_interval:
                tcp_available = client.test_tcp_connection(port=args.tcp_port)
                last_tcp_check = now

            state = client.get_link_state()
            age = "none" if state.last_udp_age_s is None else f"{state.last_udp_age_s:.2f}s"
            tcp_text = "skip" if tcp_available is None else str(tcp_available)
            seq = "none" if state.last_sequence is None else str(state.last_sequence)
            source = "none" if state.last_source is None else f"{state.last_source[0]}:{state.last_source[1]}"

            print(
                "state "
                f"board_online={state.board_online} "
                f"udp_alive={state.udp_alive} "
                f"tcp_available={tcp_text} "
                f"udp_packets={state.udp_packets} "
                f"udp_bytes={state.udp_bytes} "
                f"udp_errors={state.udp_errors} "
                f"last_age={age} "
                f"seq={seq} "
                f"source={source}"
            )
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print()
        print("stopped by Ctrl+C")
    finally:
        client.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
