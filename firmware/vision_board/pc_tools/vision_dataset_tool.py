#!/usr/bin/env python3
"""
Vision Board PC helper.

First-stage goals:
- Convert board-side RGB565 BMP captures to Edge Impulse friendly PNG files.
- Keep the PC training preprocessing path aligned with the board inference path.
- Create a standard model package template for /vision/models/<product_name>/.

No third-party packages are required.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import shutil
import struct
import sys
import tempfile
import zipfile
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, List, Optional, Sequence, Tuple


DEFAULT_ROI = (40, 0, 240, 240)
DEFAULT_SIZE = (96, 96)
DEFAULT_GRID = (5, 5)
DEFAULT_THRESHOLD = "9.3"
DEFAULT_PROFILE = "cap_v15_96"
DEFAULT_ARENA0 = 183808
DEFAULT_ARENA1 = 76192
DEFAULT_MODEL_TYPE = "visual_gmm"


@dataclass
class RgbImage:
    width: int
    height: int
    pixels: bytearray  # RGB888, row-major, 3 bytes per pixel.


@dataclass
class ConvertImagesResult:
    input_dir: Path
    output_dir: Path
    manifest_path: Path
    total_count: int
    ok_count: int
    fail_count: int
    errors: List[str]


@dataclass
class PackageModelResult:
    source: Path
    package_dir: Path
    name: str
    profile: str
    model_type: str
    input_size: Tuple[int, int]
    grid: Tuple[int, int]
    threshold: str
    arena0: int
    arena1: int
    main_tflite: Path
    anomaly_tflite: Optional[Path]
    project_id: str
    deploy_version: str
    warnings: List[str]


def parse_size(text: str) -> Tuple[int, int]:
    parts = text.lower().replace(",", "x").split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("size must be like 96x96")
    try:
        width = int(parts[0])
        height = int(parts[1])
    except ValueError as exc:
        raise argparse.ArgumentTypeError("size must contain integers") from exc
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return width, height


def parse_roi(text: str) -> Tuple[int, int, int, int]:
    parts = text.lower().replace("x", ",").split(",")
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("roi must be like 40,0,240,240")
    try:
        x, y, width, height = [int(part.strip()) for part in parts]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("roi must contain integers") from exc
    if x < 0 or y < 0 or width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("roi must be non-negative x/y and positive w/h")
    return x, y, width, height


def read_le_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_le_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_le_i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def rgb565_to_rgb888(value: int) -> Tuple[int, int, int]:
    red5 = (value >> 11) & 0x1F
    green6 = (value >> 5) & 0x3F
    blue5 = value & 0x1F
    red = (red5 << 3) | (red5 >> 2)
    green = (green6 << 2) | (green6 >> 4)
    blue = (blue5 << 3) | (blue5 >> 2)
    return red, green, blue


def load_rgb565_bmp(path: Path) -> RgbImage:
    data = path.read_bytes()
    if len(data) < 66 or data[0:2] != b"BM":
        raise ValueError(f"{path} is not a supported BMP file")

    pixel_offset = read_le_u32(data, 10)
    dib_size = read_le_u32(data, 14)
    if dib_size < 40:
        raise ValueError(f"{path} has unsupported BMP DIB header")

    width = read_le_i32(data, 18)
    raw_height = read_le_i32(data, 22)
    planes = read_le_u16(data, 26)
    bpp = read_le_u16(data, 28)
    compression = read_le_u32(data, 30)

    if planes != 1 or width <= 0 or raw_height == 0 or bpp != 16:
        raise ValueError(f"{path} is not a 16-bit BMP")
    if compression not in (0, 3):
        raise ValueError(f"{path} uses unsupported BMP compression {compression}")

    height = abs(raw_height)
    top_down = raw_height < 0
    row_stride = ((width * bpp + 31) // 32) * 4
    expected_size = pixel_offset + row_stride * height
    if expected_size > len(data):
        raise ValueError(f"{path} BMP pixel data is truncated")

    pixels = bytearray(width * height * 3)

    for dst_y in range(height):
        src_y = dst_y if top_down else (height - 1 - dst_y)
        src_row = pixel_offset + src_y * row_stride
        dst_row = dst_y * width * 3
        for x in range(width):
            value = read_le_u16(data, src_row + x * 2)
            red, green, blue = rgb565_to_rgb888(value)
            dst = dst_row + x * 3
            pixels[dst] = red
            pixels[dst + 1] = green
            pixels[dst + 2] = blue

    return RgbImage(width=width, height=height, pixels=pixels)


def crop_image(image: RgbImage, roi: Tuple[int, int, int, int]) -> RgbImage:
    x, y, width, height = roi
    if x + width > image.width or y + height > image.height:
        raise ValueError(
            f"ROI {roi} exceeds source image {image.width}x{image.height}"
        )

    out = bytearray(width * height * 3)
    for row in range(height):
        src_start = ((y + row) * image.width + x) * 3
        src_end = src_start + width * 3
        dst_start = row * width * 3
        out[dst_start : dst_start + width * 3] = image.pixels[src_start:src_end]

    return RgbImage(width=width, height=height, pixels=out)


def get_pixel(image: RgbImage, x: int, y: int) -> Tuple[int, int, int]:
    offset = (y * image.width + x) * 3
    return image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2]


def resize_bilinear(image: RgbImage, target_size: Tuple[int, int]) -> RgbImage:
    target_w, target_h = target_size
    if image.width == target_w and image.height == target_h:
        return RgbImage(image.width, image.height, bytearray(image.pixels))

    out = bytearray(target_w * target_h * 3)
    scale_x = image.width / target_w
    scale_y = image.height / target_h

    for dst_y in range(target_h):
        src_y = (dst_y + 0.5) * scale_y - 0.5
        y0 = int(max(0, min(image.height - 1, int(src_y))))
        y1 = min(image.height - 1, y0 + 1)
        fy = max(0.0, min(1.0, src_y - y0))

        for dst_x in range(target_w):
            src_x = (dst_x + 0.5) * scale_x - 0.5
            x0 = int(max(0, min(image.width - 1, int(src_x))))
            x1 = min(image.width - 1, x0 + 1)
            fx = max(0.0, min(1.0, src_x - x0))

            p00 = get_pixel(image, x0, y0)
            p10 = get_pixel(image, x1, y0)
            p01 = get_pixel(image, x0, y1)
            p11 = get_pixel(image, x1, y1)

            dst = (dst_y * target_w + dst_x) * 3
            for channel in range(3):
                top = p00[channel] * (1.0 - fx) + p10[channel] * fx
                bottom = p01[channel] * (1.0 - fx) + p11[channel] * fx
                out[dst + channel] = int(top * (1.0 - fy) + bottom * fy + 0.5)

    return RgbImage(width=target_w, height=target_h, pixels=out)


def png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(chunk_type)
    crc = zlib.crc32(payload, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + chunk_type + payload + struct.pack(">I", crc)


def save_png_rgb888(path: Path, image: RgbImage) -> None:
    raw = bytearray()
    row_bytes = image.width * 3
    for y in range(image.height):
        raw.append(0)  # PNG filter type 0.
        start = y * row_bytes
        raw.extend(image.pixels[start : start + row_bytes])

    ihdr = struct.pack(">IIBBBBB", image.width, image.height, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", ihdr)
    png += png_chunk(b"IDAT", zlib.compress(bytes(raw), level=6))
    png += png_chunk(b"IEND", b"")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def iter_bmp_files(input_dir: Path, recursive: bool) -> Iterable[Path]:
    pattern = "**/*.bmp" if recursive else "*.bmp"
    yield from sorted(input_dir.glob(pattern))


def make_output_png_path(input_root: Path, output_root: Path, bmp_path: Path, flat: bool) -> Path:
    relative = bmp_path.relative_to(input_root)
    if flat:
        name = "_".join(relative.with_suffix("").parts) + ".png"
        return output_root / name
    return (output_root / relative).with_suffix(".png")


def format_roi(roi: Tuple[int, int, int, int]) -> str:
    return f"{roi[0]},{roi[1]},{roi[2]},{roi[3]}"


def format_size(size: Tuple[int, int]) -> str:
    return f"{size[0]}x{size[1]}"


def convert_images(
    input_dir: Path,
    output_dir: Path,
    roi: Tuple[int, int, int, int] = DEFAULT_ROI,
    size: Tuple[int, int] = DEFAULT_SIZE,
    recursive: bool = True,
    flat: bool = False,
    progress_callback: Optional[Callable[[int, int, Path, bool, str], None]] = None,
) -> ConvertImagesResult:
    input_dir = Path(input_dir).resolve()
    output_dir = Path(output_dir).resolve()
    manifest_path = output_dir / "manifest.csv"
    errors: List[str] = []

    if not input_dir.exists() or not input_dir.is_dir():
        errors.append(f"input directory not found: {input_dir}")
        return ConvertImagesResult(input_dir, output_dir, manifest_path, 0, 0, 0, errors)

    bmp_files = list(iter_bmp_files(input_dir, recursive))
    if not bmp_files:
        errors.append(f"no BMP files found under {input_dir}")
        return ConvertImagesResult(input_dir, output_dir, manifest_path, 0, 0, 0, errors)

    output_dir.mkdir(parents=True, exist_ok=True)
    ok_count = 0
    fail_count = 0
    roi_text = format_roi(roi)
    size_text = format_size(size)

    with manifest_path.open("w", newline="", encoding="utf-8") as manifest_file:
        writer = csv.writer(manifest_file)
        writer.writerow(["source", "target", "roi", "size"])

        for index, bmp_path in enumerate(bmp_files, start=1):
            png_path = make_output_png_path(input_dir, output_dir, bmp_path, flat)
            try:
                image = load_rgb565_bmp(bmp_path)
                cropped = crop_image(image, roi)
                resized = resize_bilinear(cropped, size)
                save_png_rgb888(png_path, resized)
                writer.writerow([str(bmp_path), str(png_path), roi_text, size_text])
                ok_count += 1
                if progress_callback is not None:
                    progress_callback(index, len(bmp_files), bmp_path, True, "")
            except Exception as exc:  # Keep batch conversion going.
                fail_count += 1
                errors.append(f"{bmp_path}: {exc}")
                if progress_callback is not None:
                    progress_callback(index, len(bmp_files), bmp_path, False, str(exc))

    return ConvertImagesResult(
        input_dir=input_dir,
        output_dir=output_dir,
        manifest_path=manifest_path,
        total_count=len(bmp_files),
        ok_count=ok_count,
        fail_count=fail_count,
        errors=errors,
    )


def command_convert_images(args: argparse.Namespace) -> int:
    result = convert_images(
        input_dir=Path(args.input),
        output_dir=Path(args.output),
        roi=args.roi,
        size=args.size,
        recursive=args.recursive,
        flat=args.flat,
    )

    for error in result.errors:
        print(f"[pc_tool] failed: {error}", file=sys.stderr)
    print(f"[pc_tool] converted {result.ok_count}/{result.total_count} BMP files to {result.output_dir}")
    print(f"[pc_tool] manifest: {result.manifest_path}")
    if result.fail_count:
        print(f"[pc_tool] failed files: {result.fail_count}", file=sys.stderr)
    return 0 if result.ok_count > 0 else 1


def write_model_ini(
    package_dir: Path,
    name: str,
    profile: str,
    model_type: str,
    input_size: Tuple[int, int],
    grid: Tuple[int, int],
    threshold: str,
    arena0: int,
    arena1: int,
    main_file: str = "model.tflite",
    anomaly_file: str = "anomaly_model.tflite",
    source: str = "",
    project_id: str = "",
    deploy_version: str = "",
    main_sha256: str = "",
    anomaly_sha256: str = "",
) -> None:
    lines = [
        "# Vision Board model package",
        "format=1",
        f"name={name}",
        f"profile={profile}",
        f"type={model_type}",
        f"input={input_size[0]}x{input_size[1]}",
        f"grid={grid[0]}x{grid[1]}",
        f"threshold={threshold}",
        "tflite_count=2",
        f"arena0={arena0}",
        f"arena1={arena1}",
        f"main={main_file}",
        f"anomaly={anomaly_file}",
    ]
    if project_id:
        lines.append(f"project_id={project_id}")
    if deploy_version:
        lines.append(f"deploy_version={deploy_version}")
    if source:
        lines.append(f"source={source}")
    if main_sha256:
        lines.append(f"main_sha256={main_sha256}")
    if anomaly_sha256:
        lines.append(f"anomaly_sha256={anomaly_sha256}")
    content = "\n".join(lines) + "\n"
    package_dir.mkdir(parents=True, exist_ok=True)
    (package_dir / "model.ini").write_text(content, encoding="utf-8", newline="\n")


def command_init_model_package(args: argparse.Namespace) -> int:
    package_dir = Path(args.output).resolve()
    write_model_ini(
        package_dir=package_dir,
        name=args.name,
        profile=args.profile,
        model_type=args.model_type,
        input_size=args.input_size,
        grid=args.grid,
        threshold=args.threshold,
        arena0=args.arena0,
        arena1=args.arena1,
    )
    print(f"[pc_tool] model package template: {package_dir}")
    print("[pc_tool] put model.tflite and anomaly_model.tflite into this folder later")
    return 0


def find_tflite_files(root: Path) -> List[Path]:
    return sorted(path for path in root.rglob("*.tflite") if path.is_file())


def read_text_if_exists(path: Path) -> str:
    if not path.exists() or not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="ignore")


def parse_metadata_threshold(root: Path, fallback: str) -> str:
    metadata = read_text_if_exists(root / "model-parameters" / "model_metadata.h")
    match = re.search(r"#define\s+EI_CLASSIFIER_THRESHOLD\s+([0-9]+(?:\.[0-9]+)?)", metadata)
    return match.group(1) if match else fallback


def parse_metadata_int(root: Path, name: str, fallback: int) -> int:
    metadata = read_text_if_exists(root / "model-parameters" / "model_metadata.h")
    match = re.search(rf"#define\s+{re.escape(name)}\s+([0-9]+)", metadata)
    return int(match.group(1)) if match else fallback


def parse_metadata_define(root: Path, name: str) -> str:
    metadata = read_text_if_exists(root / "model-parameters" / "model_metadata.h")
    match = re.search(rf"#define\s+{re.escape(name)}\s+([^\r\n/]+)", metadata)
    return match.group(1).strip() if match else ""


def parse_metadata_string(root: Path, name: str, fallback: str = "") -> str:
    metadata = read_text_if_exists(root / "model-parameters" / "model_metadata.h")
    match = re.search(rf"#define\s+{re.escape(name)}\s+\"([^\"]*)\"", metadata)
    return match.group(1) if match else fallback


def parse_input_size(root: Path, fallback: Tuple[int, int]) -> Tuple[int, int]:
    return (
        parse_metadata_int(root, "EI_CLASSIFIER_INPUT_WIDTH", fallback[0]),
        parse_metadata_int(root, "EI_CLASSIFIER_INPUT_HEIGHT", fallback[1]),
    )


def parse_visual_grid(root: Path, fallback: Tuple[int, int]) -> Tuple[int, int]:
    variables = read_text_if_exists(root / "model-parameters" / "model_variables.h")
    match_x = re.search(r"\.grid_size_x\s*=\s*([0-9]+)", variables)
    match_y = re.search(r"\.grid_size_y\s*=\s*([0-9]+)", variables)
    if match_x and match_y:
        return int(match_x.group(1)), int(match_y.group(1))
    return fallback


def parse_model_type(root: Path, fallback: str = DEFAULT_MODEL_TYPE) -> str:
    anomaly_type = parse_metadata_define(root, "EI_CLASSIFIER_HAS_ANOMALY")
    visual_gmm = parse_metadata_int(root, "EI_CLASSIFIER_HAS_ANOMALY_VISUAL_GMM", 0)
    visual_patchcore = parse_metadata_int(root, "EI_CLASSIFIER_HAS_ANOMALY_VISUAL_PATCHCORE", 0)
    visual_any = parse_metadata_int(root, "EI_CLASSIFIER_HAS_VISUAL_ANOMALY", 0)

    if (anomaly_type == "EI_ANOMALY_TYPE_VISUAL_GMM") or (visual_gmm == 1):
        return "visual_gmm"
    if (anomaly_type == "EI_ANOMALY_TYPE_VISUAL_PATCHCORE") or (visual_patchcore == 1):
        return "visual_patchcore"
    if visual_any == 1:
        return "visual_ad"
    return fallback


def parse_project_id(root: Path) -> str:
    value = parse_metadata_int(root, "EI_CLASSIFIER_PROJECT_ID", 0)
    return str(value) if value else ""


def parse_deploy_version(root: Path) -> str:
    value = parse_metadata_int(root, "EI_CLASSIFIER_PROJECT_DEPLOY_VERSION", 0)
    return str(value) if value else ""


def file_sha256(path: Path | None) -> str:
    if path is None or not path.exists():
        return ""
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_tflite_arena_size(header: Path, fallback: int) -> int:
    text = read_text_if_exists(header)
    match = re.search(r"_ARENA_SIZE\s+([0-9]+)", text)
    return int(match.group(1)) if match else fallback


def infer_arena_sizes(root: Path, main: Path, anomaly: Path | None) -> Tuple[int, int]:
    model_dir = root / "tflite-model"
    main_header = model_dir / f"{main.stem}.h"
    arena0 = parse_tflite_arena_size(main_header, DEFAULT_ARENA0)

    if anomaly is None:
        return arena0, DEFAULT_ARENA1

    anomaly_header = model_dir / f"{anomaly.stem}.h"
    arena1 = parse_tflite_arena_size(anomaly_header, DEFAULT_ARENA1)
    return arena0, arena1


def choose_tflite_files(files: Sequence[Path]) -> Tuple[Path | None, Path | None]:
    if not files:
        return None, None

    files_by_size = sorted(files, key=lambda item: item.stat().st_size, reverse=True)
    anomaly_keywords = ("anomaly", "visual", "gmm", "learn_")
    anomaly_candidates = [
        path for path in files if any(keyword in path.name.lower() for keyword in anomaly_keywords)
    ]
    if len(files_by_size) >= 2:
        main = files_by_size[0]
        anomaly = files_by_size[1]
        if anomaly_candidates:
            small_anomaly = sorted(anomaly_candidates, key=lambda item: item.stat().st_size)[0]
            if small_anomaly != main:
                anomaly = small_anomaly
        return main, anomaly

    main = files_by_size[0]
    anomaly = None

    return main, anomaly


def sanitize_model_name(text: str) -> str:
    name = re.sub(r"[^A-Za-z0-9_.-]+", "_", text.strip())
    name = name.strip("._-")
    if not name:
        raise ValueError("model name is empty")
    return name


def safe_extract_zip(zip_path: Path, target_dir: Path) -> None:
    target_dir = target_dir.resolve()
    with zipfile.ZipFile(zip_path) as archive:
        for member in archive.infolist():
            member_path = (target_dir / member.filename).resolve()
            if target_dir != member_path and target_dir not in member_path.parents:
                raise ValueError(f"unsafe zip entry: {member.filename}")
        archive.extractall(target_dir)


def resolve_ei_source_path(source: Path, temp_dir: Path) -> Path:
    source = Path(source).resolve()
    if source.is_file() and source.suffix.lower() == ".zip":
        extract_dir = temp_dir / source.stem
        extract_dir.mkdir(parents=True, exist_ok=True)
        safe_extract_zip(source, extract_dir)
        return find_ei_export_root(extract_dir)
    if source.exists() and source.is_dir():
        return find_ei_export_root(source)
    raise FileNotFoundError(f"Edge Impulse export not found: {source}")


def find_ei_export_root(root: Path) -> Path:
    root = root.resolve()
    if (root / "model-parameters" / "model_metadata.h").exists() and (root / "tflite-model").exists():
        return root
    matches = [
        path
        for path in root.rglob("model_metadata.h")
        if path.parent.name == "model-parameters" and (path.parent.parent / "tflite-model").exists()
    ]
    if not matches:
        raise FileNotFoundError(f"model_metadata.h and tflite-model not found under {root}")
    return matches[0].parent.parent.resolve()


def package_model(
    source: Path,
    output_root: Path,
    name: str,
    profile: str = DEFAULT_PROFILE,
    model_type: str = DEFAULT_MODEL_TYPE,
    input_size: Tuple[int, int] = DEFAULT_SIZE,
    grid: Tuple[int, int] = DEFAULT_GRID,
    threshold: str = DEFAULT_THRESHOLD,
    arena0: int = DEFAULT_ARENA0,
    arena1: int = DEFAULT_ARENA1,
    metadata_auto: bool = True,
    threshold_auto: bool = True,
    arena_auto: bool = True,
    main_tflite: Path | None = None,
    anomaly_tflite: Path | None = None,
) -> PackageModelResult:
    safe_name = sanitize_model_name(name)
    output_root = Path(output_root).resolve()
    package_dir = output_root / safe_name
    warnings: List[str] = []

    with tempfile.TemporaryDirectory(prefix="vision_ei_") as temp:
        temp_dir = Path(temp)
        source_dir = resolve_ei_source_path(Path(source), temp_dir)
        main = Path(main_tflite).resolve() if main_tflite else None
        anomaly = Path(anomaly_tflite).resolve() if anomaly_tflite else None

        if main is None or anomaly is None:
            auto_main, auto_anomaly = choose_tflite_files(find_tflite_files(source_dir))
            main = main or auto_main
            anomaly = anomaly or auto_anomaly

        if main is None or not main.exists():
            raise FileNotFoundError("main .tflite not found")

        package_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(main, package_dir / "model.tflite")

        if anomaly is not None and anomaly.exists():
            shutil.copyfile(anomaly, package_dir / "anomaly_model.tflite")
        else:
            warnings.append("anomaly .tflite not found; package will be incomplete")

        resolved_threshold = parse_metadata_threshold(source_dir, threshold) if threshold_auto else threshold
        resolved_input_size = parse_input_size(source_dir, input_size) if metadata_auto else input_size
        resolved_grid = parse_visual_grid(source_dir, grid) if metadata_auto else grid
        resolved_model_type = parse_model_type(source_dir, model_type) if metadata_auto else model_type
        project_id = parse_project_id(source_dir) if metadata_auto else ""
        deploy_version = parse_deploy_version(source_dir) if metadata_auto else ""

        resolved_arena0 = arena0
        resolved_arena1 = arena1
        if arena_auto:
            resolved_arena0, resolved_arena1 = infer_arena_sizes(source_dir, main, anomaly)

        write_model_ini(
            package_dir=package_dir,
            name=safe_name,
            profile=profile,
            model_type=resolved_model_type,
            input_size=resolved_input_size,
            grid=resolved_grid,
            threshold=resolved_threshold,
            arena0=resolved_arena0,
            arena1=resolved_arena1,
            source=str(Path(source).resolve()),
            project_id=project_id,
            deploy_version=deploy_version,
            main_sha256=file_sha256(package_dir / "model.tflite"),
            anomaly_sha256=file_sha256(package_dir / "anomaly_model.tflite"),
        )

        return PackageModelResult(
            source=Path(source).resolve(),
            package_dir=package_dir,
            name=safe_name,
            profile=profile,
            model_type=resolved_model_type,
            input_size=resolved_input_size,
            grid=resolved_grid,
            threshold=resolved_threshold,
            arena0=resolved_arena0,
            arena1=resolved_arena1,
            main_tflite=package_dir / "model.tflite",
            anomaly_tflite=(package_dir / "anomaly_model.tflite") if anomaly is not None and anomaly.exists() else None,
            project_id=project_id,
            deploy_version=deploy_version,
            warnings=warnings,
        )


def command_package_model(args: argparse.Namespace) -> int:
    try:
        result = package_model(
            source=Path(args.source),
            output_root=Path(args.output),
            name=args.name,
            profile=args.profile,
            model_type=args.model_type,
            input_size=args.input_size,
            grid=args.grid,
            threshold=args.threshold,
            arena0=args.arena0,
            arena1=args.arena1,
            metadata_auto=args.metadata_auto,
            threshold_auto=args.threshold_auto,
            arena_auto=args.arena_auto,
            main_tflite=Path(args.main_tflite) if args.main_tflite else None,
            anomaly_tflite=Path(args.anomaly_tflite) if args.anomaly_tflite else None,
        )
    except Exception as exc:
        print(f"[pc_tool] package failed: {exc}", file=sys.stderr)
        return 1

    for warning in result.warnings:
        print(f"[pc_tool] warning: {warning}", file=sys.stderr)
    print(f"[pc_tool] package created: {result.package_dir}")
    print(f"[pc_tool] main: {result.main_tflite}")
    print(f"[pc_tool] anomaly: {result.anomaly_tflite if result.anomaly_tflite else 'missing'}")
    print(
        f"[pc_tool] type={result.model_type} input={result.input_size[0]}x{result.input_size[1]} "
        f"grid={result.grid[0]}x{result.grid[1]} threshold={result.threshold} "
        f"arena0={result.arena0} arena1={result.arena1}"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Vision Board PC helper")
    sub = parser.add_subparsers(dest="command", required=True)

    convert = sub.add_parser("convert-images", help="convert board BMP captures to PNG")
    convert.add_argument("--input", "-i", required=True, help="input directory containing BMP files")
    convert.add_argument("--output", "-o", required=True, help="output directory for PNG files")
    convert.add_argument("--roi", type=parse_roi, default=DEFAULT_ROI, help="crop ROI, default 40,0,240,240")
    convert.add_argument("--size", type=parse_size, default=DEFAULT_SIZE, help="output size, default 96x96")
    convert.add_argument("--recursive", action="store_true", default=True, help="scan subdirectories")
    convert.add_argument("--no-recursive", action="store_false", dest="recursive", help="scan only input directory")
    convert.add_argument("--flat", action="store_true", help="write all PNG files into one flat directory")
    convert.set_defaults(func=command_convert_images)

    init_pkg = sub.add_parser("init-model-package", help="create model.ini package template")
    init_pkg.add_argument("--output", "-o", required=True, help="model package directory")
    init_pkg.add_argument("--name", required=True, help="display/product model name")
    init_pkg.add_argument("--profile", default=DEFAULT_PROFILE, help="board profile, default cap_v15_96")
    init_pkg.add_argument("--model-type", default=DEFAULT_MODEL_TYPE, help="model type, default visual_gmm")
    init_pkg.add_argument("--input-size", type=parse_size, default=DEFAULT_SIZE, help="model input, default 96x96")
    init_pkg.add_argument("--grid", type=parse_size, default=DEFAULT_GRID, help="visual AD grid, default 5x5")
    init_pkg.add_argument("--threshold", default=DEFAULT_THRESHOLD, help="visual AD threshold, default 9.3")
    init_pkg.add_argument("--arena0", type=int, default=DEFAULT_ARENA0, help="main graph arena, default 183808")
    init_pkg.add_argument("--arena1", type=int, default=DEFAULT_ARENA1, help="anomaly graph arena, default 76192")
    init_pkg.set_defaults(func=command_init_model_package)

    package = sub.add_parser("package-model", help="standardize an Edge Impulse model export")
    package.add_argument("--source", "-s", required=True, help="Edge Impulse export directory")
    package.add_argument("--output", "-o", required=True, help="output model package directory")
    package.add_argument("--name", required=True, help="display/product model name")
    package.add_argument("--profile", default=DEFAULT_PROFILE, help="board profile, default cap_v15_96")
    package.add_argument("--model-type", default=DEFAULT_MODEL_TYPE, help="model type, default visual_gmm")
    package.add_argument("--input-size", type=parse_size, default=DEFAULT_SIZE, help="model input, default 96x96")
    package.add_argument("--grid", type=parse_size, default=DEFAULT_GRID, help="visual AD grid, default 5x5")
    package.add_argument("--threshold", default=DEFAULT_THRESHOLD, help="visual AD threshold, default 9.3")
    package.add_argument("--threshold-auto", action="store_true", default=True, help="read threshold from model_metadata.h")
    package.add_argument("--no-threshold-auto", action="store_false", dest="threshold_auto", help="use --threshold exactly")
    package.add_argument("--metadata-auto", action="store_true", default=True, help="read input/grid/type/project metadata")
    package.add_argument("--no-metadata-auto", action="store_false", dest="metadata_auto", help="use CLI metadata exactly")
    package.add_argument("--arena0", type=int, default=DEFAULT_ARENA0, help="main graph arena, default 183808")
    package.add_argument("--arena1", type=int, default=DEFAULT_ARENA1, help="anomaly graph arena, default 76192")
    package.add_argument("--arena-auto", action="store_true", default=True, help="read arena sizes from tflite-model headers")
    package.add_argument("--no-arena-auto", action="store_false", dest="arena_auto", help="use --arena0/--arena1 exactly")
    package.add_argument("--main-tflite", help="explicit main TFLite file")
    package.add_argument("--anomaly-tflite", help="explicit anomaly TFLite file")
    package.set_defaults(func=command_package_model)

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if hasattr(args, "roi"):
        args.roi_text = f"{args.roi[0]},{args.roi[1]},{args.roi[2]},{args.roi[3]}"
    if hasattr(args, "size"):
        args.size_text = f"{args.size[0]}x{args.size[1]}"

    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
