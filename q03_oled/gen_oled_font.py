#!/usr/bin/env python3.12
# -*- coding: utf-8 -*-
"""
gen_oled_font.py —— 把系统字体渲染成 SSD1306 能直接刷的点阵字库

【为什么需要这个】
    真板上的 OLED 没有"字体"这种东西，它只认一堆 0/1。
    一段文字要显示出来，必须先把每个字变成点阵字节，存进单片机，
    再由程序把这些字节搬到屏幕显存里。这个"字模数据"就是字库。

【点阵怎么排：和显示器的物理结构对应】
    SSD1306 是 128x64，显存按"页(Page)"组织，一页 8 行像素：
        显存地址 = 页号*128 + 列号
        一个字节 = 某一列上竖着叠的 8 个像素，bit0 在最上面
    所以：
        16x16 汉字 = 2 页 × 16 列 = 每个字 32 字节
         8x16 ASCII = 1 页 ×  8 列 = 每个字符 8 字节

【要改显示内容怎么办】
    改下面 CHINESE 里的字符串，然后重跑本脚本：
        python3.12 gen_oled_font.py
    会重新生成 oled_font.h。这样字库永远是跟着题目文字走的，
    不会出现"改了代码里的字符串、屏幕还是旧字"的尴尬。

【字库来源合法性】
    用的是 macOS 自带字体（宋体 Songti.ttc）本地渲染，
    属于本机排版输出，不是抄同学的例程。考场上老师问起来可以直接说清楚。
"""

import os
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "oled_font.h")

# 可换字体：宋体偏楷体风格，适合点阵；黑体更粗，翻转后小字号更清楚
FONT_PATH = "/System/Library/Fonts/Supplemental/Songti.ttc"
if not os.path.exists(FONT_PATH):
    FONT_PATH = "/Library/Fonts/Arial Unicode.ttf"

# ASCII 单独用一个无衬线字体：宋体的拉丁字母笔画细，缩到 8 像素宽会糊成一团，
# 无衬线粗体降采样后笔画还立得住，屏幕上一眼能认出来。
FONT_PATH_ASCII = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
if not os.path.exists(FONT_PATH_ASCII):
    FONT_PATH_ASCII = "/System/Library/Fonts/Helvetica.ttc"

# ============================ 要显示的文字 ============================
NAME = "鹅耶"                                  # 你的名字（对应 main_board.c 里的 MY_NAME）
PHRASE = "好想加入深大电协硬件部啊"            # 题目要求的固定句子


