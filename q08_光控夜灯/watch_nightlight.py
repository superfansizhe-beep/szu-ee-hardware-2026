"""
Q8 观察手：验证 光控夜灯 的 防抖 + 平滑渐变。
步骤：
  1) 持续"亮"环境 -> LED 占空比保持很低；
  2) 防抖测试：只暗 0.15s（<400ms稳定窗）就恢复亮 -> LED 不应升高；
  3) 持续"暗" -> 占空比应平滑(逐级)上升，最终很高；
  4) 持续"亮" -> 平滑下降回到很低。
"""
import os, sys
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ELF = os.path.join(HERE, "nightlight.elf")
LIGHT_IN = 0x2001FF00
LED_DUTY = 0x2001FF04

BRIGHT, DARK = 4000, 200


def hold(rs, light, seconds, sample=0.1):
    rs.write_ram(LIGHT_IN, light)
    vals = []
    for _ in range(int(seconds / sample)):
        rs.run_for(sample)
        vals.append(rs.read_ram(LED_DUTY))
    return vals


def main():
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，先 bash build.sh")
        return 2
    install_sigbreak_handler()
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()
        print(f"[*] Renode: {rs.renode_root}")
        rs.load_stm32f4_discovery(ELF, machine="Q8Night")
        rs.run_for(0.1)

        # 1) 持续亮：应很低
        bright_vals = hold(rs, BRIGHT, 0.8)
        print("亮环境占空比:", bright_vals[-1])

        # 2) 防抖：暗 0.15s 再亮
        rs.write_ram(LIGHT_IN, DARK); rs.run_for(0.15)
        rs.write_ram(LIGHT_IN, BRIGHT)
        deb = hold(rs, BRIGHT, 0.8)
        print("短暂变暗后占空比（应仍很低）:", deb[-1])

        # 3) 持续暗：记录上升轨迹
        up = hold(rs, DARK, 1.6)
        print("变暗后占空比轨迹:", up)

        # 4) 持续亮：记录下降轨迹
        down = hold(rs, BRIGHT, 1.6)
        print("变亮后占空比轨迹:", down)

        # ---- 断言 ----
        bright_ok = bright_vals[-1] < 100 and deb[-1] < 100          # 亮/防抖后都低
        distinct_up = len(set(up)) >= 3                              # 平滑：多个中间值
        monotonic_up = all(up[i] <= up[i + 1] for i in range(len(up) - 1))
        end_high = up[-1] > 800                                      # 暗到底很亮
        end_low = down[-1] < 100                                     # 亮到底很暗
        smooth_down = len(set(down)) >= 3

        print(f"[?] 亮环境灯暗、短暂变化被防抖忽略：{'OK' if bright_ok else 'FAIL'}")
        print(f"[?] 变暗后平滑渐变到很亮：{'OK' if distinct_up and monotonic_up and end_high else 'FAIL'}")
        print(f"[?] 变亮后平滑渐变回很暗：{'OK' if smooth_down and end_low else 'FAIL'}")
        ok = bright_ok and distinct_up and monotonic_up and end_high and smooth_down and end_low
        rs.execute("quit")
        return 0 if ok else 1
    finally:
        rs.close()


if __name__ == "__main__":
    sys.exit(main())
