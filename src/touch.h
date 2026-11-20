/*
 * touch.h —— 电容触摸输入（evdev / gslX680），M4
 *
 * 为什么需要它：板载 7 寸屏是电容触摸，gslx680 控制器注册成 evdev 设备
 * （板端实测 Name="gslX680"，ABS_X/ABS_Y + BTN_TOUCH）。有了触摸，
 * 界面就不只是「看」，音量/切歌/音效都能直接在屏幕上点。
 *
 * 线程模型：本模块是**生产者**，只把动作翻译成 ctrl_msg_t 投进控制队列，
 * 绝不自己改参数（单写者原则，boardGuide.md §5）。
 *
 * 只读 /dev/input/event*：该节点是独占读的，同一时间只允许一个 owner
 * （boardGuide.md §3.2 坑位）。本项目里没有其它进程读触摸。
 */
#ifndef TOUCH_H
#define TOUCH_H

/* 打开并匹配触摸设备；成功 0，找不到设备 -1（不影响其它业务） */
int  touch_init(void);
void touch_finish(void);

/* 线程入口：poll + read evdev，识别 tap/swipe 并投递指令 */
void *touch_thread(void *arg);

#endif /* TOUCH_H */
