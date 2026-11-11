#!/usr/bin/env bash
# =====================================================================
# armbuild.sh —— Ubuntu 集成机：交叉编译 + ABI 校验一条龙
#
# 用法（在仓库根目录或任意目录均可）：
#   bash tools/armbuild.sh
#   bash tools/armbuild.sh /usr/local/arm/5.4.0/usr/bin/arm-linux-
#
# 环境变量：
#   CROSS        工具链前缀；不指定则按 arm-linux- -> arm-none-linux-gnueabi-
#                -> arm-linux-gnueabihf- 的顺序自动探测
#   ALSA_CFLAGS  可选，sysroot 缺 ALSA 头时指定，如 -I$HOME/alsa/usr/include
#   ALSA_LIBS    可选，默认 -lasound；自带库可用 -L... -l:libasound.so.2
#
# 退出码：0 成功；1 编译/校验失败；2 工具链或依赖缺失。
# 详见 ubuntuArmBuild.md。
# =====================================================================
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

say() { printf '\n== %s ==\n' "$*"; }
die() { printf '\n[FAIL] %s\n' "$*" >&2; exit 2; }

CROSS=${1:-${CROSS:-}}
if [ -z "$CROSS" ]; then
    for p in arm-linux- arm-none-linux-gnueabi- arm-linux-gnueabihf-; do
        if command -v "${p}gcc" >/dev/null 2>&1; then CROSS="$p"; break; fi
    done
fi
[ -n "$CROSS" ] || die \
"找不到交叉编译器。试过 arm-linux-gcc / arm-none-linux-gnueabi-gcc / arm-linux-gnueabihf-gcc。
  ① 把工具链 bin 目录加入 PATH；
  ② 或显式传前缀：bash tools/armbuild.sh /usr/local/arm/5.4.0/usr/bin/arm-linux-
  ③ 安装方式见 ubuntuArmBuild.md §3。"

case "$CROSS" in
    *-) ;;
    *)  CROSS="${CROSS}-" ;;
esac

# ---------- 0. 工具链 ----------
say "0. 工具链"
command -v "${CROSS}gcc" >/dev/null 2>&1 || die \
"找不到 ${CROSS}gcc。
  ① 确认工具链已解压并把 bin 目录加入 PATH；
  ② 或把完整前缀作为参数：bash tools/armbuild.sh /usr/local/arm/5.4.0/usr/bin/arm-linux-
  ③ 安装方式见 ubuntuArmBuild.md §3。"

"${CROSS}gcc" --version | head -n 1
TARGET_TRIPLE=$("${CROSS}gcc" -dumpmachine)
echo "目标三元组：${TARGET_TRIPLE}"
case "$TARGET_TRIPLE" in
    arm-linux-gnueabihf)
        echo "ABI：硬浮点（需板端 ld-linux-armhf.so.3）" ;;
    arm-none-linux-gnueabi|arm-linux-gnueabi)
        echo "ABI：软浮点（需板端 ld-linux.so.3；本板实测就是这种）" ;;
    *)
        say "警告：未知三元组 ${TARGET_TRIPLE}，务必与板端 /lib/ld-linux* 对照" ;;
esac

command -v make >/dev/null 2>&1 || die \
"找不到 make。apt 不可用时先确认镜像是否预装；否则需要离线 .deb 安装。"

# readelf：优先工具链自带；粤嵌 5.4.0 可能没有 arm-linux-readelf，
# 此时退回宿主 readelf（binutils 能直接读 ARM ELF，不影响校验结论）。
if command -v "${CROSS}readelf" >/dev/null 2>&1; then
    READELF="${CROSS}readelf"
elif command -v readelf >/dev/null 2>&1; then
    READELF="readelf"
else
    die "找不到 readelf：装 binutils，或确认工具链 bin 目录里有 arm-linux-readelf。"
fi
echo "readelf：${READELF}"

