import argparse
import socket
import time


def main():
    parser = argparse.ArgumentParser(description="Receive W5500 TCP throughput test payload.")
    parser.add_argument("--host", default="0.0.0.0", help="bind address")
    parser.add_argument("--port", type=int, default=5000, help="bind port")
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(1)
        print(f"listening on {args.host}:{args.port}")

        conn, addr = server.accept()
        with conn:
            print(f"accepted {addr[0]}:{addr[1]}")
            total = 0
            start = time.perf_counter()

            while True:
                data = conn.recv(65536)
                if not data:
                    break
                total += len(data)

            elapsed = time.perf_counter() - start
            if elapsed <= 0:
                elapsed = 1e-9
            print(f"received={total} bytes elapsed={elapsed:.3f}s approx={total / elapsed:.0f} byte/s")


if __name__ == "__main__":
    main()
