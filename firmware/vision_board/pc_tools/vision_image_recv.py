#!/usr/bin/env python3
"""Receive one camera frame from the Vision Board TCP image server."""

from __future__ import annotations

import argparse
import struct
import time
from pathlib import Path

from vision_board_client import DEFAULT_BOARD_IP, DEFAULT_TCP_PORT, ImageFrame, VisionBoardClient


BMP_RGB565_HEADER_SIZE = 66
BMP_RGB565_COMPRESSION_BITFIELDS = 3


def _put_le16(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buf, offset, value)


def _put_le32(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buf, offset, value)


def build_rgb565_bmp(frame: ImageFrame) -> bytes:
    """Build a bottom-up RGB565 BMP from a top-down raw frame."""

    row_bytes = frame.width * 2
    row_stride = (row_bytes + 3) & ~3
    padding = b"\x00" * (row_stride - row_bytes)
    image_bytes = row_stride * frame.height
    file_size = BMP_RGB565_HEADER_SIZE + image_bytes

    header = bytearray(BMP_RGB565_HEADER_SIZE)
    header[0:2] = b"BM"
    _put_le32(header, 2, file_size)
    _put_le32(header, 10, BMP_RGB565_HEADER_SIZE)
    _put_le32(header, 14, 40)
    _put_le32(header, 18, frame.width)
    _put_le32(header, 22, frame.height)
    _put_le16(header, 26, 1)
    _put_le16(header, 28, 16)
    _put_le32(header, 30, BMP_RGB565_COMPRESSION_BITFIELDS)
    _put_le32(header, 34, image_bytes)
    _put_le32(header, 54, 0x0000F800)
    _put_le32(header, 58, 0x000007E0)
    _put_le32(header, 62, 0x0000001F)

    out = bytearray(file_size)
    out[:BMP_RGB565_HEADER_SIZE] = header
    cursor = BMP_RGB565_HEADER_SIZE
    for y in range(frame.height - 1, -1, -1):
        start = y * row_bytes
        out[cursor : cursor + row_bytes] = frame.pixels[start : start + row_bytes]
        cursor += row_bytes
        if padding:
            out[cursor : cursor + len(padding)] = padding
            cursor += len(padding)

    return bytes(out)


def default_output_path(output_dir: Path, suffix: str) -> Path:
    timestamp = time.strftime("%Y%m%d_%H%M%S")
    return output_dir / f"vision_frame_{timestamp}.{suffix}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Connect to the Vision Board and save one TCP image frame."
    )
    parser.add_argument("--host", default=DEFAULT_BOARD_IP, help="Vision Board IP address")
    parser.add_argument("--port", type=int, default=DEFAULT_TCP_PORT, help="Vision Board TCP port")
    parser.add_argument("--timeout", type=float, default=25.0, help="socket timeout in seconds")
    parser.add_argument("--output-dir", default="vision_frames", help="output directory (relative to cwd)")
    parser.add_argument("--bmp", default="", help="BMP output path. Default: auto name")
    parser.add_argument("--raw", default="", help="raw RGB565 output path. Default: auto name")
    parser.add_argument("--no-raw", action="store_true", help="do not save raw RGB565 data")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    bmp_path = Path(args.bmp) if args.bmp else default_output_path(output_dir, "bmp")
    raw_path = Path(args.raw) if args.raw else default_output_path(output_dir, "rgb565")

    client = VisionBoardClient(board_ip=args.host)

    print(f"connecting to {args.host}:{args.port}")
    frame = client.receive_image_frame(port=args.port, timeout_s=args.timeout)

    if frame.format_name != "rgb565_le":
        raise ValueError(f"unsupported image format: {frame.format_name}")

    bmp_path.write_bytes(build_rgb565_bmp(frame))
    if not args.no_raw:
        raw_path.write_bytes(frame.pixels)

    print(
        f"received seq={frame.sequence} size={frame.width}x{frame.height} "
        f"payload={frame.payload_bytes} bytes elapsed={frame.elapsed_s:.3f}s "
        f"approx={frame.byte_per_s:.0f} byte/s"
    )
    print(f"bmp={bmp_path}")
    if not args.no_raw:
        print(f"raw={raw_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
