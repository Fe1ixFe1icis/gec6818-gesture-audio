/*
 * display.h —— LCD 界面（M4 交付主体）
 *
 * 后端：
 *   BOARD=1 → /dev/fb0，800×480×32bpp，双缓冲整屏提交（boardGuide.md §3.1）
 *   BOARD=0 → 主机上退化为「终端仪表行」，同时支持 --dump-bmp 导出一帧
 *             位图，方便在没有板子时检查版式与数值位置
 *
 * 约束：
 *   - 帧缓冲无并发保护，本模块是 /dev/fb0 的唯一 owner
 *   - 中文字库不在 v1 范围：内置 5×7 ASCII 点阵并按倍数放大，
 *     中文提示留给 M4 用点阵字模工具生成（boardGuide.md §3.1 坑位）
 */
#ifndef DISPLAY_H
#define DISPLAY_H

int  display_init(void);
void display_finish(void);

/* 线程入口：按 DISPLAY_FPS 刷新 */
void *display_thread(void *arg);

/* 渲染一帧到内部画布并导出 24 位 BMP（调试版式用；无需 framebuffer） */
int  display_dump_bmp(const char *path);

#endif /* DISPLAY_H */
