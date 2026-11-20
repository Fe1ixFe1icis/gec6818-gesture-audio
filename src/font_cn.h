/*
 * font_cn.h —— LCD 中文点阵字模（自动生成，勿手改）
 *
 * 生成器：tools/gen_font_cn.py（宿主机 Pillow 渲染 16x16，1bpp）
 * 字模格式：每字 32 字节，行优先，每行 2 字节，高位在左
 */
#ifndef FONT_CN_H_INCLUDED
#define FONT_CN_H_INCLUDED

#define FONT_CN_W     16
#define FONT_CN_H     16
#define FONT_CN_BYTES 32
#define FONT_CN_COUNT 59

/* Unicode 码点 → 字模索引；无此字返回 -1 */
int font_cn_index(unsigned int codepoint);

/* 索引 → 字模首地址（FONT_CN_BYTES 字节，行优先） */
const unsigned char *font_cn_glyph(int index);

#endif /* FONT_CN_H_INCLUDED */
