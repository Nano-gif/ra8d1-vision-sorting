#!/usr/bin/env python3
"""生成上位机应用图标 hmi/assets/app.ico。

绘制逻辑与 hmi/window.py 的 MainWindow._make_brand_icon 保持一致（蓝底圆角 +
白色镜头光圈），确保 exe 文件图标、任务栏图标、界面 header logo 三者统一。

用法：python gen_app_icon.py
依赖：PySide6（绘制）、Pillow（合成多尺寸 .ico）。
"""

from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QColor, QImage, QPainter, QPen
from PySide6.QtWidgets import QApplication

ASSETS_DIR = Path(__file__).resolve().parent / "hmi" / "assets"
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def render_brand_icon(size: int) -> QImage:
    """渲染 size×size 的品牌镜头图标为带透明通道的 QImage。"""
    image = QImage(size, size, QImage.Format.Format_ARGB32)
    image.fill(Qt.GlobalColor.transparent)
    painter = QPainter(image)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)

    radius = size * 0.22
    painter.setPen(Qt.PenStyle.NoPen)
    painter.setBrush(QColor("#3f5f74"))
    painter.drawRoundedRect(0, 0, size, size, radius, radius)

    cx = cy = size / 2.0
    r_out = size * 0.30
    ring = QPen(QColor("#ffffff"))
    ring.setWidthF(max(1.0, size * 0.072))
    painter.setPen(ring)
    painter.setBrush(Qt.BrushStyle.NoBrush)
    painter.drawEllipse(QPointF(cx, cy), r_out, r_out)

    painter.setPen(Qt.PenStyle.NoPen)
    painter.setBrush(QColor("#ffffff"))
    painter.drawEllipse(QPointF(cx, cy), size * 0.12, size * 0.12)

    painter.setBrush(QColor("#9fc0d6"))
    painter.drawEllipse(QPointF(cx + r_out * 0.55, cy - r_out * 0.55), size * 0.05, size * 0.05)
    painter.end()
    return image


def qimage_to_pil(image: QImage):
    """QImage(ARGB32) -> PIL.Image(RGBA)。"""
    from PIL import Image

    image = image.convertToFormat(QImage.Format.Format_RGBA8888)
    width = image.width()
    height = image.height()
    ptr = image.constBits()
    raw = bytes(ptr)[: width * height * 4]
    return Image.frombuffer("RGBA", (width, height), raw, "raw", "RGBA", 0, 1)


def main() -> int:
    ASSETS_DIR.mkdir(parents=True, exist_ok=True)
    app = QApplication.instance() or QApplication(sys.argv)  # noqa: F841 (需存活以供绘制)

    base = qimage_to_pil(render_brand_icon(256))
    png_path = ASSETS_DIR / "app_icon_256.png"
    base.save(png_path)

    ico_path = ASSETS_DIR / "app.ico"
    base.save(ico_path, format="ICO", sizes=[(s, s) for s in ICO_SIZES])
    print(f"[OK] 生成 {ico_path}  ({', '.join(f'{s}x{s}' for s in ICO_SIZES)})")
    print(f"[OK] 生成 {png_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
