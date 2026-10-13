"""
L2 观察手：在 Renode 里跑 uart_hello.elf，把 UART4 发出的字符串抓出来。

【这个脚本在干什么？】
  它不是固件，是跑在你 Mac 上的 Python 程序，作用是"替你操作 Renode"：
    1. 启动 Renode，建一台虚拟的 STM32F4-Discovery 板，加载固件；
    2. 把 UART4 的输出接到一个文件后端（相当于给虚拟芯片的"嘴"下面放个录音笔）；
    3. 推进 2 秒虚拟时间，让固件跑起来不断发 hello；
    4. 关掉"录音笔"，把文件内容读出来打印给你看。

  它复用了 renode-mcu-survey 里已经调通的 tool.session（就是"自动帮你敲
  monitor 命令的那只手"），所以我们不用自己写 TCP 连 Renode 的细节。

跑法：/opt/homebrew/bin/python3.12 watch_uart.py
（必须用 py3.12，因为它装了 matplotlib 等依赖；系统默认 python3 没有）
"""

import os   # 处理文件路径
import sys  # 改 Python 搜索路径、退出程序

# ---------------------------------------------------------------------------
# 把范本项目的目录加进 Python 搜索路径，这样才能 import 它的 tool.session
# ---------------------------------------------------------------------------
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)   # insert(0,..) 表示优先从这个目录找模块

# RenodeSession：封装了"启动 Renode 进程、连 monitor、发命令、读回显"的类
# install_sigbreak_handler：处理 Ctrl+C，保证退出时能关掉 Renode
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

# ---------------------------------------------------------------------------
# 几个路径和参数
# ---------------------------------------------------------------------------
HERE = os.path.dirname(os.path.abspath(__file__))  # 本脚本所在目录
ELF = os.path.join(HERE, "uart_hello.elf")          # 固件路径
LOG = os.path.join(HERE, "uart4.log")               # Renode 把串口输出写进这个文件
UART_PERIPHERAL = "sysbus.uart4"                    # Renode 里 UART4 外设的名字
MIPS = 4                                            # 虚拟 CPU 性能（只影响墙钟快慢）
RUN_S = 2.0                                         # 观察 2 个虚拟秒


def main() -> int:
    # 先确认固件存在，没有就提示先编译
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，请先 bash build.sh")
        return 2

    install_sigbreak_handler()

    # 创建一个 Renode 会话对象（还没真正启动进程）
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()   # 真正启动 Renode 进程，连上它的 monitor
        print(f"[*] Renode: {rs.renode_root}")

        # 一步完成：建一台叫 "UartHello" 的虚拟机 + 载入 Discovery 板描述
        # + 设 MIPS + 加载 elf + 设好向量表
        rs.load_stm32f4_discovery(ELF, mips=MIPS, machine="UartHello")

        # ---- 关键一步：把 UART4 的输出接到文件后端 ----
        # 等价的 monitor 命令是：
        #   sysbus.uart4 CreateFileBackend @uart4.log true
        # 意思是：UART4 每发出一个字节，就直接写进 uart4.log。
        # Renode 在"外设层"就捕获了字节，不经过真实引脚，所以固件里不用配 GPIO 复用。
        rs.attach_uart_capture(UART_PERIPHERAL, LOG)
        print(f"[*] 已把 {UART_PERIPHERAL} 输出捕获到文件：{LOG}")

        # 推进虚拟时间：让固件跑 2 秒，期间它会不断发 hello
        print(f"[*] 推进 {RUN_S} 虚拟秒...")
        rs.run_for(RUN_S)

        # ---- 收网：关掉文件后端并读出内容 ----
        # drain 内部会先执行 CloseFileBackend（把缓冲区刷进文件），
        # 再用 Python 打开 uart4.log 把文本读出来返回。
        text = rs.drain_uart_capture(UART_PERIPHERAL, LOG)

        # 打印抓到的内容
        print("\n=== 从 UART4 抓到的内容 ===")
        print(text if text.strip() else "(空，没抓到任何字节)")
        print("=== 结束 ===")

        # 统计非空行数，并检查有没有抓到 "hello"
        lines = [l for l in text.splitlines() if l.strip()]
        print(f"共抓到 {len(lines)} 行非空文本")
        ok = any("hello" in l for l in lines)
        if ok:
            print("UART 打印成功：抓到了 hello ✅")
            return 0
        else:
            print("❌ 没抓到 hello，需要检查 uart_init/波特率")
            return 1
    finally:
        # finally：无论上面成功还是报错，都保证关掉 Renode 进程，不留僵尸进程
        rs.close()


# Python 惯例：直接运行本脚本时才执行 main()
if __name__ == "__main__":
    sys.exit(main())
