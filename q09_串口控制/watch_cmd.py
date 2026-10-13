"""
第9题观察手：用串口给单片机发命令，抓回复，读 LED 状态验证。

命令通过 sysbus.uart4 WriteLine 发给单片机（走串口 RX），
单片机的回复通过文件后端抓下来打印。
"""
import os, sys
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ELF = os.path.join(HERE, "q9.elf")
LOG = os.path.join(HERE, "q9.log")
GPIOD_ODR = 0x40020C14
LED_BIT = 1 << 14   # PD14


def led_on(rs) -> bool:
    return bool(rs.read_ram(GPIOD_ODR) & LED_BIT)


def main() -> int:
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，先 bash build.sh")
        return 2
    install_sigbreak_handler()
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()
        print(f"[*] Renode: {rs.renode_root}")
        rs.load_stm32f4_discovery(ELF, mips=4, machine="Q9Cmd")
        rs.attach_uart_capture("sysbus.uart4", LOG)
        rs.run_for(0.2)

        def send(cmd):
            rs.execute(f'sysbus.uart4 WriteLine "{cmd}"')
            rs.run_for(0.3)

        print("1) 发 SETLED:200  -> 应回OK，灯亮")
        send("SETLED:200");  print("   LED:", "亮" if led_on(rs) else "灭")

        print("2) 发 SETLED:0    -> 应回OK，灯灭")
        send("SETLED:0");    print("   LED:", "亮" if led_on(rs) else "灭")

        print("3) 发 SETLED:999  -> 应回 ERR:RANGE")
        send("SETLED:999")

        print("4) 发 BADCMD      -> 应回 ERR:FORMAT")
        send("BADCMD")

        print("5) 发 FLASH:2     -> 应回OK，然后闪2次")
        send("FLASH:2")
        rs.run_for(0.5); print("   闪灯中 LED:", "亮" if led_on(rs) else "灭")
        rs.run_for(0.5); print("   闪灯中 LED:", "亮" if led_on(rs) else "灭")
        rs.run_for(0.6); print("   闪完后 LED:", "亮" if led_on(rs) else "灭")

        print("6) 发 FLASH:99    -> 应回 ERR:RANGE")
        send("FLASH:99")

        # 收网，打印所有回复
        text = rs.drain_uart_capture("sysbus.uart4", LOG)
        print("\n=== 单片机回的内容 ===")
        print(text)
        rs.execute("quit")
        return 0
    finally:
        rs.close()


if __name__ == "__main__":
    sys.exit(main())
