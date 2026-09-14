#!/usr/bin/env bash
#
# 打包 Linux 产物：BatchSmith-<版本>-linux-x86_64.tar.gz
#
# 用法: VERSION=0.1.0 QT_ROOT_DIR=/path/to/qt packaging/package-linux.sh <build-dir> <out-dir>
#
# 做法：不依赖 linuxdeployqt / linuxdeploy（两者都要额外下载 AppImage，且在本机
# 无法预先验证），而是用 ldd 把 Qt 相关的动态库捞出来随包分发，配一个设置
# LD_LIBRARY_PATH / QT_PLUGIN_PATH 的启动脚本。
# 好处是零外部依赖、行为可推导；代价是没有 AppImage 那样单文件。
# 产出 AppImage 列为 P8 的改进项。

set -euo pipefail

BUILD_DIR="${1:?用法: VERSION=x.y.z $0 <build-dir> <out-dir>}"
OUT_DIR="${2:?用法: VERSION=x.y.z $0 <build-dir> <out-dir>}"
: "${VERSION:?必须设置 VERSION 环境变量，例如 VERSION=0.1.0}"
: "${QT_ROOT_DIR:?必须设置 QT_ROOT_DIR（Qt 安装前缀），例如 /opt/Qt/6.7.2/gcc_64}"

GUI_BIN="$BUILD_DIR/bin/batchsmith"
CLI_BIN="$BUILD_DIR/bin/bs"
[[ -f "$GUI_BIN" ]] || { echo "找不到 $GUI_BIN" >&2; exit 1; }
[[ -f "$CLI_BIN" ]] || { echo "找不到 $CLI_BIN" >&2; exit 1; }

stage="$OUT_DIR/BatchSmith"
rm -rf "$stage"
mkdir -p "$stage/bin" "$stage/lib" "$stage/plugins/platforms" "$stage/plugins/imageformats"

cp "$GUI_BIN" "$CLI_BIN" "$stage/bin/"
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
if [[ -x "$CLI_BIN" ]]; then
    if "$CLI_BIN" cheatsheet --html > "$stage/BatchSmith-帮助手册.html" 2>/dev/null; then
        echo "  已附 HTML 手册"
    else
        rm -f "$stage/BatchSmith-帮助手册.html"
        echo "  警告：HTML 手册生成失败，已跳过" >&2
    fi
fi
for _chm in "$BUILD_DIR"/chm-work/*.chm; do
    if [[ -f "$_chm" ]]; then
        cp "$_chm" "$stage/"
        echo "  已附 CHM 手册：$(basename "$_chm")"
        break
    fi
done

# Qt 插件：平台插件缺了整个程序起不来，图片插件缺了图标静默失效
cp -f "$QT_ROOT_DIR"/plugins/platforms/*.so "$stage/plugins/platforms/" 2>/dev/null || true
cp -f "$QT_ROOT_DIR"/plugins/imageformats/*.so "$stage/plugins/imageformats/" 2>/dev/null || true

# 用 ldd 收集所有落在 Qt 安装目录下的动态库（含传递依赖）。
# 用 case 做前缀匹配而不是 grep 正则 —— Qt 路径里可能有 .
collect_qt_libs() {
    local target
    for target in "$@"; do
        [[ -e "$target" ]] || continue
        ldd "$target" 2>/dev/null | awk '$2 == "=>" { print $3 }' | while IFS= read -r lib; do
            case "$lib" in
                "$QT_ROOT_DIR"/*) cp -n "$lib" "$stage/lib/" ;;
            esac
        done
    done
}
collect_qt_libs "$stage/bin/batchsmith" "$stage/bin/bs" \
                "$stage/plugins/platforms/"*.so "$stage/plugins/imageformats/"*.so

# 启动脚本：Qt 的 rpath 指向构建机的安装前缀，目标机上不一定存在，
# 所以用 LD_LIBRARY_PATH 显式指到随包分发的 lib/。
make_launcher() {
    local name="$1" target="$2"
    cat > "$stage/$name" <<LAUNCH
#!/usr/bin/env bash
here="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="\$here/lib:\${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="\$here/plugins"
exec "\$here/bin/$target" "\$@"
LAUNCH
    chmod +x "$stage/$name"
}
make_launcher batchsmith.sh batchsmith
make_launcher bs.sh bs

cat > "$stage/README-运行说明.txt" <<'NOTE'
运行方式：

  ./batchsmith.sh     图形界面
  ./bs.sh --help      命令行

两个脚本都会把 LD_LIBRARY_PATH 与 QT_PLUGIN_PATH 指到包内的 lib/ 与 plugins/，
因此不要求目标机安装 Qt（但需要系统有 libxcb / libxkbcommon / libGL 等基础库）。

若启动时报缺少 xcb 相关库，按发行版安装：
  Debian/Ubuntu: sudo apt install libxcb-cursor0 libxkbcommon-x11-0 libgl1
NOTE

archive="$OUT_DIR/BatchSmith-${VERSION}-linux-x86_64.tar.gz"
rm -f "$archive"
tar -C "$OUT_DIR" -czf "$archive" BatchSmith

echo "已生成: $archive"
du -h "$archive" | awk '{print "体积: " $1}'
