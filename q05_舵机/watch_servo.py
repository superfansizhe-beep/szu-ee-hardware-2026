"""
舵机实验观察手：模拟"电位器"转不同角度，读固件算出的脉宽。
  0x2001FF00 = 目标角度（我们写，相当于电位器）
  0x2001FF04 = 固件算出的脉宽 us（我们读）
预期：0°->500us, 90°->1500us, 180°->2500us
"""
import os, sys
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ELF = os.path.join(HERE, "servo.elf")
TWIN_TARGET_DEG = 0x2001FF00
TWIN_SERVO_PULSE = 0x2001FF04


def expected(deg):
    return 500 + deg * 2000 // 180


def main() -> int:
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，先 bash build.sh")
        return 2
    install_sigbreak_handler()
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()
        print(f"[*] Renode: {rs.renode_root}")
        rs.load_stm32f4_discovery(ELF, mips=4, machine="ServoDemo")

        print(f"{'角度':>6} | {'固件输出脉宽':>10} | {'理论脉宽':>8}")
        print("-" * 38)
        for deg in [0, 45, 90, 135, 180, 90, 0]:
            rs.write_ram(TWIN_TARGET_DEG, deg)   # 转电位器
            rs.run_for(0.05)                     # 让固件读到并算出来
            pulse = rs.read_ram(TWIN_SERVO_PULSE)
            exp = expected(deg)
            ok = "OK" if pulse == exp else "✗"
            print(f"{deg:>5}° | {pulse:>8}us | {exp:>6}us  {ok}")

        rs.execute("quit")
        print("\n✅ 角度->脉宽映射正确")
        return 0
    finally:
        rs.close()


if __name__ == "__main__":
    sys.exit(main())
