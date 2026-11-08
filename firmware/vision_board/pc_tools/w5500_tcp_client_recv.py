import argparse

from vision_board_client import VisionBoardClient


def main():
    parser = argparse.ArgumentParser(description="Connect to W5500 TCP server and receive test payload.")
    parser.add_argument("--host", default="192.168.1.88", help="W5500 board IP address")
    parser.add_argument("--port", type=int, default=5000, help="W5500 board TCP port")
    parser.add_argument("--timeout", type=float, default=10.0, help="socket timeout in seconds")
    parser.add_argument("--expected-bytes", type=int, default=0, help="Stop after this many bytes. 0 means until close.")
    args = parser.parse_args()

    client = VisionBoardClient(board_ip=args.host)

    print(f"connecting to {args.host}:{args.port}")
    result = client.receive_tcp_payload(
        port=args.port,
        timeout_s=args.timeout,
        expected_bytes=args.expected_bytes,
    )

    if result.error and result.bytes_received == 0:
        print(f"connect/receive failed: {result.error}")
        return 1

    close_note = " remote_reset=yes" if result.remote_reset else ""
    if result.error:
        close_note += f" error_after_data={result.error}"

    print("connected")
    print(
        f"received={result.bytes_received} bytes "
        f"elapsed={result.elapsed_s:.3f}s "
        f"approx={result.byte_per_s:.0f} byte/s{close_note}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
