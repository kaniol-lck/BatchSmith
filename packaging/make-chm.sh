#!/usr/bin/env bash
# ===========================================================================
# 生成 CHM 帮助手册（Windows 版的原生帮助文件）。
#
# 内容**全部来自 core**：`bs cheatsheet --html` 与 `--hhc` 分别给出正文与目录树，
# 所以 CHM 的目录、界面「帮助」窗口的目录、`bs cheatsheet` 的章节三者永远一致。
#
# 用法：
#   VERSION=0.3.0 bash packaging/make-chm.sh <build-dir> <out-dir>
#
# 需要 hhc.exe（见 packaging/fetch-hhc.sh），位置：
#   $CHM_HHC  >  <build-dir>/chm/tools/hhc.exe  >  PATH
# ===========================================================================
set -euo pipefail

BUILD_DIR="${1:?用法: VERSION=x.y.z $0 <build-dir> <out-dir>}"
OUT_DIR="${2:?用法: VERSION=x.y.z $0 <build-dir> <out-dir>}"
: "${VERSION:?必须设置 VERSION 环境变量，例如 VERSION=0.3.0}"

log() { printf '  %s\n' "$*"; }
die() { printf '错误：%s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 找 bs 与 hhc
# ---------------------------------------------------------------------------
BS=""
for candidate in "$BUILD_DIR/bin/bs.exe" "$BUILD_DIR/bin/bs"; do
    [ -x "$candidate" ] && BS="$candidate" && break
done
[ -n "$BS" ] || die "找不到 bs 可执行文件（在 $BUILD_DIR/bin/ 下）"

HHC="${CHM_HHC:-}"
if [ -z "$HHC" ]; then
    # hhc 是"工具"不是"构建产物"，所以规范位置在仓库根的 build/chm/tools/
    # （与具体构建目录无关），fetch-hhc.sh 默认就放在那里。
    REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    for candidate in "$REPO_ROOT/build/chm/tools/hhc.exe" \
                     "$BUILD_DIR/chm/tools/hhc.exe"; do
        [ -x "$candidate" ] && HHC="$candidate" && break
    done
fi
if [ -z "$HHC" ]; then
    command -v hhc.exe >/dev/null 2>&1 && HHC="$(command -v hhc.exe)"
fi
if [ -z "$HHC" ] || [ ! -x "$HHC" ]; then
    die "找不到 hhc.exe。先跑：bash packaging/fetch-hhc.sh（会自动下载并解出它），
     或用 CHM_HHC=/path/to/hhc.exe 指定。"
fi
log "bs : $BS"
log "hhc: $HHC"

# ---------------------------------------------------------------------------
# 工作目录
#
# ⚠️ 必须避开**以点开头的路径段**（如 .workbuddy/…）：hhc.exe 会把 `.xxx`
#    当成相对路径段处理并把它吞掉，于是输出路径变成同级的另一处，报
#    「HHC5010: Cannot open <输出>.chm」。这是实测踩到的坑，别把目录改回去。
# ---------------------------------------------------------------------------
WORK_DIR="$BUILD_DIR/chm-work"
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR" "$OUT_DIR"

log "生成正文与目录树（都来自 core）…"
"$BS" cheatsheet --html > "$WORK_DIR/index.html" 2>/dev/null
"$BS" cheatsheet --hhc  > "$WORK_DIR/help.hhc"   2>/dev/null

# ---------------------------------------------------------------------------
# .hhc 的编码
#
# HTML Help 按系统 ANSI 解释 .hhc，所以默认转成 GBK —— 否则中文目录标题在
# CHM 左侧树里可能显示成乱码。设 CHM_HHC_ENCODING=keep 可保留 UTF-8。
# （这一点我无法在无桌面环境里目视验证，所以给了开关并写在文档里。）
# ---------------------------------------------------------------------------
if [ "${CHM_HHC_ENCODING:-gbk}" != "keep" ] && command -v iconv >/dev/null 2>&1; then
    if iconv -f UTF-8 -t GBK "$WORK_DIR/help.hhc" > "$WORK_DIR/help.hhc.gbk" 2>/dev/null; then
        mv "$WORK_DIR/help.hhc.gbk" "$WORK_DIR/help.hhc"
        log ".hhc 已转成 GBK（CHM_HHC_ENCODING=keep 可保留 UTF-8）"
    else
        log "⚠ 转 GBK 失败，保留 UTF-8"
    fi
fi

cat > "$WORK_DIR/help.hhp" <<HHP
[OPTIONS]
Compatibility=1.1 or later
Compiled file=BatchSmith-${VERSION}-help.chm
Contents file=help.hhc
Default topic=index.html
Display compile progress=No
Language=0x804 中文(简体，中国)
Full-text search=No

[FILES]
index.html

[INFOTYPES]
HHP
# 关于 Full-text search=No：搜索标签页依赖 Itircl.dll，而它在现代 Windows 上
# 通常没有注册（hhc 会报 HHC6003），此时正文照样能编译、CHM 也能正常打开，
# 只是没有"搜索"标签。想启用就先注册运行时组件（需管理员权限），
# 或把这一行改成 Yes 并忽略该警告。

log "编译 CHM…"
( cd "$WORK_DIR" && "$HHC" help.hhp ) > "$WORK_DIR/hhc.log" 2>&1 || true
# ⚠️ 判据不是 hhc 的退出码：它即使报 HHC6003（搜索索引建不了）也返回 0，
#    所以这里直接看"产物是否真的生成了、且是 CHM"。
CHM="$WORK_DIR/BatchSmith-${VERSION}-help.chm"
[ -s "$CHM" ] || { sed 's/^/    /' "$WORK_DIR/hhc.log" | head -20; die "hhc 没有产出 CHM"; }

if [ "$(head -c 4 "$CHM")" != "ITSF" ]; then
    die "产出的文件不是有效的 CHM（缺少 ITSF 头）"
fi

if grep -q 'HHC6003' "$WORK_DIR/hhc.log" 2>/dev/null; then
    log "注：hhc 报了 HHC6003（Itircl.dll 未注册）—— 正文已完整编译，只是没有搜索标签页"
fi

cp "$CHM" "$OUT_DIR/"
log "已生成：$OUT_DIR/$(basename "$CHM")  ($(stat -c%s "$CHM") 字节)"
