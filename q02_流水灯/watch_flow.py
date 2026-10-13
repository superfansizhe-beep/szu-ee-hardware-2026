"""
第2题「流水灯」观察手：在 Renode 里用外部中断按键改变方向/频率。

验证点：
  1. 上电后 4 个 LED 轮流亮（流水灯）；
  2. 按 PA0（键1）-> 方向反转；
  3. 按 PA1（键2）-> 频率变化。
  关键：按键是通过 EXTI 中断触发的（主循环不查按键）。
"""
import os, sys
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ELF = os.path.join(HERE, "q2.elf")
GPIOD_ODR = 0x40020C14
# PD12=bit12 ... PD15=bit15 -> 对应 pos 0..3
LED_BITS = {12: "PD12", 13: "PD13", 14: "PD14", 15: "PD15"}

def led_name(odr: int) -> str:
    for bit, name in LED_BITS.items():
        if odr & (1 << bit):
            return name
    return "无"

def press(rs, pin, hold_s=0.2, release_s=0.15):
    # 仿真要点：按键要灌进 EXTI 中断线（sysbus.exti.OnGPIO），
    # 而不是灌进 gpioPortA.OnGPIO——后者只改 IDR，Renode 不会把它送给 EXTI。
    # 真硬件上按键接引脚，硬件会自动经 SYSCFG->EXTI->NVIC 触发中断。
    rs.execute(f"sysbus.exti OnGPIO {pin} true")   # 按下（上升沿）
    rs.run_for(hold_s)
    rs.execute(f"sysbus.exti OnGPIO {pin} false")  # 松开
    rs.run_for(release_s)

def read(rs, tag):
    odr = rs.read_ram(GPIOD_ODR)
    print(f"  [{tag}] ODR=0x{odr:08X} -> {led_name(odr)} 亮")

def main() -> int:
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，先 bash build.sh")
        return 2
    install_sigbreak_handler()
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()
        print(f"[*] Renode: {rs.renode_root}")
        rs.load_stm32f4_discovery(ELF, mips=4, machine="Q2Flow")

        print("\n===== 1) 初始流水灯（每0.6秒看一眼，应依次换灯） =====")
        for i in range(4):
            rs.run_for(0.6)
            read(rs, f"第{i+1}次")

        print("\n===== 2) 按 PA0（键1）-> 方向应反转 =====")
        press(rs, 0)
        for i in range(3):
            rs.run_for(0.6)
            read(rs, f"反转后#{i+1}")

        print("\n===== 3) 按 PA1（键2）-> 频率改变（2->1->0.5Hz） =====")
        press(rs, 1)
        print("  （频率档已切换，下面节奏会变）")
        for i in range(3):
            rs.run_for(0.6)
            read(rs, f"变速后#{i+1}")

        rs.execute("quit")
        print("\n✅ 流水灯+外部中断演示完成")
        return 0
    finally:
        rs.close()

if __name__ == "__main__":
    sys.exit(main())
