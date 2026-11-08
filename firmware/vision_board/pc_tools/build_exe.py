#!/usr/bin/env python3
"""一键把上位机 (vision_pc_hmi.py) 打包成单文件 exe。

用法：
    python build_exe.py            # 正式：--windowed --onefile，产物 dist/VisionInspect.exe
    python build_exe.py --debug    # 排错：--onedir 且保留控制台，能看到报错/缺依赖

要点：
  * 会把 hmi/assets（横幅图 contest_slogan.png、图标 app.ico）一起打进去；
    界面横幅靠这些资源，漏打会退化成文字兜底。
  * app.ico 缺失时自动调用 gen_app_icon.py 现生成。
  * 配置库/数据集不进包：运行时由 app_paths 写到 exe 旁的 vision_pc_tool/（便携）。

依赖：pyinstaller、pillow、PySide6、numpy、opencv-python-headless。
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENTRY = "vision_pc_hmi.py"
APP_NAME = "VisionInspect"
ICON_REL = "hmi/assets/app.ico"
ASSETS_SRC = "hmi/assets"


def ensure_icon() -> Path:
    icon = HERE / ICON_REL
    if icon.exists():
        return icon
    print("[*] app.ico 不存在，调用 gen_app_icon.py 生成 ...")
    env = dict(os.environ)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    subprocess.run([sys.executable, str(HERE / "gen_app_icon.py")], check=True, cwd=HERE, env=env)
    if not icon.exists():
        raise SystemExit("[!] 生成 app.ico 失败")
    return icon


def build(debug: bool) -> int:
    icon = ensure_icon()
    # --add-data 的分隔符：Windows 用 ';'，*nix 用 ':'（os.pathsep 已对应）。
    add_data = f"{ASSETS_SRC}{os.pathsep}{ASSETS_SRC}"

    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--noconfirm", "--clean",
        "--name", APP_NAME,
        "--icon", str(icon),
        "--add-data", add_data,
        "--paths", ".",
    ]
    if debug:
        # 排错模式：目录形式 + 保留控制台，能直接看到 ImportError / 资源加载失败
        cmd += ["--onedir", "--console"]
    else:
        cmd += ["--onefile", "--windowed"]
    cmd.append(ENTRY)

    print("[*] 运行:", " ".join(cmd))
    result = subprocess.run(cmd, cwd=HERE)
    if result.returncode == 0:
        out = HERE / "dist" / (APP_NAME if debug else f"{APP_NAME}.exe")
        print(f"\n[OK] 打包完成 -> {out}")
        print("     首次运行会在 exe 旁生成 vision_pc_tool/（配置库/数据集，便携）。")
    else:
        print(f"\n[!] 打包失败，返回码 {result.returncode}")
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Package the Vision Board HMI into an exe.")
    parser.add_argument("--debug", action="store_true", help="onedir + 控制台，用于排错")
    args = parser.parse_args()
    return build(args.debug)


if __name__ == "__main__":
    raise SystemExit(main())
