/*
 * ui_ext.h —— 显示层的「内部控制面」（touch ↔ display）
 *
 * 为什么放在 src/ 而不是 include/：
 *   include/ 下的头文件是模块对外接口，M1 后冻结（AGENT.md §1）。
 *   触摸命中区与版式几何是显示模块的内部细节，改动频繁，走 src/ 私有头
 *   （先例：src/effect_ext.h）。对外的 display.h 不新增任何函数。
 *
 * 分工：
 *   display.c 是几何的唯一 owner —— 画在哪里、点哪里算命中，都在这一个文件里；
 *   touch.c   只负责 evdev 事件 → (x,y) → 命中键 → ctrl_post_cmd()。
 *   这样改版式不会出现「画的和点的对不上」。
 */
#ifndef UI_EXT_H
#define UI_EXT_H

/* 触摸命中键。顺序无所谓，值被 touch.c 用来查表派发指令 */
enum {
    UI_KEY_NONE = 0,
    UI_KEY_VOL_DOWN,        /* 音量 -5 */
    UI_KEY_VOL_UP,          /* 音量 +5 */
    UI_KEY_SPEED_DOWN,      /* 速度 -0.1x */
    UI_KEY_SPEED_UP,        /* 速度 +0.1x */
    UI_KEY_ECHO_TOGGLE,     /* 回声开/关 */
    UI_KEY_LOW_DOWN,        /* 低音 -3dB */
    UI_KEY_LOW_UP,          /* 低音 +3dB */
    UI_KEY_MID_DOWN,
    UI_KEY_MID_UP,
    UI_KEY_HIGH_DOWN,
    UI_KEY_HIGH_UP,
    UI_KEY_PREV,            /* 上一首 */
    UI_KEY_TOGGLE,          /* 播放 / 暂停 */
    UI_KEY_NEXT,            /* 下一首 */
    UI_KEY_COUNT
};

/* (x,y) 是否落在某个可点区域；不是则返回 UI_KEY_NONE。坐标已缩放到 800×480 */
int  display_hit_test(int x, int y);

/* 按下反馈：把某个键的边框点亮 300ms，由 display 线程在下一帧画出 */
void display_flash_key(int key);

/* 触点反馈：在 (x,y) 画一个小方框 300ms，空点也有反馈，顺便当触摸校准证据 */
void display_touch_mark(int x, int y);

#endif /* UI_EXT_H */
