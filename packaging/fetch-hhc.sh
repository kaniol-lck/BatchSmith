#!/usr/bin/env bash
# ===========================================================================
# 获取 CHM 编译器 hhc.exe —— 不运行安装器、不写注册表、不需要管理员权限。
#
# 背景（2026-09-15 实测）：
#   * 微软的 HTML Help Workshop 官方下载已下线（原 URL 现在返回 404）；
#     winget / MSYS2 仓库里都没有它。
#   * 但那个安装包本身是「自解压 CAB」结构（PE stub + MSCF），
#     所以可以绕开安装器，直接从 CAB 里取我们需要的三个文件：
#       hhc.exe（编译器，51KB）、itcc.dll（LZX 压缩，154KB）、hha.dll（编译核心，838KB）
#
# 这样做的好处：不动系统、不需要管理员、可重复、CI 上也能跑。
#
# 用法：
#   bash packaging/fetch-hhc.sh [目标目录]      # 默认 build/chm/tools
# ===========================================================================
set -euo pipefail

OUT_DIR="${1:-build/chm/tools}"
MIRROR="${HHW_URL:-https://www.help-info.de/files_download/htmlhelp.exe}"
WORK_DIR="${OUT_DIR%/}/../_download"

log() { printf '  %s\n' "$*"; }
die() { printf '错误：%s\n' "$*" >&2; exit 1; }

# 找 7z：本机它在 Program Files 下，不一定在 PATH 上
find_7z() {
    for candidate in 7z 7za 7zr \
        "/c/Program Files/7-Zip/7z.exe" \
        "/c/Program Files (x86)/7-Zip/7z.exe"; do
        if command -v "$candidate" >/dev/null 2>&1; then
            printf '%s' "$candidate"
            return 0
        fi
    done
    return 1
}

SEVEN_ZIP="$(find_7z)" || die "找不到 7z（7-Zip）。装一个 7-Zip 再来：https://www.7-zip.org/"

if [ -x "$OUT_DIR/hhc.exe" ]; then
    log "hhc.exe 已存在：$OUT_DIR/hhc.exe"
    exit 0
fi

mkdir -p "$OUT_DIR" "$WORK_DIR"

# ---------------------------------------------------------------------------
# 1) 下载安装包（它是自解压 CAB）
# ---------------------------------------------------------------------------
INSTALLER="$WORK_DIR/htmlhelp.exe"
if [ ! -s "$INSTALLER" ] || [ "$(stat -c%s "$INSTALLER")" -lt 1000000 ]; then
    log "下载 HTML Help Workshop 安装包（约 3.3MB）…"
    # 先直连再走环境代理：本机直连通常更快，CI 上则无代理
    if ! curl -fsSL --noproxy '*' --max-time 180 -o "$INSTALLER" "$MIRROR"; then
        log "直连失败，改用环境代理重试"
        curl -fsSL --max-time 180 -o "$INSTALLER" "$MIRROR" \
            || die "下载失败。可手动下载后放到 $INSTALLER 再重跑本脚本：$MIRROR"
    fi
fi
log "安装包：$(stat -c%s "$INSTALLER") 字节"

# ---------------------------------------------------------------------------
# 2) 跳过 PE stub，找出内层 CAB
#
# 文件里会出现多个 MSCF 字样，只有真正是 CAB 头的那一个能被 7z 打开，
# 所以逐个偏移试 —— 比猜结构可靠。
# ---------------------------------------------------------------------------
CAB="$WORK_DIR/inner.cab"
need_cab=1
if [ -s "$CAB" ] && "$SEVEN_ZIP" l "$CAB" >/dev/null 2>&1; then
    need_cab=0
fi

if [ "$need_cab" -eq 1 ]; then
    log "在安装包里定位内层 CAB…"
    found=0
    while IFS=: read -r offset _rest; do
        [ -n "$offset" ] || continue
        tail -c "+$((offset + 1))" "$INSTALLER" > "$CAB" 2>/dev/null || continue
        if "$SEVEN_ZIP" l "$CAB" 2>/dev/null | grep -q 'hhc\.exe'; then
            log "命中 offset=$offset"
            found=1
            break
        fi
    done < <(grep -abo 'MSCF' "$INSTALLER" 2>/dev/null || true)
    [ "$found" -eq 1 ] || die "在这个安装包里没找到含 hhc.exe 的 CAB。镜像内容可能变了：$MIRROR"
fi

# ---------------------------------------------------------------------------
# 3) 取出需要的三个文件（e = 解到单层目录，正是我们要的）
# ---------------------------------------------------------------------------
log "提取 hhc.exe / itcc.dll / hha.dll …"
"$SEVEN_ZIP" e "$CAB" -o"$OUT_DIR" hhc.exe itcc.dll hha.dll -y > /dev/null \
    || die "从 CAB 提取失败"

for file in hhc.exe itcc.dll hha.dll; do
    [ -s "$OUT_DIR/$file" ] || die "缺少 $file —— 提取没成功"
done

log "完成：$OUT_DIR"
ls -la "$OUT_DIR" | grep -E 'hhc\.exe|itcc\.dll|hha\.dll' | awk '{printf "    %-12s %8s 字节\n", $NF, $5}'
