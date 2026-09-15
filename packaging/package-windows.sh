#!/usr/bin/env bash
#
# 打包 Windows 产物：BatchSmith-<版本>-windows-x64.zip
#
# 用法: VERSION=0.1.0 packaging/package-windows.sh <build-dir> <out-dir>
#
# 依赖 windeployqt（Qt 自带）与 7z / python（打 zip 用，见文末的回退链）。

set -euo pipefail

BUILD_DIR="${1:?用法: VERSION=x.y.z $0 <build-dir> <out-dir>}"
OUT_DIR="${2:?用法: VERSION=x.y.z $0 <build-dir> <out-dir>}"
: "${VERSION:?必须设置 VERSION 环境变量，例如 VERSION=0.1.0}"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "构建目录不存在: $BUILD_DIR" >&2
    exit 1
fi
mkdir -p "$OUT_DIR"

# ⚠ 顺序要紧：**先转成绝对路径，再做 Windows 化**。
#
# cygpath -m 对相对入参是**原样返回**的（实测 `cygpath -m dist` → `dist`）。
# 若 OUT_DIR 停在相对形态，后面那句 `cd "$OUT_DIR" && 7z … "$archive"` 里
# "$archive" 也是相对的，于是它被再拼一次 cwd ⇒ 压缩包跑到 dist/dist/ 去，
# 而脚本照样打印「已生成」，只有最后那句 du 以 cannot access 收场。
# （实测踩到过，见 .workbuddy/memory 的同日记录。）
make_absolute() { ( cd "$1" && pwd ); }
BUILD_DIR="$(make_absolute "$BUILD_DIR")"
OUT_DIR="$(make_absolute "$OUT_DIR")"

# windeployqt 与 7z 都是原生 Windows 程序，不认 Git Bash 的 POSIX 路径。
# 直接传 /tmp/dist/x.exe 会被 MSYS 拼成 \tmp\dist\x.exe（既非 POSIX 也非有效
# Windows 路径），windeployqt 只会回一句 "does not exist"。
# 用 cygpath -m 归一化成「Windows 盘符 + 正斜杠」，MSYS 工具与原生程序都能吃。
if command -v cygpath >/dev/null 2>&1; then
    BUILD_DIR="$(cygpath -m "$BUILD_DIR")"
    OUT_DIR="$(cygpath -m "$OUT_DIR")"
fi

GUI_EXE="$BUILD_DIR/bin/batchsmith.exe"
CLI_EXE="$BUILD_DIR/bin/bs.exe"
[[ -f "$GUI_EXE" ]] || { echo "找不到 $GUI_EXE" >&2; exit 1; }
[[ -f "$CLI_EXE" ]] || { echo "找不到 $CLI_EXE" >&2; exit 1; }

stage="$OUT_DIR/BatchSmith"
rm -rf "$stage"
mkdir -p "$stage"
cp "$GUI_EXE" "$CLI_EXE" "$stage/"

# 定位 Qt 的部署工具。**不假设它在 PATH 上。**
# 原写法是直接调 `windeployqt`，等于押注 install-qt-action 会把 Qt 的 bin 加进 PATH。
# 那是 CI 内部行为、本机无法复现；一旦它变了，失败点会跑到流程末尾（打包阶段），
# 白白多一轮 CI。而 install-qt-action 确实导出 `QT_ROOT_DIR`（macOS 那次 CI 日志的
# env 段里就有），所以这里优先用它，PATH 只作为回退。
resolve_qt_tool() {   # $1 = 相对 QT_ROOT_DIR 的路径；$2 = 命令名
    local root="${QT_ROOT_DIR:-}"
    if [[ -n "$root" ]]; then
        # QT_ROOT_DIR 在 Windows runner 上是 `D:\a\...` 这种形态，转成 MSYS 可执行的路径
        command -v cygpath >/dev/null 2>&1 && root="$(cygpath -u "$root")"
        if [[ -x "$root/$1" ]]; then
            printf '%s\n' "$root/$1"
            return 0
        fi
    fi
    if command -v "$2" >/dev/null 2>&1; then
        command -v "$2"
        return 0
    fi
    echo "找不到 $2：既不在 \$QT_ROOT_DIR（当前为 '${QT_ROOT_DIR:-未设置}'）下，也不在 PATH 上。" >&2
    return 1
}

WINDEPLOYQT="$(resolve_qt_tool bin/windeployqt.exe windeployqt.exe)" || exit 1
echo "windeployqt: $WINDEPLOYQT"

# 精简部署。四个开关都是实测得出的（技术方案 §5.1）：
#   默认部署 56.2MB -> 精简后 27.8MB -> 压缩后 11.7MB
#   --no-opengl-sw         省掉 opengl32sw.dll 19.7MB（Widgets 不走 OpenGL）
#   --no-translations      省掉全量翻译 4.8MB
#   --no-system-d3d-compiler 省掉 D3Dcompiler_47.dll 4.0MB
#   --no-compiler-runtime  省掉 VC 运行时（目标机通常已装；若目标机没有，去掉这一条）
"$WINDEPLOYQT" \
    --release \
    --no-translations \
    --no-opengl-sw \
    --no-system-d3d-compiler \
    --no-compiler-runtime \
    --dir "$stage" \
    "$stage/batchsmith.exe"

# 随附许可与说明（GPL 分发要求）
cp README.md LICENSE "$stage/" 2>/dev/null || true

# ---------------------------------------------------------------------------
# 帮助手册随包分发
#
#   HTML 手册：三平台都附 —— 用浏览器就能看，离线可用
#   CHM 手册：Windows 原生帮助格式（双击即看、带左侧目录树），
#            由 packaging/make-chm.sh 生成；没生成就跳过，不算失败
#
# 两份内容与界面「帮助」窗口同源（`bs cheatsheet --html / --hhc` 渲染自 core 数据），
# 所以随包手册不会与程序实际支持的语法脱节。
# ---------------------------------------------------------------------------
if [[ -x "$CLI_EXE" ]]; then
    if "$CLI_EXE" cheatsheet --html > "$stage/BatchSmith-帮助手册.html" 2>/dev/null; then
        echo "  已附 HTML 手册"
    else
        rm -f "$stage/BatchSmith-帮助手册.html"
        echo "  警告：HTML 手册生成失败，已跳过" >&2
    fi
fi
for _chm in "$BUILD_DIR"/chm-work/*.chm "$OUT_DIR"/*.chm; do
    if [[ -f "$_chm" ]]; then
        cp "$_chm" "$stage/"
        echo "  已附 CHM 手册：$(basename "$_chm")"
        break
    fi
done

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
