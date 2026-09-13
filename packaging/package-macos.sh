#!/usr/bin/env bash
#
# 打包 macOS 产物：BatchSmith-<版本>-macos-universal.dmg
#
# 用法: VERSION=0.1.0 packaging/package-macos.sh <build-dir> <out-dir>
#
# 依赖 macdeployqt（由 install-qt-action 放进 PATH）。
#
# ⚠ 这里产出的是**未签名**的 dmg。Gatekeeper 会拦，用户需要右键打开或
#    `xattr -dr com.apple.quarantine /Applications/BatchSmith.app`。
#   公开分发要另配 codesign + notarytool（见 docs/项目阶段规划.md 的 Phase 8）。

set -euo pipefail

BUILD_DIR="${1:?用法: VERSION=x.y.z $0 <build-dir> <out-dir>}"
OUT_DIR="${2:?用法: VERSION=x.y.z $0 <build-dir> <out-dir>}"
: "${VERSION:?必须设置 VERSION 环境变量，例如 VERSION=0.1.0}"

APP="$BUILD_DIR/bin/batchsmith.app"
CLI_BIN="$BUILD_DIR/bin/bs"
[[ -d "$APP" ]] || { echo "找不到 $APP" >&2; exit 1; }
[[ -f "$CLI_BIN" ]] || { echo "找不到 $CLI_BIN" >&2; exit 1; }

stage="$OUT_DIR/BatchSmith"
rm -rf "$stage"
mkdir -p "$stage"

# 先拷出来再 deploy，保持构建目录干净
cp -R "$APP" "$stage/"
cp "$CLI_BIN" "$stage/bs"          # CLI 不作 .app，直接放在 dmg 根下
cp README.md LICENSE "$stage/" 2>/dev/null || true

macdeployqt "$stage/batchsmith.app" -always-overwrite

dmg="$OUT_DIR/BatchSmith-${VERSION}-macos-universal.dmg"
rm -f "$dmg"
hdiutil create \
    -volname "BatchSmith" \
    -srcfolder "$stage" \
    -ov -format UDZO \
    "$dmg" >/dev/null

echo "已生成: $dmg"
du -h "$dmg" | awk '{print "体积: " $1}'

# 顺带确认二进制确实是 universal
echo "架构检查:"
lipo -archs "$stage/batchsmith.app/Contents/MacOS/batchsmith" || true
