#!/usr/bin/env python3
"""Reusable PC-side communication layer for the Vision Board.

This module is intentionally independent from any GUI framework.  The future
upper-computer application should call this class, while command-line tools only
wrap it for manual tests.
"""

from __future__ import annotations

import socket
import struct
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from typing import Callable, Dict, Optional, Tuple


DEFAULT_BOARD_IP = "192.168.1.88"
DEFAULT_UDP_PORT = 16000
DEFAULT_TCP_PORT = 5000
DEFAULT_CMD_PORT = 51010
DEFAULT_UDP_HEARTBEAT_TIMEOUT = 1.0
DEFAULT_TCP_PING_TIMEOUT = 0.8
TCP_PING_MESSAGE = b"PING\n"
TCP_PONG_MESSAGE = b"PONG\n"
IMAGE_FRAME_MAGIC = b"VIMG"
IMAGE_FRAME_HEADER_SIZE = 32
IMAGE_FRAME_VERSION = 1
IMAGE_FORMAT_RGB565_LE = 1

_TCP_LOCKS_GUARD = threading.Lock()
_TCP_LOCKS: Dict[Tuple[str, int], threading.RLock] = {}


def tcp_session_lock(host: str, port: int) -> threading.RLock:
    """Return the shared lock for one board TCP image service endpoint.

    The board-side W5500 image service owns one socket. Serializing all PC-side
    PING/GET_FRAME operations per host+port prevents the GUI keepalive, scanner,
    and image receive workers from racing the same board socket.
    """

    key = (host, port)
    with _TCP_LOCKS_GUARD:
        lock = _TCP_LOCKS.get(key)
        if lock is None:
            lock = threading.RLock()
            _TCP_LOCKS[key] = lock
        return lock


def bytes_to_ascii(data: bytes) -> str:
    """Convert binary payload to a printable ASCII view."""

    return "".join(chr(byte) if 32 <= byte <= 126 else "." for byte in data)


def parse_status_text(text: str) -> Dict[str, str]:
    """Parse simple board status text such as 'vision_status seq=6'.

    The current firmware test payload is plain text.  Later, when a formal
    binary protocol is added, this parser can stay as a compatibility path.
    """

    fields: Dict[str, str] = {}
    parts = text.strip().split()
    if parts:
        fields["message"] = parts[0]

    for part in parts[1:]:
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        if key:
            fields[key] = value

    return fields


@dataclass(frozen=True)
class UdpPacket:
    """One UDP packet received from the board."""

    data: bytes
    address: Tuple[str, int]
    timestamp: float
    fields: Dict[str, str] = field(default_factory=dict)

    @property
    def source_ip(self) -> str:
        return self.address[0]

    @property
    def source_port(self) -> int:
        return self.address[1]

    @property
    def length(self) -> int:
        return len(self.data)

    @property
    def ascii(self) -> str:
        return bytes_to_ascii(self.data)

    @property
    def hex(self) -> str:
        return self.data.hex(" ")

    @property
    def sequence(self) -> Optional[int]:
        value = self.fields.get("seq")
        if value is None:
            return None
        try:
            return int(value, 0)
        except ValueError:
            return None


@dataclass(frozen=True)
class LinkState:
    """Snapshot consumed by CLI tools and the future GUI."""

    board_ip: str
    board_online: bool
    udp_alive: bool
    udp_packets: int
    udp_bytes: int
    udp_errors: int
    last_udp_age_s: Optional[float]
    last_status_fields: Dict[str, str]
    last_sequence: Optional[int]
    last_source: Optional[Tuple[str, int]]


@dataclass(frozen=True)
class TcpReceiveResult:
    """Result of a TCP receive test or image transfer."""

    connected: bool
    bytes_received: int
    elapsed_s: float
    byte_per_s: float
    remote_reset: bool = False
    error: Optional[str] = None


@dataclass(frozen=True)
class ImageFrame:
    """One image frame received from the board TCP image protocol."""

    width: int
    height: int
    image_format: int
    sequence: int
    board_time_ms: int
    pixels: bytes
    elapsed_s: float
    byte_per_s: float

    @property
    def payload_bytes(self) -> int:
        return len(self.pixels)

    @property
    def format_name(self) -> str:
        if self.image_format == IMAGE_FORMAT_RGB565_LE:
            return "rgb565_le"
        return f"unknown_{self.image_format}"


