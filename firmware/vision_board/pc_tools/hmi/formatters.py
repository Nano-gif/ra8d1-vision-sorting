"""Formatting helpers for Vision Board HMI status fields."""

from __future__ import annotations

from typing import Dict


def int_field(fields: Dict[str, str], name: str, default: int = 0) -> int:
    try:
        return int(fields.get(name, str(default)), 0)
    except (TypeError, ValueError):
        return default


def text_field(fields: Dict[str, str], name: str, default: str = "-") -> str:
    value = fields.get(name)
    if value is None or value == "":
        return default
    return value


def bool_text(value: int) -> str:
    return "ON" if value else "OFF"


def result_text(fields: Dict[str, str]) -> str:
    code = text_field(fields, "result", "2")
    if code == "0":
        return "OK"
    if code == "1":
        return "NG"
    if code == "2":
        return "IDLE"
    return code


def score_text(fields: Dict[str, str]) -> str:
    score = int_field(fields, "score")
    threshold = int_field(fields, "threshold")
    return f"{score / 10.0:.1f} / {threshold / 10.0:.1f}"