def render_glyph(ch, size, font_path=None):
    """把单个字符渲染成 size×size 的黑白图，返回 set[(x,y)] 亮像素集合。

    【为什么不用「直接按 size 画再裁」】
        按 16px 直接画，汉字的墨迹宽度常常顶到 16 甚至 17 像素，
        一裁就切掉右边一竖，而且相邻字会粘在一起——在 128x64 的屏上非常难看。
    正解是点阵字库的标准流程：先用大字号（SS×4）画，取墨迹外接框，
    再按比例缩到目标尺寸居中放，最后二值化。
        这样做的好处：大字号下字形细节完整，缩下来才清晰，
        而且不管什么字体、多少留白，缩放后都能落在框正中间。
    """
    SS = 4                                  # 超采样倍数
    big = size * SS
    font = ImageFont.truetype(font_path or FONT_PATH, big)

    # 先画一张宽松的图，取出真实的墨迹范围
    tmp = Image.new("L", (big * 2, big * 2), 0)
    ImageDraw.Draw(tmp).text((big // 4, big // 4), ch, font=font, fill=255)
    bbox = tmp.getbbox()
    if bbox is None:                        # 空格这类没有墨迹的字符
        return set()

    # 按比例缩到 size×size 的框内，保持长宽比、居中
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    scale = min(size / w, size / h)
    nw, nh = max(1, int(round(w * scale))), max(1, int(round(h * scale)))
    crop = tmp.crop(bbox).resize((nw, nh), Image.LANCZOS)

    out = Image.new("L", (size, size), 0)
    out.paste(crop, ((size - nw) // 2, (size - nh) // 2))

    px = out.load()
    return {(x, y) for y in range(size) for x in range(size) if px[x, y] > 128}


def glyph_to_bytes16(pixels):
    """16x16 点阵 → SSD1306 的 32 字节（2 页 × 16 列，bit0 在上）。"""
    data = bytearray()
    for page in range(2):                       # 上下两页
        for col in range(16):                   # 每页 16 列
            byte = 0
            for bit in range(8):                # 每字节竖着 8 个像素
                if (col, page * 8 + bit) in pixels:
                    byte |= (1 << bit)
            data.append(byte)
    return data


def glyph_to_bytes8(pixels):
    """8x16 ASCII → 16 字节（2 页 × 8 列）。
    一列 16 个像素 = 2 页各 1 字节，所以每列占 2 字节，8 列共 16 字节。"""
    data = bytearray()
    for page in range(2):                       # 上下两页
        for col in range(8):
            byte = 0
            for bit in range(8):                # 每字节竖着 8 个像素
                if (col, page * 8 + bit) in pixels:
                    byte |= (1 << bit)
            data.append(byte)
    return data


def emit_table(fh, name, items, bytes_per):
    fh.write(f"static const uint8_t {name}[{len(items)}][{bytes_per}] = {{\n")
    for i, (ch, data) in enumerate(items):
        label = ch if ch not in ('"', '\\') else '\\%s' % ch
        if ch == '\n':
            label = '\\n'
        fh.write(f"    /* 0x{i:02X} {label} */ ")
        fh.write(", ".join(f"0x{b:02X}" for b in data))
        fh.write(",\n" if (i + 1) % 4 else ",\n")
    fh.write("};\n\n")


def main():
    # ---- ASCII 0x20..0x7E ----
    ascii_items = []
    for code in range(0x20, 0x7F):
        ch = chr(code)
        ascii_items.append((ch, glyph_to_bytes8(render_glyph(ch, 16, FONT_PATH_ASCII))))

    # ---- 中文（含名字和句子，去重且保持顺序） ----
    cn_chars = []
    for ch in NAME + PHRASE:
        if ch not in cn_chars:
            cn_chars.append(ch)
    cn_items = [(ch, glyph_to_bytes16(render_glyph(ch, 16))) for ch in cn_chars]

    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write("""/* ============================================================================
 * oled_font.h —— **自动生成，请勿手改**
 * ----------------------------------------------------------------------------
 * 由同目录的 gen_oled_font.py 从系统字体渲染产生。
 * 想改显示内容：改 gen_oled_font.py 里的 NAME / PHRASE，重跑脚本即可。
 *
 * 数据布局（和 SSD1306 的显存结构严格对应）：
 *   16x16 汉字 : 2 页 × 16 列 = 32 字节/字
 *   8x16 ASCII : 2 页 ×  8 列 = 16 字节/字符
 *   一个字节 = 一列上竖着叠的 8 个像素，bit0 在最上面。
 * ==========================================================================*/
#ifndef OLED_FONT_H
#define OLED_FONT_H
#include <stdint.h>

""")
        # 字符 → 字模的索引表，程序靠它查字
        fh.write("/* 中文常量的 C 标识符，方便代码里直接引用 */\n")
        for i, ch in enumerate(cn_chars):
            ident = "CN_%04X" % ord(ch)
            fh.write("#define %s %d\n" % (ident, i))
        fh.write("\n#define CN_COUNT %d\n\n" % len(cn_chars))

        emit_table(fh, "ascii_8x16", ascii_items, 16)
        emit_table(fh, "cn_16x16", cn_items, 32)

        fh.write("""
/* ASCII 码 → ascii_8x16 下标的偏移量（表首就是 0x20 空格）*/
#define ASCII_FIRST 0x20
#define ASCII_LAST  0x7E

/* 要显示的两段文字的点阵数据（由脚本内嵌，程序直接刷）*/
#endif /* OLED_FONT_H */
""")

    print("已生成 %s" % OUT)
    print("  汉字 %d 个，ASCII %d 个" % (len(cn_items), len(ascii_items)))
    print("  内容：%s / %s" % (NAME, PHRASE))


if __name__ == "__main__":
    main()
