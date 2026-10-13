"""L1 观察手：在 Renode 里跑 blink.elf，亲眼读到 PD12 在 0/1 之间翻转。

它复用 renode-mcu-survey 里已经在 macOS 调通的 tool.session（就是“自动帮你
敲 monitor 命令的手”）。等价的 monitor 命令我都写在注释里，对照着看即可：

    mach create "Blink"
    machine LoadPlatformDescription @.../stm32f4_discovery-kit.repl
    cpu PerformanceInMips 4
    sysbus LoadELF @blink.elf
    emulation RunFor "0:00:00.05"
    sysbus ReadDoubleWord 0x40020C14      # 读 GPIOD_ODR（引脚当前电平）

观察灯是否亮，读 GPIO 的 ODR 寄存器最可靠：PD12 那一位是 1 就是高电平(亮)。
Renode 里那个叫 UserLED 的设备是挂在 GPIOD 下的“纯输出指示灯”，无头模式下
没有能直接读状态的命令（雷达项目因此用串口 [LED] 标记兜底）——所以我们读 ODR。

注意：GPIOD 的基址是 0x40020C00，ODR 偏移 0x14，所以 ODR 地址是 0x40020C14。
你原来的 read_gpio_auto.py 读的是 0x40020814，那是 GPIOC，不是 GPIOD——这就是
“灯在闪却读不到变化”的原因。
"""

import os
import sys

# 复用 survey 工程里跨平台调通的 tool 包（只借它驱动 Renode，不用改它）
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ELF = os.path.join(HERE, "blink.elf")

GPIOD_ODR = 0x40020C14          # GPIOD 输出数据寄存器（正确地址）
LED_BIT = 1 << 13               # PD13 对应第 13 位（看绿灯 PD12 就改回 12）
MIPS = 4                        # 纯轮询固件 4 就够（只影响墙钟快慢，不改时序逻辑）
SLICE_S = 0.05                  # 每次推进 0.05 虚拟秒就读一次
TOTAL_S = 3.0                   # 一共观察 3 虚拟秒


def main() -> int:
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，请先 bash build.sh")
        return 2

    install_sigbreak_handler()
    rs = RenodeSession(settle=0.05)
    changes = []
    try:
        rs.start()
        print(f"[*] Renode: {rs.renode_root}")
        # 一步完成：建机 + 载入 Discovery 板 .repl + 设 MIPS + 载入 elf + 向量表
        rs.load_stm32f4_discovery(ELF, mips=MIPS, machine="Blink")

        last = None
        n = int(TOTAL_S / SLICE_S)
        for i in range(n):
            rs.run_for(SLICE_S)                                  # 推进一小段虚拟时间
            odr = rs.read_ram(GPIOD_ODR)                         # 读 GPIOD_ODR
            bit = 1 if (odr & LED_BIT) else 0                    # 取第 12 位
            t = (i + 1) * SLICE_S
            if last is None:
                print(f"[*] t={t:4.2f}s  GPIOD_ODR=0x{odr:08X}  PD13={bit}")
            elif bit != last:                                    # 只在翻转时打印
                changes.append((t, last, bit))
                state = "亮 ON（高电平）" if bit else "灭 OFF（低电平）"
                print(f"[*] t={t:4.2f}s  PD13: {last} -> {bit}   {state}")
            last = bit

        print("\n=== 观察结论 ===")
        print(f"3 个虚拟秒内 PD13 共翻转 {len(changes)} 次")
        ok = len(changes) >= 4
        print("点灯成功：灯确实在亮灭交替 ✅" if ok else "❌ 没观察到足够翻转，需要调 delay/MIPS")
        return 0 if ok else 1
    finally:
        rs.close()


if __name__ == "__main__":
    sys.exit(main())