SYSROOT=$("${CROSS}gcc" -print-sysroot 2>/dev/null || true)
if [ -n "$SYSROOT" ] && [ "$SYSROOT" != "/" ]; then
    echo "sysroot：${SYSROOT}"
    if [ -f "${SYSROOT}/usr/include/alsa/asoundlib.h" ]; then
        echo "sysroot 内 ALSA 头：找到"
    else
        # 一些厂商工具链把 sysroot 放在 <prefix>/usr/<triple>/libc
        GCC_PATH=$(command -v "${CROSS}gcc")
        PREFIX_DIR=$(cd "$(dirname "$GCC_PATH")/.." 2>/dev/null && pwd || true)
        ALT=""
        if [ -n "$PREFIX_DIR" ]; then
            ALT=$(find "$PREFIX_DIR" -name asoundlib.h 2>/dev/null | head -n 1 || true)
        fi
        if [ -n "$ALT" ]; then
            echo "sysroot 内未找到，但工具链目录里有：${ALT}"
        else
            echo "sysroot 内 ALSA 头：未找到（见 §4.3.2 的 alsa-lib 方案）"
        fi
    fi
else
    echo "sysroot：工具链未单独提供（使用系统默认，Ubuntu 交叉包常见）"
fi

# ---------- 1. ALSA 头预检 ----------
say "1. ALSA 头文件预检"
ALSA_CFLAGS=${ALSA_CFLAGS:-}
# shellcheck disable=SC2086
if printf '#include <alsa/asoundlib.h>\nint main(void){return 0;}\n' \
        | "${CROSS}gcc" ${ALSA_CFLAGS} -x c -E - >/dev/null 2>&1; then
    echo "ALSA 头可用。"
else
    cat <<'EOF'
[缺少 alsa/asoundlib.h]
  本板实测：头文件和 libasound.so.2 都没有（boardGuide.md §3.3.1）。
  推荐：用同一套 5.4.0 工具链交叉编 alsa-lib（ubuntuArmBuild.md §4.3.2）：
      tar xf alsa-lib-1.1.3.tar.bz2 -C ~ && cd ~/alsa-lib-1.1.3
      CC=arm-linux-gcc AR=arm-linux-ar RANLIB=arm-linux-ranlib \
        ./configure --host=arm-none-linux-gnueabi --prefix=/usr \
                    --disable-static --disable-python
      make -j2 && make install DESTDIR=$PWD/install
  然后：
      ALSA_CFLAGS="-I$HOME/alsa-lib-1.1.3/install/usr/include" \
      ALSA_LIBS="-L$HOME/alsa-lib-1.1.3/install/usr/lib -l:libasound.so.2" \
      bash tools/armbuild.sh
  备选：Debian armel（软浮点）libasound2 预编译包，见 §4.3.3。
EOF
    exit 2
fi

# ---------- 2. 交叉编译 ----------
say "2. 清理并交叉编译（BOARD=1）"
make clean
make BOARD=1 CROSS="$CROSS" READELF="$READELF" 2>&1 | tee /tmp/gesture_audio_build.log

# ---------- 3. ABI / 依赖校验 ----------
say "3. 产物校验"
make BOARD=1 CROSS="$CROSS" READELF="$READELF" verify

# ---------- 4. 结果 ----------
say "4. 结果"
ls -lh build/gesture_audio
file build/gesture_audio 2>/dev/null || true

# 硬性检查：必须是 ARM 且带 ARM 解释器
"$READELF" -h build/gesture_audio | grep -q 'Machine:.*ARM' \
    || die "产物不是 ARM，检查 CROSS 是否指到了宿主 gcc"
echo "Machine: ARM —— OK"

# 解释器必须与板端 /lib/ld-linux* 一致：本板是 ld-linux.so.3（软浮点）
INTERP=$("$READELF" -l build/gesture_audio | grep -o 'ld-linux[^]]*\.so\.[0-9]*' | head -n 1 || true)
if [ -n "$INTERP" ]; then
    echo "解释器：${INTERP}"
    case "$INTERP" in
        ld-linux-armhf.so.3)
            echo "警告：产物是硬浮点解释器；本板是软浮点 ld-linux.so.3，产物可能跑不起来" ;;
        ld-linux.so.3)
            echo "软浮点解释器，与本板一致 —— OK" ;;
    esac
else
    echo "警告：未从 ELF 中读到 PT_INTERP，确认是否静态链接"
fi

cat <<'EOF'

完成。下一步：
  1) 板端缺 libasound（见 boardGuide.md §3.3.1）：把交叉编出的 libasound.so.2 一起部署
  2) 拷到 NFS 共享目录：cp build/gesture_audio ~/nfs/
  3) 板端跑自检：/mnt/nfs/gesture_audio --selftest /mnt/nfs/audio/test_44100.wav
  4) 板端正式运行：/mnt/nfs/gesture_audio --sensor sim --log info /mnt/nfs/audio/test_44100.wav
EOF
