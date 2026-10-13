"""
第10题「盲人摸象」上位机 —— 它一个人扮演三个角色：
  ① 虚拟超声波传感器：固件朝某角度时，用 ray_cast 算出该方向多远，喂给固件；
  ② 虚拟舵机观测者：读固件写在 mailbox 里的脉宽，换算成当前角度；
  ③ 雷达建图：收固件串口发的 "角度:距离"，解析、校验、画极坐标图。

【一个时间片(0.19s)的闭环】  rs.twin_step(距离, 0.19) 一次往返做完：
   写距离mailbox → RunFor推进0.19s（固件趁机测距+串口上报）→ 读回舵机新角度。
   时间片必须 < 固件的200ms步进，保证喂距离时固件还停在同一个角度（最多滞后一步）。

【mailbox 地址】不用自己记，RenodeSession 里已定义：
   TWIN_DISTANCE_MM=0x2003FFF0(我们写)  TWIN_SERVO_PULSE=0x2003FFF4(固件写)
   TWIN_MAGIC=0x2003FFF8(固件写'TWIN'，twin_check 校验固件是否真的在跑)

产物：radar_uart4.log 原始串口流 / radar_points.csv 数据表 / radar_map.png 雷达图
"""
import math
import os
import re
import sys

# 把大佬项目目录加进模块搜索路径，直接复用他写好的两样东西：
SURVEY = "/Users/fansizhe/Downloads/renode-mcu-survey"
sys.path.insert(0, SURVEY)
from tool.session import RenodeSession, install_sigbreak_handler  # noqa: E402
# RenodeSession：启动/连接 Renode、加载ELF、读写RAM、抓串口、twin_step 等
from tool.world import OBSTACLES, ROOM_RADIUS_CM, ray_cast          # noqa: E402
# ray_cast(角度)：虚拟世界里"这束超声波会撞到什么、多远(mm)"；OBSTACLES=两头大象

HERE = os.path.dirname(os.path.abspath(__file__))   # 本脚本所在目录
ELF = os.path.join(HERE, "radar.elf")
LOG = os.path.join(HERE, "radar_uart4.log")
CSV = os.path.join(HERE, "radar_points.csv")
PNG = os.path.join(HERE, "radar_map.png")

# 正则：匹配固件发的两类行。r"^(\d+):(\d+)\.(\d)$" 抓 "45:67.0" 的 45/67/0
SCAN_LINE = re.compile(r"^(\d+):(\d+)\.(\d)\r?$")
LED_LINE = re.compile(r"^\[LED\] angle=(\d+)\r?$")   # 抓 "[LED] angle=90" 的 90
STEPS_PER_PASS = 37                 # 0..180 每5°一步，含两端 = 37 个角度
CYCLE_POINTS = 2 * STEPS_PER_PASS   # 一个完整往返 = 74 个测距点
CHUNK_S = 0.19                      # 时间片(虚拟秒)，要小于固件 0.2s 步进
N_CHUNKS = int(15.5 / CHUNK_S) + 6  # 跑够一个往返所需的片数（多给几片余量）


def parse(text):
    """把串口文本逐行解析成 (角度, 距离mm) 列表 和 关键角点灯列表。"""
    points, leds = [], []
    for line in text.splitlines():
        m = SCAN_LINE.match(line.strip())
        if m:
            ang = int(m.group(1))
            mm = int(m.group(2)) * 10 + int(m.group(3))  # "67.0" -> 670mm
            points.append((ang, mm))
            continue
        m = LED_LINE.match(line.strip())
        if m:
            leds.append(int(m.group(1)))
    return points, leds


def check_sweep(angles):
    """校验扫描确实是 0→180→0：相邻角度差只能是 ±5（折返点允许差0），
    且 +5 和 -5 两个方向都得出现过。"""
    dirs = set()
    for a, b in zip(angles, angles[1:]):
        d = b - a
        if d not in (-5, 0, 5):
            return False
        if d == 0 and a not in (0, 180):   # 只有在两端折返时才允许原地重复
            return False
        if d:
            dirs.add(d)
    return 0 in angles and 180 in angles and dirs == {5, -5}


def check_distance(points):
    """核心正确性：固件报的距离应等于虚拟世界 ray_cast 的真值。
    允许查相邻 ±5°（因为注入可能比固件慢一步），仍对不上就记为坏点。"""
    bad = []
    for ang, mm in points:
        cand = [ray_cast(a) for a in (ang - 5, ang, ang + 5) if 0 <= a <= 180]
        if not (min(cand) - 1 <= mm <= max(cand) + 1):
            bad.append((ang, mm))
    return bad


