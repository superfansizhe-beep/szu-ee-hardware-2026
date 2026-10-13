"""
Q3 观察手：读固件写进 SRAM 的4行文字，渲染成一张 OLED(128x64) 屏幕截图。
真板上这是 SSD1306 经 I2C 显示；仿真里用电脑字体"替它亮屏"。
"""
import os, sys
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ELF = os.path.join(HERE, "oled.elf")
PNG = os.path.join(HERE, "oled_screen.png")
OLED_BASE = 0x2001FE00
OLED_READY = 0x2001FF00
LINE_BYTES = 40
N_LINES = 4
FONT_CANDIDATES = [
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/STHeiti Light.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
]


def read_lines(rs):
    # 读 4×24=96 字节 = 24 个32位字，小端拼成字节串
    raw = bytearray()
    for i in range(N_LINES * LINE_BYTES // 4):
        w = rs.read_ram(OLED_BASE + i * 4)
        raw += w.to_bytes(4, "little")
    lines = []
    for r in range(N_LINES):
        chunk = raw[r * LINE_BYTES:(r + 1) * LINE_BYTES]
        lines.append(chunk.split(b"\x00", 1)[0].decode("utf-8", "replace"))
    return lines


def render(lines):
    from PIL import Image, ImageDraw, ImageFont
    W, H = 128, 64
    img = Image.new("1", (W, H), 0)
    d = ImageDraw.Draw(img)
    font_path = next((p for p in FONT_CANDIDATES if os.path.exists(p)), None)
    # 字号取小一点保证整句放得下 128 宽
    f_small = ImageFont.truetype(font_path, 10) if font_path else ImageFont.load_default()
    f_name = ImageFont.truetype(font_path, 13) if font_path else ImageFont.load_default()
    fonts = [f_small, f_name, f_small, f_small]
    for i, text in enumerate(lines):
        if text:
            d.text((2, 4 + i * 15), text, fill=1, font=fonts[i])
    big = img.resize((W * 4, H * 4), Image.NEAREST)   # 放大便于看清
    big.save(PNG)
    print(f"[*] OLED 截图: {PNG}")


def main():
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，先 bash build.sh")
        return 2
    install_sigbreak_handler()
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()
        print(f"[*] Renode: {rs.renode_root}")
        rs.load_stm32f4_discovery(ELF, machine="Q3Oled")
        rs.run_for(0.2)
        if rs.read_ram(OLED_READY) == 0x4F4C4544:
            lines = read_lines(rs)
            print("=== OLED 上显示的内容 ===")
            for t in lines:
                if t:
                    print("  " + t)
            render(lines)
            print("✅ OLED 显示成功")
        else:
            print("[!] 固件没准备好画面")
            return 3
        rs.execute("quit")
        return 0
    finally:
        rs.close()


if __name__ == "__main__":
    sys.exit(main())
