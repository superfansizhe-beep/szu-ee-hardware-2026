"""
Q6 观察手：喂不同反射强度 AO，在1.2s窗口内统计蜂鸣器翻转次数（越急越多），
并核对安全时绿灯常亮。
  蜂鸣器=PE0(GPIOE_ODR bit0)；绿灯=PD12 红灯=PD14 (GPIOD_ODR)。
预期：AO=100 安全(0次响、绿灯亮)；AO=1000/2200/3500 报警且响的次数递增。
"""
import os, sys
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ELF = os.path.join(HERE, "infrared.elf")
IR_AO = 0x2001FF00
GPIOE_ODR = 0x40021014
GPIOD_ODR = 0x40020C14


def count_beeps(rs, ao, window=1.2, sample=0.03):
    rs.write_ram(IR_AO, ao)
    prev_buzz, edges = 0, 0
    n = int(window / sample)
    for _ in range(n):
        rs.run_for(sample)
        buzz = rs.read_ram(GPIOE_ODR) & 1
        if buzz and not prev_buzz:
            edges += 1
        prev_buzz = buzz
    odr = rs.read_ram(GPIOD_ODR)
    green = bool(odr & (1 << 12))
    red = bool(odr & (1 << 14))
    return edges, green, red


def main():
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，先 bash build.sh")
        return 2
    install_sigbreak_handler()
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()
        print(f"[*] Renode: {rs.renode_root}")
        rs.load_stm32f4_discovery(ELF, machine="Q6Infrared")
        rs.run_for(0.1)

        cases = [(100, "安全(无遮挡)"), (1000, "远/弱反射"),
                 (2200, "中等"), (3500, "近/强反射"), (100, "回到安全")]
        results = []
        for ao, name in cases:
            edges, green, red = count_beeps(rs, ao)
            results.append((name, edges, green, red))
            print(f"{name:>10}：1.2s内蜂鸣 {edges} 次  绿灯={green} 红灯={red}")

        # 校验：安全0次且绿灯；三级报警次数严格递增
        safe = results[0][1] == 0 and results[0][2]
        back = results[4][1] == 0 and results[4][2]
        graded = results[1][1] < results[2][1] < results[3][1]
        print(f"[?] 安全绿灯静音：{'OK' if safe and back else 'FAIL'}")
        print(f"[?] 越强响得越急：{'OK' if graded else 'FAIL'}")
        rs.execute("quit")
        return 0 if (safe and back and graded) else 1
    finally:
        rs.close()


if __name__ == "__main__":
    sys.exit(main())
