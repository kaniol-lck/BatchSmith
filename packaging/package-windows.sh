#!/usr/bin/env bash
#
# 打包 Windows 产物：BatchSmith-<版本>-windows-x64.zip
#
# 用法: VERSION=0.1.0 packaging/package-windows.sh <build-dir> <out-dir>
#
# 依赖 windeployqt（由 install-qt-action 放进 PATH）与 7z（GitHub Windows runner 自带）。

set -euo pipefail

BUILD_DIR="${1:?用法: VERSION=x.y.z $0 <build-dir> <out-dir>}"
OUT_DIR="${2:?用法: VERSION=x.y.z $0 <build-dir> <out-dir>}"
: "${VERSION:?必须设置 VERSION 环境变量，例如 VERSION=0.1.0}"

# ⚠ 关键：windeployqt 与 7z 都是原生 Windows 程序，不认 Git Bash 的 POSIX 路径。
# 直接传 /tmp/dist/x.exe 会被 MSYS 拼成 \tmp\dist\x.exe（既非 POSIX 也非有效
# Windows 路径），windeployqt 只会回一句 "does not exist"。
# 用 cygpath -m 归一化成「Windows 盘符 + 正斜杠」，MSYS 工具与原生程序都能吃。
if command -v cygpath >/dev/null 2>&1; then
    BUILD_DIR="$(cygpath -m "$BUILD_DIR")"
    OUT_DIR="$(cygpath -m "$OUT_DIR")"
fi
if [[ ! -d "$BUILD_DIR" ]]; then
    echo "构建目录不存在: $BUILD_DIR" >&2
    exit 1
fi

GUI_EXE="$BUILD_DIR/bin/batchsmith.exe"
CLI_EXE="$BUILD_DIR/bin/bs.exe"
[[ -f "$GUI_EXE" ]] || { echo "找不到 $GUI_EXE" >&2; exit 1; }
[[ -f "$CLI_EXE" ]] || { echo "找不到 $CLI_EXE" >&2; exit 1; }

stage="$OUT_DIR/BatchSmith"
rm -rf "$stage"
mkdir -p "$stage"
cp "$GUI_EXE" "$CLI_EXE" "$stage/"

# 精简部署。四个开关都是实测得出的（技术方案 §5.1）：
#   默认部署 56.2MB -> 精简后 27.8MB -> 压缩后 11.7MB
#   --no-opengl-sw         省掉 opengl32sw.dll 19.7MB（Widgets 不走 OpenGL）
#   --no-translations      省掉全量翻译 4.8MB
#   --no-system-d3d-compiler 省掉 D3Dcompiler_47.dll 4.0MB
#   --no-compiler-runtime  省掉 VC 运行时（目标机通常已装；若目标机没有，去掉这一条）
windeployqt \
    --release \
    --no-translations \
    --no-opengl-sw \
    --no-system-d3d-compiler \
    --no-compiler-runtime \
    --dir "$stage" \
    "$stage/batchsmith.exe"

# 随附许可与说明（GPL 分发要求）
cp README.md LICENSE "$stage/" 2>/dev/null || true

archive="$OUT_DIR/BatchSmith-${VERSION}-windows-x64.zip"
rm -f "$archive"

# 打 zip。不假设 7z 一定在 PATH 上（GitHub runner 通常有，本地不一定），
# 找不到就退回 python 的 zipfile —— 它写出的条目用正斜杠，跨平台解压安全。
seven_zip=""
for candidate in 7z 7za 7zz; do
    if command -v "$candidate" >/dev/null 2>&1; then
        seven_zip="$candidate"
        break
    fi
done
if [[ -z "$seven_zip" && -x "/c/Program Files/7-Zip/7z.exe" ]]; then
    seven_zip="/c/Program Files/7-Zip/7z.exe"
fi

if [[ -n "$seven_zip" ]]; then
    ( cd "$OUT_DIR" && "$seven_zip" a -tzip -mx=9 "$archive" BatchSmith >/dev/null )
else
    python_bin="$(command -v python3 || command -v python || true)"
    if [[ -z "$python_bin" ]]; then
        echo "找不到 7z 也找不到 python，无法打 zip" >&2
        exit 1
    fi
    "$python_bin" - "$OUT_DIR" BatchSmith "$archive" <<'PYEOF'
import os, sys, zipfile

parent, entry, archive = sys.argv[1], sys.argv[2], sys.argv[3]
root = os.path.join(parent, entry)
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            full = os.path.join(dirpath, name)
            zf.write(full, os.path.relpath(full, parent))
PYEOF
fi

echo "已生成: $archive"
du -h "$archive" | awk '{print "体积: " $1}'
