"""
Q7 观察手：喂不同距离，读固件输出的蜂鸣器频率，打印对应音阶。
预期：从远到近 37/31/26/20/15/9/3cm 依次 Do Re Mi Fa Sol La Si（频率递增）；
      超范围(50cm、1cm) -> 频率0 静音。
"""
import os, sys
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ELF = os.path.join(HERE, "harp.elf")
DIST_MM = 0x2001FF00
BUZZ_FREQ = 0x2001FF04
NAMES = ["Do", "Re", "Mi", "Fa", "Sol", "La", "Si"]
FREQS = [523, 587, 659, 698, 784, 880, 988]


def expected(cm):
    if cm < 2 or cm > 40:
        return 0
    note = (40 - cm) * 7 // 38
    note = min(note, 6)
    return FREQS[note]


def main():
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，先 bash build.sh")
        return 2
    install_sigbreak_handler()
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()
        print(f"[*] Renode: {rs.renode_root}")
        rs.load_stm32f4_discovery(ELF, machine="Q7Harp")
        rs.run_for(0.1)

        seq = [(37, "远"), (31, ""), (26, ""), (20, ""),
               (15, ""), (9, ""), (3, "近"), (50, "超远"), (1, "太近")]
        freqs_got, ok_all = [], True
        for cm, tag in seq:
            rs.write_ram(DIST_MM, cm * 10)
            rs.run_for(0.05)
            f = rs.read_ram(BUZZ_FREQ)
            freqs_got.append(f)
            exp = expected(cm)
            note = "静音" if f == 0 else NAMES[FREQS.index(f)]
            good = "OK" if f == exp else "✗"
            if f != exp:
                ok_all = False
            print(f"  {cm:>2}cm {tag:>2}：{f:>4}Hz ({note})  {good}")

        # 7个音阶频率应严格递增（前7个有效点）
        seven = freqs_got[:7]
        rising = all(seven[i] < seven[i + 1] for i in range(6))
        muted = freqs_got[7] == 0 and freqs_got[8] == 0
        print(f"[?] 越近音越高（7音阶递增）：{'OK' if rising else 'FAIL'}")
        print(f"[?] 超范围静音：{'OK' if muted else 'FAIL'}")
        rs.execute("quit")
        return 0 if (ok_all and rising and muted) else 1
    finally:
        rs.close()


if __name__ == "__main__":
    sys.exit(main())
