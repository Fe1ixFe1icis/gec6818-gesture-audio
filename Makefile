# =====================================================================
# gec6818-gesture-audio —— 构建入口
#
#   板端交叉编译：  make BOARD=1 CROSS=arm-linux-gnueabihf-
#   主机自测：      make                      （BOARD=0，模拟后端）
#   跑自检：        make selftest
#   生成测试素材：  make tools && ./build/gen_wav test_44100.wav 8
#
# 说明：
#   - BOARD=1 时链接 ALSA；BOARD=0 时不依赖任何硬件库，便于在无板环境
#     验证「采集 → 仲裁 → 播放 → 显示 → 网络」全链路。
#   - 交叉编译器前缀必须与板上 ABI 一致。本板实测软浮点
#     （/lib/ld-linux.so.3 + glibc 2.23）→ 用 arm-linux-gcc（arm-none-linux-gnueabi）；
#     用 gnueabihf 编出的程序会要求 /lib/ld-linux-armhf.so.3，板上报
#     "No such file or directory"，见 boardGuide.md §2.5。
# =====================================================================
CROSS   ?=
BOARD   ?= 0
BUILD   := build
HOSTCC  ?= cc

CC      := $(CROSS)gcc
READELF ?= $(CROSS)readelf
SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
TARGET  := $(BUILD)/gesture_audio

CFLAGS  := -std=gnu99 -O2 -g -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -Iinclude -DBOARD=$(BOARD) -D_GNU_SOURCE -pthread
LDFLAGS := -pthread
LDLIBS  := -lm
AQLDLIBS := -lm       # 离线音质工具（audio_quality）的链接库

# ALSA 头文件/库的位置。默认走交叉工具链的 sysroot（最省事）；
# 若工具链 sysroot 缺 ALSA 开发包，可在命令行覆盖，把板端库和头文件指进来：
#   make BOARD=1 CROSS=arm-linux-gnueabihf- \
#        ALSA_CFLAGS="-I$HOME/alsa/usr/include" \
#        ALSA_LIBS="-L$HOME/alsa/usr/lib/arm-linux-gnueabihf -l:libasound.so.2"
# 详见 ubuntuArmBuild.md §4。
ALSA_CFLAGS ?=
ALSA_LIBS   ?= -lasound

ifeq ($(BOARD),1)
CFLAGS  += $(ALSA_CFLAGS)
LDLIBS  += $(ALSA_LIBS)
endif

ifeq ($(OS),Windows_NT)
  # 仅开发机自测用（MinGW + Git Bash 的 sh）；板端/Ubuntu 不走这里
  SHELL  := sh
  HOSTCC := gcc
  LDLIBS += -lws2_32
  AQLDLIBS += -lws2_32
endif

MKDIR := mkdir -p $(BUILD)
RMDIR := rm -rf $(BUILD)

all: $(TARGET)

$(BUILD):
	@$(MKDIR)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TARGET): $(OBJS) | $(BUILD)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)
	@echo "== built: $@  (BOARD=$(BOARD)) =="

# 测试素材生成器（主机侧工具，不进板端产物）
tools: | $(BUILD)
	$(HOSTCC) -O2 -o $(BUILD)/gen_wav tools/gen_wav.c -lm
	@echo "== tool: $(BUILD)/gen_wav =="

# 离线音质测试工具（M5Q-07，主机侧，链接真实 src/effect.c + src/resample.c + src/log.c）
audio_quality: | $(BUILD)
	$(HOSTCC) -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -Iinclude -Isrc -DBOARD=0 -D_GNU_SOURCE -pthread \
	  tools/audio_quality.c src/effect.c src/resample.c src/pitch.c src/log.c \
	  -o $(BUILD)/audio_quality $(AQLDLIBS)
	@echo "== tool: $(BUILD)/audio_quality =="

selftest: $(TARGET)
	$(TARGET) --selftest

# 交叉编译快捷入口（Ubuntu 集成机）：make arm CROSS=arm-linux-gnueabihf-
arm:
	@if [ -z "$(CROSS)" ]; then \
	  echo "错误：CROSS 为空。示例：make arm CROSS=arm-linux-gnueabihf-"; \
	  exit 2; \
	fi
	@$(MAKE) BOARD=1 CROSS=$(CROSS)

# 产物 ABI/依赖校验（必须在 Ubuntu/交叉工具链侧跑；Windows 无 readelf）
# 注意：readelf 的字段名会跟随 locale 本地化（中文环境是「类别:」「机器:」），
# 这里统一加 LC_ALL=C，否则 grep "Class:|Machine:" 匹配不到，目标会误判失败
# （2026-11-19 在中文 VM 上踩过）。
verify: $(TARGET)
	@echo "== 产物校验：$(TARGET) =="
	@file $(TARGET) 2>/dev/null || true
	@LC_ALL=C $(READELF) -h $(TARGET) | grep -E "Class:|Machine:"
	@LC_ALL=C $(READELF) -A $(TARGET) 2>/dev/null | grep -E "Tag_ABI_VFP_args|Tag_CPU_arch" || true
	@LC_ALL=C $(READELF) -l $(TARGET) | grep -E "interpreter" || true
	@LC_ALL=C $(READELF) -d $(TARGET) | grep -E "NEEDED" || true
	@echo "-- GLIBC 符号版本（板端必须 >= 这些值）--"
	@LC_ALL=C $(READELF) --version-info $(TARGET) | grep -o "GLIBC_[0-9.]*" | sort -u || true

clean:
	@$(RMDIR)
	@echo "== cleaned: $(BUILD)/ =="

-include $(wildcard $(OBJS:.o=.d))

.PHONY: all tools audio_quality selftest arm verify clean