def plot(points):
    """把一圈 (角度,距离) 画成极坐标雷达图，并叠上虚拟世界真值做对照。"""
    import matplotlib
    matplotlib.use("Agg")   # Agg=不弹窗、直接存图片（想弹窗可改 TkAgg）
    import matplotlib.pyplot as plt
    plt.rcParams["font.sans-serif"] = ["PingFang SC", "Heiti SC", "Arial Unicode MS"]
    plt.rcParams["axes.unicode_minus"] = False

    fig = plt.figure(figsize=(7, 7))
    ax = fig.add_subplot(111, projection="polar")   # 极坐标
    ax.set_theta_zero_location("N")   # 0° 放在正上方（正前方）
    ax.set_theta_direction(-1)        # 角度顺时针增长，和世界模型一致
    ax.set_ylim(0, ROOM_RADIUS_CM)    # 半径=距离，最大180cm

    ang = [math.radians(a) for a, _ in points]      # 度→弧度
    dist = [mm / 10.0 for _, mm in points]          # mm→cm
    ax.plot(ang, dist, "-", color="#1f77b4", alpha=0.6, lw=1)
    ax.scatter(ang, dist, s=10, color="#d62728", zorder=3, label="扫描测距点")

    # 叠真值：灰色虚线圆=边界墙；绿色虚线弧=两头大象被扫到的那几段
    th = [math.radians(a) for a in range(0, 181)]
    ax.plot(th, [ROOM_RADIUS_CM] * len(th), "--", color="gray", lw=1, label="边界墙(180cm)")
    for ob in OBSTACLES:
        r = []
        for t in th:
            v = ray_cast(math.degrees(t), [ob]) / 10.0   # 只放这一个障碍算一遍
            r.append(v if v < ROOM_RADIUS_CM else float("nan"))  # 没命中的角度留空
        ax.plot(th, r, "--", color="green", lw=1)
    ax.plot([], [], "--", color="green", lw=1, label="障碍真值")

    ax.set_title("盲人摸象 · 雷达扫描建图", va="bottom")
    ax.legend(loc="lower center", bbox_to_anchor=(0.5, -0.12), ncol=3, fontsize=9)
    fig.savefig(PNG, dpi=150, bbox_inches="tight")
    print(f"[*] 雷达图: {PNG}")


def main():
    if not os.path.isfile(ELF):
        print(f"[!] 找不到 {ELF}，先 bash build.sh")
        return 2
    install_sigbreak_handler()   # Ctrl+C 时也能在 finally 里把 Renode 进程收掉
    rs = RenodeSession(settle=0.05)
    try:
        rs.start()
        print(f"[*] Renode: {rs.renode_root}")
        rs.load_stm32f4_discovery(ELF, machine="RadarTwin")  # 建机+平台+加载ELF

        rs.hcsr04_set_distance_mm(0)                    # 上电先给"无有效读数"
        rs.attach_uart_capture("sysbus.uart4", LOG)     # 把串口输出接到日志文件
        rs.run_for(CHUNK_S)                             # 先推进一小段让固件启动
        if not rs.twin_check():                        # 读 magic，确认固件真的跑了
            print(f"[!] mailbox 不通，PC=0x{rs.cpu_pc():08X}")
            return 3
        print("[*] mailbox 连通，固件已运行，开始扫描…")

        # 启动片结束时固件已写下第一个角度；先读出来，循环里才好喂对应距离
        angle = rs.servo_read_angle()
        for _ in range(N_CHUNKS):
            dist_mm = ray_cast(angle) if angle is not None else 0
            angle = rs.twin_step(dist_mm, CHUNK_S)   # 喂距离→推进→读新角度

        # 跑完一整轮，关闭文件后端并把抓到的串口文本全部读回来
        text = rs.drain_uart_capture("sysbus.uart4", LOG)
        with open(LOG, "w", encoding="utf-8", errors="replace") as f:
            f.write(text)

        points, leds = parse(text)
        print(f"[*] 收到 {len(points)} 个测距点，{len(leds)} 次关键角点灯")

        cyc = points[:CYCLE_POINTS]                  # 只取第一个完整往返做校验
        sweep_ok = check_sweep([a for a, _ in cyc])
        leds_ok = sorted(leds[:6]) == [0, 0, 90, 90, 180, 180]  # 去/回程各闪3次
        bad = check_distance(cyc)
        print(f"[?] 扫描 0→180→0：{'OK' if sweep_ok else 'FAIL'}")
        print(f"[?] 关键点灯 0/90/180 各两次：{'OK' if leds_ok else 'FAIL ' + str(sorted(leds))}")
        print(f"[?] 距离与虚拟世界一致：{'OK' if not bad else 'FAIL ' + str(bad[:5])}")

        # 存 CSV（角度,距离mm），再画图
        with open(CSV, "w", encoding="utf-8") as f:
            f.write("angle_deg,distance_mm\n")
            for a, mm in cyc:
                f.write(f"{a},{mm}\n")
        print(f"[*] 数据: {CSV}")

        plot(cyc)
        rs.execute("quit")
        return 0 if (sweep_ok and leds_ok and not bad) else 1
    finally:
        rs.close()    # 无论成功失败都关掉 Renode，避免残留进程


if __name__ == "__main__":
    sys.exit(main())
