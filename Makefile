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
#   - 交叉编译器前缀必须与板上 ABI 一致（硬浮点 → gnueabihf），
#     混用 gnueabi 会在板上报 "No such file or directory"，见 boardGuide.md §2.5。
# =====================================================================
CROSS   ?=
BOARD   ?= 0
BUILD   := build
HOSTCC  ?= cc

CC      := $(CROSS)gcc
SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
TARGET  := $(BUILD)/gesture_audio

CFLAGS  := -std=gnu99 -O2 -g -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -Iinclude -DBOARD=$(BOARD) -D_GNU_SOURCE -pthread
LDFLAGS := -pthread
LDLIBS  := -lm

ifeq ($(BOARD),1)
LDLIBS  += -lasound
endif

ifeq ($(OS),Windows_NT)
  # 仅开发机自测用（MinGW + Git Bash 的 sh）；板端/Ubuntu 不走这里
  SHELL  := sh
  HOSTCC := gcc
  LDLIBS += -lws2_32
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

selftest: $(TARGET)
	$(TARGET) --selftest

clean:
	@$(RMDIR)
	@echo "== cleaned: $(BUILD)/ =="

-include $(wildcard $(OBJS:.o=.d))

.PHONY: all tools selftest clean
