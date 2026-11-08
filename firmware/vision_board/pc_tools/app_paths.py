"""便携式应用数据目录解析，为打包exe准备。"""

from __future__ import annotations

import sys
from pathlib import Path

APP_DIR_NAME = "vision_pc_tool"


def is_frozen() -> bool:
    """是否运行在 PyInstaller / cx_Freeze 等打包后的 exe 环境下。"""
    return getattr(sys, "frozen", False)


def get_exe_dir() -> Path:
    """exe所在目录（打包后）或pc_tools目录（开发时）。"""
    if is_frozen():
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def _is_writable(path: Path) -> bool:
    test = path / ".writable_test"
    try:
        test.write_text("ok", encoding="utf-8")
        test.unlink()
        return True
    except (PermissionError, OSError):
        return False


def get_app_data_dir() -> Path:
    """便携式优先：exe旁边/vision_pc_tool；不可写则退化到用户目录。"""
    exe_dir = get_exe_dir()
    portable = exe_dir / APP_DIR_NAME
    try:
        portable.mkdir(parents=True, exist_ok=True)
        if _is_writable(portable):
            return portable
    except (PermissionError, OSError):
        pass
    fallback = Path.home() / f".{APP_DIR_NAME}"
    fallback.mkdir(parents=True, exist_ok=True)
    return fallback


def get_pc_vision_configs_dir() -> Path:
    """PC视觉配置库根目录，按模型名组织子目录。"""
    d = get_app_data_dir() / "pc_vision_configs"
    d.mkdir(parents=True, exist_ok=True)
    return d


def get_datasets_root() -> Path:
    """数据集PNG输出根目录（便携式优先）。"""
    d = get_app_data_dir() / "datasets"
    d.mkdir(parents=True, exist_ok=True)
    return d


def get_recent_record_path() -> Path:
    """最近操作记录文件。"""
    return get_app_data_dir() / "recent.json"


def get_pc_vision_config_dir(model_name: str) -> Path:
    """某个模型名对应的PC视觉配置目录。"""
    return get_pc_vision_configs_dir() / model_name