def format_udp_packet(packet: UdpPacket, index: Optional[int] = None) -> str:
    now_text = datetime.fromtimestamp(packet.timestamp).strftime("%H:%M:%S.%f")[:-3]
    prefix = f"#{index} " if index is not None else ""
    return (
        f"[{now_text}] {prefix}from {packet.source_ip}:{packet.source_port} "
        f"len={packet.length} ascii=|{packet.ascii}| hex={packet.hex}"
    )


def format_status_summary(packet: UdpPacket) -> Optional[str]:
    """Return a compact one-line summary for Vision Board status packets."""

    if packet.fields.get("message") != "vstat":
        return None

    def field(name: str, default: str = "-") -> str:
        return packet.fields.get(name, default)

    result = field("result")
    if result == "0":
        result_text = "OK"
    elif result == "1":
        result_text = "NG"
    elif result == "2":
        result_text = "IDLE"
    else:
        result_text = result

    return (
        "status "
        f"seq={field('seq')} "
        f"model={field('model')} "
        f"result={result_text} "
        f"score={field('score')}/{field('threshold')} "
        f"frame={field('frame')} "
        f"total={field('total')} ok={field('ok')} ng={field('ng')} "
        f"cam={field('cam')} link={field('link')} "
        f"bus={field('bus')} arm={field('arm')} conv={field('conv')} "
        f"rpm={field('rpm')} acc={field('acc')} "
        f"arm_ready={field('arm_ready')} estop={field('arm_estop')} action={field('action')}"
    )


