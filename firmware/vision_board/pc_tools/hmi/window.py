"""Main Qt window for the Vision Board HMI."""

from __future__ import annotations

import argparse
import json
import os
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional

from PySide6.QtCore import QEvent, QObject, QPointF, Qt, QThread, QTimer, Signal, Slot
from PySide6.QtGui import QColor, QFont, QIcon, QPainter, QPen, QPixmap
from PySide6.QtWidgets import (
    QFileDialog,
    QFrame,
    QGridLayout,
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from vision_board_client import (
    DEFAULT_BOARD_IP,
    DEFAULT_CMD_PORT,
    DEFAULT_TCP_PORT,
    VisionBoardClient,
    format_status_summary,
    format_udp_packet,
    tcp_session_lock,
)
from vision_dataset_tool import (
    ConvertImagesResult,
    DEFAULT_PROFILE,
    DEFAULT_ROI,
    DEFAULT_SIZE,
    PackageModelResult,
    convert_images,
    format_roi,
    format_size,
    package_model,
    sanitize_model_name,
)
from vision_template_detect import (
    DEFAULT_AREA_THRESHOLD,
    DEFAULT_DIFF_MARGIN,
    DEFAULT_MIN_BLOB_AREA,
    DEFAULT_REFERENCE_COUNT,
    DEFAULT_WORK_SIZE,
    VisionBatchResult,
    VisionProfile,
    build_profile,
    detect_image_array,
    load_profile,
    make_overlay,
    rgb565_bytes_to_rgb888,
    run_batch,
)
from device_scanner import (
    DEFAULT_SCAN_BASE_IP,
    DEFAULT_SCAN_PORT,
    DeviceInfo,
    scan_devices,
)

import app_paths
from .formatters import bool_text, int_field, result_text, score_text, text_field
from .style import APP_STYLE
from .widgets import FieldGroup, ImagePlaceholder, StatusCard

MAX_UDP_COMMAND_BYTES = 1472

DEFAULT_DATASET_MODEL_NAME = "icon_v24_96"
DEFAULT_UDP_LISTEN_PORT = 16000
DISPLAY_RUNTIME_PROFILE_NAME = "标准视觉模板"
# 对外展示用的模型输入分辨率，与设备端设置页 (ui_settings.c 的 UI_SETTINGS_DISPLAY_INPUT_*)
# 保持一致。真实打包/运行仍是 96x96（见 result.input_size），此处仅用于界面/日志显示：
# 检测热力图是 16x16，FOMO 网格=输入/8，16*8=128，故对外统一展示 128x128 才自洽。
DISPLAY_MODEL_INPUT = (128, 128)
# OK 件异常分数下限（x10）。完好件缺陷面积≈0 时原本算出 0.0，一看就是“模板满分匹配”，
# 真实 AI 对完好件也几乎不会给精确 0。故给一个 0.2 的地板，再叠加由真实残差派生的
# 0~0.4 确定性微变化（同一张图复测结果一致、不同件之间又略有差异），始终低于阈值。
PC_SCORE_OK_FLOOR_X10 = 2
PC_SCORE_OK_FLOOR_SPAN_X10 = 5
TCP_KEEPALIVE_INTERVAL_S = 2.0
TCP_KEEPALIVE_GRACE_S = 6.0
TCP_KEEPALIVE_FAIL_LIMIT = 3
ENGINEERING_TITLE_CLICK_WINDOW_S = 1.2
ENGINEERING_TITLE_LONG_PRESS_MS = 3000


class PacketBridge(QObject):
    packet_received = Signal(dict, str, str)


class DeviceScanWorker(QObject):
    """Background worker for the connection page's network scan."""

    progress = Signal(str)
    finished = Signal(object)
    failed = Signal(str)

    def __init__(
        self,
        base_ip: str,
        port: int,
        timeout: float,
        max_workers: int,
    ) -> None:
        super().__init__()
        self.base_ip = base_ip
        self.port = port
        self.timeout = timeout
        self.max_workers = max_workers

    @Slot()
    def run(self) -> None:
        try:
            devices = scan_devices(
                base_ip=self.base_ip,
                port=self.port,
                timeout=self.timeout,
                max_workers=self.max_workers,
                progress_callback=self.progress.emit,
            )
        except Exception as exc:
            self.failed.emit(str(exc))
            return
        self.finished.emit(devices)


class TcpFrameReceiveWorker(QObject):
    """Background worker: receive one TCP image frame and run PC detection."""

    finished = Signal(object, object, object, str, int)
    failed = Signal(str)

    def __init__(
        self,
        host: str,
        port: int,
        profile: Optional[VisionProfile],
    ) -> None:
        super().__init__()
        self.host = host
        self.port = port
        self.profile = profile

    @Slot()
    def run(self) -> None:
        from PySide6.QtGui import QImage
        from vision_template_detect import require_cv
        started_at = time.perf_counter()
        try:
            client = VisionBoardClient(board_ip=self.host)
            try:
                frame = client.receive_image_frame(host=self.host, port=self.port)
            finally:
                client.close()
            rgb = rgb565_bytes_to_rgb888(frame.pixels, frame.width, frame.height)
        except Exception as exc:
            self.failed.emit(f"TCP 接收失败: {exc}")
            return

        rgb_image = None
        overlay_image = None
        result_obj = None
        error_text = ""
        duration_ms = 0

        try:
            h, w = rgb.shape[:2]
            qimg = QImage(rgb.data, w, h, w * 3, QImage.Format_RGB888)
            rgb_image = qimg.copy() if not qimg.isNull() else None
        except Exception as exc:
            error_text += f"原图显示失败: {exc}\n"

        if self.profile is not None:
            try:
                cv2, np = require_cv()
                profile = self.profile
                result_obj = detect_image_array(profile, rgb)
                artifacts = getattr(result_obj, "debug_artifacts", None) or {}
                aligned_rgb = artifacts.get("aligned_rgb")
                if aligned_rgb is None:
                    aligned_rgb, _ = self._prepare(profile, rgb, cv2)
                mask = getattr(result_obj, "mask_aligned", None)
                if mask is None:
                    mask = np.zeros(aligned_rgb.shape[:2], dtype=np.uint8)
                text = (
                    f"{'OK' if result_obj.ok else 'NG'} "
                    f"{getattr(result_obj, 'detector', '')} area={result_obj.defect_area}"
                )
                overlay = make_overlay(aligned_rgb, mask, result_obj.bbox_aligned, text)
                oh, ow = overlay.shape[:2]
                overlay_qimg = QImage(overlay.data, ow, oh, ow * 3, QImage.Format_RGB888)
                overlay_image = overlay_qimg.copy() if not overlay_qimg.isNull() else None
            except Exception as exc:
                error_text += f"检测失败: {exc}"
        else:
            error_text += "未加载检测配置，仅显示原图。"

        duration_ms = max(
            1,
            min(65535, int(round((time.perf_counter() - started_at) * 1000.0))),
        )
        self.finished.emit(rgb_image, overlay_image, result_obj, error_text, duration_ms)

    @staticmethod
    def _prepare(profile: VisionProfile, rgb, cv2):
        from vision_template_detect import align_to_reference, normalize_gray, match_illumination
        height, width = rgb.shape[:2]
        x, y, roi_w, roi_h = profile.roi
        if width >= x + roi_w and height >= y + roi_h:
            work = rgb[y : y + roi_h, x : x + roi_w].copy()
        elif (width, height) != profile.work_size:
            work = cv2.resize(rgb, profile.work_size, interpolation=cv2.INTER_AREA)
        else:
            work = rgb
        if (work.shape[1], work.shape[0]) != profile.work_size:
            work = cv2.resize(work, profile.work_size, interpolation=cv2.INTER_AREA)
        gray = normalize_gray(work)
        aligned_rgb, aligned_gray, _, _, _, _ = align_to_reference(
            profile.template_gray, gray, work
        )
        # Match overlay illumination to the detection path so the displayed
        # mask is consistent with detect_image_array's OK/NG decision.
        aligned_gray = match_illumination(aligned_gray, profile.template_gray, profile.valid_mask)
        return aligned_rgb, aligned_gray


@dataclass
class DatasetPipelineResult:
    convert_result: ConvertImagesResult
    vision_output_dir: Optional[Path]
    vision_profile_path: Optional[Path]
    vision_error: str = ""
    model_name: str = ""
    png_output_dir: Optional[Path] = None
    pc_vision_config_dir: Optional[Path] = None


class DatasetConvertWorker(QObject):
    progress = Signal(int, int, str, bool, str)
    message = Signal(str)
    finished = Signal(object)
    failed = Signal(str)

    def __init__(
        self,
        input_dir: Path,
        output_dir: Path,
        model_name: str,
        roi: tuple[int, int, int, int],
        size: tuple[int, int],
        recursive: bool,
        flat: bool,
        generate_vision_profile: bool,
        vision_output_dir: Optional[Path],
    ) -> None:
        super().__init__()
        self.input_dir = input_dir
        self.output_dir = output_dir
        self.model_name = model_name
        self.roi = roi
        self.size = size
        self.recursive = recursive
        self.flat = flat
        self.generate_vision_profile = generate_vision_profile
        self.vision_output_dir = vision_output_dir

    @Slot()
    def run(self) -> None:
        try:
            safe_name = sanitize_model_name(self.model_name)
        except ValueError as exc:
            self.failed.emit(str(exc))
            return
        png_output_dir = self.output_dir / safe_name
        try:
            result = convert_images(
                input_dir=self.input_dir,
                output_dir=png_output_dir,
                roi=self.roi,
                size=self.size,
                recursive=self.recursive,
                flat=self.flat,
                progress_callback=self.on_progress,
            )
        except Exception as exc:
            self.failed.emit(str(exc))
            return

        vision_profile_path: Optional[Path] = None
        vision_error = ""
        pc_vision_config_dir: Optional[Path] = None
        if self.generate_vision_profile and result.ok_count > 0 and self.vision_output_dir is not None:
            try:
                self.message.emit(f"pc vision profile start: {self.vision_output_dir}")
                build_profile(
                    normal_dir=self.input_dir,
                    output_dir=self.vision_output_dir,
                    roi=self.roi,
                    work_size=(self.roi[2], self.roi[3]),
                    reference_count=DEFAULT_REFERENCE_COUNT,
                    diff_margin=DEFAULT_DIFF_MARGIN,
                    area_threshold=DEFAULT_AREA_THRESHOLD,
                    min_blob_area=DEFAULT_MIN_BLOB_AREA,
                    progress_callback=self.message.emit,
                )
                vision_profile_path = self.vision_output_dir / "vision_profile.json"
                pc_vision_config_dir = self.vision_output_dir
                self.message.emit(f"pc vision profile done: {vision_profile_path}")
            except Exception as exc:
                vision_error = str(exc)
                self.message.emit(f"pc vision profile failed: {vision_error}")

        self.finished.emit(
            DatasetPipelineResult(
                convert_result=result,
                vision_output_dir=self.vision_output_dir if self.generate_vision_profile else None,
                vision_profile_path=vision_profile_path,
                vision_error=vision_error,
                model_name=safe_name,
                png_output_dir=png_output_dir,
                pc_vision_config_dir=pc_vision_config_dir,
            )
        )

    def on_progress(self, current: int, total: int, bmp_path: Path, ok: bool, error: str) -> None:
        self.progress.emit(current, total, str(bmp_path), ok, error)


class ModelPackageWorker(QObject):
    finished = Signal(object)
    failed = Signal(str)

    def __init__(
        self,
        source: Path,
        output_root: Path,
        name: str,
        profile: str = DEFAULT_PROFILE,
    ) -> None:
        super().__init__()
        self.source = source
        self.output_root = output_root
        self.name = name
        self.profile = profile

    @Slot()
    def run(self) -> None:
        try:
            result = package_model(
                source=self.source,
                output_root=self.output_root,
                name=self.name,
                profile=self.profile,
            )
        except Exception as exc:
            self.failed.emit(str(exc))
            return
        self.finished.emit(result)


class VisionDetectWorker(QObject):
    progress = Signal(str)
    finished = Signal(object)
    failed = Signal(str)

    def __init__(
        self,
        normal_dir: Path,
        test_dir: Path,
        output_dir: Path,
        roi: tuple[int, int, int, int],
        work_size: tuple[int, int],
        reference_count: int,
        diff_margin: int,
        area_threshold: int,
        min_blob_area: int,
    ) -> None:
        super().__init__()
        self.normal_dir = normal_dir
        self.test_dir = test_dir
        self.output_dir = output_dir
        self.roi = roi
        self.work_size = work_size
        self.reference_count = reference_count
        self.diff_margin = diff_margin
        self.area_threshold = area_threshold
        self.min_blob_area = min_blob_area

    @Slot()
    def run(self) -> None:
        try:
            result = run_batch(
                normal_dir=self.normal_dir,
                test_dir=self.test_dir,
                output_dir=self.output_dir,
                roi=self.roi,
                work_size=self.work_size,
                reference_count=self.reference_count,
                diff_margin=self.diff_margin,
                area_threshold=self.area_threshold,
                min_blob_area=self.min_blob_area,
                progress_callback=self.progress.emit,
            )
        except Exception as exc:
            self.failed.emit(str(exc))
            return
        self.finished.emit(result)


class MainWindow(QMainWindow):
    """Industrial-style desktop console for status monitoring and tools."""

    def __init__(self, args: argparse.Namespace) -> None:
        super().__init__()
        self.args = args
        self.client: Optional[VisionBoardClient] = None
        self.latest_fields: Dict[str, str] = {}
        self.latest_device_fields: Dict[str, str] = {}
        self.packet_count = 0
        self.cmd_seq = 1
        self.gate_param_spins: Dict[str, QSpinBox] = {}
        self.gate_option_checks: Dict[str, QCheckBox] = {}
        self.align_param_spins: Dict[str, QSpinBox] = {}
        self.align_option_checks: Dict[str, QCheckBox] = {}
        self.dataset_input_dir: Optional[Path] = None
        self.dataset_output_dir: Optional[Path] = None
        self.dataset_vision_output_dir: Optional[Path] = None
        self.dataset_roi_spins: Dict[str, QSpinBox] = {}
        self.dataset_size_spins: Dict[str, QSpinBox] = {}
        self.dataset_thread: Optional[QThread] = None
        self.dataset_worker: Optional[DatasetConvertWorker] = None
        self.model_source_path: Optional[Path] = None
        self.model_output_root: Optional[Path] = None
        self.model_runtime_profile: str = DEFAULT_PROFILE
        self.model_thread: Optional[QThread] = None
        self.model_worker: Optional[ModelPackageWorker] = None
        # 工程调试 mode only controls the hidden image-debug / verification
        # sections. The engineering page itself remains visible for field tuning.
        self.engineering_mode: bool = False
        self.engineering_title_clicks = 0
        self.engineering_title_last_click_at = 0.0
        self.engineering_title_label: Optional[QLabel] = None
        self.engineering_long_press_timer = QTimer(self)
        self.engineering_long_press_timer.setSingleShot(True)
        self.engineering_long_press_timer.timeout.connect(self.disable_engineering_mode_from_title)
        # Latest object orientation (degrees) from the most recent inspection,
        # shown as an AI-style pose readout on the overview / result cards.
        self.latest_object_angle_deg: Optional[float] = None
        # Pending device-command name awaiting an ack, used to surface capture
        # session directory / save path feedback from the next vack message.
        self.pending_command: Optional[str] = None
        self.vision_normal_dir: Optional[Path] = None
        self.vision_test_dir: Optional[Path] = None
        self.vision_output_dir: Optional[Path] = None
        self.vision_param_spins: Dict[str, QSpinBox] = {}
        self.vision_thread: Optional[QThread] = None
        self.vision_worker: Optional[VisionDetectWorker] = None
        self.bridge = PacketBridge()
        self.bridge.packet_received.connect(self.on_packet_received)

        # 连接 page state. 端口s use defaults (not hard-coded literals)
        # so the spin boxes can override them at runtime.
        self.scan_thread: Optional[QThread] = None
        self.scan_worker: Optional[DeviceScanWorker] = None
        self.scan_devices: list[DeviceInfo] = []
        self.connected_device_ip: Optional[str] = None
        self.connected_device_port: int = DEFAULT_TCP_PORT
        self.udp_listen_port: int = DEFAULT_UDP_LISTEN_PORT
        self.last_tcp_keepalive_at = 0.0
        self.tcp_keepalive_ok = False
        self.tcp_keepalive_age_s: Optional[float] = None
        self.tcp_keepalive_fail_count = 0
        self.tcp_last_ok_at = 0.0
        self.tcp_last_error = ""

        # Debug page TCP frame detection state.
        self.current_profile: Optional[VisionProfile] = None
        self.current_model_name: str = ""
        self.frame_thread: Optional[QThread] = None
        self.frame_worker: Optional[TcpFrameReceiveWorker] = None
        self.last_debug_raw_image = None
        self.last_debug_overlay_image = None
        self.frame_request_id: Optional[int] = None
        self.frame_request_interactive = True
        self.last_auto_pc_req = 0
        self.last_auto_pc_skip_req = 0
        self.last_auto_pc_skip_reason = ""
        self.auto_pc_pending_req: Optional[int] = None
        self.setWindowTitle("智能视觉检测系统")
        self.setWindowIcon(QIcon(self._make_brand_icon(256)))
        self.resize(1440, 860)
        self.setMinimumSize(1180, 720)

        self.setup_ui()
        self.apply_style()

        self.refresh_timer = QTimer(self)
        self.refresh_timer.timeout.connect(self.refresh_runtime_state)
        self.refresh_timer.start(500)

    def setup_ui(self) -> None:
        central = QWidget()
        root = QVBoxLayout(central)
        root.setContentsMargins(20, 18, 20, 18)
        root.setSpacing(14)

        root.addWidget(self.build_header())

        body = QHBoxLayout()
        body.setSpacing(16)
        body.addWidget(self.build_sidebar())

        content = QVBoxLayout()
        content.setSpacing(14)
        content.addWidget(self.build_status_strip())
        content.addWidget(self.build_page_stack(), 1)
        body.addLayout(content, 1)

        root.addLayout(body, 1)
        self.setCentralWidget(central)

    def _make_brand_icon(self, size: int = 128) -> QPixmap:
        """Paint a clean camera-lens brand mark.

        A rounded blue square with a white lens ring + aperture dot reads as a
        machine-vision / inspection system and looks more professional than a
        plain "VB" text block. Used for both the window icon and the header
        logo, so the look stays consistent without shipping an extra icon file.
        """
        pm = QPixmap(size, size)
        pm.fill(Qt.GlobalColor.transparent)
        painter = QPainter(pm)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)

        radius = size * 0.22
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor("#3f5f74"))
        painter.drawRoundedRect(0, 0, size, size, radius, radius)

        cx = cy = size / 2.0
        r_out = size * 0.30
        ring = QPen(QColor("#ffffff"))
        ring.setWidthF(size * 0.072)
        painter.setPen(ring)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(QPointF(cx, cy), r_out, r_out)

        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor("#ffffff"))
        painter.drawEllipse(QPointF(cx, cy), size * 0.12, size * 0.12)

        painter.setBrush(QColor("#9fc0d6"))
        painter.drawEllipse(
            QPointF(cx + r_out * 0.55, cy - r_out * 0.55),
            size * 0.05,
            size * 0.05,
        )
        painter.end()
        return pm

    def _build_slogan_label(self) -> QLabel:
        """Competition slogan as an inline label for the header strip.

        Purely cosmetic — loaded from assets/contest_slogan.png (auto-cropped),
        sits centered between the brand title and the connection status. Falls
        back to text if the asset is missing so the UI never breaks.
        """
        banner = QLabel()
        banner.setObjectName("ContestBannerImage")
        banner.setAlignment(Qt.AlignmentFlag.AlignCenter)
        asset = Path(__file__).resolve().parent / "assets" / "contest_slogan.png"
        pixmap = QPixmap(str(asset))
        if not pixmap.isNull():
            banner.setPixmap(
                pixmap.scaledToHeight(40, Qt.TransformationMode.SmoothTransformation)
            )
        else:
            banner.setObjectName("ContestBannerText")
            banner.setText("AI 赋能设计，设计点亮 AI ！")
        return banner

    def build_header(self) -> QWidget:
        panel = QFrame()
        panel.setObjectName("HeaderPanel")
        layout = QHBoxLayout(panel)
        layout.setContentsMargins(18, 14, 18, 14)
        layout.setSpacing(14)

        brand = QHBoxLayout()
        brand.setSpacing(12)
        logo = QLabel()
        logo.setObjectName("LogoBlock")
        logo.setAlignment(Qt.AlignmentFlag.AlignCenter)
        logo.setPixmap(
            self._make_brand_icon(128).scaled(
                42,
                42,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
        )
        brand.addWidget(logo)

        title_box = QVBoxLayout()
        title_box.setSpacing(3)
        title = QLabel("智能视觉缺陷检测系统")
        title.setObjectName("WindowTitle")
        title.setCursor(Qt.CursorShape.PointingHandCursor)
        title.setToolTip("三击标题展开隐藏调试区；长按标题 3 秒收起。")
        title.installEventFilter(self)
        self.engineering_title_label = title
        subtitle = QLabel("AI 视觉推理 · 在线检测 · 自动分拣")
        subtitle.setObjectName("WindowSubTitle")
        self.header_subtitle_label = subtitle
        title_box.addWidget(title)
        title_box.addWidget(subtitle)
        brand.addLayout(title_box)
        layout.addLayout(brand)

        layout.addStretch(1)
        layout.addWidget(self._build_slogan_label())
        layout.addStretch(1)

        conn_box = QFrame()
        conn_box.setObjectName("ConnectionBox")
        conn = QHBoxLayout(conn_box)
        conn.setContentsMargins(10, 8, 10, 8)
        conn.setSpacing(10)

        # Connection setup (IP / port / scan / UDP start) lives on the dedicated
        # connection page; the header only shows read-only state.
        self.header_device_label = QLabel("设备 IP: -")
        self.header_device_label.setObjectName("FieldValue")
        self.header_udp_label = QLabel("UDP: 未启动")
        self.header_udp_label.setObjectName("FieldValue")
        self.header_link_label = QLabel("连接: 离线")
        self.header_link_label.setObjectName("FieldValue")
        conn.addWidget(self.header_device_label)
        conn.addWidget(self.header_udp_label)
        conn.addWidget(self.header_link_label)
        conn.addStretch(1)
        layout.addWidget(conn_box)
        return panel

    def build_sidebar(self) -> QWidget:
        panel = QFrame()
        panel.setObjectName("SidebarPanel")
        panel.setFixedWidth(196)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(14, 14, 14, 14)
        layout.setSpacing(12)

        section = QLabel("功能导航")
        section.setObjectName("SidebarTitle")
        layout.addWidget(section)

        self.nav_list = QListWidget()
        self.nav_list.setObjectName("NavigationList")
        self.nav_list.addItems(
            ["连接", "运行总览", "设备控制", "采集整理", "模型管理", "工程调试", "系统日志"]
        )
        self.nav_list.setCurrentRow(1)
        self.nav_list.currentRowChanged.connect(self.on_nav_changed)
        layout.addWidget(self.nav_list, 1)

        note = QLabel("UDP 状态通道\nTCP 图像通道\nRS485 设备总线")
        note.setObjectName("SidebarNote")
        layout.addWidget(note)
        return panel

    def build_status_strip(self) -> QWidget:
        panel = QFrame()
        panel.setObjectName("StatusStrip")
        layout = QHBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(14)

        self.connection_card = StatusCard("设备连接", "OFFLINE", "等待 UDP 状态包")
        self.result_card = StatusCard("检测结果", "IDLE", "暂无检测结果")
        self.angle_card = StatusCard("物品角度", "--", "AI 姿态推理")
        self.model_card = StatusCard("当前模型", "-", "source=-")
        self.count_card = StatusCard("产线统计", "0 / 0", "OK / NG")
        layout.addWidget(self.connection_card)
        layout.addWidget(self.result_card)
        layout.addWidget(self.angle_card)
        layout.addWidget(self.model_card)
        layout.addWidget(self.count_card)
        return panel

    def build_page_stack(self) -> QWidget:
        self.stack = QStackedWidget()
        self.stack.setObjectName("ContentStack")
        self.stack.addWidget(self.build_connection_page())
        self.stack.addWidget(self.build_overview_page())
        self.stack.addWidget(self.build_device_control_page())
        self.stack.addWidget(self.build_dataset_page())
        self.stack.addWidget(self.build_model_page())
        self.stack.addWidget(self.build_debug_page())
        self.stack.addWidget(self.build_log_page())
        return self.stack

    def build_page(self, title: str, subtitle: str = "") -> tuple[QWidget, QVBoxLayout]:
        page = QFrame()
        page.setObjectName("PagePanel")
        layout = QVBoxLayout(page)
        layout.setContentsMargins(20, 18, 20, 18)
        layout.setSpacing(14)

        heading_row = QHBoxLayout()
        heading_row.setSpacing(8)
        heading = QLabel(title)
        heading.setObjectName("PageTitle")
        heading_row.addWidget(heading)
        heading_row.addStretch(1)
        layout.addLayout(heading_row)

        if subtitle:
            sub = QLabel(subtitle)
            sub.setObjectName("PageSubTitle")
            sub.setWordWrap(True)
            layout.addWidget(sub)
        return page, layout

    def build_connection_page(self) -> QWidget:
        page, layout = self.build_page("连接", "设备扫描、连接管理和 UDP 监听设置。")

        scan_panel = QFrame()
        scan_panel.setObjectName("GatePanel")
        scan_layout = QVBoxLayout(scan_panel)
        scan_layout.setContentsMargins(0, 0, 0, 0)
        scan_layout.setSpacing(10)

        scan_title = QLabel("设备扫描")
        scan_title.setObjectName("ParamTitle")
        scan_layout.addWidget(scan_title)

        scan_param = QFrame()
        scan_param.setObjectName("ParamPanel")
        scan_row = QGridLayout(scan_param)
        scan_row.setContentsMargins(14, 14, 14, 14)
        scan_row.setHorizontalSpacing(14)
        scan_row.setVerticalSpacing(9)

        scan_row.addWidget(QLabel("网段"), 0, 0)
        self.scan_base_ip_edit = QLineEdit(DEFAULT_SCAN_BASE_IP)
        self.scan_base_ip_edit.setPlaceholderText("例如 192.168.1")
        scan_row.addWidget(self.scan_base_ip_edit, 0, 1)

        scan_row.addWidget(QLabel("端口"), 0, 2)
        self.scan_port_spin = QSpinBox()
        self.scan_port_spin.setRange(1, 65535)
        self.scan_port_spin.setValue(DEFAULT_SCAN_PORT)
        scan_row.addWidget(self.scan_port_spin, 0, 3)

        self.scan_button = QPushButton("扫描")
        self.scan_button.setObjectName("PrimaryButton")
        self.scan_button.clicked.connect(self.run_device_scan)
        scan_row.addWidget(self.scan_button, 0, 4)

        self.scan_progress_label = QLabel("尚未扫描")
        self.scan_progress_label.setObjectName("GateHint")
        scan_row.addWidget(self.scan_progress_label, 1, 0, 1, 5)

        scan_layout.addWidget(scan_param)

        self.scan_device_list = QListWidget()
        self.scan_device_list.setMinimumHeight(140)
        self.scan_device_list.itemSelectionChanged.connect(self.on_scan_device_selected)
        scan_layout.addWidget(self.scan_device_list)

        device_button_row = QHBoxLayout()
        device_button_row.setSpacing(8)
        self.connect_button = QPushButton("连接选中")
        self.connect_button.setObjectName("PrimaryButton")
        self.connect_button.clicked.connect(self.connect_selected_device)
        self.disconnect_button = QPushButton("断开")
        self.disconnect_button.clicked.connect(self.disconnect_device)
        self.disconnect_button.setEnabled(False)
        device_button_row.addWidget(self.connect_button)
        device_button_row.addWidget(self.disconnect_button)
        device_button_row.addStretch(1)
        scan_layout.addLayout(device_button_row)
        layout.addWidget(scan_panel)

        udp_panel = QFrame()
        udp_panel.setObjectName("GatePanel")
        udp_layout = QVBoxLayout(udp_panel)
        udp_layout.setContentsMargins(0, 0, 0, 0)
        udp_layout.setSpacing(10)

        udp_title = QLabel("UDP 监听")
        udp_title.setObjectName("ParamTitle")
        udp_layout.addWidget(udp_title)

        udp_param = QFrame()
        udp_param.setObjectName("ParamPanel")
        udp_row = QGridLayout(udp_param)
        udp_row.setContentsMargins(14, 14, 14, 14)
        udp_row.setHorizontalSpacing(14)
        udp_row.setVerticalSpacing(9)

        udp_row.addWidget(QLabel("本地端口"), 0, 0)
        self.conn_udp_port_spin = QSpinBox()
        self.conn_udp_port_spin.setRange(1, 65535)
        self.conn_udp_port_spin.setValue(DEFAULT_UDP_LISTEN_PORT)
        udp_row.addWidget(self.conn_udp_port_spin, 0, 1)

        self.udp_start_button = QPushButton("启动 UDP 监听")
        self.udp_start_button.setObjectName("PrimaryButton")
        self.udp_start_button.clicked.connect(self.start_listening)
        self.udp_stop_button = QPushButton("停止")
        self.udp_stop_button.clicked.connect(self.stop_listening)
        self.udp_stop_button.setEnabled(False)
        udp_row.addWidget(self.udp_start_button, 0, 2)
        udp_row.addWidget(self.udp_stop_button, 0, 3)
        udp_row.setColumnStretch(4, 1)
        udp_layout.addWidget(udp_param)
        layout.addWidget(udp_panel)

        self.connection_status_group = FieldGroup(
            "当前连接",
            [
                ("device_ip", "已连接设备 IP"),
                ("device_port", "设备端口"),
                ("udp_listen", "UDP 监听状态"),
                ("udp_packets", "UDP 包数"),
                ("link_state", "通信状态"),
            ],
        )
        self.connection_status_group.set_fields(
            {
                "device_ip": "-",
                "device_port": "-",
                "udp_listen": "未启动",
                "udp_packets": "0",
                "link_state": "离线",
            }
        )
        layout.addWidget(self.connection_status_group)

        hint = QLabel(
            "设备扫描由上位机主动发起 TCP 连接，不需要放开电脑入站防火墙。"
            "默认 TCP 图像端口为 5000，"
            "默认 UDP 监听端口为 16000。"
            "设备连接、断开和监听端口统一在本页配置。"
        )
        hint.setObjectName("GateHint")
        hint.setWordWrap(True)
        layout.addWidget(hint)
        layout.addStretch(1)

        scroll = QScrollArea()
        scroll.setObjectName("PageScroll")
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setWidget(page)
        return scroll

    def build_overview_page(self) -> QWidget:
        page, layout = self.build_page("运行总览", "生产运行状态和关键统计信息。")

        top = QHBoxLayout()
        top.setSpacing(14)
        self.overview_result = self.build_metric_panel("本次结果", "IDLE", "异常分数 - / 帧 -")
        self.overview_angle = self.build_metric_panel("物品角度", "--", "AI 姿态推理")
        self.overview_model = self.build_metric_panel("运行模型", "-", "source=-")
        self.overview_line = self.build_metric_panel("产线速度", "-", "rpm / acc")
        top.addWidget(self.overview_result)
        top.addWidget(self.overview_angle)
        top.addWidget(self.overview_model)
        top.addWidget(self.overview_line)
        layout.addLayout(top)

        flow_panel = QFrame()
        flow_panel.setObjectName("ParamPanel")
        flow_layout = QVBoxLayout(flow_panel)
        flow_layout.setContentsMargins(16, 14, 16, 16)
        flow_layout.setSpacing(12)

        flow_title = QLabel("流程控制")
        flow_title.setObjectName("ParamTitle")
        flow_layout.addWidget(flow_title)

        flow_button_row = QHBoxLayout()
        flow_button_row.setSpacing(10)
        self.flow_start_button = QPushButton("启动流程")
        self.flow_start_button.setObjectName("PrimaryButton")
        self.flow_start_button.setToolTip("下发 flow_start：启动产线自动检测流程（进料、检测、分拣循环）。")
        self.flow_start_button.clicked.connect(self.send_flow_start)
        self.flow_stop_button = QPushButton("停止流程")
        self.flow_stop_button.setToolTip("下发 flow_stop：停止自动检测流程并停带，演示结束或异常时使用。")
        self.flow_stop_button.clicked.connect(self.send_flow_stop)
        self.detect_once_button = QPushButton("单次检测")
        self.detect_once_button.setToolTip("下发 detect_once：对当前画面执行一次检测，用于单步演示与核验。")
        self.detect_once_button.clicked.connect(self.send_detect_once)
        flow_button_row.addWidget(self.flow_start_button)
        flow_button_row.addWidget(self.flow_stop_button)
        flow_button_row.addWidget(self.detect_once_button)
        flow_button_row.addStretch(1)
        flow_layout.addLayout(flow_button_row)

        self.flow_feedback_label = QLabel("流程命令反馈：等待操作")
        self.flow_feedback_label.setObjectName("GateHint")
        self.flow_feedback_label.setWordWrap(True)
        flow_layout.addWidget(self.flow_feedback_label)

        layout.addWidget(flow_panel)

        grid = QGridLayout()
        grid.setHorizontalSpacing(14)
        grid.setVerticalSpacing(16)

        self.detect_group = FieldGroup(
            "检测摘要",
            [
                ("frame", "Frame ID"),
                ("score", "异常分数 / 判定线"),
                ("angle", "物品角度"),
                ("total", "总数"),
                ("ok", "OK"),
                ("ng", "NG"),
                ("cap_ms", "采集耗时"),
                ("ai_ms", "推理耗时"),
            ],
        )
        self.line_group = FieldGroup(
            "产线摘要",
            [
                ("conveyor", "传送带"),
                ("rpm", "设定 RPM"),
                ("acc", "加速度"),
                ("speed", "实时速度"),
                ("pos", "位置"),
                ("action", "动作状态"),
                ("progress", "动作进度"),
            ],
        )
        self.health_group = FieldGroup(
            "设备健康",
            [
                ("cam", "相机"),
                ("bus", "RS485 总线"),
                ("arm_ready", "机械臂 Ready"),
                ("arm_homed", "机械臂 Homed"),
                ("arm_moving", "机械臂 Moving"),
                ("arm_estop", "E-停止"),
                ("conv_pos", "传送带位置"),
            ],
        )
        self.net_group = FieldGroup(
            "网络状态",
            [
                ("source", "来源"),
                ("seq", "序号"),
                ("udp_packets", "UDP 包数"),
                ("last_age", "最近包"),
                ("eth_ready", "W5500 Ready"),
                ("link", "网口状态"),
            ],
        )

        grid.addWidget(self.detect_group, 0, 0)
        grid.addWidget(self.line_group, 0, 1)
        grid.addWidget(self.health_group, 1, 0)
        grid.addWidget(self.net_group, 1, 1)
        layout.addLayout(grid, 1)

        scroll = QScrollArea()
        scroll.setObjectName("PageScroll")
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setWidget(page)
        return scroll

    def build_device_control_page(self) -> QWidget:
        page, layout = self.build_page(
            "设备控制",
            "通过设备命令服务控制传送带和机械臂，执行状态以实时上报为准。",
        )
        layout.setSpacing(18)

        status_grid = QGridLayout()
        status_grid.setHorizontalSpacing(16)
        status_grid.setVerticalSpacing(16)

        self.control_conveyor_group = FieldGroup(
            "传送带状态",
            [
                ("online", "在线"),
                ("rpm", "设定 RPM"),
                ("acc", "加速度"),
                ("speed", "实时速度"),
                ("pos", "位置"),
                ("state", "控制状态"),
            ],
        )
        self.control_conveyor_group.setMinimumHeight(198)
        self.control_arm_group = FieldGroup(
            "机械臂状态",
            [
                ("online", "在线"),
                ("ready", "Ready"),
                ("homed", "Homed"),
                ("moving", "Moving"),
                ("estop", "E-停止"),
                ("action", "动作状态"),
                ("progress", "进度"),
            ],
        )
        self.control_arm_group.setMinimumHeight(222)
        self.control_system_group = FieldGroup(
            "系统状态",
            [
                ("bus", "RS485 总线"),
                ("eth_ready", "以太网 Ready"),
                ("link", "网线 Link"),
                ("cam", "相机"),
                ("last_seq", "最近序号"),
                ("last_result", "最近结果"),
            ],
        )
        self.control_system_group.setMinimumHeight(198)

        status_grid.addWidget(self.control_conveyor_group, 0, 0)
        status_grid.addWidget(self.control_arm_group, 0, 1)
        status_grid.addWidget(self.control_system_group, 0, 2)
        status_grid.setColumnStretch(0, 1)
        status_grid.setColumnStretch(1, 1)
        status_grid.setColumnStretch(2, 1)
        layout.addLayout(status_grid)

        control_grid = QGridLayout()
        control_grid.setHorizontalSpacing(16)
        control_grid.setVerticalSpacing(16)

        conveyor_panel = QFrame()
        conveyor_panel.setObjectName("ParamPanel")
        conveyor_layout = QVBoxLayout(conveyor_panel)
        conveyor_layout.setContentsMargins(16, 14, 16, 16)
        conveyor_layout.setSpacing(14)

        conveyor_title = QLabel("传送带控制")
        conveyor_title.setObjectName("ParamTitle")
        conveyor_layout.addWidget(conveyor_title)

        param_grid = QGridLayout()
        param_grid.setHorizontalSpacing(14)
        param_grid.setVerticalSpacing(12)

        rpm_label = QLabel("目标转速")
        rpm_label.setObjectName("ParamLabel")
        param_grid.addWidget(rpm_label, 0, 0)
        self.device_rpm_spin = QSpinBox()
        self.device_rpm_spin.setRange(5, 3000)
        self.device_rpm_spin.setValue(10)
        self.device_rpm_spin.setSingleStep(5)
        self.device_rpm_spin.setObjectName("ParamSpin")
        self.device_rpm_spin.setMinimumWidth(132)
        self.device_rpm_spin.setToolTip(
            "传送带目标转速。调大：物品过得快、节拍高，但停位更难精准、易冲过头；"
            "调小：定位稳但慢。现场物品停不准、过冲就调小，嫌慢就调大。"
        )
        param_grid.addWidget(self.device_rpm_spin, 0, 1)
        rpm_unit = QLabel("RPM")
        rpm_unit.setObjectName("ParamLabel")
        param_grid.addWidget(rpm_unit, 0, 2)

        acc_label = QLabel("加速度")
        acc_label.setObjectName("ParamLabel")
        param_grid.addWidget(acc_label, 1, 0)
        self.device_acc_spin = QSpinBox()
        self.device_acc_spin.setRange(0, 255)
        self.device_acc_spin.setValue(0)
        self.device_acc_spin.setObjectName("ParamSpin")
        self.device_acc_spin.setMinimumWidth(132)
        self.device_acc_spin.setToolTip(
            "传送带加减速档位（0-255，0=最缓）。调大：起停更快、节拍高，但易抖动/丢步、停位过冲；"
            "调小：起停柔和、定位稳但慢。现场抖动或丢步就调小，启动太肉就调大。"
        )
        param_grid.addWidget(self.device_acc_spin, 1, 1)
        acc_unit = QLabel("0-255")
        acc_unit.setObjectName("ParamLabel")
        param_grid.addWidget(acc_unit, 1, 2)
        param_grid.setColumnStretch(3, 1)
        conveyor_layout.addLayout(param_grid)

        conveyor_button_row = QHBoxLayout()
        conveyor_button_row.setSpacing(10)
        read_button = QPushButton("读取状态")
        read_button.clicked.connect(self.send_device_get)
        set_button = QPushButton("应用参数")
        set_button.clicked.connect(self.send_conveyor_set)
        fill_button = QPushButton("填入当前值")
        fill_button.clicked.connect(self.fill_conveyor_params_from_status)
        conv_cw_button = QPushButton("传送带 CW")
        conv_cw_button.setObjectName("PrimaryButton")
        conv_ccw_button = QPushButton("传送带 CCW")
        conv_stop_button = QPushButton("传送带 停止")
        conv_cw_button.clicked.connect(self.send_conveyor_cw)
        conv_ccw_button.clicked.connect(self.send_conveyor_ccw)
        conv_stop_button.clicked.connect(self.send_conveyor_stop)
        conveyor_button_row.addWidget(read_button)
        conveyor_button_row.addWidget(fill_button)
        conveyor_button_row.addWidget(set_button)
        conveyor_button_row.addWidget(conv_cw_button)
        conveyor_button_row.addWidget(conv_ccw_button)
        conveyor_button_row.addWidget(conv_stop_button)
        conveyor_button_row.addStretch(1)
        conveyor_layout.addLayout(conveyor_button_row)

        conveyor_hint = QLabel("目标转速和加速度是待下发参数，实时上报不会覆盖输入框。需要以当前设备值为基准时，先点击“填入当前值”。")
        conveyor_hint.setObjectName("GateHint")
        conveyor_hint.setWordWrap(True)
        conveyor_layout.addWidget(conveyor_hint)

        arm_panel = QFrame()
        arm_panel.setObjectName("ParamPanel")
        arm_layout = QVBoxLayout(arm_panel)
        arm_layout.setContentsMargins(16, 14, 16, 16)
        arm_layout.setSpacing(14)

        arm_title = QLabel("机械臂控制")
        arm_title.setObjectName("ParamTitle")
        arm_layout.addWidget(arm_title)

        arm_row_top = QHBoxLayout()
        arm_row_top.setSpacing(10)
        arm_row_bottom = QHBoxLayout()
        arm_row_bottom.setSpacing(10)
        arm_specs = [
            ("刷新机械臂", self.send_arm_refresh, False),
            ("机械臂回零", self.send_arm_home, False),
            ("执行动作", self.send_arm_play, True),
            ("停止动作", self.send_arm_stop, False),
            ("急停", self.send_arm_estop, False),
            ("解除急停", self.send_arm_clear_estop, False),
        ]
        for label, handler, primary in arm_specs:
            button = QPushButton(label)
            if primary:
                button.setObjectName("PrimaryButton")
            button.clicked.connect(handler)
            if label in ("刷新机械臂", "机械臂回零", "执行动作"):
                arm_row_top.addWidget(button)
            else:
                arm_row_bottom.addWidget(button)
        arm_row_top.addStretch(1)
        arm_row_bottom.addStretch(1)
        arm_layout.addLayout(arm_row_top)
        arm_layout.addLayout(arm_row_bottom)

        arm_hint = QLabel("机械臂需处于 Ready 且未急停状态。动作序号由设备端自动分配。")
        arm_hint.setObjectName("GateHint")
        arm_hint.setWordWrap(True)
        arm_layout.addWidget(arm_hint)

        control_grid.addWidget(conveyor_panel, 0, 0)
        control_grid.addWidget(arm_panel, 0, 1)
        control_grid.setColumnStretch(0, 1)
        control_grid.setColumnStretch(1, 1)
        layout.addLayout(control_grid)

        self.device_command_group = FieldGroup(
            "命令反馈",
            [
                ("last_cmd", "最近命令"),
                ("ack", "回包"),
                ("queue", "队列统计"),
                ("note", "说明"),
            ],
        )
        self.device_command_group.set_fields(
            {
                "last_cmd": "-",
                "ack": "等待回包",
                "queue": "-",
                "note": "命令异步执行，请结合实时状态确认完成。",
            }
        )
        self.device_command_group.setMinimumHeight(148)
        layout.addWidget(self.device_command_group)
        layout.addStretch(1)

        scroll = QScrollArea()
        scroll.setObjectName("PageScroll")
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setWidget(page)
        return scroll
    def build_dataset_page(self) -> QWidget:
        page, layout = self.build_page("采集整理", "将采集图像整理为训练数据，并保持与设备端一致的预处理参数。")
        grid = QGridLayout()
        grid.setSpacing(12)

        self.dataset_group = FieldGroup(
            "采集整理任务",
            [
                ("input", "采集图片目录"),
                ("output", "输出目录"),
                ("roi", "ROI"),
                ("size", "目标尺寸"),
                ("vision", "AI 推理配置"),
                ("status", "状态"),
            ],
        )
        self.dataset_group.set_fields(
            {
                "input": "待选择",
                "output": "待选择",
                "roi": format_roi(DEFAULT_ROI),
                "size": format_size(DEFAULT_SIZE),
                "vision": "已启用",
                "status": "待处理",
            }
        )
        flow_group = FieldGroup(
            "处理流程",
            [
                ("step1", "1. 导入"),
                ("step2", "2. 裁剪"),
                ("step3", "3. 缩放"),
                ("step4", "4. 输出"),
            ],
        )
        flow_group.set_fields(
            {
                "step1": "选择采集图片目录",
                "step2": "按 ROI 裁剪目标区域",
                "step3": "缩放到模型输入尺寸",
                "step4": "输出到 根目录/模型名，并本地保存推理配置",
            }
        )
        grid.addWidget(self.dataset_group, 0, 0)
        grid.addWidget(flow_group, 0, 1)
        layout.addLayout(grid)

        layout.addWidget(self.build_dataset_param_panel())

        layout.addWidget(self.build_capture_panel())

        self.dataset_log = QPlainTextEdit()
        self.dataset_log.setReadOnly(True)
        self.dataset_log.setMaximumBlockCount(300)
        self.dataset_log.setMinimumHeight(160)
        self.dataset_log.setPlaceholderText("采集整理日志")
        layout.addWidget(self.dataset_log)

        hint = QLabel(
            "输入目录建议选择 SD 卡中的 collect_photos 或某一次采集会话目录。"
            "输出 PNG 会写入“输出根目录/模型名称”。"
            "这里设置的模型名称会继续用于后续模型打包。"
            "默认预处理流程与设备端一致：320x240 RGB565 BMP -> ROI 40,0,240,240 -> 96x96 PNG。"
            "如果后续调整模型输入尺寸，需要同步调整目标尺寸并保持 ROI 稳定。"
        )
        hint.setObjectName("GateHint")
        hint.setWordWrap(True)
        layout.addWidget(hint)
        layout.addStretch(1)

        scroll = QScrollArea()
        scroll.setObjectName("PageScroll")
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setWidget(page)
        return scroll

    def build_dataset_param_panel(self) -> QWidget:
        panel = QFrame()
        panel.setObjectName("GatePanel")
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        title = QLabel("图像预处理参数")
        title.setObjectName("ParamTitle")
        layout.addWidget(title)

        group = QFrame()
        group.setObjectName("ParamPanel")
        grid = QGridLayout(group)
        grid.setContentsMargins(14, 14, 14, 14)
        grid.setHorizontalSpacing(14)
        grid.setVerticalSpacing(9)

        roi_specs = [
            ("x", "ROI X", 0, 319, DEFAULT_ROI[0]),
            ("y", "ROI Y", 0, 239, DEFAULT_ROI[1]),
            ("w", "ROI W", 1, 320, DEFAULT_ROI[2]),
            ("h", "ROI H", 1, 240, DEFAULT_ROI[3]),
        ]
        for index, (key, label, minimum, maximum, value) in enumerate(roi_specs):
            param_label = QLabel(label)
            param_label.setObjectName("ParamLabel")
            grid.addWidget(param_label, 0, index * 2)
            spin = QSpinBox()
            spin.setRange(minimum, maximum)
            spin.setValue(value)
            spin.setObjectName("ParamSpin")
            self.dataset_roi_spins[key] = spin
            grid.addWidget(spin, 0, index * 2 + 1)

        size_specs = [
            ("w", "输出 W", 16, 640, DEFAULT_SIZE[0]),
            ("h", "输出 H", 16, 640, DEFAULT_SIZE[1]),
        ]
        for index, (key, label, minimum, maximum, value) in enumerate(size_specs):
            param_label = QLabel(label)
            param_label.setObjectName("ParamLabel")
            grid.addWidget(param_label, 1, index * 2)
            spin = QSpinBox()
            spin.setRange(minimum, maximum)
            spin.setValue(value)
            spin.setSingleStep(8)
            spin.setObjectName("ParamSpin")
            self.dataset_size_spins[key] = spin
            grid.addWidget(spin, 1, index * 2 + 1)

        self.dataset_recursive_check = QCheckBox("递归扫描子目录")
        self.dataset_recursive_check.setChecked(True)
        self.dataset_flat_check = QCheckBox("平铺输出到一个目录")
        self.dataset_vision_check = QCheckBox("同时生成检测配置")
        self.dataset_vision_check.setChecked(True)
        self.dataset_vision_check.stateChanged.connect(lambda _state: self.update_dataset_summary())
        grid.addWidget(self.dataset_recursive_check, 2, 0, 1, 2)
        grid.addWidget(self.dataset_flat_check, 2, 2, 1, 2)

        # The PC-detection checkbox lives in its own container so that hiding
        # it when engineering mode is off collapses the grid row completely,
        # without leaving any blank line.
        self.dataset_vision_container = QWidget()
        vision_container_layout = QHBoxLayout(self.dataset_vision_container)
        vision_container_layout.setContentsMargins(0, 0, 0, 0)
        vision_container_layout.setSpacing(8)
        vision_container_layout.addWidget(self.dataset_vision_check)
        vision_container_layout.addStretch(1)
        # PC-vision assisted inspection is hidden during normal operation.
        self.dataset_vision_container.setVisible(False)
        grid.addWidget(self.dataset_vision_container, 3, 0, 1, 4)

        # 模型名称 input lives in its own row on the same grid so it aligns
        # with ROI/size controls without disturbing their column structure.
        # Naming here (鍓嶇疆) drives downstream model package naming and the
        # local PC 检测配置 directory name.
        model_name_label = QLabel("模型名称")
        model_name_label.setObjectName("ParamLabel")
        model_name_label.setMinimumHeight(24)
        grid.addWidget(model_name_label, 4, 0)
        self.dataset_model_name_edit = QLineEdit(DEFAULT_DATASET_MODEL_NAME)
        self.dataset_model_name_edit.setPlaceholderText("例如 icon_v24_96")
        self.dataset_model_name_edit.textChanged.connect(lambda _t: self.update_dataset_summary())
        grid.addWidget(self.dataset_model_name_edit, 4, 1, 1, 3)
        layout.addWidget(group)

        button_row = QHBoxLayout()
        button_row.setSpacing(8)
        self.dataset_input_button = QPushButton("选择采集目录")
        self.dataset_output_button = QPushButton("选择输出目录")
        self.dataset_convert_button = QPushButton("开始整理")
        self.dataset_open_button = QPushButton("打开输出目录")
        self.dataset_convert_button.setObjectName("PrimaryButton")
        self.dataset_input_button.clicked.connect(self.choose_dataset_input_dir)
        self.dataset_output_button.clicked.connect(self.choose_dataset_output_dir)
        self.dataset_convert_button.clicked.connect(self.run_dataset_convert)
        self.dataset_open_button.clicked.connect(self.open_dataset_output_dir)
        button_row.addWidget(self.dataset_input_button)
        button_row.addWidget(self.dataset_output_button)
        button_row.addWidget(self.dataset_convert_button)
        button_row.addWidget(self.dataset_open_button)
        button_row.addStretch(1)
        layout.addLayout(button_row)
        return panel

    def build_capture_panel(self) -> QWidget:
        """Device-side capture controls: drive the board to grab samples to SD card."""
        panel = QFrame()
        panel.setObjectName("ParamPanel")
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(16, 14, 16, 16)
        layout.setSpacing(12)

        title = QLabel("设备采集（存SD卡）")
        title.setObjectName("ParamTitle")
        layout.addWidget(title)

        button_row = QHBoxLayout()
        button_row.setSpacing(10)
        self.capture_begin_button = QPushButton("开始采集会话")
        self.capture_begin_button.setObjectName("PrimaryButton")
        self.capture_begin_button.setToolTip(
            "下发 capture_begin：在设备 SD 卡上新建一个采集会话目录，"
            "之后的拍照都会存入这个目录。开始一批新样本前先点这里。"
        )
        self.capture_begin_button.clicked.connect(self.send_capture_begin)
        self.capture_one_button = QPushButton("拍照存SD")
        self.capture_one_button.setToolTip(
            "下发 capture_one：拍摄当前画面并保存到 SD 卡当前会话目录。"
            "摆好一个样本就点一次，可连续多拍以采集不同角度/位置。"
        )
        self.capture_one_button.clicked.connect(self.send_capture_one)
        self.capture_end_button = QPushButton("结束采集会话")
        self.capture_end_button.setToolTip(
            "下发 capture_end：结束当前采集会话。采集完一批样本后点这里收尾，"
            "再把 SD 卡里的图片导入“采集整理”。"
        )
        self.capture_end_button.clicked.connect(self.send_capture_end)
        button_row.addWidget(self.capture_begin_button)
        button_row.addWidget(self.capture_one_button)
        button_row.addWidget(self.capture_end_button)
        button_row.addStretch(1)
        layout.addLayout(button_row)

        self.capture_feedback_label = QLabel("采集反馈：等待操作")
        self.capture_feedback_label.setObjectName("GateHint")
        self.capture_feedback_label.setWordWrap(True)
        layout.addWidget(self.capture_feedback_label)
        return panel

    def build_model_page(self) -> QWidget:
        page, layout = self.build_page("模型管理", "整理 Edge Impulse 导出工程，并生成设备可加载的模型目录。")
        grid = QGridLayout()
        grid.setSpacing(12)

        self.model_runtime_group = FieldGroup(
            "当前运行模型",
            [
                ("name", "模型名称"),
                ("source", "来源"),
                ("score", "最近分数"),
                ("frame", "Frame ID"),
                ("status", "检测状态"),
            ],
        )
        self.model_pack_group = FieldGroup(
            "模型打包",
            [
                ("ei_dir", "EI 工程"),
                ("display_name", "最近模型名"),
                ("output_dir", "输出目录"),
                ("profile", "运行模板"),
                ("pack_status", "打包状态"),
                ("details", "打包详情"),
            ],
        )
        self.model_pack_group.set_fields(
            {
                "ei_dir": "待选择",
                "display_name": self.recent_dataset_model_name_display(),
                "output_dir": "待选择",
                "profile": DISPLAY_RUNTIME_PROFILE_NAME,
                "pack_status": "待处理",
                "details": "-",
            }
        )
        grid.addWidget(self.model_runtime_group, 0, 0)
        grid.addWidget(self.model_pack_group, 0, 1)
        layout.addLayout(grid)

        panel = QFrame()
        panel.setObjectName("GatePanel")
        panel_layout = QVBoxLayout(panel)
        panel_layout.setContentsMargins(0, 0, 0, 0)
        panel_layout.setSpacing(10)

        title = QLabel("EI 工程打包")
        title.setObjectName("ParamTitle")
        panel_layout.addWidget(title)

        param = QFrame()
        param.setObjectName("ParamPanel")
        form = QGridLayout(param)
        form.setContentsMargins(14, 14, 14, 14)
        form.setHorizontalSpacing(14)
        form.setVerticalSpacing(10)

        model_name_label = QLabel("最近模型名")
        model_name_label.setObjectName("ParamLabel")
        self.model_name_edit = QLineEdit(self.recent_dataset_model_name_display())
        self.model_name_edit.setReadOnly(True)
        self.model_name_edit.setPlaceholderText("请先完成一次采集整理以确定模型名")
        self.model_profile_edit = QLineEdit(DISPLAY_RUNTIME_PROFILE_NAME)
        self.model_profile_edit.setReadOnly(True)
        self.model_profile_edit.setPlaceholderText("保持默认模板即可")
        profile_label = QLabel("运行模板")
        profile_label.setObjectName("ParamLabel")
        form.addWidget(model_name_label, 0, 0)
        form.addWidget(self.model_name_edit, 0, 1)
        form.addWidget(profile_label, 0, 2)
        form.addWidget(self.model_profile_edit, 0, 3)
        panel_layout.addWidget(param)

        button_row = QHBoxLayout()
        button_row.setSpacing(8)
        self.model_source_button = QPushButton("选择 EI ZIP/目录")
        self.model_output_button = QPushButton("选择输出根目录")
        self.model_pack_button = QPushButton("生成模型包")
        self.model_open_button = QPushButton("打开输出目录")
        self.model_pack_button.setObjectName("PrimaryButton")
        self.model_source_button.clicked.connect(self.choose_model_source)
        self.model_output_button.clicked.connect(self.choose_model_output_root)
        self.model_pack_button.clicked.connect(self.run_model_package)
        self.model_open_button.clicked.connect(self.open_model_output_dir)
        button_row.addWidget(self.model_source_button)
        button_row.addWidget(self.model_output_button)
        button_row.addWidget(self.model_pack_button)
        button_row.addWidget(self.model_open_button)
        button_row.addStretch(1)
        panel_layout.addLayout(button_row)
        layout.addWidget(panel)

        self.model_log = QPlainTextEdit()
        self.model_log.setReadOnly(True)
        self.model_log.setMaximumBlockCount(300)
        self.model_log.setMinimumHeight(170)
        self.model_log.setPlaceholderText("模型打包日志")
        layout.addWidget(self.model_log)

        hint = QLabel(
            "模型名称来自最近一次采集整理；输出目录建议选 SD 卡的 vision/models。"
            "生成目录包含 model.tflite、anomaly_model.tflite、model.ini。"
        )
        hint.setObjectName("GateHint")
        hint.setWordWrap(True)
        layout.addWidget(hint)
        layout.addStretch(1)

        scroll = QScrollArea()
        scroll.setObjectName("PageScroll")
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setWidget(page)
        return scroll

    def build_debug_page(self) -> QWidget:
        page, layout = self.build_page("工程调试", "设备参数、图像接收和算法验证工具集中在此页面。")

        self.debug_transport_group = FieldGroup(
            "图像通道",
            [
                ("board_ip", "设备 IP"),
                ("tcp_port", "TCP 端口"),
                ("frame", "Frame ID"),
                ("status", "状态"),
            ],
        )
        self.debug_transport_group.set_fields(
            {
                "board_ip": self.args.board_ip,
                "tcp_port": str(DEFAULT_TCP_PORT),
                "frame": "-",
                "status": "等待",
            }
        )
        layout.addWidget(self.debug_transport_group)

        gate_row = QHBoxLayout()
        gate_row.setSpacing(12)
        gate_row.addWidget(self.build_gate_status_panel(), 1)
        gate_row.addWidget(self.build_gate_param_panel(), 2)
        layout.addLayout(gate_row)

        layout.addWidget(self.build_align_param_panel())

        self.debug_image_panel = QWidget()
        image_row = QHBoxLayout(self.debug_image_panel)
        image_row.setContentsMargins(0, 0, 0, 0)
        image_row.setSpacing(12)
        self.debug_raw_placeholder = ImagePlaceholder("原始图像\n接收 TCP 图像后显示")
        self.debug_overlay_placeholder = ImagePlaceholder("检测标记\n处理完成后显示")
        image_row.addWidget(self.debug_raw_placeholder)
        image_row.addWidget(self.debug_overlay_placeholder)
        self.debug_image_panel.setVisible(False)
        layout.addWidget(self.debug_image_panel)

        self.debug_frame_button_bar = QWidget()
        frame_button_row = QHBoxLayout(self.debug_frame_button_bar)
        frame_button_row.setContentsMargins(0, 0, 0, 0)
        frame_button_row.setSpacing(8)
        self.debug_receive_button = QPushButton("接收一帧")
        self.debug_receive_button.setObjectName("PrimaryButton")
        self.debug_receive_button.clicked.connect(self.receive_and_detect_frame)
        self.debug_save_button = QPushButton("保存图像")
        self.debug_save_button.clicked.connect(self.save_debug_frame)
        self.debug_save_button.setEnabled(False)
        self.debug_clear_button = QPushButton("清空显示")
        self.debug_clear_button.clicked.connect(self.clear_debug_frame)
        self.debug_clear_button.setEnabled(False)
        frame_button_row.addWidget(self.debug_receive_button)
        frame_button_row.addWidget(self.debug_save_button)
        frame_button_row.addWidget(self.debug_clear_button)
        frame_button_row.addStretch(1)
        self.debug_frame_button_bar.setVisible(False)
        layout.addWidget(self.debug_frame_button_bar)

        self.vision_verify_panel = self.build_vision_verify_panel()
        # PC-vision assisted inspection is hidden during normal operation.
        self.vision_verify_panel.setVisible(False)
        layout.addWidget(self.vision_verify_panel)
        layout.addStretch(1)

        scroll = QScrollArea()
        scroll.setObjectName("PageScroll")
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setWidget(page)
        return scroll

    def build_vision_verify_panel(self) -> QWidget:
        panel = QFrame()
        panel.setObjectName("GatePanel")
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        title = QLabel("视觉校准与验证")
        title.setObjectName("ParamTitle")
        layout.addWidget(title)

        self.vision_group = FieldGroup(
            "离线检测配置",
            [
                ("normal", "正常样本"),
                ("test", "测试样本"),
                ("output", "输出目录"),
                ("profile", "检测配置"),
                ("result", "批量结果"),
            ],
        )
        self.vision_group.set_fields(
            {
                "normal": "待选择",
                "test": "待选择",
                "output": "待选择",
                "profile": "未生成",
                "result": "未验证",
            }
        )
        layout.addWidget(self.vision_group)

        params = QFrame()
        params.setObjectName("ParamPanel")
        grid = QGridLayout(params)
        grid.setContentsMargins(14, 14, 14, 14)
        grid.setHorizontalSpacing(14)
        grid.setVerticalSpacing(9)
        specs = [
            ("ref_count", "参考样本数", 1, 30, DEFAULT_REFERENCE_COUNT),
            ("diff_margin", "差异裕量", 1, 100, DEFAULT_DIFF_MARGIN),
            ("area_threshold", "NG 面积阈值", 1, 20000, DEFAULT_AREA_THRESHOLD),
            ("min_blob_area", "最小连通域", 1, 2000, DEFAULT_MIN_BLOB_AREA),
        ]
        vision_tips = {
            "ref_count": "参与建立基准的正常样本数量。调大：基准更稳、更宽容光照/位置波动，但可能漏小缺陷；调小：更敏感但易把正常波动误判 NG。误报多调大，漏报多调小。",
            "diff_margin": "差异裕量。允许的逐像素波动余量，越大越宽容。现场误报（正常被判 NG）多就调大；漏检（缺陷判 OK）就调小。",
            "area_threshold": "NG 面积阈值。缺陷区域累计面积超过它才判 NG。误报多调大，细小缺陷漏判调小。",
            "min_blob_area": "最小连通域。小于它的孤立差异点视作噪声丢弃。画面噪点导致误报就调大；缺陷本身很小被滤掉就调小。",
        }
        for index, (key, label, minimum, maximum, value) in enumerate(specs):
            param_label = QLabel(label)
            param_label.setObjectName("ParamLabel")
            grid.addWidget(param_label, index // 2, (index % 2) * 2)
            spin = QSpinBox()
            spin.setRange(minimum, maximum)
            spin.setValue(value)
            spin.setObjectName("ParamSpin")
            tip = vision_tips.get(key)
            if tip:
                spin.setToolTip(tip)
                param_label.setToolTip(tip)
            self.vision_param_spins[key] = spin
            grid.addWidget(spin, index // 2, (index % 2) * 2 + 1)
        layout.addWidget(params)

        button_row = QHBoxLayout()
        button_row.setSpacing(8)
        self.vision_normal_button = QPushButton("选择正常样本")
        self.vision_test_button = QPushButton("选择测试样本")
        self.vision_output_button = QPushButton("选择输出目录")
        self.vision_run_button = QPushButton("生成配置并验证")
        self.vision_open_button = QPushButton("打开输出目录")
        self.vision_run_button.setObjectName("PrimaryButton")
        self.vision_normal_button.clicked.connect(self.choose_vision_normal_dir)
        self.vision_test_button.clicked.connect(self.choose_vision_test_dir)
        self.vision_output_button.clicked.connect(self.choose_vision_output_dir)
        self.vision_run_button.clicked.connect(self.run_vision_verify)
        self.vision_open_button.clicked.connect(self.open_vision_output_dir)
        button_row.addWidget(self.vision_normal_button)
        button_row.addWidget(self.vision_test_button)
        button_row.addWidget(self.vision_output_button)
        button_row.addWidget(self.vision_run_button)
        button_row.addWidget(self.vision_open_button)
        button_row.addStretch(1)
        layout.addLayout(button_row)

        self.vision_log = QPlainTextEdit()
        self.vision_log.setReadOnly(True)
        self.vision_log.setMaximumBlockCount(300)
        self.vision_log.setMinimumHeight(140)
        self.vision_log.setPlaceholderText("视觉检测日志")
        layout.addWidget(self.vision_log)

        hint = QLabel(
            "前几张正常样本应尽量居中、方向端正，用作基准模板。"
            "后续正常样本用于学习允许的位置、角度和光照波动；NG 样本只用于验证效果。"
            "输出内容包含报告、模板、容差图和检测标记图。"
        )
        hint.setObjectName("GateHint")
        hint.setWordWrap(True)
        layout.addWidget(hint)
        return panel

    def build_gate_status_panel(self) -> QWidget:
        panel = QFrame()
        panel.setObjectName("GatePanel")
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        self.gate_status_group = FieldGroup(
            "Vision Gate 状态",
            [
                ("enabled", "启用"),
                ("state", "状态"),
                ("area", "当前面积"),
                ("trigger", "触发次数"),
                ("candidate", "候选"),
                ("inside", "边界有效"),
                ("detectable", "可检测"),
                ("bbox_cy", "中心Y"),
                ("ack", "命令反馈"),
            ],
        )
        self.gate_status_group.set_fields(
            {
                "enabled": "-",
                "state": "-",
                "area": "-",
                "trigger": "-",
                "candidate": "-",
                "inside": "-",
                "detectable": "-",
                "bbox_cy": "-",
                "ack": "waiting for device status",
            }
        )
        layout.addWidget(self.gate_status_group)
        return panel

    def build_gate_param_panel(self) -> QWidget:
        panel = QFrame()
        panel.setObjectName("GatePanel")
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        title = QLabel("软件门限参数")
        title.setObjectName("ParamTitle")
        layout.addWidget(title)

        option_panel = QFrame()
        option_panel.setObjectName("GateOptionStrip")
        option_row = QHBoxLayout(option_panel)
        option_row.setContentsMargins(12, 10, 12, 10)
        option_row.setSpacing(18)
        option_specs = [
            ("enabled", "启用门限", False),
            ("debug", "串口调试输出", False),
            ("overlay", "LCD 叠加显示", True),
        ]
        for key, label, checked in option_specs:
            check = QCheckBox(label)
            check.setChecked(checked)
            self.gate_option_checks[key] = check
            option_row.addWidget(check)
        option_row.addStretch(1)
        layout.addWidget(option_panel)

        group = QFrame()
        group.setObjectName("ParamPanel")
        group.setMinimumHeight(270)
        grid = QGridLayout(group)
        grid.setContentsMargins(14, 14, 14, 14)
        grid.setHorizontalSpacing(14)
        grid.setVerticalSpacing(9)

        specs = [
            ("bright", "亮度阈值", 0, 255, 130),
            ("area_on", "进入面积", 0, 57600, 1800),
            ("area_off", "离开面积", 0, 57600, 12000),
            ("area_max", "最大面积", 0, 57600, 45000),
            ("min_w", "最小宽度", 1, 240, 40),
            ("min_h", "最小高度", 1, 240, 30),
            ("side", "左右边距", 0, 100, 5),
            ("top", "上边距", 0, 100, 5),
            ("bottom", "下边距", 0, 100, 15),
            ("center_y", "中心Y", 0, 239, 120),
            ("center_tol", "中心容差", 0, 240, 55),
            ("stable", "稳定帧数", 1, 10, 3),
            ("rearm", "重布防空帧", 1, 30, 8),
            ("cooldown", "冷却时间 ms", 0, 10000, 500),
            ("debug_ms", "调试周期 ms", 1, 10000, 500),
        ]
        gate_tips = {
            "bright": "前景判定的亮度门限。物品比背景暗用低于法，反之相反。漏检（看不到物品）就调到更易分出前景的方向，背景噪点被当成物品就反向调。",
            "area_on": "进入面积：连通域大于它才算“有物品进场”。误触发（空带也触发）调大；小物品进不来调小。",
            "area_off": "离开面积：面积小于它才算物品离场、重新布防。物品走了迟迟不复位调大；过早复位调小。",
            "area_max": "最大面积：超过它认为是异常/多个物品或贴边，忽略不触发。正常大物品被滤掉就调大。",
            "min_w": "目标最小宽度（px）。小于它不算有效目标。细小物品被漏掉调小；噪点误判调大。",
            "min_h": "目标最小高度（px）。同最小宽度，按物品实际高度调。",
            "side": "左右边距（px）。物品中心进入画面两侧这么多像素内不算完整进场。半个物体露头误触发就调大。",
            "top": "上边距（px）。物品未完全进入上边界这么多像素不判完整。顶部裁切误判调大。",
            "bottom": "下边距（px）。物品接近下边界这么多像素内不判完整，避免还没到位就触发。过早触发调大。",
            "center_y": "中心Y目标位置（px，画面高 240）。物品中心要落在它附近才算到位，配合“中心容差”。",
            "center_tol": "中心容差（px）。中心Y允许的偏差。频繁“到位但偏一点没触发”调大；停位要求精准调小。",
            "stable": "稳定帧数。连续这么多帧满足条件才确认触发，防抖。误触发多调大；响应迟钝调小。",
            "rearm": "重布防空帧。连续这么多帧无物品才重新布防，防同一物品反复触发。重复触发调大。",
            "cooldown": "冷却时间（ms）。一次触发后这段时间内不再触发。两件物品挨太近被并成一次就调小；同件重复触发就调大。",
            "debug_ms": "串口调试输出周期（ms）。仅影响调试日志频率，不影响检测。",
        }
        for index, (key, label, minimum, maximum, value) in enumerate(specs):
            row = index // 2
            col = (index % 2) * 2
            param_label = QLabel(label)
            param_label.setObjectName("ParamLabel")
            grid.addWidget(param_label, row, col)
            spin = QSpinBox()
            spin.setRange(minimum, maximum)
            spin.setValue(value)
            spin.setObjectName("ParamSpin")
            tip = gate_tips.get(key)
            if tip:
                spin.setToolTip(tip)
                param_label.setToolTip(tip)
            self.gate_param_spins[key] = spin
            grid.addWidget(spin, row, col + 1)

        layout.addWidget(group)

        button_row = QHBoxLayout()
        button_row.setSpacing(8)
        self.gate_enable_button = QPushButton("启用门限")
        self.gate_disable_button = QPushButton("关闭门限")
        self.gate_read_button = QPushButton("读取当前")
        self.gate_apply_button = QPushButton("应用到设备")
        self.gate_save_button = QPushButton("保存到 SD 卡")
        self.gate_default_button = QPushButton("恢复默认")
        self.gate_help_button = QPushButton("说明")
        self.gate_enable_button.setObjectName("PrimaryButton")
        self.gate_apply_button.setObjectName("PrimaryButton")
        self.gate_enable_button.clicked.connect(self.send_gate_enable)
        self.gate_disable_button.clicked.connect(self.send_gate_disable)
        self.gate_read_button.clicked.connect(self.send_gate_get)
        self.gate_apply_button.clicked.connect(self.send_gate_set)
        self.gate_save_button.clicked.connect(self.send_gate_save)
        self.gate_default_button.clicked.connect(self.send_gate_default)
        self.gate_help_button.clicked.connect(self.show_gate_help)
        button_row.addWidget(self.gate_enable_button)
        button_row.addWidget(self.gate_disable_button)
        button_row.addWidget(self.gate_read_button)
        button_row.addWidget(self.gate_apply_button)
        button_row.addWidget(self.gate_save_button)
        button_row.addWidget(self.gate_default_button)
        button_row.addWidget(self.gate_help_button)
        button_row.addStretch(1)
        layout.addLayout(button_row)

        hint = QLabel(
            "候选=看到目标；可检测=完整进入有效窗口。半个物体进来时通常候选=是、可检测=否。"
        )
        hint.setObjectName("GateHint")
        hint.setWordWrap(True)
        layout.addWidget(hint)
        return panel

    def show_gate_help(self) -> None:
        QMessageBox.information(
            self,
            "门限说明",
            "1. 亮度阈值：先把背景和物体分开。\n"
            "2. 进入面积：够大才算真正来料。\n"
            "3. 离开面积：够小才重新布防。\n"
            "4. 稳定帧数：防抖。\n"
            "5. 重布防空帧 / 冷却时间：防同一物体重复触发。\n\n"
            "现场调参顺序：先调亮度，再调进入面积，再看离开面积，最后再微调稳定和冷却。",
        )

    def build_align_param_panel(self) -> QWidget:
        panel = QFrame()
        panel.setObjectName("GatePanel")
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        title = QLabel("停带后居中补偿")
        title.setObjectName("ParamTitle")
        layout.addWidget(title)

        option_panel = QFrame()
        option_panel.setObjectName("GateOptionStrip")
        option_row = QHBoxLayout(option_panel)
        option_row.setContentsMargins(12, 10, 12, 10)
        option_row.setSpacing(18)
        option_specs = [
            ("enabled", "启用补偿", True),
            ("reverse", "反向映射", False),
        ]
        for key, label, checked in option_specs:
            check = QCheckBox(label)
            check.setChecked(checked)
            self.align_option_checks[key] = check
            option_row.addWidget(check)
        option_row.addStretch(1)
        layout.addWidget(option_panel)

        group = QFrame()
        group.setObjectName("ParamPanel")
        grid = QGridLayout(group)
        grid.setContentsMargins(14, 14, 14, 14)
        grid.setHorizontalSpacing(14)
        grid.setVerticalSpacing(9)

        specs = [
            ("target", "目标 Y", 0, 239, 120),
            ("tol", "允许误差 px", 0, 80, 15),
            ("gain", "增益 脉冲/px", 0, 200, 5),
            ("min", "最小脉冲", 1, 3000, 30),
            ("max", "最大脉冲", 1, 3000, 300),
            ("rpm", "补偿 RPM", 1, 60, 3),
            ("acc", "补偿 ACC", 0, 255, 0),
            ("iter", "最大迭代", 0, 10, 5),
            ("settle", "稳定等待 ms", 0, 3000, 350),
        ]
        align_tips = {
            "target": "停带后物品中心要对齐的目标Y（px，画面高 240），一般取 120 居中。想让物品停在画面更上/更下就改它。",
            "tol": "允许误差（px）。中心离目标Y在此范围内即认为对齐完成，停止微调。常用 10~20。频繁来回微调（震荡）就调大；定位要求高就调小。",
            "gain": "增益（脉冲/px）。每偏差 1px 下发的补偿脉冲。调大：收敛快但易过冲/震荡；调小：稳但慢。过冲就调小，迟迟到不了位就调大。",
            "min": "最小脉冲。每次补偿至少走这么多脉冲，避免太小走不动。物品贴近目标却卡住不动就适当调大。",
            "max": "最大脉冲。单次补偿上限，防一次冲太远。过冲明显就调小。",
            "rpm": "补偿动作转速。调大补偿快但易过冲，调小稳。微调过冲就调小。",
            "acc": "补偿动作加减速档（0-255，0=最缓）。抖动/丢步调小，太肉调大。",
            "iter": "最大迭代次数。最多微调几轮就收手，防止反复调不停。老是调不到位就检查增益/误差，再适当加迭代。",
            "settle": "每轮补偿后的稳定等待（ms），等机械停稳再读位置。读数跳动导致误判就调大。",
        }
        for index, (key, label, minimum, maximum, value) in enumerate(specs):
            row = index // 3
            col = (index % 3) * 2
            param_label = QLabel(label)
            param_label.setObjectName("ParamLabel")
            grid.addWidget(param_label, row, col)
            spin = QSpinBox()
            spin.setRange(minimum, maximum)
            spin.setValue(value)
            spin.setObjectName("ParamSpin")
            tip = align_tips.get(key)
            if tip:
                spin.setToolTip(tip)
                param_label.setToolTip(tip)
            self.align_param_spins[key] = spin
            grid.addWidget(spin, row, col + 1)

        layout.addWidget(group)

        button_row = QHBoxLayout()
        button_row.setSpacing(8)
        self.align_read_button = QPushButton("读取当前")
        self.align_apply_button = QPushButton("应用到设备")
        self.align_default_button = QPushButton("恢复默认")
        self.align_apply_button.setObjectName("PrimaryButton")
        self.align_read_button.clicked.connect(self.send_align_get)
        self.align_apply_button.clicked.connect(self.send_align_set)
        self.align_default_button.clicked.connect(self.send_align_default)
        button_row.addWidget(self.align_read_button)
        button_row.addWidget(self.align_apply_button)
        button_row.addWidget(self.align_default_button)
        button_row.addStretch(1)
        layout.addLayout(button_row)

        hint = QLabel(
            "目标 Y is usually near 120. Larger gain gives stronger compensation; reduce it if motion overshoots. "
            "Tolerance is the accepted centering error. Use 10 to 20 px for normal operation. "
            "If direction is wrong, enable reverse direction mapping."
        )
        hint.setObjectName("GateHint")
        hint.setWordWrap(True)
        layout.addWidget(hint)
        return panel

    def build_log_page(self) -> QWidget:
        page, layout = self.build_page("系统日志", "通信摘要和原始 UDP 状态包。")
        log_row = QHBoxLayout()
        log_row.setSpacing(12)
        self.summary_log = QPlainTextEdit()
        self.summary_log.setReadOnly(True)
        self.summary_log.setMaximumBlockCount(300)
        self.summary_log.setPlaceholderText("状态摘要")
        self.raw_log = QPlainTextEdit()
        self.raw_log.setReadOnly(True)
        self.raw_log.setMaximumBlockCount(500)
        self.raw_log.setPlaceholderText("UDP 原始状态包")
        log_row.addWidget(self.summary_log)
        log_row.addWidget(self.raw_log)
        layout.addLayout(log_row, 1)
        return page

    def build_metric_panel(self, title: str, value: str, subtitle: str) -> QFrame:
        panel = QFrame()
        panel.setObjectName("MetricPanel")
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(14, 12, 14, 12)
        layout.setSpacing(6)
        title_label = QLabel(title)
        title_label.setObjectName("MetricTitle")
        value_label = QLabel(value)
        value_label.setObjectName("MetricValue")
        sub_label = QLabel(subtitle)
        sub_label.setObjectName("MetricSub")
        layout.addWidget(title_label)
        layout.addWidget(value_label)
        layout.addWidget(sub_label)
        panel.title_label = title_label  # type: ignore[attr-defined]
        panel.value_label = value_label  # type: ignore[attr-defined]
        panel.sub_label = sub_label  # type: ignore[attr-defined]
        return panel

    def set_metric(self, panel: QFrame, value: str, subtitle: str = "") -> None:
        panel.value_label.setText(value)  # type: ignore[attr-defined]
        panel.sub_label.setText(subtitle)  # type: ignore[attr-defined]

    def build_disabled_button_row(self, labels: list[str]) -> QWidget:
        panel = QFrame()
        panel.setObjectName("ButtonStrip")
        layout = QHBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(8)
        for label in labels:
            button = QPushButton(label)
            button.setEnabled(False)
            layout.addWidget(button)
        layout.addStretch(1)
        return panel

    def on_nav_changed(self, row: int) -> None:
        if row >= 0:
            self.stack.setCurrentIndex(row)

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:  # noqa: N802
        if watched is self.engineering_title_label:
            event_type = event.type()
            if event_type in (QEvent.Type.MouseButtonPress, QEvent.Type.MouseButtonDblClick):
                if getattr(event, "button", lambda: None)() != Qt.MouseButton.LeftButton:
                    return False
                if self.engineering_mode:
                    self.engineering_long_press_timer.start(ENGINEERING_TITLE_LONG_PRESS_MS)
                self.handle_engineering_title_click()
                return False
            if event_type in (QEvent.Type.MouseButtonRelease, QEvent.Type.Leave):
                if self.engineering_long_press_timer.isActive():
                    self.engineering_long_press_timer.stop()
                return False
        return super().eventFilter(watched, event)

    def handle_engineering_title_click(self) -> None:
        now = time.monotonic()
        if (now - self.engineering_title_last_click_at) > ENGINEERING_TITLE_CLICK_WINDOW_S:
            self.engineering_title_clicks = 0
        self.engineering_title_last_click_at = now
        self.engineering_title_clicks += 1
        if self.engineering_title_clicks >= 3:
            self.engineering_title_clicks = 0
            self.set_engineering_mode(True, "隐藏调试区已开启；长按标题 3 秒可收起。")

    def disable_engineering_mode_from_title(self) -> None:
        self.engineering_title_clicks = 0
        self.set_engineering_mode(False, "隐藏调试区已关闭。")

    def set_engineering_mode(self, enabled: bool, message: str = "") -> None:
        if self.engineering_mode == enabled:
            return
        self.on_engineering_mode_toggled(enabled)
        if message:
            self.append_summary(message)
            if hasattr(self, "debug_transport_group") and self.debug_transport_group is not None:
                self.debug_transport_group.set_field("status", message)

    # ---- 连接 page: scan + connect + UDP 监听状态 ----

    def run_device_scan(self) -> None:
        if self.scan_thread is not None:
            QMessageBox.information(self, "正在扫描", "设备扫描正在进行，请等待完成。")
            return
        base_ip = self.scan_base_ip_edit.text().strip() or DEFAULT_SCAN_BASE_IP
        port = int(self.scan_port_spin.value())
        self.scan_button.setEnabled(False)
        self.scan_progress_label.setText("正在扫描...")
        self.scan_device_list.clear()

        self.scan_thread = QThread(self)
        self.scan_worker = DeviceScanWorker(
            base_ip=base_ip,
            port=port,
            timeout=0.3,
            max_workers=50,
        )
        self.scan_worker.moveToThread(self.scan_thread)
        self.scan_thread.started.connect(self.scan_worker.run)
        self.scan_worker.progress.connect(self.on_scan_progress)
        self.scan_worker.finished.connect(self.on_scan_finished)
        self.scan_worker.failed.connect(self.on_scan_failed)
        self.scan_worker.finished.connect(self.scan_thread.quit)
        self.scan_worker.failed.connect(self.scan_thread.quit)
        self.scan_thread.finished.connect(self.cleanup_scan_worker)
        self.scan_thread.start()

    @Slot(str)
    def on_scan_progress(self, text: str) -> None:
        self.scan_progress_label.setText(text)

    @Slot(object)
    def on_scan_finished(self, devices_obj: object) -> None:
        devices = devices_obj if isinstance(devices_obj, list) else []
        self.scan_devices = [d for d in devices if isinstance(d, DeviceInfo)]
        if not self.scan_devices:
            self.scan_progress_label.setText("扫描完成，未发现设备")
            return
        self.scan_progress_label.setText(f"扫描完成，发现 {len(self.scan_devices)} 台设备")
        for device in self.scan_devices:
            text = f"{device.ip}:{device.port}  {device.latency_ms}ms"
            if device.model_name:
                text += f"  model={device.model_name}"
            self.scan_device_list.addItem(text)

    @Slot(str)
    def on_scan_failed(self, message: str) -> None:
        self.scan_progress_label.setText(f"扫描失败: {message}")

    @Slot()
    def cleanup_scan_worker(self) -> None:
        if self.scan_worker is not None:
            self.scan_worker.deleteLater()
        if self.scan_thread is not None:
            self.scan_thread.deleteLater()
        self.scan_worker = None
        self.scan_thread = None
        if hasattr(self, "scan_button"):
            self.scan_button.setEnabled(True)

    def on_scan_device_selected(self) -> None:
        self.connect_button.setEnabled(self.scan_device_list.currentRow() >= 0)

    def connect_selected_device(self) -> None:
        row = self.scan_device_list.currentRow()
        if row < 0 or row >= len(self.scan_devices):
            QMessageBox.warning(self, "未选择设备", "请先在列表中选择设备。")
            return
        device = self.scan_devices[row]
        self.connected_device_ip = device.ip
        self.connected_device_port = device.port
        self.last_tcp_keepalive_at = 0.0
        self.mark_tcp_service_ok("scan")
        if self.client is not None:
            self.client.board_ip = device.ip
        self.disconnect_button.setEnabled(True)
        self.append_summary(f"设备已连接：{device.ip}:{device.port}")
        # Auto-start UDP 监听状态 if not already running so the user gets a
        # one-click bring-up; the listener port is independent of the device.
        if self.client is None:
            self.start_listening()
        self.refresh_connection_status()
        QMessageBox.information(
            self,
            "连接成功",
            f"设备已连接（{device.ip}）。\n\n可在“运行总览”页启动流程并查看检测状态。",
        )

    def disconnect_device(self) -> None:
        self.connected_device_ip = None
        self.connected_device_port = DEFAULT_TCP_PORT
        self.last_tcp_keepalive_at = 0.0
        self.tcp_keepalive_ok = False
        self.tcp_keepalive_age_s = None
        self.tcp_keepalive_fail_count = 0
        self.tcp_last_ok_at = 0.0
        self.tcp_last_error = ""
        if self.client is not None:
            self.client.board_ip = DEFAULT_BOARD_IP
        self.disconnect_button.setEnabled(False)
        self.append_summary("设备已断开")
        self.refresh_connection_status()

    def mark_tcp_service_ok(self, source: str) -> None:
        now = time.monotonic()
        self.tcp_keepalive_ok = True
        self.tcp_keepalive_fail_count = 0
        self.tcp_last_ok_at = now
        self.tcp_keepalive_age_s = 0.0
        self.tcp_last_error = source

    def mark_tcp_service_failed(self, reason: str) -> None:
        now = time.monotonic()
        self.tcp_keepalive_fail_count += 1
        self.tcp_last_error = reason
        if self.tcp_last_ok_at > 0.0:
            self.tcp_keepalive_age_s = now - self.tcp_last_ok_at
        else:
            self.tcp_keepalive_age_s = None
        if (
            self.tcp_keepalive_fail_count >= TCP_KEEPALIVE_FAIL_LIMIT
            or self.tcp_last_ok_at <= 0.0
            or (now - self.tcp_last_ok_at) > TCP_KEEPALIVE_GRACE_S
        ):
            self.tcp_keepalive_ok = False

    def note_auto_pc_skip(self, req: int, reason: str) -> None:
        if req == self.last_auto_pc_skip_req and reason == self.last_auto_pc_skip_reason:
            return
        self.last_auto_pc_skip_req = req
        self.last_auto_pc_skip_reason = reason
        self.append_summary(f"自动检测跳过：{reason} req={req}")
        if hasattr(self, "debug_transport_group"):
            self.debug_transport_group.set_field("status", f"跳过：{reason}")

    # ---- Debug page: TCP frame receive + detect ----

    def receive_and_detect_frame(self) -> None:
        self.start_frame_receive(auto_request_id=None, interactive=True)

    def start_frame_receive(self, auto_request_id: Optional[int], interactive: bool) -> bool:
        if self.frame_thread is not None:
            if interactive:
                QMessageBox.information(self, "正在接收", "上一帧图像仍在接收，请等待完成。")
            else:
                self.append_summary(f"自动检测跳过：上一帧仍在接收 req={auto_request_id}")
            return False
        if not self.connected_device_ip:
            if interactive:
                QMessageBox.warning(self, "设备未连接", "请先在“连接”页面连接设备。")
            else:
                self.append_summary(f"自动检测跳过：设备未连接 req={auto_request_id}")
            return False
        if auto_request_id is not None and self.current_profile is None:
            self.append_summary(f"自动检测跳过：未加载当前模型的检测配置 req={auto_request_id}")
            return False

        host = self.connected_device_ip
        port = self.connected_device_port
        self.frame_request_id = auto_request_id
        self.frame_request_interactive = interactive
        self.auto_pc_pending_req = auto_request_id
        self.debug_receive_button.setEnabled(False)
        if auto_request_id is None:
            self.debug_transport_group.set_field("status", "接收中")
        else:
            self.debug_transport_group.set_field("status", f"自动检测中 req={auto_request_id}")

        self.frame_thread = QThread(self)
        self.frame_worker = TcpFrameReceiveWorker(
            host=host,
            port=port,
            profile=self.current_profile,
        )
        self.frame_worker.moveToThread(self.frame_thread)
        self.frame_thread.started.connect(self.frame_worker.run)
        self.frame_worker.finished.connect(self.on_frame_received)
        self.frame_worker.failed.connect(self.on_frame_failed)
        self.frame_worker.finished.connect(self.frame_thread.quit)
        self.frame_worker.failed.connect(self.frame_thread.quit)
        self.frame_thread.finished.connect(self.cleanup_frame_worker)
        self.frame_thread.start()
        return True

    @Slot(object, object, object, str, int)
    def on_frame_received(
        self,
        rgb_image: object,
        overlay_image: object,
        result_obj: object,
        error_text: str,
        duration_ms: int,
    ) -> None:
        request_id = self.frame_request_id
        interactive = self.frame_request_interactive
        self.update_debug_frame_images(rgb_image, overlay_image)
        has_image = self.last_debug_raw_image is not None
        self.debug_save_button.setEnabled(has_image)
        self.debug_clear_button.setEnabled(has_image)
        if result_obj is not None:
            self.mark_tcp_service_ok("frame")
            effective_area = (
                self.detection_effective_area(result_obj)
                if self.current_profile is not None
                else int(getattr(result_obj, "defect_area", 0))
            )
            effective_threshold = (
                self.detection_area_threshold(result_obj, self.current_profile)
                if self.current_profile is not None
                else "-"
            )
            status = ("OK" if result_obj.ok else "NG") + f" 面积={effective_area}"
            if request_id is not None:
                status += f" req={request_id}"
            self.debug_transport_group.set_fields(
                {
                    "status": status,
                    "frame": self.latest_fields.get("frame", "-"),
                }
            )
            self.latest_object_angle_deg = getattr(result_obj, "angle_deg", None)
            self.update_object_angle_display(self.latest_object_angle_deg)
            angle_text = (
                f"{float(self.latest_object_angle_deg):.1f}°"
                if self.latest_object_angle_deg is not None
                else "--"
            )
            if self.engineering_mode:
                # 工程调试 mode: surface the real OpenCV template-diff metrics.
                diag = (
                    f"{'OK' if result_obj.ok else 'NG'}\n\n"
                    f"缺陷面积 = {effective_area}\n"
                    f"面积阈值 = {effective_threshold}\n"
                    f"处理耗时 = {duration_ms} ms\n"
                    f"最大灰度差 = {result_obj.max_diff}\n"
                    f"平均灰度差 = {result_obj.mean_diff:.1f}\n"
                    f"校正角度 = {result_obj.angle_deg:.2f} 度\n"
                    f"机械臂角度 = {getattr(result_obj, 'robot_angle_deg', -result_obj.angle_deg):.2f} 度\n"
                    f"位置偏移 dx = {result_obj.dx:.1f}, dy = {result_obj.dy:.1f}"
                )
            else:
                # Normal demo mode: AI-styled, neutral phrasing only.
                anomaly_score = self.detection_anomaly_score(result_obj, effective_area, effective_threshold)
                diag = (
                    f"{'OK' if result_obj.ok else 'NG'}\n\n"
                    f"异常分数 = {anomaly_score}\n"
                    f"推理耗时 = {duration_ms} ms\n"
                    f"物品角度 = {angle_text}"
                )
            if request_id is not None:
                diag += f"\n请求号 = {request_id}"
            if error_text:
                diag += f"\n\n说明：{error_text}"
            if self.engineering_mode:
                self.append_summary("检测结果：" + diag.replace("\n", " "))
            else:
                self.append_summary(
                    f"检测结果：{'OK' if result_obj.ok else 'NG'} "
                    f"异常分数={self.detection_anomaly_score(result_obj, effective_area, effective_threshold)} "
                    f"物品角度={angle_text} 推理耗时={duration_ms} ms"
                )
            self.send_pc_detection_result(
                result_obj,
                request_id=request_id,
                duration_ms=duration_ms,
                show_errors=interactive,
            )
            if interactive:
                QMessageBox.information(self, "检测结果", diag)
        else:
            self.mark_tcp_service_ok("frame")
            self.debug_transport_group.set_field("status", "仅显示原图" if not error_text else "已接收")
            self.append_summary(f"图像接收：{error_text}")
            if interactive:
                QMessageBox.warning(
                    self,
                    "未完成检测",
                    error_text or "已收到图像，但未加载检测配置。",
                )

    def update_object_angle_display(self, angle_deg: Optional[float]) -> None:
        """Refresh the object-angle status card / overview / detect summary.

        Shows a value like ``26.6°`` when an angle is available, otherwise ``--``.
        """
        if angle_deg is None:
            value = "--"
        else:
            try:
                value = f"{float(angle_deg):.1f}°"
            except (TypeError, ValueError):
                value = "--"
        subtitle = "AI 姿态推理"
        if hasattr(self, "angle_card") and self.angle_card is not None:
            self.angle_card.set_value(value, subtitle)
        if hasattr(self, "overview_angle") and self.overview_angle is not None:
            self.set_metric(self.overview_angle, value, subtitle)
        if hasattr(self, "detect_group") and self.detect_group is not None:
            self.detect_group.set_field("angle", value)

    def update_debug_frame_images(self, rgb_image: object, overlay_image: object) -> None:
        from PySide6.QtGui import QImage, QPixmap

        self.last_debug_raw_image = (
            rgb_image if isinstance(rgb_image, QImage) and not rgb_image.isNull() else None
        )
        self.last_debug_overlay_image = (
            overlay_image if isinstance(overlay_image, QImage) and not overlay_image.isNull() else None
        )
        if self.last_debug_raw_image is not None:
            self.debug_raw_placeholder.set_pixmap(QPixmap.fromImage(self.last_debug_raw_image))
        else:
            self.debug_raw_placeholder.clear()
        if self.last_debug_overlay_image is not None:
            self.debug_overlay_placeholder.set_pixmap(QPixmap.fromImage(self.last_debug_overlay_image))
        else:
            self.debug_overlay_placeholder.clear()

    @Slot(str)
    def on_frame_failed(self, message: str) -> None:
        request_id = self.frame_request_id
        self.mark_tcp_service_failed(message)
        self.debug_transport_group.set_field("status", "接收失败")
        if self.frame_request_interactive:
            QMessageBox.critical(self, "接收失败", message)
        else:
            self.append_summary(f"自动检测接收失败 req={request_id}: {message}")

    @Slot()
    def cleanup_frame_worker(self) -> None:
        if self.frame_worker is not None:
            self.frame_worker.deleteLater()
        if self.frame_thread is not None:
            self.frame_thread.deleteLater()
        self.frame_worker = None
        self.frame_thread = None
        self.frame_request_id = None
        self.frame_request_interactive = True
        self.auto_pc_pending_req = None
        if hasattr(self, "debug_receive_button"):
            self.debug_receive_button.setEnabled(True)

    def save_debug_frame(self) -> None:
        raw_image = self.last_debug_raw_image
        if raw_image is None or raw_image.isNull():
            QMessageBox.warning(self, "无图像", "当前没有可保存的图像，请先接收一帧。")
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "保存图像", "", "PNG (*.png);;所有文件 (*.*)"
        )
        if not path:
            return

        raw_path = Path(path)
        if raw_path.suffix.lower() != ".png":
            raw_path = raw_path.with_suffix(".png")
        overlay_path = raw_path.with_name(f"{raw_path.stem}_overlay{raw_path.suffix}")

        raw_saved = raw_image.save(str(raw_path))
        overlay_saved = True
        if self.last_debug_overlay_image is not None and not self.last_debug_overlay_image.isNull():
            overlay_saved = self.last_debug_overlay_image.save(str(overlay_path))

        if raw_saved and overlay_saved:
            self.append_summary(f"frame saved: {raw_path}")
            if self.last_debug_overlay_image is not None and not self.last_debug_overlay_image.isNull():
                self.append_summary(f"overlay saved: {overlay_path}")
        else:
            QMessageBox.warning(self, "保存失败", f"无法写入文件:\n{raw_path}")

    def clear_debug_frame(self) -> None:
        self.last_debug_raw_image = None
        self.last_debug_overlay_image = None
        self.debug_raw_placeholder.clear()
        self.debug_overlay_placeholder.clear()
        self.debug_save_button.setEnabled(False)
        self.debug_clear_button.setEnabled(False)
        self.debug_transport_group.set_field("status", "已清空")

    @Slot(bool)
    def on_engineering_mode_toggled(self, checked: bool) -> None:
        """Show or hide the hidden image-debug and verification UI elements."""
        self.engineering_mode = checked
        if hasattr(self, "dataset_vision_container") and self.dataset_vision_container is not None:
            self.dataset_vision_container.setVisible(checked)
        if hasattr(self, "vision_verify_panel") and self.vision_verify_panel is not None:
            self.vision_verify_panel.setVisible(checked)
        if hasattr(self, "debug_image_panel") and self.debug_image_panel is not None:
            self.debug_image_panel.setVisible(checked)
        if hasattr(self, "debug_frame_button_bar") and self.debug_frame_button_bar is not None:
            self.debug_frame_button_bar.setVisible(checked)

    def start_listening(self) -> None:
        if self.client is not None:
            return

        port = int(self.conn_udp_port_spin.value())
        board_ip = self.connected_device_ip or DEFAULT_BOARD_IP
        self.udp_listen_port = port
        self.client = VisionBoardClient(
            board_ip=board_ip,
            udp_listen_host=self.args.listen_host,
            udp_listen_port=port,
            heartbeat_timeout_s=self.args.heartbeat_timeout,
        )

        try:
            self.client.start_udp_listener(on_packet=self.on_udp_packet)
        except OSError as exc:
            self.client = None
            QMessageBox.critical(
                self,
                "UDP 监听失败",
                f"无法监听 UDP {port}: {exc}\n\n请检查该端口是否已被占用。",
            )
            return

        self.udp_start_button.setEnabled(False)
        self.udp_stop_button.setEnabled(True)
        self.conn_udp_port_spin.setEnabled(False)
        self.append_summary(f"UDP 监听已启动：{self.args.listen_host}:{port}")
        self.refresh_runtime_state()
        self.refresh_connection_status()

    def stop_listening(self) -> None:
        if self.client is not None:
            self.client.close()
            self.client = None

        self.udp_start_button.setEnabled(True)
        self.udp_stop_button.setEnabled(False)
        self.conn_udp_port_spin.setEnabled(True)
        self.connection_card.set_value("OFFLINE", "监听未启动")
        self.append_summary("监听已停止")
        self.refresh_runtime_state()
        self.refresh_connection_status()

    def use_latest_source_ip(self) -> None:
        source = self.latest_fields.get("_source_ip")
        if source:
            self.append_summary(f"从状态包识别到设备 IP：{source}")

    def on_udp_packet(self, packet) -> None:  # type: ignore[no-untyped-def]
        self.packet_count += 1
        fields = dict(packet.fields)
        fields["_source_ip"] = packet.source_ip
        fields["_source_port"] = str(packet.source_port)
        fields["_length"] = str(packet.length)
        raw = format_udp_packet(packet, self.packet_count)
        summary = format_status_summary(packet) or raw
        self.bridge.packet_received.emit(fields, raw, summary)

    def on_packet_received(self, fields: Dict[str, str], raw: str, summary: str) -> None:
        message = fields.get("message", "")

        # 来源 IP filtering: when a device is explicitly connected, only
        # trust packets from that device's IP. Without a connection, accept
        # everything (passive-listen mode).
        source_ip = fields.get("_source_ip", "")
        if self.connected_device_ip and source_ip and source_ip != self.connected_device_ip:
            return

        if message == "vstat":
            self.latest_fields = fields
            self.latest_device_fields = fields
            self.update_from_fields(fields)
            self.update_gate_status(fields)
            self.maybe_reload_vision_profile(fields)
            self.refresh_connection_status()
            self.maybe_run_auto_pc_detection(fields)
        elif message == "vack":
            self.latest_device_fields = fields
            self.update_gate_status(fields)
            self.update_device_control_from_ack(fields)
            self.load_gate_params_from_fields(fields)
            self.handle_command_ack(fields)
        else:
            self.latest_fields = fields
            self.update_from_fields(fields)

        self.raw_log.appendPlainText(raw)
        self.append_summary(summary)

    def maybe_run_auto_pc_detection(self, fields: Dict[str, str]) -> None:
        if int_field(fields, "flow_pc_wait", 0) <= 0:
            return

        req = int_field(fields, "flow_pc_req", 0)
        if req <= 0:
            return
        if req == self.last_auto_pc_req or req == self.auto_pc_pending_req:
            return
        if self.frame_thread is not None:
            self.note_auto_pc_skip(req, "上一帧仍在处理")
            return
        if self.client is None or not self.connected_device_ip:
            self.note_auto_pc_skip(req, "设备未连接")
            return
        if not self.tcp_keepalive_ok:
            self.note_auto_pc_skip(req, "服务未确认")
            return
        if self.current_profile is None:
            self.note_auto_pc_skip(req, "未加载检测配置")
            return

        if self.start_frame_receive(auto_request_id=req, interactive=False):
            self.last_auto_pc_req = req
            self.append_summary(f"自动检测请求已接管：req={req}")

    def refresh_tcp_keepalive(self) -> None:
        if self.client is None or not self.connected_device_ip:
            self.tcp_keepalive_ok = False
            self.tcp_keepalive_age_s = None
            self.tcp_keepalive_fail_count = 0
            self.tcp_last_ok_at = 0.0
            self.tcp_last_error = ""
            return

        if self.frame_thread is not None:
            if self.tcp_last_ok_at > 0.0:
                self.tcp_keepalive_age_s = time.monotonic() - self.tcp_last_ok_at
            return

        now = time.monotonic()
        if (now - self.last_tcp_keepalive_at) < TCP_KEEPALIVE_INTERVAL_S:
            if self.tcp_last_ok_at > 0.0:
                self.tcp_keepalive_age_s = now - self.tcp_last_ok_at
            return

        lock = tcp_session_lock(self.connected_device_ip, self.connected_device_port)
        if not lock.acquire(blocking=False):
            if self.tcp_last_ok_at > 0.0:
                self.tcp_keepalive_age_s = now - self.tcp_last_ok_at
            return

        self.last_tcp_keepalive_at = now
        try:
            ok = self.client.ping_tcp_service(
                port=self.connected_device_port,
                timeout_s=0.3,
                host=self.connected_device_ip,
            )
            if ok:
                self.mark_tcp_service_ok("ping")
            else:
                self.mark_tcp_service_failed("ping timeout")
        finally:
            lock.release()

    def refresh_runtime_state(self) -> None:
        self.refresh_tcp_keepalive()
        if self.client is None:
            self.net_group.set_fields(
                {
                    "source": "-",
                    "seq": "-",
                    "udp_packets": "-",
                    "last_age": "-",
                    "eth_ready": "-",
                    "link": "-",
                }
            )
            self.refresh_connection_status()
            return

        state = self.client.get_link_state()
        age_text = "none" if state.last_udp_age_s is None else f"{state.last_udp_age_s:.1f}s"
        self.connection_card.set_value(
            "ONLINE" if state.udp_alive else "WAIT",
            f"last={age_text} packets={state.udp_packets}",
        )
        source = "-" if state.last_source is None else f"{state.last_source[0]}:{state.last_source[1]}"
        self.net_group.set_fields(
            {
                "source": source,
                "seq": "-" if state.last_sequence is None else state.last_sequence,
                "udp_packets": state.udp_packets,
                "last_age": age_text,
                "eth_ready": text_field(self.latest_fields, "eth_ready"),
                "link": text_field(self.latest_fields, "link"),
            }
        )
        self.refresh_connection_status()

    def refresh_connection_status(self) -> None:
        """Refresh the read-only connection readouts on header + connection page."""
        device_ip = self.connected_device_ip or "-"
        device_port = str(self.connected_device_port) if self.connected_device_ip else "-"

        if self.client is None:
            udp_state = "未启动"
            udp_packets = "0"
            link_state = "离线"
        else:
            udp_state = f"监听中 ({self.udp_listen_port})"
            state = self.client.get_link_state()
            udp_packets = str(state.udp_packets)
            if self.connected_device_ip:
                if self.tcp_keepalive_ok:
                    age = 0.0 if self.tcp_keepalive_age_s is None else self.tcp_keepalive_age_s
                    link_state = f"服务已确认 {age:.1f}s"
                else:
                    if self.tcp_keepalive_fail_count > 0:
                        link_state = f"服务确认失败 {self.tcp_keepalive_fail_count}/{TCP_KEEPALIVE_FAIL_LIMIT}"
                    else:
                        link_state = "服务确认中"
            else:
                link_state = "在线" if state.udp_alive else "等待状态包"

        if hasattr(self, "connection_status_group") and self.connection_status_group is not None:
            self.connection_status_group.set_fields(
                {
                    "device_ip": device_ip,
                    "device_port": device_port,
                    "udp_listen": udp_state,
                    "udp_packets": udp_packets,
                    "link_state": link_state,
                }
            )
        if hasattr(self, "header_device_label") and self.header_device_label is not None:
            self.header_device_label.setText(f"设备 IP: {device_ip}")
        if hasattr(self, "header_udp_label") and self.header_udp_label is not None:
            self.header_udp_label.setText(f"UDP: {udp_state}")
        if hasattr(self, "header_link_label") and self.header_link_label is not None:
            self.header_link_label.setText(f"连接: {link_state}")

    def update_from_fields(self, fields: Dict[str, str]) -> None:
        total = int_field(fields, "total")
        ok = int_field(fields, "ok")
        ng = int_field(fields, "ng")
        score = score_text(fields)
        model_source = "sdcard" if text_field(fields, "model_src", "0") == "1" else "builtin"
        result = result_text(fields)
        frame = text_field(fields, "frame")

        self.result_card.set_value(result, f"score={score} frame={frame}")
        self.model_card.set_value(text_field(fields, "model"), f"source={model_source}")
        self.count_card.set_value(f"{ok} / {ng}", f"total={total}")
        self.set_metric(self.overview_result, result, f"score={score} frame={frame}")
        self.set_metric(self.overview_model, text_field(fields, "model"), f"source={model_source}")
        self.set_metric(
            self.overview_line,
            f"{text_field(fields, 'rpm')} rpm",
            f"acc={text_field(fields, 'acc')} speed={text_field(fields, 'speed')}",
        )

        self.detect_group.set_fields(
            {
                "frame": frame,
                "score": score,
                "total": total,
                "ok": ok,
                "ng": ng,
                "cap_ms": f"{text_field(fields, 'cap_ms')} ms",
                "ai_ms": f"{text_field(fields, 'ai_ms')} ms",
            }
        )
        self.line_group.set_fields(
            {
                "conveyor": bool_text(int_field(fields, "conv")),
                "rpm": text_field(fields, "rpm"),
                "acc": text_field(fields, "acc"),
                "speed": text_field(fields, "speed"),
                "pos": text_field(fields, "pos"),
                "action": text_field(fields, "action"),
                "progress": self.action_progress_text(fields),
            }
        )
        self.health_group.set_fields(
            {
                "cam": bool_text(int_field(fields, "cam")),
                "bus": bool_text(int_field(fields, "bus")),
                "arm_ready": bool_text(int_field(fields, "arm_ready")),
                "arm_homed": bool_text(int_field(fields, "arm_homed")),
                "arm_moving": bool_text(int_field(fields, "arm_moving")),
                "arm_estop": bool_text(int_field(fields, "arm_estop")),
                "conv_pos": text_field(fields, "pos"),
            }
        )
        self.control_conveyor_group.set_fields(
            {
                "online": bool_text(int_field(fields, "conv")),
                "rpm": text_field(fields, "rpm"),
                "acc": text_field(fields, "acc"),
                "speed": text_field(fields, "speed"),
                "pos": text_field(fields, "pos"),
                "state": "realtime",
            }
        )
        self.control_arm_group.set_fields(
            {
                "online": bool_text(int_field(fields, "arm")),
                "ready": bool_text(int_field(fields, "arm_ready")),
                "homed": bool_text(int_field(fields, "arm_homed")),
                "moving": bool_text(int_field(fields, "arm_moving")),
                "estop": bool_text(int_field(fields, "arm_estop")),
                "action": text_field(fields, "action"),
                "progress": self.action_progress_text(fields),
            }
        )
        self.control_system_group.set_fields(
            {
                "bus": bool_text(int_field(fields, "bus")),
                "eth_ready": bool_text(int_field(fields, "eth_ready")),
                "link": bool_text(int_field(fields, "link")),
                "cam": bool_text(int_field(fields, "cam")),
                "last_seq": text_field(fields, "last_seq"),
                "last_result": text_field(fields, "last_result"),
            }
        )
        self.model_runtime_group.set_fields(
            {
                "name": text_field(fields, "model"),
                "source": model_source,
                "score": score,
                "frame": frame,
                "status": result,
            }
        )
        # Only refresh transport/identity fields from UDP status. Do NOT touch
        # "status" here: it holds the latest PC-vision detection result (OK/NG)
        # set by on_frame_received, and a 1 Hz UDP refresh would otherwise keep
        # overwriting it back to a placeholder.
        self.debug_transport_group.set_fields(
            {
                "board_ip": self.connected_device_ip or DEFAULT_BOARD_IP,
                "tcp_port": str(self.connected_device_port),
                "frame": frame,
            }
        )
        self.refresh_runtime_state()

    def update_device_control_from_ack(self, fields: Dict[str, str]) -> None:
        if not hasattr(self, "device_command_group"):
            return

        ok = int_field(fields, "ok")
        msg = text_field(fields, "msg", "-")
        seq = text_field(fields, "seq", "-")
        ack_text = f"{'OK' if ok else 'FAIL'} seq={seq} {msg}"
        self.device_command_group.set_fields(
            {
                "ack": ack_text,
                "queue": f"poll={text_field(fields, 'dev_poll')} cmd={text_field(fields, 'dev_cmd')}",
            }
        )

        self.control_conveyor_group.set_fields(
            {
                "online": bool_text(int_field(fields, "dev_conv")),
                "rpm": text_field(fields, "dev_rpm"),
                "acc": text_field(fields, "dev_acc"),
                "speed": text_field(fields, "dev_speed"),
                "pos": text_field(fields, "dev_pos"),
                "state": "command accepted" if ok else "command failed",
            }
        )
        self.control_arm_group.set_fields(
            {
                "online": bool_text(int_field(fields, "dev_arm")),
                "ready": bool_text(int_field(fields, "arm_ready")),
                "homed": bool_text(int_field(fields, "arm_homed")),
                "moving": bool_text(int_field(fields, "arm_moving")),
                "estop": bool_text(int_field(fields, "arm_estop")),
                "action": text_field(fields, "action"),
                "progress": self.action_progress_text(fields),
            }
        )
        self.control_system_group.set_fields(
            {
                "bus": bool_text(int_field(fields, "dev_bus")),
                "eth_ready": text_field(self.latest_fields, "eth_ready"),
                "link": text_field(self.latest_fields, "link"),
                "cam": text_field(self.latest_fields, "cam"),
                "last_seq": text_field(fields, "last_seq"),
                "last_result": text_field(fields, "last_result", text_field(fields, "dev_last")),
            }
        )

    def fill_conveyor_params_from_status(self) -> None:
        if not hasattr(self, "device_rpm_spin"):
            return

        fields = self.latest_device_fields or self.latest_fields
        rpm_key = "dev_rpm" if "dev_rpm" in fields else "rpm"
        acc_key = "dev_acc" if "dev_acc" in fields else "acc"
        if rpm_key in fields:
            value = int_field(fields, rpm_key, self.device_rpm_spin.value())
            self.device_rpm_spin.setValue(max(self.device_rpm_spin.minimum(), min(self.device_rpm_spin.maximum(), value)))
        if acc_key in fields:
            value = int_field(fields, acc_key, self.device_acc_spin.value())
            self.device_acc_spin.setValue(max(self.device_acc_spin.minimum(), min(self.device_acc_spin.maximum(), value)))
        if hasattr(self, "device_command_group"):
            self.device_command_group.set_field("ack", "已填入当前上报值，点击“应用参数”后下发到设备。")

    def maybe_reload_vision_profile(self, fields: Dict[str, str]) -> None:
        """Reload PC vision profile when the board reports a new model name.

        Looks up the model-specific config dir via app_paths so the path logic
        stays consistent with the dataset pipeline. Failures are silent: the
        debug page simply keeps the previous (or empty) profile and shows the
        error string when the user tries to receive a frame.
        """
        model_name = text_field(fields, "model", "").strip()
        if not model_name:
            return
        if model_name == self.current_model_name and self.current_profile is not None:
            return
        config_dir = app_paths.get_pc_vision_config_dir(model_name)
        profile_path = config_dir / "vision_profile.json"
        if not profile_path.exists():
            # Do not keep a stale profile after the board reports another model.
            self.current_profile = None
            self.current_model_name = model_name
            return
        try:
            self.current_profile = load_profile(config_dir)
            self.current_model_name = model_name
            if self.engineering_mode:
                self.append_summary(f"pc vision profile loaded: {profile_path}")
            else:
                self.append_summary(f"AI 推理配置已加载：{model_name}")
        except Exception as exc:
            if self.engineering_mode:
                self.append_summary(f"pc vision profile load failed: {exc}")
            else:
                self.append_summary(f"AI 推理配置加载失败：{model_name}")

    def update_gate_status(self, fields: Dict[str, str]) -> None:
        if not hasattr(self, "gate_status_group"):
            return

        ack_text = self.gate_status_group.value_labels["ack"].text()
        if fields.get("message") == "vack":
            ok = int_field(fields, "ok")
            msg = text_field(fields, "msg", "-")
            seq = text_field(fields, "seq", "-")
            ack_text = f"{'OK' if ok else 'FAIL'} seq={seq} {msg}"

        self.gate_status_group.set_fields(
            {
                "enabled": bool_text(int_field(fields, "gate_en")),
                "state": self.gate_state_text(fields),
                "area": text_field(fields, "gate_area"),
                "trigger": text_field(fields, "gate_trig"),
                "candidate": bool_text(int_field(fields, "gate_candidate")),
                "inside": bool_text(int_field(fields, "gate_inside")),
                "detectable": bool_text(int_field(fields, "gate_detectable")),
                "bbox_cy": text_field(fields, "gate_bbox_cy"),
                "ack": ack_text,
            }
        )

        self.load_align_params_from_fields(fields)

    def load_gate_params_from_fields(self, fields: Dict[str, str]) -> None:
        option_mapping = {
            "enabled": "gate_en",
            "debug": "gate_debug",
            "overlay": "gate_overlay",
        }
        for check_key, field_key in option_mapping.items():
            if field_key not in fields:
                continue
            check = self.gate_option_checks.get(check_key)
            if check is not None:
                check.setChecked(bool(int_field(fields, field_key)))

        mapping = {
            "bright": "gate_bright",
            "area_on": "gate_on",
            "area_off": "gate_off",
            "area_max": "gate_area_max",
            "min_w": "gate_min_w",
            "min_h": "gate_min_h",
            "side": "gate_side",
            "top": "gate_top",
            "bottom": "gate_bottom",
            "center_y": "gate_center_y",
            "center_tol": "gate_center_tol",
            "stable": "gate_stable",
            "rearm": "gate_rearm",
            "cooldown": "gate_cool",
            "debug_ms": "gate_debug_ms",
        }
        for spin_key, field_key in mapping.items():
            if field_key not in fields:
                continue
            spin = self.gate_param_spins.get(spin_key)
            if spin is None:
                continue
            value = int_field(fields, field_key, spin.value())
            spin.setValue(max(spin.minimum(), min(spin.maximum(), value)))

    def load_align_params_from_fields(self, fields: Dict[str, str]) -> None:
        option_mapping = {
            "enabled": "align_en",
            "reverse": "align_rev",
        }
        for check_key, field_key in option_mapping.items():
            if field_key not in fields:
                continue
            check = self.align_option_checks.get(check_key)
            if check is not None:
                check.setChecked(bool(int_field(fields, field_key)))

        mapping = {
            "target": "align_target",
            "tol": "align_tol",
            "gain": "align_gain",
            "min": "align_min",
            "max": "align_max",
            "rpm": "align_rpm",
            "acc": "align_acc",
            "iter": "align_iter",
            "settle": "align_settle",
        }
        for spin_key, field_key in mapping.items():
            if field_key not in fields:
                continue
            spin = self.align_param_spins.get(spin_key)
            if spin is None:
                continue
            value = int_field(fields, field_key, spin.value())
            spin.setValue(max(spin.minimum(), min(spin.maximum(), value)))

    def gate_state_text(self, fields: Dict[str, str]) -> str:
        states = {
            0: "IDLE",
            1: "WAIT",
            2: "SEEN",
            3: "TRIGGERED",
            4: "COOLDOWN",
        }
        value = int_field(fields, "gate_state", -1)
        return states.get(value, text_field(fields, "gate_state"))

    def board_threshold_x10(self) -> int:
        # 基准阈值 = 当前模型 model.ini 里的异常阈值（设备经 UDP 上报的 threshold 字段）。
        # 不写死：始终跟随当前加载的模型；字段暂未刷新时沿用上次已知值，避免阈值闪变(如 9.3↔20)。
        threshold = int_field(self.latest_device_fields or self.latest_fields, "threshold", 0)
        if threshold > 0:
            self._last_threshold_x10 = threshold
            return threshold
        return getattr(self, "_last_threshold_x10", 93)

    @staticmethod
    def detection_metric(result_obj: object, key: str, default: object = None) -> object:
        metrics = getattr(result_obj, "metrics", None)
        if isinstance(metrics, dict) and key in metrics:
            return metrics.get(key)
        return default

    def detection_area_threshold(self, result_obj: object, profile: VisionProfile) -> int:
        threshold = self.detection_metric(result_obj, "local_area_threshold", None)
        if threshold is None:
            threshold = self.detection_metric(result_obj, "area_threshold", None)
        if threshold is None:
            threshold = profile.area_threshold
        try:
            return max(1, int(round(float(threshold))))
        except (TypeError, ValueError):
            return max(1, int(profile.area_threshold))

    def detection_effective_area(self, result_obj: object) -> int:
        area = self.detection_metric(result_obj, "local_defect_area", None)
        if area is None:
            area = getattr(result_obj, "defect_area", 0)
        try:
            return max(0, int(round(float(area))))
        except (TypeError, ValueError):
            return max(0, int(getattr(result_obj, "defect_area", 0)))

    def detection_anomaly_score(self, result_obj: object, effective_area: object, effective_threshold: object) -> str:
        """Render an AI-styled anomaly score string for the normal-mode result popup.

        Reuses the same area->score mapping as the device-facing result so the
        number shown to operators matches what the device "AI" reports, without
        exposing template-diff internals.
        """
        if self.current_profile is not None:
            try:
                threshold_x10 = self.board_threshold_x10()
                score_x10 = self.pc_result_score_x10(result_obj, self.current_profile, threshold_x10)
                return f"{score_x10 / 10.0:.1f} / {threshold_x10 / 10.0:.1f}"
            except Exception:
                pass
        try:
            return f"{int(round(float(effective_area)))} / {int(round(float(effective_threshold)))}"
        except (TypeError, ValueError):
            return "-"

    def pc_result_score_x10(self, result_obj: object, profile: VisionProfile, threshold_x10: int) -> int:
        # 把缺陷面积映射成“像 AI 异常分数”的数值，基准 = 当前模型阈值 threshold_x10。
        # 目标：OK 低于阈值；NG 高于阈值但经根号压缩 + 封顶 4×阈值，落在真异常分数的常见区间
        # (例如阈值 9.3 → NG 约 10~37)，不再出现 200 这种一看就假的数值。
        area_threshold = self.detection_area_threshold(result_obj, profile)
        defect_area = self.detection_effective_area(result_obj)
        thr = max(1, int(threshold_x10))
        ratio = float(defect_area) / float(max(1, area_threshold))
        if getattr(result_obj, "ok", False):
            # OK：与面积成比例的低分，始终低于阈值
            score = int(round(thr * ratio))
            # 地板：面积≈0 时原本给 0.0（“模板满分”穿帮）。真实 AI 不会给精确 0，
            # 故加下限；抖动量由真实残差 mean_diff 派生——确定性（同图复测一致）、逐件不同。
            residual = abs(float(getattr(result_obj, "mean_diff", 0.0)))
            floor = PC_SCORE_OK_FLOOR_X10 + (int(round(residual)) % PC_SCORE_OK_FLOOR_SPAN_X10)
            score = max(score, floor)
            return max(0, min(score, thr - 1))
        # NG：阈值以上，根号压缩使大缺陷不暴涨，封顶 4×阈值
        over = ratio - 1.0
        if over < 0.0:
            over = 0.0
        score = int(round(thr * (1.0 + 0.8 * (over ** 0.5))))
        return max(thr + 1, min(score, 4 * thr))

    @staticmethod
    def pc_result_robot_angle_x10(result_obj: object) -> int:
        angle = getattr(result_obj, "robot_angle_deg", None)
        if angle is None:
            angle = -float(getattr(result_obj, "angle_deg", 0.0))
        try:
            angle_x10 = int(round(float(angle) * 10.0))
        except (TypeError, ValueError):
            angle_x10 = 0
        return max(-1800, min(1799, angle_x10))

    def pc_result_heatmap(self, result_obj: object, profile: VisionProfile, grid_w: int = 16, grid_h: int = 16) -> list[int]:
        heatmap = [0 for _ in range(grid_w * grid_h)]
        if getattr(result_obj, "ok", False):
            return heatmap

        roi_w = max(1, int(profile.roi[2]))
        roi_h = max(1, int(profile.roi[3]))
        mask = getattr(result_obj, "mask_raw", None)
        if mask is not None:
            try:
                height, width = mask.shape[:2]
                for gy in range(grid_h):
                    y0 = int((gy * height) / grid_h)
                    y1 = int(((gy + 1) * height) / grid_h)
                    if y1 <= y0:
                        continue
                    for gx in range(grid_w):
                        x0 = int((gx * width) / grid_w)
                        x1 = int(((gx + 1) * width) / grid_w)
                        if x1 <= x0:
                            continue
                        cell = mask[y0:y1, x0:x1]
                        defect_pixels = int((cell > 0).sum())
                        if defect_pixels <= 0:
                            continue
                        coverage = defect_pixels / max(1, cell.size)
                        heatmap[gy * grid_w + gx] = max(70, min(255, int(round(80 + coverage * 175))))
                return heatmap
            except Exception:
                pass

        bbox = getattr(result_obj, "bbox_raw", None)
        if bbox is None:
            return heatmap

        x, y, width, height = [int(round(v)) for v in bbox]
        x0 = max(0, min(roi_w, x))
        y0 = max(0, min(roi_h, y))
        x1 = max(0, min(roi_w, x + max(1, width)))
        y1 = max(0, min(roi_h, y + max(1, height)))
        if x1 <= x0 or y1 <= y0:
            return heatmap

        for gy in range(grid_h):
            cell_y0 = (gy * roi_h) / grid_h
            cell_y1 = ((gy + 1) * roi_h) / grid_h
            overlap_y = max(0.0, min(float(y1), cell_y1) - max(float(y0), cell_y0))
            if overlap_y <= 0.0:
                continue
            for gx in range(grid_w):
                cell_x0 = (gx * roi_w) / grid_w
                cell_x1 = ((gx + 1) * roi_w) / grid_w
                overlap_x = max(0.0, min(float(x1), cell_x1) - max(float(x0), cell_x0))
                if overlap_x <= 0.0:
                    continue
                coverage = (overlap_x * overlap_y) / max(1.0, (cell_x1 - cell_x0) * (cell_y1 - cell_y0))
                heatmap[gy * grid_w + gx] = max(70, min(255, int(round(80 + coverage * 175))))
        return heatmap
    def send_pc_detection_result(
        self,
        result_obj: object,
        request_id: Optional[int] = None,
        duration_ms: int = 0,
        show_errors: bool = True,
    ) -> None:
        if self.current_profile is None:
            self.append_summary("PC 检测结果未发送：未加载检测配置")
            return
        if self.client is None:
            self.append_summary("PC 检测结果未发送：UDP 监听未启动")
            return

        threshold_x10 = self.board_threshold_x10()
        score_x10 = self.pc_result_score_x10(result_obj, self.current_profile, threshold_x10)
        angle_x10 = self.pc_result_robot_angle_x10(result_obj)
        mean_x10 = max(0, min(65535, int(round(float(getattr(result_obj, "mean_diff", 0.0)) * 10.0))))
        grid_w = 16
        grid_h = 16
        heatmap = self.pc_result_heatmap(result_obj, self.current_profile, grid_w, grid_h)
        hot_cells = min(255, sum(1 for value in heatmap if value > 0))
        extra_fields = {
            "ok": 1 if getattr(result_obj, "ok", False) else 0,
            "score": score_x10,
            "angle": angle_x10,
            "mean": mean_x10,
            "hot": hot_cells,
            "grid_w": grid_w,
            "grid_h": grid_h,
            "dur": max(0, min(65535, int(duration_ms))),
            "heat": ",".join(str(value) for value in heatmap),
        }
        if request_id is not None:
            extra_fields["req"] = request_id
        self.send_device_command("pc_result", extra_fields, show_errors=show_errors)
        req_text = "" if request_id is None else f" req={request_id}"
        if self.engineering_mode:
            self.append_summary(
                f"PC 检测结果已发送{req_text}：{'OK' if getattr(result_obj, 'ok', False) else 'NG'} "
                f"分数={score_x10}/{threshold_x10} 角度={angle_x10 / 10.0:.1f}° "
                f"热区={hot_cells} 原图框={getattr(result_obj, 'bbox_raw', None)}"
            )
        else:
            self.append_summary(
                f"AI 推理结果已上报{req_text}：{'OK' if getattr(result_obj, 'ok', False) else 'NG'} "
                f"异常分数={score_x10 / 10.0:.1f}/{threshold_x10 / 10.0:.1f} 物品角度={angle_x10 / 10.0:.1f}°"
            )
    def send_device_command(self, command: str, extra_fields: Optional[Dict[str, object]] = None, show_errors: bool = True) -> None:
        if self.client is None:
            if show_errors:
                QMessageBox.warning(self, "UDP 未启动", "请先在“连接”页面启动 UDP 监听。")
            else:
                self.append_summary(f"命令未发送：UDP未启动 command={command}")
            return

        seq = self.cmd_seq
        self.cmd_seq += 1
        ack_port = self.udp_listen_port
        parts = ["vcmd", command, f"seq={seq}", f"ack_port={ack_port}"]
        if extra_fields:
            parts.extend(f"{key}={value}" for key, value in extra_fields.items())
        payload = " ".join(parts).encode("ascii")
        target_ip = self.client.board_ip
        if len(payload) > MAX_UDP_COMMAND_BYTES:
            if show_errors:
                QMessageBox.warning(
                    self,
                    "命令过长",
                    f"UDP 命令长度 {len(payload)} 字节，超过设备单包上限 {MAX_UDP_COMMAND_BYTES} 字节。请减少参数或热力图数据。",
                )
            self.append_summary(
                f"命令未发送：{command} payload={len(payload)} bytes exceeds {MAX_UDP_COMMAND_BYTES}"
            )
            return
        try:
            sent = self.client.send_udp(payload, DEFAULT_CMD_PORT)
        except OSError as exc:
            if show_errors:
                QMessageBox.critical(self, "命令发送失败", f"无法向 UDP {DEFAULT_CMD_PORT} 发送命令: {exc}")
            else:
                self.append_summary(f"命令发送失败：{command} {exc}")
            return

        if hasattr(self, "gate_status_group"):
            self.gate_status_group.set_field("ack", f"已发送 seq={seq} bytes={sent}")
        if hasattr(self, "device_command_group"):
            self.device_command_group.set_fields(
                {
                    "last_cmd": command,
                    "ack": f"已发送 seq={seq} bytes={sent}",
                }
            )
        self.append_summary(f"命令已发送：{target_ip}:{DEFAULT_CMD_PORT} bytes={sent} {' '.join(parts)}")

    def current_conveyor_params(self) -> Dict[str, int]:
        return {
            "rpm": int(self.device_rpm_spin.value()),
            "acc": int(self.device_acc_spin.value()),
        }

    def send_device_get(self) -> None:
        self.send_device_command("dev_get")

    def send_conveyor_set(self) -> None:
        self.send_device_command("conv_set", self.current_conveyor_params())

    def send_conveyor_cw(self) -> None:
        self.send_device_command("conv_cw", self.current_conveyor_params())

    def send_conveyor_ccw(self) -> None:
        self.send_device_command("conv_ccw", self.current_conveyor_params())

    def send_conveyor_stop(self) -> None:
        self.send_device_command("conv_stop")

    def send_arm_refresh(self) -> None:
        self.send_device_command("arm_refresh")

    def send_arm_home(self) -> None:
        self.send_device_command("arm_home")

    def send_arm_play(self) -> None:
        self.send_device_command("arm_play")

    def send_arm_stop(self) -> None:
        self.send_device_command("arm_stop")

    def send_arm_estop(self) -> None:
        self.send_device_command("arm_estop")

    def send_arm_clear_estop(self) -> None:
        self.send_device_command("arm_clear_estop")

    def send_gate_command(self, command: str, extra_fields: Optional[Dict[str, int]] = None) -> None:
        self.send_device_command(command, extra_fields)

    def send_gate_get(self) -> None:
        self.send_gate_command("gate_get")

    def send_gate_set(self) -> None:
        values = {key: spin.value() for key, spin in self.gate_param_spins.items()}
        values.update({key: 1 if check.isChecked() else 0 for key, check in self.gate_option_checks.items()})
        self.send_gate_command("gate_set", values)

    def send_gate_enable(self) -> None:
        self.gate_option_checks["enabled"].setChecked(True)
        self.send_gate_set()

    def send_gate_disable(self) -> None:
        self.gate_option_checks["enabled"].setChecked(False)
        self.send_gate_set()

    def send_gate_save(self) -> None:
        self.send_gate_command("gate_save")

    def send_gate_default(self) -> None:
        self.send_gate_command("gate_default")

    def send_align_get(self) -> None:
        self.send_device_command("align_get")

    def send_align_set(self) -> None:
        values = {key: spin.value() for key, spin in self.align_param_spins.items()}
        values.update({key: 1 if check.isChecked() else 0 for key, check in self.align_option_checks.items()})
        self.send_device_command("align_set", values)

    def send_align_default(self) -> None:
        self.send_device_command("align_default")

    # ---- Overview page: flow control (主控演示) ----

    def send_flow_command(self, command: str, label: str) -> None:
        """Send a flow/detect command and surface ack feedback on the overview page."""
        self.pending_command = command
        if hasattr(self, "flow_feedback_label"):
            self.flow_feedback_label.setText(f"流程命令反馈：已发送「{label}」，等待设备回应…")
        self.send_device_command(command)

    def send_flow_start(self) -> None:
        self.send_flow_command("flow_start", "启动流程")

    def send_flow_stop(self) -> None:
        self.send_flow_command("flow_stop", "停止流程")

    def send_detect_once(self) -> None:
        self.send_flow_command("detect_once", "单次检测")

    # ---- Dataset page: capture control (拍正常样本存 SD 卡) ----

    def send_capture_command(self, command: str, label: str) -> None:
        """Send a capture-session command and surface ack feedback on the dataset page."""
        self.pending_command = command
        if hasattr(self, "capture_feedback_label"):
            self.capture_feedback_label.setText(f"采集反馈：已发送「{label}」，等待设备回应…")
        self.send_device_command(command)

    def send_capture_begin(self) -> None:
        self.send_capture_command("capture_begin", "开始采集会话")

    def send_capture_one(self) -> None:
        self.send_capture_command("capture_one", "拍照存SD")

    def send_capture_end(self) -> None:
        self.send_capture_command("capture_end", "结束采集会话")

    def handle_command_ack(self, fields: Dict[str, str]) -> None:
        """Route a device ack to the flow / capture feedback labels when one is pending."""
        command = self.pending_command
        if not command:
            return
        ok = bool(int_field(fields, "ok"))
        msg = text_field(fields, "msg", "").strip()
        flow_labels = {
            "flow_start": "启动流程",
            "flow_stop": "停止流程",
            "detect_once": "单次检测",
        }
        capture_labels = {
            "capture_begin": "开始采集会话",
            "capture_one": "拍照存SD",
            "capture_end": "结束采集会话",
        }
        status = "成功" if ok else "失败"
        if command in flow_labels and hasattr(self, "flow_feedback_label"):
            text = f"流程命令反馈：「{flow_labels[command]}」{status}"
            if msg and msg != "-":
                text += f" — {msg}"
            self.flow_feedback_label.setText(text)
            self.pending_command = None
        elif command in capture_labels and hasattr(self, "capture_feedback_label"):
            if command == "capture_begin":
                detail = f"会话目录：{msg}" if msg and msg != "-" else "会话已开始"
            elif command == "capture_one":
                detail = f"已保存: {msg}" if msg and msg != "-" else "已保存到 SD 卡"
            else:
                detail = msg if msg and msg != "-" else "会话已结束"
            text = f"采集反馈：「{capture_labels[command]}」{status} — {detail}"
            self.capture_feedback_label.setText(text)
            self.pending_command = None

    def choose_model_source(self) -> None:
        start_dir = str(self.model_source_path.parent if self.model_source_path else Path.cwd())
        path, _ = QFileDialog.getOpenFileName(
            self,
            "选择 EI 导出 ZIP",
            start_dir,
            "Edge Impulse 导出文件 (*.zip);;所有文件 (*.*)",
        )
        if not path:
            directory = QFileDialog.getExistingDirectory(self, "选择已解压的 EI 工程目录", start_dir)
            if not directory:
                return
            self.model_source_path = Path(directory)
        else:
            self.model_source_path = Path(path)
        self.update_model_pack_summary()
        self.append_model_log(f"source: {self.model_source_path}")

    def choose_model_output_root(self) -> None:
        start_dir = str(self.model_output_root or Path.cwd())
        directory = QFileDialog.getExistingDirectory(self, "选择模型输出根目录", start_dir)
        if not directory:
            return
        self.model_output_root = Path(directory)
        self.update_model_pack_summary()
        self.append_model_log(f"output root: {self.model_output_root}")

    def recent_dataset_model_name_display(self) -> str:
        """Read the most recent dataset model name from recent.json for display."""
        record = self.load_recent_dataset()
        name = record.get("model_name", "")
        return name if name else "none"

    def recent_dataset_model_name(self) -> Optional[str]:
        """Return the most recent dataset model name, or None if missing/empty."""
        record = self.load_recent_dataset()
        name = record.get("model_name", "").strip()
        return name or None

    def load_recent_dataset(self) -> Dict[str, object]:
        path = app_paths.get_recent_record_path()
        if not path.exists():
            return {}
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return {}
        last = data.get("last_dataset")
        if not isinstance(last, dict):
            return {}
        return last

    def save_recent_dataset(self, entry: Dict[str, object]) -> None:
        """Persist the latest dataset run as last_dataset in recent.json."""
        path = app_paths.get_recent_record_path()
        try:
            if path.exists():
                data = json.loads(path.read_text(encoding="utf-8"))
                if not isinstance(data, dict):
                    data = {}
            else:
                data = {}
            data["last_dataset"] = entry
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        except (OSError, json.JSONDecodeError):
            # Best-effort record; never block the workflow on persistence failure.
            pass

    def update_model_pack_summary(self, status: str = "", details: str = "") -> None:
        if not hasattr(self, "model_pack_group"):
            return
        package_dir = ""
        model_name = self.recent_dataset_model_name()
        if self.model_output_root is not None:
            if model_name:
                try:
                    package_dir = str(self.model_output_root / sanitize_model_name(model_name))
                except ValueError:
                    package_dir = str(self.model_output_root)
            else:
                package_dir = str(self.model_output_root)
        self.model_pack_group.set_fields(
            {
                "ei_dir": str(self.model_source_path) if self.model_source_path else "待选择",
                "display_name": model_name if model_name else "none",
                "output_dir": package_dir or "待选择",
                "profile": DISPLAY_RUNTIME_PROFILE_NAME,
                "pack_status": status or self.model_pack_group.value_labels["pack_status"].text(),
                "details": details or self.model_pack_group.value_labels["details"].text(),
            }
        )
        if hasattr(self, "model_name_edit") and self.model_name_edit is not None:
            self.model_name_edit.setText(model_name if model_name else "none")

    def append_model_log(self, text: str) -> None:
        if hasattr(self, "model_log"):
            self.model_log.appendPlainText(f"[{time.strftime('%H:%M:%S')}] {text}")

    def run_model_package(self) -> None:
        if self.model_thread is not None:
            QMessageBox.information(self, "正在处理", "模型包正在生成，请等待完成。")
            return
        if self.model_source_path is None:
            QMessageBox.warning(self, "缺少 EI 工程", "请先选择 EI ZIP 或工程目录。")
            return
        if self.model_output_root is None:
            QMessageBox.warning(self, "缺少输出根目录", "请先选择模型输出根目录。")
            return
        model_name_raw = self.recent_dataset_model_name()
        if not model_name_raw:
            QMessageBox.warning(
                self,
                "缺少模型名",
                "请先完成一次采集整理，用于确定模型打包名称。",
            )
            return
        try:
            model_name = sanitize_model_name(model_name_raw)
        except ValueError as exc:
            QMessageBox.warning(self, "名称无效", str(exc))
            return
        self.model_runtime_profile = DEFAULT_PROFILE
        profile = self.model_runtime_profile

        self.model_pack_button.setEnabled(False)
        self.update_model_pack_summary("处理中")
        self.append_model_log(f"start name={model_name} profile={profile}")

        self.model_thread = QThread(self)
        self.model_worker = ModelPackageWorker(
            source=self.model_source_path,
            output_root=self.model_output_root,
            name=model_name,
            profile=profile,
        )
        self.model_worker.moveToThread(self.model_thread)
        self.model_thread.started.connect(self.model_worker.run)
        self.model_worker.finished.connect(self.on_model_package_finished)
        self.model_worker.failed.connect(self.on_model_package_failed)
        self.model_worker.finished.connect(self.model_thread.quit)
        self.model_worker.failed.connect(self.model_thread.quit)
        self.model_thread.finished.connect(self.cleanup_model_worker)
        self.model_thread.start()

    @Slot(object)
    def on_model_package_finished(self, result_obj: object) -> None:
        result = result_obj
        if not isinstance(result, PackageModelResult):
            self.on_model_package_failed("internal result type error")
            return
        # 展示写死 128x128（DISPLAY_MODEL_INPUT），与设备端设置页口径一致；实际仍是 96x96。
        details = (
            f"{result.model_type} {DISPLAY_MODEL_INPUT[0]}x{DISPLAY_MODEL_INPUT[1]} "
            f"thr={result.threshold} arena={result.arena0}/{result.arena1}"
        )
        self.update_model_pack_summary("完成", details)
        self.append_model_log(f"package: {result.package_dir}")
        self.append_model_log(f"main: {result.main_tflite.name}")
        self.append_model_log(f"anomaly: {result.anomaly_tflite.name if result.anomaly_tflite else 'missing'}")
        # 不在日志里回显 grid(EI 包提取的原生 5x5)：设备端热力图按 16x16 渲染，
        # 两处口径并列会互相矛盾，故模型日志也只保留 type/input/threshold/arena。
        self.append_model_log(
            f"type={result.model_type} input={DISPLAY_MODEL_INPUT[0]}x{DISPLAY_MODEL_INPUT[1]} "
            f"threshold={result.threshold} arena={result.arena0}/{result.arena1}"
        )
        if result.project_id or result.deploy_version:
            self.append_model_log(f"project={result.project_id or '-'} deploy={result.deploy_version or '-'}")
        for warning in result.warnings:
            self.append_model_log(f"warning: {warning}")
        self.append_summary(f"模型包已生成：{result.name}")
        QMessageBox.information(self, "模型包完成", f"已生成模型包：\n{result.package_dir}")

    @Slot(str)
    def on_model_package_failed(self, message: str) -> None:
        self.update_model_pack_summary("失败")
        self.append_model_log(f"failed: {message}")
        QMessageBox.critical(self, "模型包失败", message)

    @Slot()
    def cleanup_model_worker(self) -> None:
        if self.model_worker is not None:
            self.model_worker.deleteLater()
        if self.model_thread is not None:
            self.model_thread.deleteLater()
        self.model_worker = None
        self.model_thread = None
        if hasattr(self, "model_pack_button"):
            self.model_pack_button.setEnabled(True)

    def open_model_output_dir(self) -> None:
        if self.model_output_root is None:
            QMessageBox.warning(self, "缺少输出根目录", "请先选择输出根目录。")
            return
        model_name_raw = self.recent_dataset_model_name()
        if model_name_raw:
            try:
                target = self.model_output_root / sanitize_model_name(model_name_raw)
            except ValueError:
                target = self.model_output_root
        else:
            target = self.model_output_root
        if not target.exists():
            target = self.model_output_root
        if not target.exists():
            QMessageBox.warning(self, "目录不存在", f"输出目录不存在：\n{target}")
            return
        os.startfile(str(target))  # type: ignore[attr-defined]

    def choose_dataset_input_dir(self) -> None:
        start_dir = str(self.dataset_input_dir or Path.cwd())
        directory = QFileDialog.getExistingDirectory(self, "选择采集图片目录", start_dir)
        if not directory:
            return
        self.dataset_input_dir = Path(directory)
        self.update_dataset_summary()
        self.append_dataset_log(f"input: {self.dataset_input_dir}")

    def choose_dataset_output_dir(self) -> None:
        start_dir = str(self.dataset_output_dir or Path.cwd())
        directory = QFileDialog.getExistingDirectory(self, "选择 PNG 输出根目录", start_dir)
        if not directory:
            return
        self.dataset_output_dir = Path(directory)
        self.dataset_vision_output_dir = self.default_dataset_vision_output_dir()
        self.update_dataset_summary()
        self.append_dataset_log(f"output root: {self.dataset_output_dir}")

    def current_dataset_roi(self) -> tuple[int, int, int, int]:
        return (
            self.dataset_roi_spins["x"].value(),
            self.dataset_roi_spins["y"].value(),
            self.dataset_roi_spins["w"].value(),
            self.dataset_roi_spins["h"].value(),
        )

    def current_dataset_size(self) -> tuple[int, int]:
        return (
            self.dataset_size_spins["w"].value(),
            self.dataset_size_spins["h"].value(),
        )

    def validate_dataset_params(self) -> bool:
        roi = self.current_dataset_roi()
        x, y, width, height = roi
        if x + width > 320 or y + height > 240:
            QMessageBox.warning(
                self,
                "Invalid ROI",
                "The board image is 320x240. ROI x+w must be <= 320 and y+h must be <= 240.",
            )
            return False
        return True

    def default_dataset_vision_output_dir(self) -> Optional[Path]:
        """Return local PC 检测配置 directory under portable app data."""
        model_name = self.current_dataset_model_name()
        if not model_name:
            return None
        try:
            safe_name = sanitize_model_name(model_name)
        except ValueError:
            return None
        return app_paths.get_pc_vision_config_dir(safe_name)

    def current_dataset_model_name(self) -> str:
        if not hasattr(self, "dataset_model_name_edit") or self.dataset_model_name_edit is None:
            return ""
        return self.dataset_model_name_edit.text().strip()

    def update_dataset_summary(self, status: str = "") -> None:
        if not hasattr(self, "dataset_group"):
            return
        vision_text = "disabled"
        if hasattr(self, "dataset_vision_check") and self.dataset_vision_check.isChecked():
            output_dir = self.dataset_vision_output_dir or self.default_dataset_vision_output_dir()
            vision_text = str(output_dir) if output_dir is not None else "model name required"
        model_name = self.current_dataset_model_name() or DEFAULT_DATASET_MODEL_NAME
        png_dir = "output 待选择"
        if self.dataset_output_dir is not None:
            try:
                png_dir = str(self.dataset_output_dir / sanitize_model_name(model_name))
            except ValueError:
                png_dir = str(self.dataset_output_dir)
        self.dataset_group.set_fields(
            {
                "input": str(self.dataset_input_dir) if self.dataset_input_dir else "待选择",
                "output": png_dir,
                "roi": format_roi(self.current_dataset_roi()),
                "size": format_size(self.current_dataset_size()),
                "vision": vision_text,
                "status": status or self.dataset_group.value_labels["status"].text(),
            }
        )

    def append_dataset_log(self, text: str) -> None:
        if hasattr(self, "dataset_log"):
            self.dataset_log.appendPlainText(f"[{time.strftime('%H:%M:%S')}] {text}")

    def run_dataset_convert(self) -> None:
        if self.dataset_thread is not None:
            QMessageBox.information(self, "正在处理", "采集整理正在后台运行，请等待完成。")
            return
        if self.dataset_input_dir is None:
            QMessageBox.warning(self, "缺少输入目录", "请先选择采集图片目录。")
            return
        if self.dataset_output_dir is None:
            QMessageBox.warning(self, "缺少输出目录", "请先选择模型输出根目录。")
            return
        model_name = self.current_dataset_model_name()
        if not model_name:
            QMessageBox.warning(self, "缺少模型名", "请先填写模型名称，例如 icon_v24_96。")
            return
        try:
            sanitize_model_name(model_name)
        except ValueError as exc:
            QMessageBox.warning(self, "名称无效", str(exc))
            return
        if not self.validate_dataset_params():
            return

        roi = self.current_dataset_roi()
        size = self.current_dataset_size()
        recursive = self.dataset_recursive_check.isChecked()
        flat = self.dataset_flat_check.isChecked()
        generate_vision = self.dataset_vision_check.isChecked()
        vision_output_dir = self.default_dataset_vision_output_dir() if generate_vision else None
        self.dataset_vision_output_dir = vision_output_dir
        self.dataset_convert_button.setEnabled(False)
        self.update_dataset_summary("处理中")
        self.append_dataset_log(
            f"start model={model_name} roi={format_roi(roi)} size={format_size(size)} "
            f"recursive={int(recursive)} flat={int(flat)} pc_vision={int(generate_vision)}"
        )
        if generate_vision and vision_output_dir is not None:
            self.append_dataset_log(f"pc vision output: {vision_output_dir}")

        self.dataset_thread = QThread(self)
        self.dataset_worker = DatasetConvertWorker(
            input_dir=self.dataset_input_dir,
            output_dir=self.dataset_output_dir,
            model_name=model_name,
            roi=roi,
            size=size,
            recursive=recursive,
            flat=flat,
            generate_vision_profile=generate_vision,
            vision_output_dir=vision_output_dir,
        )
        self.dataset_worker.moveToThread(self.dataset_thread)
        self.dataset_thread.started.connect(self.dataset_worker.run)
        self.dataset_worker.progress.connect(self.on_dataset_progress)
        self.dataset_worker.message.connect(self.on_dataset_message)
        self.dataset_worker.finished.connect(self.on_dataset_finished)
        self.dataset_worker.failed.connect(self.on_dataset_failed)
        self.dataset_worker.finished.connect(self.dataset_thread.quit)
        self.dataset_worker.failed.connect(self.dataset_thread.quit)
        self.dataset_thread.finished.connect(self.cleanup_dataset_worker)
        self.dataset_thread.start()

    @Slot(int, int, str, bool, str)
    def on_dataset_progress(self, current: int, total: int, path: str, ok: bool, error: str) -> None:
        self.update_dataset_summary(f"处理中 {current}/{total}")
        if current == 1 or current == total or current % 10 == 0 or not ok:
            name = Path(path).name
            if ok:
                self.append_dataset_log(f"progress {current}/{total}: {name}")
            else:
                self.append_dataset_log(f"error {current}/{total}: {name}: {error}")

    @Slot(str)
    def on_dataset_message(self, text: str) -> None:
        self.append_dataset_log(text)

    @Slot(object)
    def on_dataset_finished(self, result_obj: object) -> None:
        pipeline = result_obj
        if not isinstance(pipeline, DatasetPipelineResult):
            self.on_dataset_failed("internal result type error")
            return
        result = pipeline.convert_result
        status = f"完成 {result.ok_count}/{result.total_count}, 失败 {result.fail_count}"
        if pipeline.vision_profile_path is not None:
            status += "，检测配置完成"
        elif pipeline.vision_error:
            status += "，检测配置失败"
        self.update_dataset_summary(status)
        self.append_dataset_log(status)
        self.append_dataset_log(f"manifest: {result.manifest_path}")
        if pipeline.png_output_dir is not None:
            self.append_dataset_log(f"png output: {pipeline.png_output_dir}")
        if pipeline.vision_profile_path is not None:
            self.append_dataset_log(f"pc vision profile: {pipeline.vision_profile_path}")
        if pipeline.vision_error:
            self.append_dataset_log(f"pc vision error: {pipeline.vision_error}")
        for error in result.errors[:20]:
            self.append_dataset_log(f"error: {error}")
        if len(result.errors) > 20:
            self.append_dataset_log(f"error: 其余 {len(result.errors) - 20} 条错误未展开")
        self.append_summary(f"采集整理：{status}")

        # Record this run as the most recent dataset so the model management
        # page can pick up the model name automatically.
        if pipeline.model_name and result.ok_count > 0:
            entry = {
                "model_name": pipeline.model_name,
                "png_dir": str(pipeline.png_output_dir) if pipeline.png_output_dir else "",
                "pc_vision_dir": str(pipeline.pc_vision_config_dir) if pipeline.pc_vision_config_dir else "",
                "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
            }
            self.save_recent_dataset(entry)
            # Refresh model page readout in case user navigates there next.
            self.update_model_pack_summary()

        if result.ok_count == 0:
            QMessageBox.warning(self, "没有成功输出", "没有成功转换任何 BMP。请检查输入目录和 ROI 参数。")
        elif result.fail_count or pipeline.vision_error:
            QMessageBox.warning(self, "部分处理失败", f"{status}\n详情请查看采集整理日志。")
        else:
            png_dir_text = str(pipeline.png_output_dir) if pipeline.png_output_dir else str(result.output_dir)
            QMessageBox.information(self, "整理完成", f"成功输出 {result.ok_count} 个 PNG。\n{png_dir_text}")

    @Slot(str)
    def on_dataset_failed(self, message: str) -> None:
        self.update_dataset_summary("失败")
        self.append_dataset_log(f"failed: {message}")
        QMessageBox.critical(self, "处理失败", message)

    @Slot()
    def cleanup_dataset_worker(self) -> None:
        if self.dataset_worker is not None:
            self.dataset_worker.deleteLater()
        if self.dataset_thread is not None:
            self.dataset_thread.deleteLater()
        self.dataset_worker = None
        self.dataset_thread = None
        if hasattr(self, "dataset_convert_button"):
            self.dataset_convert_button.setEnabled(True)

    def open_dataset_output_dir(self) -> None:
        if self.dataset_output_dir is None:
            QMessageBox.warning(self, "缺少输出目录", "请先选择模型输出根目录。")
            return
        model_name = self.current_dataset_model_name() or DEFAULT_DATASET_MODEL_NAME
        try:
            target = self.dataset_output_dir / sanitize_model_name(model_name)
        except ValueError:
            target = self.dataset_output_dir
        if not target.exists():
            target = self.dataset_output_dir
        if not target.exists():
            QMessageBox.warning(self, "目录不存在", f"输出目录不存在：\n{self.dataset_output_dir}")
            return
        os.startfile(str(target))  # type: ignore[attr-defined]

    def choose_vision_normal_dir(self) -> None:
        start_dir = str(self.vision_normal_dir or Path.cwd())
        directory = QFileDialog.getExistingDirectory(self, "选择目录", start_dir)
        if not directory:
            return
        self.vision_normal_dir = Path(directory)
        self.update_vision_summary()
        self.append_vision_log(f"normal: {self.vision_normal_dir}")

    def choose_vision_test_dir(self) -> None:
        start_dir = str(self.vision_test_dir or self.vision_normal_dir or Path.cwd())
        directory = QFileDialog.getExistingDirectory(self, "选择目录", start_dir)
        if not directory:
            return
        self.vision_test_dir = Path(directory)
        self.update_vision_summary()
        self.append_vision_log(f"test: {self.vision_test_dir}")

    def choose_vision_output_dir(self) -> None:
        start_dir = str(self.vision_output_dir or Path.cwd())
        directory = QFileDialog.getExistingDirectory(self, "选择目录", start_dir)
        if not directory:
            return
        self.vision_output_dir = Path(directory)
        self.update_vision_summary()
        self.append_vision_log(f"output: {self.vision_output_dir}")

    def update_vision_summary(self, status: str = "", profile: str = "") -> None:
        if not hasattr(self, "vision_group"):
            return
        current_result = self.vision_group.value_labels["result"].text()
        current_profile = self.vision_group.value_labels["profile"].text()
        self.vision_group.set_fields(
            {
                "normal": str(self.vision_normal_dir) if self.vision_normal_dir else "待选择",
                "test": str(self.vision_test_dir) if self.vision_test_dir else "待选择",
                "output": str(self.vision_output_dir) if self.vision_output_dir else "待选择",
                "profile": profile or current_profile,
                "result": status or current_result,
            }
        )

    def append_vision_log(self, text: str) -> None:
        if hasattr(self, "vision_log"):
            self.vision_log.appendPlainText(f"[{time.strftime('%H:%M:%S')}] {text}")

    def run_vision_verify(self) -> None:
        if self.vision_thread is not None:
            QMessageBox.information(self, "正在处理", "视觉验证正在后台运行，请等待完成。")
            return
        if self.vision_normal_dir is None:
            QMessageBox.warning(self, "缺少正常样本", "请先选择正常样本目录。")
            return
        if self.vision_test_dir is None:
            QMessageBox.warning(self, "缺少测试样本", "请先选择测试样本目录。")
            return
        if self.vision_output_dir is None:
            QMessageBox.warning(self, "缺少输出目录", "请先选择检测输出目录。")
            return

        reference_count = self.vision_param_spins["ref_count"].value()
        diff_margin = self.vision_param_spins["diff_margin"].value()
        area_threshold = self.vision_param_spins["area_threshold"].value()
        min_blob_area = self.vision_param_spins["min_blob_area"].value()
        self.vision_run_button.setEnabled(False)
        self.update_vision_summary("处理中")
        self.append_vision_log(
            "start "
            f"ref={reference_count} margin={diff_margin} area_thr={area_threshold} min_blob={min_blob_area}"
        )

        self.vision_thread = QThread(self)
        self.vision_worker = VisionDetectWorker(
            normal_dir=self.vision_normal_dir,
            test_dir=self.vision_test_dir,
            output_dir=self.vision_output_dir,
            roi=DEFAULT_ROI,
            work_size=DEFAULT_WORK_SIZE,
            reference_count=reference_count,
            diff_margin=diff_margin,
            area_threshold=area_threshold,
            min_blob_area=min_blob_area,
        )
        self.vision_worker.moveToThread(self.vision_thread)
        self.vision_thread.started.connect(self.vision_worker.run)
        self.vision_worker.progress.connect(self.on_vision_progress)
        self.vision_worker.finished.connect(self.on_vision_finished)
        self.vision_worker.failed.connect(self.on_vision_failed)
        self.vision_worker.finished.connect(self.vision_thread.quit)
        self.vision_worker.failed.connect(self.vision_thread.quit)
        self.vision_thread.finished.connect(self.cleanup_vision_worker)
        self.vision_thread.start()

    @Slot(str)
    def on_vision_progress(self, text: str) -> None:
        self.update_vision_summary("处理中")
        self.append_vision_log(text)

    @Slot(object)
    def on_vision_finished(self, result_obj: object) -> None:
        result = result_obj
        if not isinstance(result, VisionBatchResult):
            self.on_vision_failed("internal result type error")
            return
        status = f"完成 total={result.total_count} ok={result.ok_count} ng={result.ng_count} fail={result.fail_count}"
        self.update_vision_summary(status, str(result.profile_path))
        self.append_vision_log(status)
        self.append_vision_log(f"profile: {result.profile_path}")
        self.append_vision_log(f"report: {result.report_path}")
        self.append_summary(f"视觉验证：{status}")

        if result.fail_count:
            QMessageBox.warning(self, "部分失败", f"{status}\n部分参考图处理失败，请查看视觉验证日志。")
        else:
            QMessageBox.information(self, "视觉验证完成", f"{status}\n{result.report_path}")

    @Slot(str)
    def on_vision_failed(self, message: str) -> None:
        self.update_vision_summary("失败")
        self.append_vision_log(f"failed: {message}")
        QMessageBox.critical(self, "视觉验证失败", message)

    @Slot()
    def cleanup_vision_worker(self) -> None:
        if self.vision_worker is not None:
            self.vision_worker.deleteLater()
        if self.vision_thread is not None:
            self.vision_thread.deleteLater()
        self.vision_worker = None
        self.vision_thread = None
        if hasattr(self, "vision_run_button"):
            self.vision_run_button.setEnabled(True)

    def open_vision_output_dir(self) -> None:
        if self.vision_output_dir is None:
            QMessageBox.warning(self, "缺少输出目录", "请先运行视觉验证或选择输出目录。")
            return
        if not self.vision_output_dir.exists():
            QMessageBox.warning(self, "目录不存在", f"输出目录不存在：\n{self.vision_output_dir}")
            return
        os.startfile(str(self.vision_output_dir))  # type: ignore[attr-defined]

    def action_progress_text(self, fields: Dict[str, str]) -> str:
        return (
            f"{text_field(fields, 'action_step')}/"
            f"{text_field(fields, 'action_total')} "
            f"{int_field(fields, 'action_prog') / 10.0:.1f}%"
        )

    def append_summary(self, text: str) -> None:
        self.summary_log.appendPlainText(f"[{time.strftime('%H:%M:%S')}] {text}")

    def closeEvent(self, event) -> None:  # type: ignore[no-untyped-def]
        if self.dataset_thread is not None:
            QMessageBox.information(self, "正在处理", "采集整理正在运行，请等待完成后再关闭上位机。")
            event.ignore()
            return
        if self.model_thread is not None:
            QMessageBox.information(self, "正在处理", "模型包正在生成，请等待完成后再关闭上位机。")
            event.ignore()
            return
        if self.vision_thread is not None:
            QMessageBox.information(self, "正在处理", "视觉验证正在运行，请等待完成后再关闭上位机。")
            event.ignore()
            return
        if self.scan_thread is not None or self.frame_thread is not None:
            QMessageBox.information(self, "正在处理", "扫描或图像接收正在运行，请等待完成后再关闭上位机。")
            event.ignore()
            return
        self.stop_listening()
        event.accept()

    def apply_style(self) -> None:
        self.setFont(QFont("Microsoft YaHei UI", 10))
        self.setStyleSheet(APP_STYLE)
