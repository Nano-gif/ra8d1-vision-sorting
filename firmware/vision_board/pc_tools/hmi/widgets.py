"""Reusable Qt widgets for the Vision Board HMI."""

from __future__ import annotations

from typing import Dict, Optional

from PySide6.QtCore import Qt
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import QFrame, QGridLayout, QGroupBox, QLabel, QSizePolicy, QVBoxLayout


class StatusCard(QFrame):
    def __init__(self, title: str, value: str = "-", subtitle: str = "") -> None:
        super().__init__()
        self.setObjectName("StatusCard")
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)

        self.title_label = QLabel(title)
        self.title_label.setObjectName("CardTitle")
        self.value_label = QLabel(value)
        self.value_label.setObjectName("CardValue")
        self.subtitle_label = QLabel(subtitle)
        self.subtitle_label.setObjectName("CardSub")

        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 12, 16, 12)
        layout.setSpacing(5)
        layout.addWidget(self.title_label)
        layout.addWidget(self.value_label)
        layout.addWidget(self.subtitle_label)

    def set_value(self, value: str, subtitle: str = "") -> None:
        self.value_label.setText(value)
        self.subtitle_label.setText(subtitle)


class FieldGroup(QGroupBox):
    def __init__(self, title: str, fields: list[tuple[str, str]]) -> None:
        super().__init__(title)
        self.value_labels: Dict[str, QLabel] = {}
        layout = QGridLayout(self)
        layout.setHorizontalSpacing(20)
        layout.setVerticalSpacing(9)
        layout.setContentsMargins(16, 20, 16, 16)

        for row, (key, label_text) in enumerate(fields):
            name_label = QLabel(label_text)
            name_label.setObjectName("FieldName")
            name_label.setMinimumWidth(118)
            name_label.setMinimumHeight(24)
            value_label = QLabel("-")
            value_label.setObjectName("FieldValue")
            value_label.setMinimumHeight(24)
            value_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            self.value_labels[key] = value_label
            layout.addWidget(name_label, row, 0)
            layout.addWidget(value_label, row, 1)

        layout.setColumnStretch(1, 1)

    def set_field(self, key: str, value: object) -> None:
        label = self.value_labels.get(key)
        if label is not None:
            label.setText(str(value))

    def set_fields(self, values: Dict[str, object]) -> None:
        for key, value in values.items():
            self.set_field(key, value)


class ImagePlaceholder(QFrame):
    """Image display slot that falls back to placeholder text.

    Keeps a title label so the slot still reads as a labeled panel when no image
    has been loaded. ``set_pixmap`` swaps in a real image scaled to the slot;
    ``clear`` restores the placeholder text.
    """

    def __init__(self, title: str) -> None:
        super().__init__()
        self.setObjectName("ImagePlaceholder")
        self.setMinimumHeight(260)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        self._title_text = title
        self._title_label = QLabel(title)
        self._title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._title_label.setObjectName("ImagePlaceholderText")

        self._image_label = QLabel()
        self._image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._image_label.setObjectName("ImagePlaceholderImage")
        self._image_label.hide()

        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.addWidget(self._title_label, alignment=Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self._image_label, 1)

    def set_pixmap(self, pixmap: Optional[QPixmap]) -> None:
        """Display a pixmap scaled to the current slot size.

        Passing ``None`` or an empty pixmap restores the placeholder text.
        """
        if pixmap is None or pixmap.isNull():
            self.clear()
            return
        self._title_label.hide()
        self._image_label.show()
        # Scale to the current widget area while preserving aspect ratio.
        margins = self.contentsMargins()
        max_w = max(1, self.width() - margins.left() - margins.right())
        max_h = max(1, self.height() - margins.top() - margins.bottom())
        if max_w < 32 or max_h < 32:
            self._image_label.setPixmap(pixmap)
            return
        scaled = pixmap.scaled(
            max_w,
            max_h,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        self._image_label.setPixmap(scaled)

    def clear(self) -> None:
        """Restore the placeholder text and drop any cached pixmap."""
        self._image_label.clear()
        self._image_label.hide()
        self._title_label.setText(self._title_text)
        self._title_label.show()