class VisionBoardClient:
    """PC-side communication facade for the Vision Board.

    UDP does not have a real connection state.  The GUI should treat it as
    alive when status packets are received within heartbeat_timeout_s.
    TCP is checked by an actual connect or by receiving an image/test payload.
    """

    def __init__(
        self,
        board_ip: str = DEFAULT_BOARD_IP,
        udp_listen_host: str = "0.0.0.0",
        udp_listen_port: int = DEFAULT_UDP_PORT,
        heartbeat_timeout_s: float = DEFAULT_UDP_HEARTBEAT_TIMEOUT,
        udp_socket_timeout_s: float = 0.5,
        udp_bufsize: int = 2048,
    ) -> None:
        self.board_ip = board_ip
        self.udp_listen_host = udp_listen_host
        self.udp_listen_port = udp_listen_port
        self.heartbeat_timeout_s = heartbeat_timeout_s
        self.udp_socket_timeout_s = udp_socket_timeout_s
        self.udp_bufsize = udp_bufsize

        self._lock = threading.Lock()
        self._udp_stop = threading.Event()
        self._udp_thread: Optional[threading.Thread] = None
        self._udp_socket: Optional[socket.socket] = None
        self._udp_callback: Optional[Callable[[UdpPacket], None]] = None

        self._udp_packets = 0
        self._udp_bytes = 0
        self._udp_errors = 0
        self._last_udp_packet: Optional[UdpPacket] = None

    def start_udp_listener(
        self, on_packet: Optional[Callable[[UdpPacket], None]] = None
    ) -> None:
        """Start the background UDP listener used by status/heartbeat data."""

        if self._udp_thread and self._udp_thread.is_alive():
            self._udp_callback = on_packet
            return

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(self.udp_socket_timeout_s)
        try:
            sock.bind((self.udp_listen_host, self.udp_listen_port))
        except OSError:
            sock.close()
            raise

        self._udp_socket = sock
        self._udp_callback = on_packet
        self._udp_stop.clear()
        self._udp_thread = threading.Thread(
            target=self._udp_loop,
            name="vision-board-udp",
            daemon=True,
        )
        self._udp_thread.start()

    def stop_udp_listener(self) -> None:
        self._udp_stop.set()
        if self._udp_socket is not None:
            self._udp_socket.close()
            self._udp_socket = None
        if self._udp_thread is not None:
            self._udp_thread.join(timeout=1.0)
            self._udp_thread = None

    def close(self) -> None:
        self.stop_udp_listener()

    def is_udp_alive(self, timeout_s: Optional[float] = None) -> bool:
        state = self.get_link_state()
        if state.last_udp_age_s is None:
            return False
        limit = self.heartbeat_timeout_s if timeout_s is None else timeout_s
        return state.last_udp_age_s <= limit

    def is_board_online(self) -> bool:
        """Return true when the board sent a recent UDP heartbeat/status packet."""

        return self.is_udp_alive()

    def test_tcp_connection(
        self,
        port: int = DEFAULT_TCP_PORT,
        timeout_s: float = DEFAULT_TCP_PING_TIMEOUT,
        host: Optional[str] = None,
    ) -> bool:
        """Check whether a board-side TCP server accepts a PING/PONG handshake."""

        target_host = self.board_ip if host is None else host
        with tcp_session_lock(target_host, port):
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as conn:
                    conn.settimeout(timeout_s)
                    conn.connect((target_host, port))
                    conn.sendall(TCP_PING_MESSAGE)
                    reply = b""
                    while len(reply) < len(TCP_PONG_MESSAGE):
                        chunk = conn.recv(len(TCP_PONG_MESSAGE) - len(reply))
                        if not chunk:
                            break
                        reply += chunk
                    return reply == TCP_PONG_MESSAGE
            except OSError:
                return False

    def ping_tcp_service(
        self,
        port: int = DEFAULT_TCP_PORT,
        timeout_s: float = DEFAULT_TCP_PING_TIMEOUT,
        host: Optional[str] = None,
    ) -> bool:
        """Send a PING and wait for the board to reply with PONG."""

        return self.test_tcp_connection(port=port, timeout_s=timeout_s, host=host)

    def receive_tcp_payload(
        self,
        port: int = DEFAULT_TCP_PORT,
        timeout_s: float = 10.0,
        host: Optional[str] = None,
        expected_bytes: int = 0,
        chunk_size: int = 65536,
    ) -> TcpReceiveResult:
        """Connect to board TCP server and receive one payload.

        W5500 test firmware may reset/close the socket immediately after the
        payload is sent.  If bytes were received before reset, this is reported
        as remote_reset=True instead of raising a traceback.
        """

        target_host = self.board_ip if host is None else host
        total = 0
        start = time.perf_counter()
        remote_reset = False

        with tcp_session_lock(target_host, port):
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as conn:
                    conn.settimeout(timeout_s)
                    conn.connect((target_host, port))

                    while expected_bytes <= 0 or total < expected_bytes:
                        try:
                            data = conn.recv(chunk_size)
                        except ConnectionResetError:
                            remote_reset = True
                            break

                        if not data:
                            break
                        total += len(data)
            except OSError as exc:
                elapsed = max(time.perf_counter() - start, 1e-9)
                return TcpReceiveResult(
                    connected=total > 0,
                    bytes_received=total,
                    elapsed_s=elapsed,
                    byte_per_s=total / elapsed,
                    remote_reset=remote_reset,
                    error=str(exc),
                )

        elapsed = max(time.perf_counter() - start, 1e-9)
        return TcpReceiveResult(
            connected=True,
            bytes_received=total,
            elapsed_s=elapsed,
            byte_per_s=total / elapsed,
            remote_reset=remote_reset,
            error=None,
        )

    def receive_image_frame(
        self,
        port: int = DEFAULT_TCP_PORT,
        timeout_s: float = 20.0,
        host: Optional[str] = None,
        chunk_size: int = 65536,
    ) -> ImageFrame:
        """Connect to the board persistent TCP service and fetch one RGB565 frame.

        The board keeps a persistent TCP service on port 5000 that accepts a
        ``GET_FRAME\\n`` command and replies with one VIMG frame on the same
        socket. This replaces the old one-shot ``camera_tcp_server_send`` path
        which only opened the port for a single frame.
        """

        target_host = self.board_ip if host is None else host
        start = time.perf_counter()
        with tcp_session_lock(target_host, port):
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as conn:
                conn.settimeout(timeout_s)
                conn.connect((target_host, port))
                conn.sendall(b"GET_FRAME\n")

                header = self._recv_exact(conn, IMAGE_FRAME_HEADER_SIZE, chunk_size)
                if header[0:4] != IMAGE_FRAME_MAGIC:
                    raise ValueError(f"bad image magic: {header[0:4]!r}")

                version, header_size, width, height, image_format, flags = struct.unpack_from(
                    "<HHHHHH", header, 4
                )
                payload_bytes, sequence, board_time_ms, reserved = struct.unpack_from(
                    "<IIII", header, 16
                )
                del flags, reserved

                if version != IMAGE_FRAME_VERSION:
                    raise ValueError(f"unsupported image frame version: {version}")
                if header_size != IMAGE_FRAME_HEADER_SIZE:
                    raise ValueError(f"unsupported image header size: {header_size}")
                if width <= 0 or height <= 0:
                    raise ValueError(f"invalid image size: {width}x{height}")
                if image_format != IMAGE_FORMAT_RGB565_LE:
                    raise ValueError(f"unsupported image format: {image_format}")
                expected_payload = width * height * 2
                if payload_bytes != expected_payload:
                    raise ValueError(
                        f"payload size mismatch: header={payload_bytes}, expected={expected_payload}"
                    )

                pixels = self._recv_exact(conn, payload_bytes, chunk_size)

        elapsed = max(time.perf_counter() - start, 1e-9)
        total_bytes = IMAGE_FRAME_HEADER_SIZE + len(pixels)
        return ImageFrame(
            width=width,
            height=height,
            image_format=image_format,
            sequence=sequence,
            board_time_ms=board_time_ms,
            pixels=pixels,
            elapsed_s=elapsed,
            byte_per_s=total_bytes / elapsed,
        )

    @staticmethod
    def _recv_exact(conn: socket.socket, size: int, chunk_size: int) -> bytes:
        data = bytearray()
        while len(data) < size:
            want = min(chunk_size, size - len(data))
            chunk = conn.recv(want)
            if not chunk:
                raise ConnectionError(f"connection closed after {len(data)}/{size} bytes")
            data.extend(chunk)
        return bytes(data)

    def send_udp(
        self,
        data: bytes,
        dest_port: int,
        dest_ip: Optional[str] = None,
        timeout_s: float = 1.0,
    ) -> int:
        """Send a UDP command to the board. Reserved for future command tests."""

        target_ip = self.board_ip if dest_ip is None else dest_ip
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(timeout_s)
            return sock.sendto(data, (target_ip, dest_port))

    def get_link_state(self) -> LinkState:
        with self._lock:
            last_packet = self._last_udp_packet
            udp_packets = self._udp_packets
            udp_bytes = self._udp_bytes
            udp_errors = self._udp_errors

        if last_packet is None:
            last_age = None
            last_fields: Dict[str, str] = {}
            last_sequence = None
            last_source = None
        else:
            last_age = time.time() - last_packet.timestamp
            last_fields = dict(last_packet.fields)
            last_sequence = last_packet.sequence
            last_source = last_packet.address

        udp_alive = last_age is not None and last_age <= self.heartbeat_timeout_s
        return LinkState(
            board_ip=self.board_ip,
            board_online=udp_alive,
            udp_alive=udp_alive,
            udp_packets=udp_packets,
            udp_bytes=udp_bytes,
            udp_errors=udp_errors,
            last_udp_age_s=last_age,
            last_status_fields=last_fields,
            last_sequence=last_sequence,
            last_source=last_source,
        )

    def _udp_loop(self) -> None:
        while not self._udp_stop.is_set():
            sock = self._udp_socket
            if sock is None:
                break

            try:
                data, address = sock.recvfrom(self.udp_bufsize)
            except socket.timeout:
                continue
            except OSError:
                if not self._udp_stop.is_set():
                    with self._lock:
                        self._udp_errors += 1
                break

            text = bytes_to_ascii(data)
            packet = UdpPacket(
                data=data,
                address=address,
                timestamp=time.time(),
                fields=parse_status_text(text),
            )

            with self._lock:
                self._udp_packets += 1
                self._udp_bytes += len(data)
                self._last_udp_packet = packet

            callback = self._udp_callback
            if callback is not None:
                callback(packet)
