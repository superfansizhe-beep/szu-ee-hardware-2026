"""
第1题「点灯大师」观察手（逐行注释版）

【这个脚本在干什么？】
  它跑在你 Mac 上，不是固件。它的工作是"替你操作 Renode"：
    1. 启动 Renode，建一台虚拟 STM32F4-Discovery，加载 q1.elf；
    2. 用 sysbus.gpioPortA OnGPIO 命令模拟按键按下/松开（PA0=A, PA1=B）；
    3. 每一步读 GPIOD_ODR（输出寄存器），解码当前亮的是红/绿/蓝/全灭；
    4. 按题目流程走一遍：初始灭 -> 按A亮红 -> 按B三色循环 -> 按A熄灭。

  它复用了 renode-mcu-survey 里的 tool.session（自动帮你敲 monitor 命令的手）。

跑法：/opt/homebrew/bin/python3.12 watch_buttons.py
（必须用 py3.12，系统默认 python3 没有依赖）
"""

import os   # 处理文件路径
import sys  # 改 Python 搜索路径、退出程序

# ---------------------------------------------------------------------------
# 把范本项目目录加进 Python 搜索路径，才能 import 它的 tool.session
# ---------------------------------------------------------------------------
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)   # insert(0,..) 表示优先从这个目录找模块

# RenodeSession：封装了"启动 Renode、连 monitor、发命令、读回显"
# install_sigbreak_handler：处理 Ctrl+C，保证退出时能关掉 Renode
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

# ---------------------------------------------------------------------------
# 路径和常量
# ---------------------------------------------------------------------------
HERE = os.path.dirname(os.path.abspath(__file__))  # 本脚本所在目录
ELF = os.path.join(HERE, "q1.elf")                 # 固件路径

GPIOD_ODR = 0x40020C14   # GPIOD 输出数据寄存器地址（读它就知道哪些灯亮着）
LED_RED   = 1 << 14      # PD14 红，ODR 第14位
LED_GREEN = 1 << 12      # PD12 绿，ODR 第12位
LED_BLUE  = 1 << 15      # PD15 蓝，ODR 第15位


def color_name(odr: int) -> str:
    """
    根据 ODR 的值解码当前亮的颜色。
    原理：ODR 第 n 位=1 表示 PDn 输出高电平（灯亮）。
    用 & 把对应位屏蔽出来判断。本固件一次只亮一个灯，但这里兼容多个。
    """
    on = []                              # 收集亮着的颜色名
    if odr & LED_RED:   on.append("红")  # 第14位非0 -> 红亮
    if odr & LED_GREEN: on.append("绿")  # 第12位非0 -> 绿亮
    if odr & LED_BLUE:  on.append("蓝")  # 第15位非0 -> 蓝亮
    # on 非空就用 "+" 连起来（比如"红"），空就返回"全灭"
    return "+".join(on) if on else "全灭"


def press(rs, pin: int, hold_s: float = 0.25, release_s: float = 0.15):
    """
    模拟一次完整的按键动作：按下 -> 保持 -> 松开。
      rs       : RenodeSession 对象
      pin      : 哪个引脚，0=PA0(按键A)，1=PA1(按键B)
      hold_s   : 按住多久（虚拟秒），默认0.25秒
      release_s: 松开后等多久（让固件稳定），默认0.15秒

    关键命令：sysbus.gpioPortA OnGPIO <引脚> <true|false>
      true  = 把引脚拉高 = 按下
      false = 把引脚拉低 = 松开
    固件读 GPIOA_IDR 就能看到这个变化。
    """
    rs.execute(f"sysbus.gpioPortA OnGPIO {pin} true")   # 按下（拉高）
    rs.run_for(hold_s)                                  # 保持按住 hold_s 秒
    rs.execute(f"sysbus.gpioPortA OnGPIO {pin} false")  # 松开（拉低）
    rs.run_for(release_s)                               # 松开后等一会儿


def read_led(rs, tag: str):
    """读一次 GPIOD_ODR，打印当前灯色。tag 是打印时的阶段标签。"""
    odr = rs.read_ram(GPIOD_ODR)                        # 从虚拟内存读 ODR
    print(f"  [{tag}] GPIOD_ODR=0x{odr:08X}  -> {color_name(odr)}")


def main() -> int:
    # 先确认固件存在，没有就提示先编译
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，请先 bash build.sh")
        return 2

    install_sigbreak_handler()

    # 创建 Renode 会话（还没启动进程）
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()   # 真正启动 Renode 进程，连上 monitor
        print(f"[*] Renode: {rs.renode_root}")

        # 一步完成：建虚拟机 + 载入 Discovery 板描述 + 设性能 + 加载固件
        rs.load_stm32f4_discovery(ELF, mips=4, machine="Q1Blink")

        # ========== 阶段0：初始状态（应全灭） ==========
        print("\n===== 阶段0：初始状态（应全灭） =====")
        rs.run_for(0.2)        # 让固件跑0.2秒完成初始化
        read_led(rs, "初始")

        # ========== 阶段1：按按键 A（应亮红色） ==========
        print("\n===== 阶段1：按按键 A（应亮红色） =====")
        press(rs, 0)           # PA0 = 按键A
        read_led(rs, "按A后")

        # ========== 阶段2：按按键 B（应进入三色循环） ==========
        print("\n===== 阶段2：按按键 B（应进入红->绿->蓝 循环） =====")
        press(rs, 1)           # PA1 = 按键B
        # 连续读6次，每次间隔0.15秒，应该看到颜色在红/绿/蓝之间变
        for i in range(6):
            rs.run_for(0.15)
            read_led(rs, f"循环中#{i+1}")

        # ========== 阶段3：再按按键 A（应熄灭） ==========
        print("\n===== 阶段3：再按按键 A（应熄灭） =====")
        press(rs, 0)
        read_led(rs, "按A后")

        rs.execute("quit")     # 退出 Renode
        print("\n✅ 状态机流程演示完成")
        return 0
    finally:
        # finally：无论上面成功还是报错，都保证关掉 Renode，不留僵尸进程
        rs.close()


# Python 惯例：直接运行本脚本时才执行 main()
if __name__ == "__main__":
    sys.exit(main())
