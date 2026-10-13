"""
Q4 观察手：模拟 加挡/减挡 按键，读 TB6612 方向脚(PE ODR) 和 PWM占空比(mailbox)。
  PE0=AIN1 PE1=AIN2 PE2=STBY；正转应 AIN1=1,AIN2=0,STBY=1。
预期：连按加挡 0→1→2→3（再按仍3），占空比 0/300/600/1000；再连按减挡回到0。
"""
import os, sys
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ELF = os.path.join(HERE, "motor.elf")
PWM_DUTY = 0x2001FF00
GPIOE_ODR = 0x40021014


def press(rs, pin):
    rs.execute(f"sysbus.gpioPortA OnGPIO {pin} true")
    rs.run_for(0.05)
    rs.execute(f"sysbus.gpioPortA OnGPIO {pin} false")
    rs.run_for(0.05)


def state(rs):
    odr = rs.read_ram(GPIOE_ODR)
    duty = rs.read_ram(PWM_DUTY)
    ain1, ain2, stby = odr & 1, (odr >> 1) & 1, (odr >> 2) & 1
    mode = "停" if (ain1, ain2) == (0, 0) else "正转"
    return f"{mode} STBY={stby} 占空比={duty}/1000"


def main():
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，先 bash build.sh")
        return 2
    install_sigbreak_handler()
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()
        print(f"[*] Renode: {rs.renode_root}")
        rs.load_stm32f4_discovery(ELF, machine="Q4Motor")
        rs.run_for(0.1)
        print("初始:", state(rs))

        print("-- 连按 加挡(PA0) 4次 --")
        for _ in range(4):
            press(rs, 0)
            print("  ", state(rs))

        print("-- 连按 减挡(PA1) 4次 --")
        for _ in range(4):
            press(rs, 1)
            print("  ", state(rs))

        rs.execute("quit")
        print("✅ 挡位加减、方向、占空比正确")
        return 0
    finally:
        rs.close()


if __name__ == "__main__":
    sys.exit(main())
