#!/usr/bin/env python3
"""Offline template-based vision inspection for Vision Board captures.

The module is intentionally GUI-independent. The HMI uses the same functions
for offline batch validation today and can reuse them for TCP frames later.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Optional, Sequence, Tuple

from vision_dataset_tool import DEFAULT_ROI, load_rgb565_bmp, parse_roi


SUPPORTED_EXTENSIONS = {".bmp", ".png", ".jpg", ".jpeg"}
DEFAULT_WORK_SIZE = (240, 240)
DEFAULT_REFERENCE_COUNT = 5
DEFAULT_DIFF_MARGIN = 22
# 高光钳制后正常基线被压到 ~200-300（与物体无关），大缺陷（记号笔涂黑/撕角）≈1000+，
# 故默认阈值取 600 即可干净分离；细微划痕暂不在检测目标内。
DEFAULT_AREA_THRESHOLD = 600
DEFAULT_MIN_BLOB_AREA = 25
DEFAULT_BARCODE_AREA_THRESHOLD = 210
DEFAULT_BARCODE_LOCAL_AREA_THRESHOLD = 90
DEFAULT_BARCODE_SCORE_THRESHOLD = 150.0
DEFAULT_BARCODE_DARK_RATIO_THRESHOLD = 0.18
# 高光钳制上限：diff 前把模板与实时灰度都 min(gray, clip)。
# 玻璃/铜膜卡面的镜面高光逐帧漂移会在白色区域产生巨量伪缺陷（实测占误判面积 ~87%），
# 钳制后“白更白”归零，而白底上的暗缺陷（记号笔涂黑/撕角露底）走 dark_loss 仍能检出。
# 255 表示关闭钳制（如条形码这类无白底高光场景）。
DEFAULT_HIGHLIGHT_CLIP = 205
# 特征边缘松弛：模板梯度强处（图标内部线条/笔画边缘）额外放宽的容差量。
# 物体轻微旋转/位移时 ECC 无法把每条细线都对齐，线条边缘会产生“假残差”误判（用户实测底部线条被误标）。
# 在模板梯度 > FEATURE_EDGE_GRAD_THR 的像素带上把 allowed 抬高该值即可清除。
# 0 = 关闭（保持原行为）；代价是正好压在细线上的极小缺陷灵敏度下降，故大缺陷（涂黑/撕角）不受影响。
DEFAULT_FEATURE_EDGE_RELAX = 40
FEATURE_EDGE_GRAD_THR = 60.0
# 边缘带腐蚀(A)：判定缺陷前对 valid_mask 向内腐蚀的像素数。
# 圆形/弱纹理 logo 的对齐残差最集中在物体最外圈一圈，本就不是检测目标，
# 向内腐蚀 ~12-16px 即可把这圈对齐残差排除出缺陷计数。0 = 关闭（保持原行为）。
DEFAULT_BORDER_ERODE_PX = 6
# 自适应容差(E)：allowed = max(tolerance * adaptive_k, tolerance + diff_margin)。
# 乘性项让高波动区(纹理/边缘附近 tolerance 本就大)自动放宽，加性项保证低波动区仍严格。
DEFAULT_ADAPTIVE_TOL_K = 1.5
# 对齐升级(B)：喂给 ECC 的灰度高斯模糊核(降低 equalizeHist 噪声对梯度的干扰)。<=1 关闭。
DEFAULT_ALIGN_BLUR_KSIZE = 5
# 对齐后 NCC 低于该值视为对齐质量差(供诊断/质量字段)。
ALIGN_NCC_WARN = 0.7
# 采集质量门槛(C)：标定时正常基线 normal_p99 超过该值则在 profile 写 quality_warning。
NORMAL_P99_QUALITY_WARN = 150.0
DEFAULT_DETECTOR = "auto"
DEFAULT_FALLBACK_DETECTOR = "template_diff"
DETECTOR_TEMPLATE_DIFF = "template_diff"
DETECTOR_BARCODE = "barcode"
DETECTOR_HINT_TEMPLATE_DIFF = "template_diff"
DETECTOR_HINT_BARCODE = "barcode"
DETECTOR_HINT_UNKNOWN = "unknown"


@dataclass
class VisionProfile:
    name: str
    roi: Tuple[int, int, int, int]
    work_size: Tuple[int, int]
    template_rgb: object
    template_gray: object
    tolerance_gray: object
    valid_mask: object
    diff_margin: int
    area_threshold: int
    min_blob_area: int
    reference_count: int
    normal_count: int
    detector: str = DEFAULT_DETECTOR
    fallback_detector: str = DEFAULT_FALLBACK_DETECTOR
    detector_params: Optional[dict] = None
    detector_hint: str = DETECTOR_HINT_UNKNOWN
    detector_confidence: float = 0.0
    detector_reason: str = ""
    highlight_clip: int = DEFAULT_HIGHLIGHT_CLIP
    feature_edge_relax: int = DEFAULT_FEATURE_EDGE_RELAX
    border_erode_px: int = DEFAULT_BORDER_ERODE_PX
    adaptive_tol_k: float = DEFAULT_ADAPTIVE_TOL_K
    align_blur_ksize: int = DEFAULT_ALIGN_BLUR_KSIZE
    quality_warning: str = ""
    barcode_area_threshold: int = DEFAULT_BARCODE_AREA_THRESHOLD
    barcode_local_area_threshold: int = DEFAULT_BARCODE_LOCAL_AREA_THRESHOLD
    barcode_score_threshold: float = DEFAULT_BARCODE_SCORE_THRESHOLD
    barcode_dark_ratio_threshold: float = DEFAULT_BARCODE_DARK_RATIO_THRESHOLD


@dataclass
class VisionDetectResult:
    path: Path
    ok: bool
    score: float
    defect_area: int
    max_diff: int
    mean_diff: float
    angle_deg: float
    robot_angle_deg: float
    dx: float
    dy: float
    bbox_raw: Optional[Tuple[int, int, int, int]]
    bbox_aligned: Optional[Tuple[int, int, int, int]]
    mask_raw: Optional[object] = None
    mask_aligned: Optional[object] = None
    overlay_path: Optional[Path] = None
    error: str = ""
    detector: str = DEFAULT_DETECTOR
    reason: str = ""
    metrics: Optional[dict] = None
    debug_artifacts: Optional[dict] = None


def normalize_robot_angle_deg(angle_deg: float) -> float:
    """Convert visual alignment angle to robot +Z right-hand angle."""

    robot_angle = -float(angle_deg)
    while robot_angle >= 180.0:
        robot_angle -= 360.0
    while robot_angle < -180.0:
        robot_angle += 360.0
    return robot_angle


@dataclass
class VisionBatchResult:
    normal_dir: Path
    test_dir: Path
    output_dir: Path
    profile_path: Path
    report_path: Path
    total_count: int
    ok_count: int
    ng_count: int
    fail_count: int
    reference_count: int
    normal_count: int
    results: list[VisionDetectResult]


def require_cv() -> tuple[object, object]:
    try:
        import cv2  # type: ignore
        import numpy as np  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "Missing OpenCV/Numpy dependency. Run: python -m pip install -r pc_tools\\requirements.txt"
        ) from exc
    return cv2, np


def rgb565_bytes_to_rgb888(data: bytes, width: int, height: int):
    """Convert little-endian RGB565 bytes to an RGB888 numpy array."""

    cv2, np = require_cv()
    if width <= 0 or height <= 0:
        raise ValueError(f"invalid image size: {width}x{height}")
    expected = width * height * 2
    if len(data) < expected:
        raise ValueError(
            f"rgb565 payload too short: got {len(data)}, expected {expected}"
        )
    buf = np.frombuffer(data[:expected], dtype="<u2").astype(np.uint16)
    buf = buf.reshape((height, width))
    red5 = (buf >> 11) & 0x1F
    green6 = (buf >> 5) & 0x3F
    blue5 = buf & 0x1F
    red = (red5 << 3) | (red5 >> 2)
    green = (green6 << 2) | (green6 >> 4)
    blue = (blue5 << 3) | (blue5 >> 2)
    rgb = np.stack((red, green, blue), axis=-1).astype(np.uint8)
    return rgb


def load_profile(config_dir: Path) -> VisionProfile:
    """Load a PC vision profile from vision_profile.json and PNG assets."""

    cv2, np = require_cv()
    profile_path = config_dir / "vision_profile.json"
    if not profile_path.exists():
        raise FileNotFoundError(f"PC vision profile not found: {profile_path}")
    data = json.loads(profile_path.read_text(encoding="utf-8"))

    roi = tuple(data.get("roi", list(DEFAULT_ROI)))
    if len(roi) != 4:
        roi = DEFAULT_ROI
    work_size = tuple(data.get("work_size", list(DEFAULT_WORK_SIZE)))
    if len(work_size) != 2:
        work_size = DEFAULT_WORK_SIZE

    template_path = config_dir / data.get("files", {}).get(
        "template", "reference_template.png"
    )
    tolerance_path = config_dir / data.get("files", {}).get(
        "tolerance", "normal_tolerance.png"
    )
    mask_path = config_dir / data.get("files", {}).get("valid_mask", "valid_mask.png")

    template_bgr = cv2.imread(str(template_path), cv2.IMREAD_COLOR)
    if template_bgr is None:
        raise ValueError(f"failed to read template image: {template_path}")
    template_rgb = cv2.cvtColor(template_bgr, cv2.COLOR_BGR2RGB)
    template_gray = cv2.cvtColor(template_rgb, cv2.COLOR_RGB2GRAY)
    if (template_rgb.shape[1], template_rgb.shape[0]) != work_size:
        template_rgb = cv2.resize(template_rgb, work_size, interpolation=cv2.INTER_AREA)
        template_gray = cv2.cvtColor(template_rgb, cv2.COLOR_RGB2GRAY)

    tolerance_gray = cv2.imread(str(tolerance_path), cv2.IMREAD_GRAYSCALE)
    if tolerance_gray is None:
        # Fall back to a fully permissive tolerance map if the file is missing.
        tolerance_gray = np.full(work_size[::-1], 255, dtype=np.uint8)
    if tolerance_gray.shape[:2] != work_size[::-1]:
        tolerance_gray = cv2.resize(tolerance_gray, work_size, interpolation=cv2.INTER_AREA)

    valid_mask = cv2.imread(str(mask_path), cv2.IMREAD_GRAYSCALE)
    if valid_mask is None:
        valid_mask = np.full(work_size[::-1], 255, dtype=np.uint8)
    if valid_mask.shape[:2] != work_size[::-1]:
        valid_mask = cv2.resize(valid_mask, work_size, interpolation=cv2.INTER_NEAREST)

    detector_params = data.get("detector_params") if isinstance(data.get("detector_params"), dict) else {}
    barcode_params = {}
    if isinstance(detector_params.get("barcode_calibration"), dict):
        barcode_params = detector_params.get("barcode_calibration")  # type: ignore[assignment]

    detector_hint = str(data.get("detector_hint", "") or "")
    detector_confidence = float(data.get("detector_confidence", 0.0) or 0.0)
    detector_reason = str(data.get("detector_reason", "") or "")
    if not detector_hint:
        detector_hint, detector_confidence, detector_reason, _ = analyze_template_detector_hint(
            template_rgb, valid_mask
        )

    return VisionProfile(
        name=data.get("name", config_dir.name or "vision_profile"),
        roi=roi,  # type: ignore[arg-type]
        work_size=work_size,  # type: ignore[arg-type]
        template_rgb=template_rgb,
        template_gray=template_gray,
        tolerance_gray=tolerance_gray,
        valid_mask=valid_mask,
        diff_margin=int(data.get("diff_margin", DEFAULT_DIFF_MARGIN)),
        area_threshold=int(data.get("area_threshold", DEFAULT_AREA_THRESHOLD)),
        min_blob_area=int(data.get("min_blob_area", DEFAULT_MIN_BLOB_AREA)),
        reference_count=int(data.get("reference_count", DEFAULT_REFERENCE_COUNT)),
        normal_count=int(data.get("normal_count", 0)),
        detector=str(data.get("detector", DEFAULT_DETECTOR)),
        fallback_detector=str(data.get("fallback_detector", DEFAULT_FALLBACK_DETECTOR)),
        detector_params=detector_params,
        detector_hint=detector_hint,
        detector_confidence=detector_confidence,
        detector_reason=detector_reason,
        highlight_clip=int(data.get("highlight_clip", DEFAULT_HIGHLIGHT_CLIP)),
        feature_edge_relax=int(data.get("feature_edge_relax", DEFAULT_FEATURE_EDGE_RELAX)),
        border_erode_px=int(data.get("border_erode_px", DEFAULT_BORDER_ERODE_PX)),
        adaptive_tol_k=float(data.get("adaptive_tol_k", DEFAULT_ADAPTIVE_TOL_K)),
        align_blur_ksize=int(data.get("align_blur_ksize", DEFAULT_ALIGN_BLUR_KSIZE)),
        quality_warning=str(data.get("quality_warning", "") or ""),
        barcode_area_threshold=int(
            data.get(
                "barcode_area_threshold",
                barcode_params.get("barcode_area_threshold", DEFAULT_BARCODE_AREA_THRESHOLD),
            )
        ),
        barcode_local_area_threshold=int(
            data.get(
                "barcode_local_area_threshold",
                barcode_params.get("barcode_local_area_threshold", DEFAULT_BARCODE_LOCAL_AREA_THRESHOLD),
            )
        ),
        barcode_score_threshold=float(
            data.get(
                "barcode_score_threshold",
                barcode_params.get("barcode_score_threshold", DEFAULT_BARCODE_SCORE_THRESHOLD),
            )
        ),
        barcode_dark_ratio_threshold=float(
            data.get(
                "barcode_dark_ratio_threshold",
                barcode_params.get("barcode_dark_ratio_threshold", DEFAULT_BARCODE_DARK_RATIO_THRESHOLD),
            )
        ),
    )


def detect_image_array(profile: VisionProfile, rgb) -> VisionDetectResult:
    """Run detection on an in-memory RGB888 numpy array."""

    detector = make_detector(profile)
    return detector.detect_array(rgb, Path("<tcp-frame>"))


def iter_image_files(root: Path, recursive: bool = True) -> Iterable[Path]:
    if recursive:
        candidates = root.rglob("*")
    else:
        candidates = root.glob("*")
    for path in sorted(candidates):
        if path.is_file() and path.suffix.lower() in SUPPORTED_EXTENSIONS:
            yield path


def _rgb_image_to_array(image: object):
    _, np = require_cv()
    arr = np.frombuffer(image.pixels, dtype=np.uint8)  # type: ignore[attr-defined]
    return arr.reshape((image.height, image.width, 3)).copy()  # type: ignore[attr-defined]


def load_image_rgb(path: Path):
    cv2, np = require_cv()
    if path.suffix.lower() == ".bmp":
        image = load_rgb565_bmp(path)
        return _rgb_image_to_array(image)

    bgr = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if bgr is None:
        raise ValueError(f"failed to read image: {path}")
    return cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)


def crop_or_resize_for_work(path: Path, roi: Tuple[int, int, int, int], work_size: Tuple[int, int]):
    cv2, _ = require_cv()
    rgb = load_image_rgb(path)
    return crop_or_resize_rgb_for_work(rgb, roi, work_size)


def crop_or_resize_rgb_for_work(rgb, roi: Tuple[int, int, int, int], work_size: Tuple[int, int]):
    cv2, _ = require_cv()
    height, width = rgb.shape[:2]
    x, y, roi_w, roi_h = roi
    if width >= x + roi_w and height >= y + roi_h:
        rgb = rgb[y : y + roi_h, x : x + roi_w].copy()
    elif (width, height) != work_size:
        # Already-converted PNGs are accepted, but original BMP captures are preferred.
        rgb = cv2.resize(rgb, work_size, interpolation=cv2.INTER_AREA)

    if (rgb.shape[1], rgb.shape[0]) != work_size:
        rgb = cv2.resize(rgb, work_size, interpolation=cv2.INTER_AREA)
    return rgb


def normalize_gray(rgb):
    cv2, _ = require_cv()
    gray = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY)
    return cv2.equalizeHist(gray)


def build_valid_mask(template_rgb):
    cv2, np = require_cv()
    gray = cv2.cvtColor(template_rgb, cv2.COLOR_RGB2GRAY)
    blur = cv2.GaussianBlur(gray, (5, 5), 0)
    threshold = max(80, int(np.percentile(blur, 70)))
    mask = (blur >= threshold).astype(np.uint8) * 255
    kernel = np.ones((7, 7), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=2)
    mask = cv2.dilate(mask, np.ones((9, 9), np.uint8), iterations=1)
    return mask


def analyze_template_detector_hint(template_rgb, valid_mask=None) -> tuple[str, float, str, dict]:
    """Infer the best detector family from template geometry, not from model names."""

    cv2, np = require_cv()
    if template_rgb is None:
        return DETECTOR_HINT_UNKNOWN, 0.0, "template missing", {}

    gray = cv2.cvtColor(template_rgb, cv2.COLOR_RGB2GRAY)
    mask = valid_mask > 0 if valid_mask is not None else np.ones(gray.shape, dtype=bool)
    if not np.any(mask):
        mask = np.ones(gray.shape, dtype=bool)

    gray_f = gray.astype(np.float32)
    sobel_x = np.abs(cv2.Sobel(gray_f, cv2.CV_32F, 1, 0, ksize=3))
    sobel_y = np.abs(cv2.Sobel(gray_f, cv2.CV_32F, 0, 1, ksize=3))
    edge_x = float(sobel_x[mask].mean())
    edge_y = float(sobel_y[mask].mean())
    edge_ratio = edge_x / (edge_y + 1e-6)

    masked_gray = gray_f.copy()
    masked_gray[~mask] = float(gray_f[mask].mean())
    col_profile = masked_gray.mean(axis=0)
    row_profile = masked_gray.mean(axis=1)
    col_grad = float(np.mean(np.abs(np.diff(col_profile))))
    row_grad = float(np.mean(np.abs(np.diff(row_profile))))
    projection_ratio = col_grad / (row_grad + 1e-6)
    col_std = float(col_profile.std())

    _, binary_inv = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
    dark_ratio = float(np.count_nonzero(binary_inv & mask.astype(np.uint8)) / max(1, np.count_nonzero(mask)))
    col_dark = (binary_inv > 0).mean(axis=0)
    stripe_transition = float(np.mean(np.abs(np.diff(col_dark))))

    score_parts = [
        min(1.0, max(0.0, (edge_ratio - 1.45) / 1.2)),
        min(1.0, max(0.0, (projection_ratio - 1.25) / 1.2)),
        min(1.0, max(0.0, (col_grad - 3.0) / 5.0)),
        min(1.0, max(0.0, (stripe_transition - 0.018) / 0.035)),
    ]
    confidence = float(sum(score_parts) / len(score_parts))
    is_barcode = (
        edge_ratio >= 1.8
        and projection_ratio >= 1.45
        and col_grad >= 4.0
        and confidence >= 0.58
        and 0.08 <= dark_ratio <= 0.92
    )

    metrics = {
        "edge_x": edge_x,
        "edge_y": edge_y,
        "edge_ratio": edge_ratio,
        "col_grad": col_grad,
        "row_grad": row_grad,
        "projection_ratio": projection_ratio,
        "col_std": col_std,
        "dark_ratio": dark_ratio,
        "stripe_transition": stripe_transition,
        "confidence": confidence,
    }
    if is_barcode:
        return (
            DETECTOR_HINT_BARCODE,
            confidence,
            f"vertical stripe template edge_ratio={edge_ratio:.2f} projection_ratio={projection_ratio:.2f}",
            metrics,
        )
    return (
        DETECTOR_HINT_TEMPLATE_DIFF,
        confidence,
        f"generic template edge_ratio={edge_ratio:.2f} projection_ratio={projection_ratio:.2f}",
        metrics,
    )


def match_illumination(aligned_gray, template_gray, valid_mask):
    """Match realtime-frame brightness and contrast to the template."""






    cv2, np = require_cv()
    mask = valid_mask > 0
    if not np.any(mask):
        return aligned_gray
    rt = aligned_gray[mask].astype(np.float32)
    tpl = template_gray[mask].astype(np.float32)
    rt_mean = float(rt.mean())
    tpl_mean = float(tpl.mean())
    rt_std = float(rt.std())
    tpl_std = float(tpl.std())
    if rt_std < 1e-3:
        scale = 1.0
    else:
        scale = tpl_std / rt_std
        scale = max(0.5, min(2.0, scale))  # Clamp to avoid amplifying low-contrast noise.
    out = (aligned_gray.astype(np.float32) - rt_mean) * scale + tpl_mean
    return np.clip(out, 0, 255).astype(np.uint8)


def build_edge_relaxed_allowed(profile):
    """Build an edge-relaxed tolerance map for contour alignment residuals."""



    cv2, np = require_cv()
    tol = profile.tolerance_gray.astype(np.float32)
    # 自适应容差(E)：乘性 + 加性取大。高波动区(tolerance 大)由乘性项放宽，
    # 低波动区(tolerance 小)由加性项 diff_margin 保证仍严格，避免一刀切放水。
    k = float(getattr(profile, "adaptive_tol_k", DEFAULT_ADAPTIVE_TOL_K))
    additive = tol + float(profile.diff_margin)
    multiplicative = tol * k
    base = np.clip(np.maximum(multiplicative, additive), 0, 255).astype(np.int16)
    mask = (profile.valid_mask > 0).astype(np.uint8)
    # Edge band = mask - eroded mask.
    eroded = cv2.erode(mask, np.ones((6, 6), np.uint8), iterations=1)
    edge_band = (mask > 0) & (eroded == 0)
    relaxed = base.copy()
    relaxed[edge_band] = np.clip(relaxed[edge_band] + 60, 0, 255)
    return relaxed.astype(np.uint8)


def apply_feature_edge_relax(profile, allowed):
    """在模板内部线条/笔画边缘（梯度强处）放宽 allowed 容差，吸收轻微旋转/位移造成的对齐残差。

    feature_edge_relax<=0 时原样返回（关闭）。返回 uint8 容差图。
    """
    relax = int(getattr(profile, "feature_edge_relax", DEFAULT_FEATURE_EDGE_RELAX))
    if relax <= 0:
        return allowed
    cv2, np = require_cv()
    gx = cv2.Sobel(profile.template_gray, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(profile.template_gray, cv2.CV_32F, 0, 1, ksize=3)
    edge = (cv2.magnitude(gx, gy) > FEATURE_EDGE_GRAD_THR).astype(np.uint8)
    edge = cv2.dilate(edge, np.ones((3, 3), np.uint8), iterations=1)
    out = allowed.astype(np.int16)
    out[edge > 0] = out[edge > 0] + relax
    return np.clip(out, 0, 255).astype(np.uint8)


def build_edge_relaxed_dark_threshold(profile):
    """Build an edge-relaxed threshold map for dark-loss defects."""



    cv2, np = require_cv()
    base_thr = max(24, int(profile.diff_margin))
    thr = np.full(profile.template_gray.shape, base_thr, dtype=np.int16)
    mask = (profile.valid_mask > 0).astype(np.uint8)
    eroded = cv2.erode(mask, np.ones((6, 6), np.uint8), iterations=1)
    edge_band = (mask > 0) & (eroded == 0)
    thr[edge_band] = np.clip(thr[edge_band] + 60, 0, 255)
    return thr.astype(np.uint8)


class VisionDetector:
    """Base class for PC vision detectors."""

    name = "base"

    def __init__(self, profile: VisionProfile) -> None:
        self.profile = profile

    def detect_array(self, rgb, path: Path) -> VisionDetectResult:
        raise NotImplementedError

    def detect_path(self, image_path: Path) -> VisionDetectResult:
        return self.detect_array(load_image_rgb(image_path), image_path)


class TemplateDiffDetector(VisionDetector):
    """Existing template-difference detector, kept as the compatibility baseline."""

    name = "template_diff"

    def detect_array(self, rgb, path: Path) -> VisionDetectResult:
        cv2, np = require_cv()

        if rgb is None or rgb.size == 0:
            raise ValueError("detect_image_array: empty rgb input")
        if rgb.ndim != 3 or rgb.shape[2] != 3:
            raise ValueError(f"detect_image_array: expected (H,W,3), got {rgb.shape}")

        work = crop_or_resize_rgb_for_work(rgb, self.profile.roi, self.profile.work_size)
        gray = normalize_gray(work)
        aligned_rgb, aligned_gray, warp, dx, dy, angle = align_to_reference(
            self.profile.template_gray, gray, work,
            blur_ksize=int(getattr(self.profile, "align_blur_ksize", DEFAULT_ALIGN_BLUR_KSIZE)),
        )
        align_ncc = float(getattr(align_to_reference, "last_ncc", 0.0))
        aligned_gray = match_illumination(aligned_gray, self.profile.template_gray, self.profile.valid_mask)

        # 高光钳制：把模板与实时灰度都压到 highlight_clip 以下，消除白色/镜面高光的逐帧漂移
        # （实测占误判面积约 87%）。clip>=255 时为恒等操作（关闭钳制）。
        clip = int(getattr(self.profile, "highlight_clip", DEFAULT_HIGHLIGHT_CLIP))
        template_gray = np.minimum(self.profile.template_gray, clip)
        aligned_gray = np.minimum(aligned_gray, clip)

        diff = cv2.absdiff(aligned_gray, template_gray)
        allowed = build_edge_relaxed_allowed(self.profile)
        allowed = apply_feature_edge_relax(self.profile, allowed)
        dark_loss = np.clip(
            template_gray.astype(np.int16) - aligned_gray.astype(np.int16), 0, 255
        ).astype(np.uint8)
        dark_thr = build_edge_relaxed_dark_threshold(self.profile)
        defect = ((diff > allowed) | (dark_loss > dark_thr)).astype(np.uint8) * 255
        # 边缘带腐蚀(A)：圆形/弱纹理 logo 的对齐残差最集中在物体最外圈，本就不是检测目标。
        # 把 valid_mask 向内腐蚀 border_erode_px 再做 AND，剔除最外圈对齐残差，
        # 同时 logo 内部的真实缺陷(在腐蚀后区域内)仍完整保留。
        erode_px = int(getattr(self.profile, "border_erode_px", DEFAULT_BORDER_ERODE_PX))
        if erode_px > 0:
            ek = 2 * erode_px + 1
            defect_mask = cv2.erode(
                self.profile.valid_mask, np.ones((ek, ek), np.uint8), iterations=1
            )
        else:
            defect_mask = self.profile.valid_mask
        defect = cv2.bitwise_and(defect, defect_mask)

        kernel = np.ones((3, 3), np.uint8)
        defect = cv2.morphologyEx(defect, cv2.MORPH_OPEN, kernel, iterations=1)
        defect = cv2.morphologyEx(defect, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8), iterations=1)

        num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(defect, 8)
        clean = np.zeros_like(defect)
        boxes = []
        kept_components = 0
        max_blob_area = 0
        for label in range(1, num_labels):
            area = int(stats[label, cv2.CC_STAT_AREA])
            if area < self.profile.min_blob_area:
                continue
            bx = int(stats[label, cv2.CC_STAT_LEFT])
            by = int(stats[label, cv2.CC_STAT_TOP])
            bw = int(stats[label, cv2.CC_STAT_WIDTH])
            bh = int(stats[label, cv2.CC_STAT_HEIGHT])
            clean[labels == label] = 255
            boxes.append((bx, by, bw, bh))
            kept_components += 1
            max_blob_area = max(max_blob_area, area)

        defect_area = int(np.count_nonzero(clean))
        max_diff = (
            int(diff[self.profile.valid_mask > 0].max())
            if np.any(self.profile.valid_mask > 0)
            else int(diff.max())
        )
        mean_diff = (
            float(diff[self.profile.valid_mask > 0].mean())
            if np.any(self.profile.valid_mask > 0)
            else float(diff.mean())
        )
        score = float(defect_area)
        ok = defect_area < self.profile.area_threshold

        bbox_aligned = None
        if boxes:
            x0 = min(box[0] for box in boxes)
            y0 = min(box[1] for box in boxes)
            x1 = max(box[0] + box[2] for box in boxes)
            y1 = max(box[1] + box[3] for box in boxes)
            bbox_aligned = (x0, y0, x1 - x0, y1 - y0)

        bbox_raw = (
            transform_bbox_to_raw(bbox_aligned, warp, (work.shape[0], work.shape[1]))
            if bbox_aligned
            else None
        )
        mask_raw = transform_mask_to_raw(clean, warp, (work.shape[0], work.shape[1]))
        robot_angle = normalize_robot_angle_deg(angle)
        reason = "area below threshold" if ok else "defect area exceeds threshold"
        metrics = {
            "detector": self.name,
            "score": score,
            "defect_area": defect_area,
            "area_threshold": int(self.profile.area_threshold),
            "max_diff": max_diff,
            "mean_diff": mean_diff,
            "component_count": kept_components,
            "max_blob_area": max_blob_area,
            "angle_deg": angle,
            "robot_angle_deg": robot_angle,
            "dx": dx,
            "dy": dy,
            "align_ncc": round(align_ncc, 4),
        }
        debug_artifacts = {
            "work_rgb": work,
            "aligned_rgb": aligned_rgb,
            "aligned_gray": aligned_gray,
            "diff": diff,
            "allowed": allowed,
            "defect_raw": defect,
            "defect_clean": clean,
        }
        return VisionDetectResult(
            path=path,
        ok=ok,
        score=score,
        defect_area=defect_area,
        max_diff=max_diff,
        mean_diff=mean_diff,
        angle_deg=angle,
        robot_angle_deg=robot_angle,
        dx=dx,
        dy=dy,
        bbox_raw=bbox_raw,
        bbox_aligned=bbox_aligned,
        mask_raw=mask_raw,
        mask_aligned=clean,
            detector=self.name,
            reason=reason,
            metrics=metrics,
            debug_artifacts=debug_artifacts,
        )


class BarcodeDetector(VisionDetector):
    """Barcode-oriented detector that checks stripe structure instead of raw pixel equality."""

    name = DETECTOR_BARCODE

    def __init__(self, profile: VisionProfile) -> None:
        super().__init__(profile)
        self.params = profile.detector_params or {}
        self.template_features = self._extract_features(profile.template_rgb, profile.valid_mask)

    def _extract_features(self, rgb, valid_mask=None) -> dict:
        cv2, np = require_cv()
        gray = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY)
        mask = self._find_label_mask(gray, valid_mask)
        barcode_mask = self._find_barcode_mask(gray, mask)
        binary = self._binary_barcode(gray, mask)
        col_profile = self._column_profile(binary, barcode_mask)
        row_profile = self._row_profile(binary, barcode_mask)
        bbox = mask_to_bbox(barcode_mask)
        return {
            "gray": gray,
            "label_mask": mask,
            "barcode_mask": barcode_mask,
            "binary": binary,
            "col_profile": col_profile,
            "row_profile": row_profile,
            "bbox": bbox,
        }

    def _find_label_mask(self, gray, valid_mask=None):
        cv2, np = require_cv()
        if valid_mask is not None and np.count_nonzero(valid_mask) > 0:
            base = (valid_mask > 0).astype(np.uint8) * 255
        else:
            blur = cv2.GaussianBlur(gray, (5, 5), 0)
            _, base = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
        kernel = np.ones((9, 9), np.uint8)
        base = cv2.morphologyEx(base.astype(np.uint8), cv2.MORPH_CLOSE, kernel, iterations=2)
        base = cv2.morphologyEx(base, cv2.MORPH_OPEN, np.ones((5, 5), np.uint8), iterations=1)
        num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(base, 8)
        if num_labels <= 1:
            return np.full(gray.shape, 255, dtype=np.uint8)
        best = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
        mask = np.zeros_like(base)
        mask[labels == best] = 255
        mask = cv2.dilate(mask, np.ones((5, 5), np.uint8), iterations=1)
        return mask

    def _find_barcode_mask(self, gray, label_mask):
        cv2, np = require_cv()
        gx = np.abs(cv2.Sobel(gray.astype(np.float32), cv2.CV_32F, 1, 0, ksize=3))
        gy = np.abs(cv2.Sobel(gray.astype(np.float32), cv2.CV_32F, 0, 1, ksize=3))
        vertical_edges = np.clip(gx - 0.55 * gy, 0, 255).astype(np.uint8)
        vertical_edges = cv2.bitwise_and(vertical_edges, label_mask)
        if np.count_nonzero(vertical_edges) == 0:
            return label_mask.copy()

        _, edge_mask = cv2.threshold(vertical_edges, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
        edge_mask = cv2.morphologyEx(edge_mask, cv2.MORPH_CLOSE, np.ones((5, 13), np.uint8), iterations=2)
        edge_mask = cv2.dilate(edge_mask, np.ones((7, 7), np.uint8), iterations=1)
        edge_mask = cv2.bitwise_and(edge_mask, label_mask)

        num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(edge_mask, 8)
        if num_labels <= 1:
            return label_mask.copy()
        candidates = []
        height, width = gray.shape[:2]
        for label in range(1, num_labels):
            area = int(stats[label, cv2.CC_STAT_AREA])
            w = int(stats[label, cv2.CC_STAT_WIDTH])
            h = int(stats[label, cv2.CC_STAT_HEIGHT])
            if w < width * 0.12 or h < height * 0.18:
                continue
            candidates.append((area, label))
        if not candidates:
            return label_mask.copy()
        _, best = max(candidates)
        mask = np.zeros_like(edge_mask)
        mask[labels == best] = 255
        mask = cv2.dilate(mask, np.ones((7, 7), np.uint8), iterations=1)
        return cv2.bitwise_and(mask, label_mask)

    def _binary_barcode(self, gray, label_mask):
        cv2, np = require_cv()
        blur = cv2.GaussianBlur(gray, (3, 3), 0)
        try:
            binary = cv2.adaptiveThreshold(
                blur,
                255,
                cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
                cv2.THRESH_BINARY_INV,
                31,
                5,
            )
        except cv2.error:
            _, binary = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
        binary = cv2.bitwise_and(binary, label_mask)
        binary = cv2.morphologyEx(binary, cv2.MORPH_OPEN, np.ones((2, 2), np.uint8), iterations=1)
        return binary

    def _column_profile(self, binary, mask):
        _, np = require_cv()
        active = mask > 0
        profile = np.zeros(binary.shape[1], dtype=np.float32)
        for x in range(binary.shape[1]):
            col_mask = active[:, x]
            if not np.any(col_mask):
                continue
            profile[x] = float(np.count_nonzero(binary[:, x][col_mask])) / float(np.count_nonzero(col_mask))
        return profile

    def _row_profile(self, binary, mask):
        _, np = require_cv()
        active = mask > 0
        profile = np.zeros(binary.shape[0], dtype=np.float32)
        for y in range(binary.shape[0]):
            row_mask = active[y, :]
            if not np.any(row_mask):
                continue
            profile[y] = float(np.count_nonzero(binary[y, :][row_mask])) / float(np.count_nonzero(row_mask))
        return profile

    def _smooth_profile(self, profile, window: int = 5):
        _, np = require_cv()
        values = np.asarray(profile, dtype=np.float32)
        if values.size < 3:
            return values
        window = max(3, int(window) | 1)
        kernel = np.ones(window, dtype=np.float32) / float(window)
        return np.convolve(values, kernel, mode="same").astype(np.float32)

    def _shift_profile(self, profile, shift: int):
        _, np = require_cv()
        values = np.asarray(profile, dtype=np.float32)
        shifted = np.zeros_like(values)
        valid = np.zeros(values.shape, dtype=bool)
        if shift > 0:
            shifted[shift:] = values[:-shift]
            valid[shift:] = True
        elif shift < 0:
            shifted[:shift] = values[-shift:]
            valid[:shift] = True
        else:
            shifted[:] = values
            valid[:] = True
        return shifted, valid

    def _shift_mask_x(self, mask, shift: int):
        _, np = require_cv()
        shifted = np.zeros_like(mask)
        if shift > 0:
            shifted[:, shift:] = mask[:, :-shift]
        elif shift < 0:
            shifted[:, :shift] = mask[:, -shift:]
        else:
            shifted[:] = mask
        return shifted

    def _best_shifted_profile_delta(self, current, template, active, max_shift: int = 24):
        _, np = require_cv()
        current_s = self._smooth_profile(current, 5)
        template_s = self._smooth_profile(template, 5)
        best = None
        for shift in range(-max_shift, max_shift + 1):
            shifted, valid = self._shift_profile(template_s, shift)
            eval_mask = active & valid
            if np.count_nonzero(eval_mask) < 8:
                continue
            delta = np.abs(current_s - shifted)
            mean_delta = float(delta[eval_mask].mean())
            p90_delta = float(np.percentile(delta[eval_mask], 90))
            score = mean_delta + p90_delta * 0.20
            if best is None or score < best[0]:
                best = (score, shift, shifted, valid, delta, mean_delta, p90_delta)
        if best is None:
            shifted, valid = self._shift_profile(template_s, 0)
            delta = np.abs(current_s - shifted)
            eval_mask = active & valid
            return 0, shifted, valid, delta, float(delta.mean()), float(delta.max())
        _, shift, shifted, valid, delta, mean_delta, p90_delta = best
        return shift, shifted, valid, delta, mean_delta, p90_delta

    def estimate_calibration(self, image_paths: Sequence[Path], progress_callback: Optional[Callable[[str], None]] = None) -> dict:
        """Estimate barcode-specific thresholds from normal samples."""

        cv2, np = require_cv()
        defect_areas: list[int] = []
        local_defect_areas: list[int] = []
        scores: list[float] = []
        dark_deltas: list[float] = []

        total = len(image_paths)
        for index, path in enumerate(image_paths, start=1):
            try:
                result = self.detect_path(path)
            except Exception:
                continue
            if result.error:
                continue
            defect_areas.append(int(result.defect_area))
            if result.metrics:
                local_defect_areas.append(int(result.metrics.get("local_defect_area", 0) or 0))
                scores.append(float(result.metrics.get("score", result.score) or result.score))
                dark_deltas.append(float(result.metrics.get("dark_ratio_delta", 0.0) or 0.0))
            if progress_callback and (index == 1 or index == total or index % 50 == 0):
                progress_callback(f"barcode calibrate {index}/{total}")

        if not defect_areas:
            return {}

        defect_arr = np.array(defect_areas, dtype=np.float64)
        local_arr = np.array(local_defect_areas or [0], dtype=np.float64)
        score_arr = np.array(scores or [0.0], dtype=np.float64)
        dark_arr = np.array(dark_deltas or [0.0], dtype=np.float64)

        p99_defect = float(np.percentile(defect_arr, 99))
        max_defect = int(defect_arr.max())
        p99_local = float(np.percentile(local_arr, 99))
        max_local = int(local_arr.max())
        p99_score = float(np.percentile(score_arr, 99))
        max_score = float(score_arr.max())
        p99_dark = float(np.percentile(dark_arr, 99))
        max_dark = float(dark_arr.max())

        area_threshold = max(int(DEFAULT_BARCODE_AREA_THRESHOLD), int(round(max(p99_defect * 2.4, max_defect * 1.25))))
        local_area_threshold = max(int(DEFAULT_BARCODE_LOCAL_AREA_THRESHOLD), int(round(max(p99_local * 1.8, max_local * 1.2))))
        score_threshold = max(float(DEFAULT_BARCODE_SCORE_THRESHOLD), float(round(max(p99_score * 1.2, max_score * 1.08), 1)))
        dark_ratio_threshold = max(float(DEFAULT_BARCODE_DARK_RATIO_THRESHOLD), float(round(max(p99_dark * 1.35, max_dark * 1.12), 4)))

        return {
            "barcode_area_threshold": int(area_threshold),
            "barcode_local_area_threshold": int(local_area_threshold),
            "barcode_score_threshold": float(score_threshold),
            "barcode_dark_ratio_threshold": float(dark_ratio_threshold),
            "barcode_calibration": {
                "normal_mean_defect_area": round(float(defect_arr.mean()), 1),
                "normal_p99_defect_area": round(p99_defect, 1),
                "normal_max_defect_area": max_defect,
                "normal_p99_local_defect_area": round(p99_local, 1),
                "normal_max_local_defect_area": max_local,
                "normal_p99_score": round(p99_score, 1),
                "normal_max_score": round(max_score, 1),
                "normal_p99_dark_ratio_delta": round(p99_dark, 4),
                "normal_max_dark_ratio_delta": round(max_dark, 4),
                "barcode_area_threshold": int(area_threshold),
                "barcode_local_area_threshold": int(local_area_threshold),
                "barcode_score_threshold": float(score_threshold),
                "barcode_dark_ratio_threshold": float(dark_ratio_threshold),
            },
        }

    def detect_array(self, rgb, path: Path) -> VisionDetectResult:
        cv2, np = require_cv()

        if rgb is None or rgb.size == 0:
            raise ValueError("detect_image_array: empty rgb input")
        if rgb.ndim != 3 or rgb.shape[2] != 3:
            raise ValueError(f"detect_image_array: expected (H,W,3), got {rgb.shape}")

        work = crop_or_resize_rgb_for_work(rgb, self.profile.roi, self.profile.work_size)
        gray = normalize_gray(work)
        # barcode 路径保持旧的纯 ECC 对齐行为(无模糊/无粗估)，不受 icon 稳健化改动影响。
        aligned_rgb, aligned_gray, warp, dx, dy, angle = align_to_reference(
            self.profile.template_gray, gray, work, blur_ksize=0, coarse_init=False
        )
        aligned_gray = match_illumination(aligned_gray, self.profile.template_gray, self.profile.valid_mask)
        aligned_rgb = cv2.cvtColor(aligned_gray, cv2.COLOR_GRAY2RGB)

        label_mask = self._find_label_mask(aligned_gray, self.profile.valid_mask)
        barcode_mask = self._find_barcode_mask(aligned_gray, label_mask)
        binary = self._binary_barcode(aligned_gray, label_mask)
        col_profile = self._column_profile(binary, barcode_mask)
        row_profile = self._row_profile(binary, barcode_mask)

        tpl_col = self.template_features["col_profile"]
        tpl_row = self.template_features["row_profile"]
        tpl_mask = self.template_features["barcode_mask"]
        col_active_seed = (barcode_mask > 0).any(axis=0) | (tpl_mask > 0).any(axis=0)
        profile_shift, shifted_tpl_col, col_valid, col_delta, mean_col_delta, p90_col_delta = (
            self._best_shifted_profile_delta(col_profile, tpl_col, col_active_seed)
        )
        shifted_tpl_mask = self._shift_mask_x(tpl_mask, profile_shift)
        common_mask = cv2.bitwise_and(label_mask, shifted_tpl_mask)
        if np.count_nonzero(common_mask) < max(80, int(0.2 * np.count_nonzero(tpl_mask))):
            common_mask = shifted_tpl_mask if np.count_nonzero(shifted_tpl_mask) else self.profile.valid_mask

        row_delta = np.abs(self._smooth_profile(row_profile, 5) - self._smooth_profile(tpl_row, 5))
        col_active = ((common_mask > 0).any(axis=0) | col_active_seed) & col_valid
        row_active = (common_mask > 0).any(axis=1) | (tpl_mask > 0).any(axis=1)
        max_col_delta = float(col_delta[col_active].max()) if np.any(col_active) else float(col_delta.max())
        mean_row_delta = float(row_delta[row_active].mean()) if np.any(row_active) else float(row_delta.mean())

        defect = np.zeros_like(binary)
        if np.any(col_active):
            bad_cols = col_active & (col_delta > 0.58)
            defect[:, bad_cols] = 255
        defect = cv2.bitwise_and(defect, common_mask)

        shifted_tpl_binary = self._shift_mask_x(self.template_features["binary"], profile_shift)
        local_kernel = np.ones((3, 3), np.uint8)
        current_relaxed = cv2.dilate(binary, local_kernel, iterations=1)
        template_relaxed = cv2.dilate(shifted_tpl_binary, local_kernel, iterations=1)
        missing_dark = cv2.bitwise_and(shifted_tpl_binary, cv2.bitwise_not(current_relaxed))
        extra_dark = cv2.bitwise_and(binary, cv2.bitwise_not(template_relaxed))
        local_defect = cv2.bitwise_or(missing_dark, extra_dark)
        local_defect = cv2.bitwise_and(local_defect, common_mask)
        local_defect = cv2.morphologyEx(local_defect, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8), iterations=1)
        local_defect = cv2.morphologyEx(local_defect, cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8), iterations=1)
        local_defect = remove_small_components(local_defect, max(80, self.profile.min_blob_area * 3))
        defect = cv2.bitwise_or(defect, local_defect)

        tpl_dark_ratio = float(np.count_nonzero(shifted_tpl_binary & common_mask) / max(1, np.count_nonzero(common_mask)))
        cur_dark_ratio = float(np.count_nonzero(binary & common_mask) / max(1, np.count_nonzero(common_mask)))
        dark_ratio_delta = abs(cur_dark_ratio - tpl_dark_ratio)
        local_defect_area = int(np.count_nonzero(local_defect))

        defect = cv2.morphologyEx(defect, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8), iterations=1)
        defect = cv2.morphologyEx(defect, cv2.MORPH_CLOSE, np.ones((7, 5), np.uint8), iterations=1)
        defect = remove_small_components(defect, max(35, self.profile.min_blob_area))
        defect_area = int(np.count_nonzero(defect))

        bbox_aligned = mask_to_bbox(defect)
        bbox_raw = (
            transform_bbox_to_raw(bbox_aligned, warp, (work.shape[0], work.shape[1]))
            if bbox_aligned
            else None
        )
        mask_raw = transform_mask_to_raw(defect, warp, (work.shape[0], work.shape[1]))
        robot_angle = normalize_robot_angle_deg(angle)

        structural_score = (
            mean_col_delta * 500.0
            + p90_col_delta * 180.0
            + mean_row_delta * 160.0
            + dark_ratio_delta * 450.0
            + max(0.0, max_col_delta - 0.82) * 260.0
            + defect_area * 0.12
            + local_defect_area * 0.05
        )
        area_threshold = int(
            self.params.get(
                "barcode_area_threshold",
                getattr(self.profile, "barcode_area_threshold", DEFAULT_BARCODE_AREA_THRESHOLD),
            )
        )
        local_area_threshold = int(
            self.params.get(
                "barcode_local_area_threshold",
                getattr(self.profile, "barcode_local_area_threshold", DEFAULT_BARCODE_LOCAL_AREA_THRESHOLD),
            )
        )
        score_threshold = float(
            self.params.get(
                "barcode_score_threshold",
                getattr(self.profile, "barcode_score_threshold", DEFAULT_BARCODE_SCORE_THRESHOLD),
            )
        )
        dark_ratio_threshold = float(
            self.params.get(
                "barcode_dark_ratio_threshold",
                getattr(self.profile, "barcode_dark_ratio_threshold", DEFAULT_BARCODE_DARK_RATIO_THRESHOLD),
            )
        )
        ok = (
            defect_area < area_threshold
            and local_defect_area < local_area_threshold
            and structural_score < score_threshold
            and dark_ratio_delta < dark_ratio_threshold
        )
        reason = "barcode structure within tolerance" if ok else "barcode structure differs from template"

        diff = cv2.absdiff(aligned_gray, self.profile.template_gray)
        max_diff = int(diff[common_mask > 0].max()) if np.any(common_mask > 0) else int(diff.max())
        mean_diff = float(diff[common_mask > 0].mean()) if np.any(common_mask > 0) else float(diff.mean())
        num_labels, _, stats, _ = cv2.connectedComponentsWithStats(defect, 8)
        max_blob_area = int(stats[1:, cv2.CC_STAT_AREA].max()) if num_labels > 1 else 0

        metrics = {
            "detector": self.name,
            "score": float(structural_score),
            "defect_area": defect_area,
            "area_threshold": area_threshold,
            "local_area_threshold": local_area_threshold,
            "score_threshold": score_threshold,
            "dark_ratio_threshold": dark_ratio_threshold,
            "component_count": max(0, num_labels - 1),
            "max_blob_area": max_blob_area,
            "max_diff": max_diff,
            "mean_diff": mean_diff,
            "angle_deg": angle,
            "robot_angle_deg": robot_angle,
            "dx": dx,
            "dy": dy,
            "mean_col_delta": mean_col_delta,
            "p90_col_delta": p90_col_delta,
            "max_col_delta": max_col_delta,
            "mean_row_delta": mean_row_delta,
            "template_dark_ratio": tpl_dark_ratio,
            "current_dark_ratio": cur_dark_ratio,
            "dark_ratio_delta": dark_ratio_delta,
            "local_defect_area": local_defect_area,
            "profile_shift": profile_shift,
            "hint": self.profile.detector_hint,
            "hint_confidence": self.profile.detector_confidence,
        }
        debug_artifacts = {
            "work_rgb": work,
            "aligned_rgb": aligned_rgb,
            "aligned_gray": aligned_gray,
            "label_mask": label_mask,
            "barcode_mask": barcode_mask,
            "binary": binary,
            "template_binary": self.template_features["binary"],
            "local_defect": local_defect,
            "projection_delta": profile_to_debug_image(col_delta, binary.shape[0]),
            "defect_clean": defect,
        }
        return VisionDetectResult(
            path=path,
            ok=ok,
            score=float(structural_score),
            defect_area=defect_area,
            max_diff=max_diff,
            mean_diff=mean_diff,
            angle_deg=angle,
            robot_angle_deg=robot_angle,
            dx=dx,
            dy=dy,
            bbox_raw=bbox_raw,
            bbox_aligned=bbox_aligned,
            mask_raw=mask_raw,
            mask_aligned=defect,
            detector=self.name,
            reason=reason,
            metrics=metrics,
            debug_artifacts=debug_artifacts,
        )


def make_detector(profile: VisionProfile) -> VisionDetector:
    detector_name = (profile.detector or DEFAULT_DETECTOR).strip().lower()
    if detector_name in ("auto", "ensemble"):
        hint = (profile.detector_hint or DETECTOR_HINT_UNKNOWN).strip().lower()
        if hint == DETECTOR_HINT_BARCODE:
            return BarcodeDetector(profile)
        return TemplateDiffDetector(profile)
    if detector_name in ("barcode", "bar_code"):
        return BarcodeDetector(profile)
    if detector_name in ("template", "template_diff"):
        return TemplateDiffDetector(profile)
    if profile.fallback_detector and profile.fallback_detector != detector_name:
        fallback = VisionProfile(
            name=profile.name,
            roi=profile.roi,
            work_size=profile.work_size,
            template_rgb=profile.template_rgb,
            template_gray=profile.template_gray,
            tolerance_gray=profile.tolerance_gray,
            valid_mask=profile.valid_mask,
            diff_margin=profile.diff_margin,
            area_threshold=profile.area_threshold,
            min_blob_area=profile.min_blob_area,
            reference_count=profile.reference_count,
            normal_count=profile.normal_count,
            detector=profile.fallback_detector,
            fallback_detector=DEFAULT_FALLBACK_DETECTOR,
            detector_params=profile.detector_params,
            detector_hint=profile.detector_hint,
            detector_confidence=profile.detector_confidence,
            detector_reason=profile.detector_reason,
            highlight_clip=profile.highlight_clip,
            feature_edge_relax=profile.feature_edge_relax,
            border_erode_px=profile.border_erode_px,
            adaptive_tol_k=profile.adaptive_tol_k,
            align_blur_ksize=profile.align_blur_ksize,
            quality_warning=profile.quality_warning,
            barcode_area_threshold=profile.barcode_area_threshold,
            barcode_local_area_threshold=profile.barcode_local_area_threshold,
            barcode_score_threshold=profile.barcode_score_threshold,
            barcode_dark_ratio_threshold=profile.barcode_dark_ratio_threshold,
        )
        return make_detector(fallback)
    raise ValueError(f"unsupported detector: {profile.detector}")


def _masked_ncc(a, b, mask=None):
    """归一化互相关 (NCC)，可选只在 mask 内计算。a/b 同形 uint8/float。"""
    _, np = require_cv()
    af = a.astype(np.float32)
    bf = b.astype(np.float32)
    if mask is not None:
        sel = mask > 0
        if not np.any(sel):
            return 0.0
        af = af[sel]
        bf = bf[sel]
    else:
        af = af.ravel()
        bf = bf.ravel()
    af = af - af.mean()
    bf = bf - bf.mean()
    denom = float(np.sqrt((af * af).sum()) * np.sqrt((bf * bf).sum()))
    if denom < 1e-6:
        return 0.0
    return float((af * bf).sum() / denom)


def _estimate_coarse_warp(reference_gray, moving_gray):
    """用 ORB+RANSAC 粗估 旋转/平移 作为 ECC 的初值。

    弱纹理圆形 logo 上 ECC 从单位阵起步常收敛到错误的局部最优，给它一个接近真值的
    旋转/平移初值能显著提升旋转配准成功率。失败时返回 None。
    返回 2x3 仿射(限定为相似变换的旋转+平移部分)。
    """
    cv2, np = require_cv()
    try:
        orb = cv2.ORB_create(nfeatures=800)
        kp1, des1 = orb.detectAndCompute(reference_gray, None)
        kp2, des2 = orb.detectAndCompute(moving_gray, None)
        if des1 is None or des2 is None or len(kp1) < 8 or len(kp2) < 8:
            return None
        matcher = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=True)
        matches = matcher.match(des1, des2)
        if len(matches) < 8:
            return None
        matches = sorted(matches, key=lambda m: m.distance)[:120]
        # moving -> reference 方向（与 warpAffine WARP_INVERSE_MAP 约定一致）。
        src = np.float32([kp2[m.trainIdx].pt for m in matches]).reshape(-1, 1, 2)
        dst = np.float32([kp1[m.queryIdx].pt for m in matches]).reshape(-1, 1, 2)
        mat, inliers = cv2.estimateAffinePartial2D(
            src, dst, method=cv2.RANSAC, ransacReprojThreshold=3.0
        )
        if mat is None or inliers is None or int(inliers.sum()) < 6:
            return None
        # 只取旋转+平移，丢弃尺度(检测场景尺度近似固定)，避免尺度误估放大残差。
        a, b = float(mat[0, 0]), float(mat[0, 1])
        scale = math.hypot(a, b)
        if scale < 1e-3 or scale > 3.0:
            return None
        ang = math.atan2(float(mat[1, 0]), float(mat[0, 0]))
        if abs(math.degrees(ang)) > 60.0:
            return None
        cos_a, sin_a = math.cos(ang), math.sin(ang)
        warp = np.array(
            [[cos_a, -sin_a, float(mat[0, 2])], [sin_a, cos_a, float(mat[1, 2])]],
            dtype=np.float32,
        )
        return warp
    except cv2.error:
        return None


def align_to_reference(
    reference_gray,
    moving_gray,
    moving_rgb,
    blur_ksize: int = DEFAULT_ALIGN_BLUR_KSIZE,
    coarse_init: bool = True,
):
    cv2, np = require_cv()
    height, width = reference_gray.shape[:2]

    # 对齐估计专用灰度：轻度高斯模糊，压住 equalizeHist 在弱纹理上放大的噪声，
    # 让 ECC 的梯度更干净（差分仍用未模糊的 moving_gray，灵敏度不受影响）。
    k = int(blur_ksize)
    if k >= 3 and k % 2 == 1:
        ref_est = cv2.GaussianBlur(reference_gray, (k, k), 0)
        mov_est = cv2.GaussianBlur(moving_gray, (k, k), 0)
    else:
        ref_est, mov_est = reference_gray, moving_gray

    identity = np.eye(2, 3, dtype=np.float32)
    # 旋转初值：ORB+RANSAC 粗估，失败回退单位阵。coarse_init=False 时退化为旧的纯 ECC。
    coarse = _estimate_coarse_warp(ref_est, mov_est) if coarse_init else None
    init_warp = coarse.copy() if coarse is not None else identity.copy()

    # 无粗估路径(如 barcode)沿用旧的 ECC 收敛准则，保证旧行为不被带偏；
    # 旋转稳健化路径用更紧的准则以充分精修旋转。
    if coarse_init:
        criteria = (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 100, 1e-6)
    else:
        criteria = (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 80, 1e-5)
    ref_f = ref_est.astype(np.float32) / 255.0
    mov_f = mov_est.astype(np.float32) / 255.0

    def _refine(seed):
        w = seed.copy()
        _, w = cv2.findTransformECC(ref_f, mov_f, w, cv2.MOTION_EUCLIDEAN, criteria, None, 3)
        return w

    candidates = []  # (ncc, warp)
    for seed in ([init_warp, identity] if coarse is not None else [identity]):
        try:
            w = _refine(seed)
            aligned_est = cv2.warpAffine(
                mov_est, w, (width, height),
                flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP,
                borderMode=cv2.BORDER_REPLICATE,
            )
            candidates.append((_masked_ncc(aligned_est, ref_est), w))
        except cv2.error:
            continue
    # 也把粗估(未精修)作为兜底候选，防止 ECC 在旋转上发散得比初值还差。
    if coarse is not None:
        aligned_coarse = cv2.warpAffine(
            mov_est, init_warp, (width, height),
            flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP,
            borderMode=cv2.BORDER_REPLICATE,
        )
        candidates.append((_masked_ncc(aligned_coarse, ref_est), init_warp))

    if candidates:
        ncc, warp = max(candidates, key=lambda c: c[0])
    else:
        warp, ncc = identity, _masked_ncc(mov_est, ref_est)

    aligned_rgb = cv2.warpAffine(
        moving_rgb,
        warp,
        (width, height),
        flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP,
        borderMode=cv2.BORDER_REPLICATE,
    )
    aligned_gray = normalize_gray(aligned_rgb)
    dx = float(warp[0, 2])
    dy = float(warp[1, 2])
    angle = math.degrees(math.atan2(float(warp[1, 0]), float(warp[0, 0])))
    align_to_reference.last_ncc = float(ncc)
    return aligned_rgb, aligned_gray, warp, dx, dy, angle


def transform_bbox_to_raw(bbox: Tuple[int, int, int, int], warp, raw_shape: tuple[int, int]):
    _, np = require_cv()
    x, y, width, height = bbox
    points = np.array(
        [
            [x, y, 1.0],
            [x + width, y, 1.0],
            [x, y + height, 1.0],
            [x + width, y + height, 1.0],
        ],
        dtype=np.float32,
    )
    mapped = points @ warp.T
    max_h, max_w = raw_shape
    x0 = max(0, min(max_w - 1, int(np.floor(mapped[:, 0].min()))))
    y0 = max(0, min(max_h - 1, int(np.floor(mapped[:, 1].min()))))
    x1 = max(0, min(max_w - 1, int(np.ceil(mapped[:, 0].max()))))
    y1 = max(0, min(max_h - 1, int(np.ceil(mapped[:, 1].max()))))
    if x1 <= x0 or y1 <= y0:
        return None
    return (x0, y0, x1 - x0, y1 - y0)

def transform_mask_to_raw(mask, warp, raw_shape: tuple[int, int]):
    cv2, np = require_cv()
    if mask is None:
        return None
    raw_h, raw_w = raw_shape
    if raw_h <= 0 or raw_w <= 0:
        return None
    return cv2.warpAffine(
        mask.astype(np.uint8),
        warp,
        (raw_w, raw_h),
        flags=cv2.INTER_NEAREST,
        borderMode=cv2.BORDER_CONSTANT,
        borderValue=0,
    )


def mask_to_bbox(mask) -> Optional[Tuple[int, int, int, int]]:
    _, np = require_cv()
    if mask is None or np.count_nonzero(mask) == 0:
        return None
    ys, xs = np.where(mask > 0)
    x0 = int(xs.min())
    y0 = int(ys.min())
    x1 = int(xs.max()) + 1
    y1 = int(ys.max()) + 1
    return (x0, y0, x1 - x0, y1 - y0)


def remove_small_components(mask, min_area: int):
    cv2, np = require_cv()
    if mask is None or np.count_nonzero(mask) == 0:
        return mask
    num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(mask.astype(np.uint8), 8)
    clean = np.zeros_like(mask, dtype=np.uint8)
    for label in range(1, num_labels):
        area = int(stats[label, cv2.CC_STAT_AREA])
        if area >= min_area:
            clean[labels == label] = 255
    return clean


def profile_to_debug_image(profile, height: int):
    _, np = require_cv()
    values = np.asarray(profile, dtype=np.float32)
    if values.size == 0:
        return np.zeros((max(1, height), 1), dtype=np.uint8)
    normalized = np.clip(values, 0.0, 1.0)
    image = np.tile((normalized * 255.0).astype(np.uint8)[None, :], (max(1, height), 1))
    return image


def make_overlay(rgb, mask, bbox: Optional[Tuple[int, int, int, int]], text: str):
    cv2, np = require_cv()
    overlay = rgb.copy()
    red = np.zeros_like(overlay)
    red[:, :, 0] = 255
    alpha = (mask > 0).astype(np.float32)[:, :, None] * 0.45
    overlay = (overlay.astype(np.float32) * (1.0 - alpha) + red.astype(np.float32) * alpha).astype(np.uint8)
    if bbox is not None:
        x, y, width, height = bbox
        cv2.rectangle(overlay, (x, y), (x + width, y + height), (255, 0, 0), 2)
    cv2.putText(overlay, text, (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 0, 0), 2, cv2.LINE_AA)
    return overlay


def save_rgb_png(path: Path, rgb) -> None:
    cv2, _ = require_cv()
    path.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(path), cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR))


def save_gray_png(path: Path, gray) -> None:
    cv2, _ = require_cv()
    path.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(path), gray)


def _apply_auto_calibration(
    profile: VisionProfile,
    detector: VisionDetector,
    normal_files: Sequence[Path],
    auto_threshold_floor: int,
    auto_threshold_factor: float,
    progress_callback: Optional[Callable[[str], None]] = None,
) -> VisionProfile:
    """Apply detector-specific calibration without changing the template_diff baseline."""

    cv2, np = require_cv()
    if isinstance(detector, BarcodeDetector) and profile.detector_hint == DETECTOR_HINT_BARCODE:
        barcode_calibration = detector.estimate_calibration(normal_files, progress_callback=progress_callback)
        if barcode_calibration:
            profile.barcode_area_threshold = int(
                barcode_calibration.get("barcode_area_threshold", profile.barcode_area_threshold)
            )
            profile.barcode_local_area_threshold = int(
                barcode_calibration.get(
                    "barcode_local_area_threshold",
                    profile.barcode_local_area_threshold,
                )
            )
            profile.barcode_score_threshold = float(
                barcode_calibration.get("barcode_score_threshold", profile.barcode_score_threshold)
            )
            profile.barcode_dark_ratio_threshold = float(
                barcode_calibration.get(
                    "barcode_dark_ratio_threshold",
                    profile.barcode_dark_ratio_threshold,
                )
            )
            profile.detector_params = {
                **(profile.detector_params or {}),
                **barcode_calibration,
            }
            if progress_callback:
                progress_callback(
                    "barcode calibration: "
                    f"area={profile.barcode_area_threshold} local={profile.barcode_local_area_threshold} "
                    f"score={profile.barcode_score_threshold:.1f} dark={profile.barcode_dark_ratio_threshold:.4f}"
                )
        return profile

    areas: list[int] = []
    total = len(normal_files)
    for index, path in enumerate(normal_files, start=1):
        try:
            areas.append(int(detector.detect_path(path).defect_area))
        except Exception:
            continue
        if progress_callback and (index == 1 or index == total or index % 50 == 0):
            progress_callback(f"calibrate {index}/{total}")
    if areas:
        arr = np.array(areas, dtype=np.float64)
        mean_a = float(arr.mean())
        p99 = float(np.percentile(arr, 99))
        max_a = int(arr.max())
        calc = max(int(auto_threshold_floor), int(round(max(p99 * auto_threshold_factor, max_a * 1.3))))
        profile.area_threshold = calc
        separation = calc / max(1.0, p99)
        profile.detector_params = {
            **(profile.detector_params or {}),
            "calibration": {
                "normal_mean": round(mean_a, 1),
                "normal_p99": round(p99, 1),
                "normal_max": max_a,
                "area_threshold": calc,
                "separation_ratio": round(separation, 2),
            },
        }
        if progress_callback:
            progress_callback(
                f"auto-threshold: normal mean={mean_a:.0f} p99={p99:.0f} max={max_a} "
                f"-> area_threshold={calc} (分离余量x{separation:.1f})"
            )
            if p99 > 800:
                progress_callback(
                    "WARN 正常基线偏高(p99>800)：分离度可能不足，建议改善光照/固定摆位/补采正常图后重建"
                )
    return profile


def build_profile(
    normal_dir: Path,
    output_dir: Path,
    roi: Tuple[int, int, int, int] = DEFAULT_ROI,
    work_size: Tuple[int, int] = DEFAULT_WORK_SIZE,
    reference_count: int = DEFAULT_REFERENCE_COUNT,
    diff_margin: int = DEFAULT_DIFF_MARGIN,
    area_threshold: int = DEFAULT_AREA_THRESHOLD,
    min_blob_area: int = DEFAULT_MIN_BLOB_AREA,
    highlight_clip: int = DEFAULT_HIGHLIGHT_CLIP,
    feature_edge_relax: int = DEFAULT_FEATURE_EDGE_RELAX,
    border_erode_px: int = DEFAULT_BORDER_ERODE_PX,
    adaptive_tol_k: float = DEFAULT_ADAPTIVE_TOL_K,
    align_blur_ksize: int = DEFAULT_ALIGN_BLUR_KSIZE,
    auto_threshold: bool = True,
    auto_threshold_factor: float = 2.5,
    auto_threshold_floor: int = 250,
    progress_callback: Optional[Callable[[str], None]] = None,
) -> VisionProfile:
    cv2, np = require_cv()
    normal_files = list(iter_image_files(normal_dir, recursive=True))
    if not normal_files:
        raise FileNotFoundError(f"no normal images found: {normal_dir}")

    ref_files = normal_files[: max(1, min(reference_count, len(normal_files)))]
    if progress_callback:
        progress_callback(f"reference files={len(ref_files)} normal files={len(normal_files)}")

    first_rgb = crop_or_resize_for_work(ref_files[0], roi, work_size)
    first_gray = normalize_gray(first_rgb)
    ref_aligned = [first_rgb]
    for path in ref_files[1:]:
        rgb = crop_or_resize_for_work(path, roi, work_size)
        gray = normalize_gray(rgb)
        aligned, _, _, _, _, _ = align_to_reference(first_gray, gray, rgb)
        ref_aligned.append(aligned)

    template_rgb = np.median(np.stack(ref_aligned, axis=0), axis=0).astype(np.uint8)
    template_gray = normalize_gray(template_rgb)
    valid_mask = build_valid_mask(template_rgb)

    aligned_normals = []
    for index, path in enumerate(normal_files, start=1):
        rgb = crop_or_resize_for_work(path, roi, work_size)
        gray = normalize_gray(rgb)
        aligned, _, _, _, _, _ = align_to_reference(template_gray, gray, rgb)
        aligned_normals.append(aligned)
        if progress_callback and (index == 1 or index == len(normal_files) or index % 25 == 0):
            progress_callback(f"align normal {index}/{len(normal_files)}: {path.name}")

    normal_stack = np.stack(aligned_normals, axis=0)
    template_rgb = np.median(normal_stack, axis=0).astype(np.uint8)
    template_gray = normalize_gray(template_rgb)
    normal_gray = np.stack([normalize_gray(img) for img in aligned_normals], axis=0).astype(np.int16)
    tolerance = np.percentile(np.abs(normal_gray - template_gray.astype(np.int16)), 95, axis=0).astype(np.uint8)
    tolerance = cv2.GaussianBlur(tolerance, (5, 5), 0)
    valid_mask = build_valid_mask(template_rgb)
    detector_hint, detector_confidence, detector_reason, detector_metrics = analyze_template_detector_hint(
        template_rgb, valid_mask
    )

    profile = VisionProfile(
        name=normal_dir.name or "vision_profile",
        roi=roi,
        work_size=work_size,
        template_rgb=template_rgb,
        template_gray=template_gray,
        tolerance_gray=tolerance,
        valid_mask=valid_mask,
        diff_margin=diff_margin,
        area_threshold=area_threshold,
        min_blob_area=min_blob_area,
        reference_count=len(ref_files),
        normal_count=len(normal_files),
        detector=DEFAULT_DETECTOR,
        fallback_detector=DEFAULT_FALLBACK_DETECTOR,
        detector_params={"template_analysis": detector_metrics},
        detector_hint=detector_hint,
        detector_confidence=detector_confidence,
        detector_reason=detector_reason,
        highlight_clip=highlight_clip,
        feature_edge_relax=feature_edge_relax,
        border_erode_px=border_erode_px,
        adaptive_tol_k=adaptive_tol_k,
        align_blur_ksize=align_blur_ksize,
    )

    # 采集质量门槛(C)：检查正常基线 normal_p99(valid_mask 内逐像素 |normal-template| 的 p99)。
    # 偏高说明标定样本对齐/光照不稳，分离度会不足；写 quality_warning 并打印告警(非硬失败)。
    mask_sel = valid_mask > 0
    if np.any(mask_sel):
        per_pixel = np.abs(normal_gray - template_gray.astype(np.int16))
        normal_p99 = float(np.percentile(per_pixel[:, mask_sel], 99))
    else:
        normal_p99 = float(np.percentile(np.abs(normal_gray - template_gray.astype(np.int16)), 99))
    profile.detector_params = {
        **(profile.detector_params or {}),
        "normal_p99": round(normal_p99, 1),
    }
    if normal_p99 > NORMAL_P99_QUALITY_WARN:
        warning = (
            f"normal_p99={normal_p99:.0f} > {NORMAL_P99_QUALITY_WARN:.0f}: "
            "采集质量偏低(对齐/光照不稳)，分离度可能不足，建议固定摆位/改善光照/补采正常图后重建"
        )
        profile.quality_warning = warning
        msg = f"WARN 采集质量门槛: {warning}"
        if progress_callback:
            progress_callback(msg)
        print(msg)

    # 阈值自动标定 + 分离度体检：用刚建好的 profile（与运行时同一套检测逻辑）把正常图全跑一遍，
    # 按正常缺陷面积分布自动定 area_threshold，使其对“没见过的新物体”也能自适应。
    if auto_threshold:
        detector = make_detector(profile)
        profile = _apply_auto_calibration(
            profile,
            detector,
            normal_files,
            auto_threshold_floor,
            auto_threshold_factor,
            progress_callback,
        )
        write_profile_files(profile, output_dir)
        return profile

    write_profile_files(profile, output_dir)
    return profile


def write_profile_files(profile: VisionProfile, output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    save_rgb_png(output_dir / "reference_template.png", profile.template_rgb)
    save_gray_png(output_dir / "normal_tolerance.png", profile.tolerance_gray)
    save_gray_png(output_dir / "valid_mask.png", profile.valid_mask)
    data = {
        "format": 1,
        "name": profile.name,
        "roi": list(profile.roi),
        "work_size": list(profile.work_size),
        "reference_count": profile.reference_count,
        "normal_count": profile.normal_count,
        "diff_margin": profile.diff_margin,
        "area_threshold": profile.area_threshold,
        "min_blob_area": profile.min_blob_area,
        "detector": profile.detector,
        "fallback_detector": profile.fallback_detector,
        "detector_params": profile.detector_params or {},
        "detector_hint": profile.detector_hint,
        "detector_confidence": profile.detector_confidence,
        "detector_reason": profile.detector_reason,
        "highlight_clip": profile.highlight_clip,
        "feature_edge_relax": profile.feature_edge_relax,
        "border_erode_px": profile.border_erode_px,
        "adaptive_tol_k": profile.adaptive_tol_k,
        "align_blur_ksize": profile.align_blur_ksize,
        "quality_warning": profile.quality_warning,
        "barcode_area_threshold": profile.barcode_area_threshold,
        "barcode_local_area_threshold": profile.barcode_local_area_threshold,
        "barcode_score_threshold": profile.barcode_score_threshold,
        "barcode_dark_ratio_threshold": profile.barcode_dark_ratio_threshold,
        "files": {
            "template": "reference_template.png",
            "tolerance": "normal_tolerance.png",
            "valid_mask": "valid_mask.png",
        },
    }
    profile_path = output_dir / "vision_profile.json"
    profile_path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    return profile_path


def detect_image(profile: VisionProfile, image_path: Path) -> VisionDetectResult:
    detector = make_detector(profile)
    return detector.detect_path(image_path)


def write_overlay(profile: VisionProfile, result: VisionDetectResult, output_dir: Path) -> Optional[Path]:
    if result.error:
        return None
    artifacts = result.debug_artifacts or {}
    aligned_rgb = artifacts.get("aligned_rgb")
    mask = result.mask_aligned
    if aligned_rgb is None:
        aligned_rgb = crop_or_resize_for_work(result.path, profile.roi, profile.work_size)
    if mask is None:
        _, np = require_cv()
        mask = np.zeros(aligned_rgb.shape[:2], dtype=np.uint8)
    text = f"{'OK' if result.ok else 'NG'} {result.detector} area={result.defect_area}"
    overlay = make_overlay(aligned_rgb, mask, result.bbox_aligned, text)
    rel_name = result.path.with_suffix("").name + "_overlay.png"
    overlay_path = output_dir / "overlays" / rel_name
    save_rgb_png(overlay_path, overlay)
    result.overlay_path = overlay_path
    return overlay_path


def save_debug_artifacts(result: VisionDetectResult, output_dir: Path) -> dict:
    artifacts = result.debug_artifacts or {}
    saved: dict[str, str] = {}
    stem = result.path.with_suffix("").name
    debug_dir = output_dir / "debug" / stem
    for name, image in artifacts.items():
        if image is None:
            continue
        path = debug_dir / f"{name}.png"
        try:
            if hasattr(image, "ndim") and image.ndim == 3:
                save_rgb_png(path, image)
            else:
                save_gray_png(path, image)
            saved[name] = str(path)
        except Exception as exc:
            saved[name] = f"save failed: {exc}"

    metrics_path = debug_dir / "metrics.json"
    metrics_path.parent.mkdir(parents=True, exist_ok=True)
    metrics_path.write_text(
        json.dumps(result.metrics or {}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    saved["metrics"] = str(metrics_path)
    return saved


def eval_profile(
    profile_dir: Path,
    input_dir: Path,
    output_dir: Path,
    recursive: bool = True,
    write_debug: bool = True,
    include_overlay_inputs: bool = False,
    progress_callback: Optional[Callable[[str], None]] = None,
) -> VisionBatchResult:
    output_dir.mkdir(parents=True, exist_ok=True)
    profile = load_profile(profile_dir)
    image_files = list(iter_image_files(input_dir, recursive=recursive))
    if include_overlay_inputs is not True:
        image_files = [path for path in image_files if not path.stem.lower().endswith("_overlay")]
    if not image_files:
        raise FileNotFoundError(f"no images found: {input_dir}")

    results: list[VisionDetectResult] = []
    for index, path in enumerate(image_files, start=1):
        try:
            result = detect_image(profile, path)
            write_overlay(profile, result, output_dir)
            if write_debug:
                saved = save_debug_artifacts(result, output_dir)
                if result.metrics is None:
                    result.metrics = {}
                result.metrics["debug_files"] = saved
        except Exception as exc:
            result = VisionDetectResult(
                path=path,
                ok=False,
                score=0.0,
                defect_area=0,
                max_diff=0,
                mean_diff=0.0,
                angle_deg=0.0,
                robot_angle_deg=0.0,
                dx=0.0,
                dy=0.0,
                bbox_raw=None,
                bbox_aligned=None,
                error=str(exc),
                detector=profile.detector,
                reason="exception",
            )
        results.append(result)
        if progress_callback and (index == 1 or index == len(image_files) or index % 10 == 0 or result.error):
            state = "ERR" if result.error else ("OK" if result.ok else "NG")
            progress_callback(
                f"eval {index}/{len(image_files)} {state}: {path.name} "
                f"detector={result.detector} score={result.score:.1f} area={result.defect_area}"
            )

    report_path = output_dir / "metrics.csv"
    write_report(report_path, results)
    summary_path = output_dir / "summary.txt"
    ok_count = sum(1 for item in results if item.ok and not item.error)
    fail_count = sum(1 for item in results if item.error)
    ng_count = len(results) - ok_count - fail_count
    summary_path.write_text(
        "\n".join(
            [
                f"profile={profile_dir}",
                f"input={input_dir}",
                f"detector={profile.detector}",
                f"total={len(results)}",
                f"ok={ok_count}",
                f"ng={ng_count}",
                f"fail={fail_count}",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    return VisionBatchResult(
        normal_dir=profile_dir,
        test_dir=input_dir,
        output_dir=output_dir,
        profile_path=profile_dir / "vision_profile.json",
        report_path=report_path,
        total_count=len(results),
        ok_count=ok_count,
        ng_count=ng_count,
        fail_count=fail_count,
        reference_count=profile.reference_count,
        normal_count=profile.normal_count,
        results=results,
    )


def run_batch(
    normal_dir: Path,
    test_dir: Path,
    output_dir: Path,
    roi: Tuple[int, int, int, int] = DEFAULT_ROI,
    work_size: Tuple[int, int] = DEFAULT_WORK_SIZE,
    reference_count: int = DEFAULT_REFERENCE_COUNT,
    diff_margin: int = DEFAULT_DIFF_MARGIN,
    area_threshold: int = DEFAULT_AREA_THRESHOLD,
    min_blob_area: int = DEFAULT_MIN_BLOB_AREA,
    progress_callback: Optional[Callable[[str], None]] = None,
) -> VisionBatchResult:
    output_dir.mkdir(parents=True, exist_ok=True)
    profile = build_profile(
        normal_dir=normal_dir,
        output_dir=output_dir,
        roi=roi,
        work_size=work_size,
        reference_count=reference_count,
        diff_margin=diff_margin,
        area_threshold=area_threshold,
        min_blob_area=min_blob_area,
        progress_callback=progress_callback,
    )

    test_files = list(iter_image_files(test_dir, recursive=True))
    if not test_files:
        raise FileNotFoundError(f"no test images found: {test_dir}")

    results: list[VisionDetectResult] = []
    for index, path in enumerate(test_files, start=1):
        try:
            result = detect_image(profile, path)
            write_overlay(profile, result, output_dir)
        except Exception as exc:
            result = VisionDetectResult(
                path=path,
                ok=False,
                score=0.0,
                defect_area=0,
                max_diff=0,
                mean_diff=0.0,
                angle_deg=0.0,
                robot_angle_deg=0.0,
                dx=0.0,
                dy=0.0,
                bbox_raw=None,
                bbox_aligned=None,
                error=str(exc),
            )
        results.append(result)
        if progress_callback and (index == 1 or index == len(test_files) or index % 10 == 0 or result.error):
            state = "ERR" if result.error else ("OK" if result.ok else "NG")
            progress_callback(f"detect {index}/{len(test_files)} {state}: {path.name} area={result.defect_area}")

    report_path = output_dir / "batch_report.csv"
    write_report(report_path, results)
    ok_count = sum(1 for item in results if item.ok and not item.error)
    fail_count = sum(1 for item in results if item.error)
    ng_count = len(results) - ok_count - fail_count
    return VisionBatchResult(
        normal_dir=normal_dir,
        test_dir=test_dir,
        output_dir=output_dir,
        profile_path=output_dir / "vision_profile.json",
        report_path=report_path,
        total_count=len(results),
        ok_count=ok_count,
        ng_count=ng_count,
        fail_count=fail_count,
        reference_count=profile.reference_count,
        normal_count=profile.normal_count,
        results=results,
    )


def write_report(path: Path, results: Sequence[VisionDetectResult]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(
            [
                "path",
                "result",
                "detector",
                "reason",
                "score",
                "defect_area",
                "area_threshold",
                "component_count",
                "max_blob_area",
                "max_diff",
                "mean_diff",
                "angle_deg",
                "robot_angle_deg",
                "dx",
                "dy",
                "bbox_raw",
                "bbox_aligned",
                "overlay",
                "error",
            ]
        )
        for item in results:
            writer.writerow(
                [
                    str(item.path),
                    "ERROR" if item.error else ("OK" if item.ok else "NG"),
                    item.detector,
                    item.reason,
                    f"{item.score:.3f}",
                    item.defect_area,
                    "" if not item.metrics else item.metrics.get("area_threshold", ""),
                    "" if not item.metrics else item.metrics.get("component_count", ""),
                    "" if not item.metrics else item.metrics.get("max_blob_area", ""),
                    item.max_diff,
                    f"{item.mean_diff:.3f}",
                    f"{item.angle_deg:.3f}",
                    f"{item.robot_angle_deg:.3f}",
                    f"{item.dx:.3f}",
                    f"{item.dy:.3f}",
                    "" if item.bbox_raw is None else ",".join(str(v) for v in item.bbox_raw),
                    "" if item.bbox_aligned is None else ",".join(str(v) for v in item.bbox_aligned),
                    "" if item.overlay_path is None else str(item.overlay_path),
                    item.error,
                ]
            )


def parse_size(text: str) -> Tuple[int, int]:
    parts = text.lower().replace(",", "x").split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("size must be like 240x240")
    try:
        width, height = int(parts[0]), int(parts[1])
    except ValueError as exc:
        raise argparse.ArgumentTypeError("size must contain integers") from exc
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return width, height


def _build_profile_parser(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--normal", required=True, type=Path, help="normal sample directory")
    parser.add_argument("--test", required=True, type=Path, help="test sample directory")
    parser.add_argument("--output", required=True, type=Path, help="output profile/report directory")
    parser.add_argument("--roi", type=parse_roi, default=DEFAULT_ROI, help="crop ROI, default 40,0,240,240")
    parser.add_argument("--work-size", type=parse_size, default=DEFAULT_WORK_SIZE, help="work size, default 240x240")
    parser.add_argument("--reference-count", type=int, default=DEFAULT_REFERENCE_COUNT, help="front normal images used as references")
    parser.add_argument("--diff-margin", type=int, default=DEFAULT_DIFF_MARGIN, help="extra gray difference margin")
    parser.add_argument("--area-threshold", type=int, default=DEFAULT_AREA_THRESHOLD, help="NG area threshold in pixels")
    parser.add_argument("--min-blob-area", type=int, default=DEFAULT_MIN_BLOB_AREA, help="ignore smaller connected components")


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Build, validate, and evaluate PC vision profiles.")
    subparsers = parser.add_subparsers(dest="command")

    build_parser = subparsers.add_parser("build-profile", help="build a profile from normal samples and validate a test set")
    _build_profile_parser(build_parser)

    eval_parser = subparsers.add_parser("eval-profile", help="evaluate an existing profile and write debug artifacts")
    eval_parser.add_argument("--profile", required=True, type=Path, help="profile directory containing vision_profile.json")
    eval_parser.add_argument("--input", required=True, type=Path, help="image directory to evaluate")
    eval_parser.add_argument("--output", required=True, type=Path, help="output report/debug directory")
    eval_parser.add_argument("--no-debug", action="store_true", help="skip intermediate debug image output")
    eval_parser.add_argument("--flat", action="store_true", help="do not recurse into subdirectories")
    eval_parser.add_argument("--include-overlays", action="store_true", help="include files named *_overlay.* as inputs")

    # Backward compatibility: old callers passed --normal/--test directly.
    if argv is None:
        import sys

        argv = sys.argv[1:]
    argv = list(argv)
    if argv and argv[0] not in ("build-profile", "eval-profile"):
        argv.insert(0, "build-profile")
    args = parser.parse_args(argv)

    def log(text: str) -> None:
        print(f"[vision] {text}")

    if args.command == "eval-profile":
        result = eval_profile(
            profile_dir=args.profile,
            input_dir=args.input,
            output_dir=args.output,
            recursive=not args.flat,
            write_debug=not args.no_debug,
            include_overlay_inputs=args.include_overlays,
            progress_callback=log,
        )
        print(
            f"[vision] done total={result.total_count} ok={result.ok_count} "
            f"ng={result.ng_count} fail={result.fail_count}"
        )
        print(f"[vision] report={result.report_path}")
        return 0 if result.fail_count == 0 else 2

    result = run_batch(
        normal_dir=args.normal,
        test_dir=args.test,
        output_dir=args.output,
        roi=args.roi,
        work_size=args.work_size,
        reference_count=args.reference_count,
        diff_margin=args.diff_margin,
        area_threshold=args.area_threshold,
        min_blob_area=args.min_blob_area,
        progress_callback=log,
    )
    print(
        f"[vision] done total={result.total_count} ok={result.ok_count} "
        f"ng={result.ng_count} fail={result.fail_count}"
    )
    print(f"[vision] profile={result.profile_path}")
    print(f"[vision] report={result.report_path}")
    return 0 if result.fail_count == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
