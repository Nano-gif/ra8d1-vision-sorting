"""TCP scan helpers for the Vision Board.

This scanner treats a device as online only after a full PING/PONG handshake.
A plain TCP connect is not enough, because the board keeps other sockets and
services alive independently.
"""

from __future__ import annotations

import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import Callable, List, Optional

from vision_board_client import VisionBoardClient

DEFAULT_SCAN_BASE_IP = "192.168.1"
DEFAULT_SCAN_PORT = 5000
DEFAULT_SCAN_TIMEOUT = 0.3
DEFAULT_SCAN_WORKERS = 50


@dataclass
class DeviceInfo:
    ip: str
    port: int
    online: bool = True
    model_name: str = ""
    latency_ms: int = 0
    last_seen: float = 0.0
    error: str = ""


def _probe_device(ip: str, port: int, timeout: float) -> Optional[DeviceInfo]:
    start = time.perf_counter()
    client = VisionBoardClient(board_ip=ip)

    if not client.test_tcp_connection(port=port, timeout_s=timeout, host=ip):
        return None

    latency_ms = int((time.perf_counter() - start) * 1000)
    return DeviceInfo(
        ip=ip,
        port=port,
        online=True,
        model_name="",
        latency_ms=latency_ms,
        last_seen=time.time(),
        error="",
    )


def scan_devices(
    base_ip: str = DEFAULT_SCAN_BASE_IP,
    port: int = DEFAULT_SCAN_PORT,
    timeout: float = DEFAULT_SCAN_TIMEOUT,
    max_workers: int = DEFAULT_SCAN_WORKERS,
    progress_callback: Optional[Callable[[str], None]] = None,
) -> List[DeviceInfo]:
    """Scan 192.168.1.1-254 and return the devices that answer PING/PONG."""

    base = (base_ip or "").strip().rstrip(".")
    if not base:
        base = DEFAULT_SCAN_BASE_IP

    ips = [f"{base}.{i}" for i in range(1, 255)]
    found: List[DeviceInfo] = []
    completed = 0
    total = len(ips)

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = {executor.submit(_probe_device, ip, port, timeout): ip for ip in ips}
        for future in as_completed(futures):
            completed += 1
            try:
                info = future.result()
            except Exception:
                info = None
            if info is not None and info.online:
                found.append(info)
            if progress_callback is not None and (
                completed == total or completed % 16 == 0 or info is not None
            ):
                progress_callback(f"扫描 {completed}/{total}，服务确认 {len(found)} 台")

    found.sort(key=lambda d: (d.latency_ms, d.ip))
    return found
