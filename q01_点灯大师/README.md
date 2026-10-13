# L3 笔试第1题「点灯大师」（STM32F407 + Renode 全仿真）

题目：按键 A 按下灯亮（红）；按键 B 按下开始三色循环（红→绿→蓝）；再按 A 熄灭。

## 硬件映射（用 Discovery 板 4 个用户 LED 模拟三色灯）

| 题目 | 仿真实现 |
|---|---|
| 红色 | PD14 |
| 绿色 | PD12 |
| 蓝色 | PD15 |
| 按键 A | PA0（Renode 用 `OnGPIO 0 true/false` 模拟） |
| 按键 B | PA1 |

## 文件

- `main.c`：固件（GPIO 输入 + 状态机，带详细注释）
- `stm32f407.ld` / `build.sh`：链接脚本、编译脚本（同前）
- `watch_buttons.py`：模拟按键序列并读 ODR 观察灯色

## 跑法

```bash
cd /Users/fansizhe/jupyter_workspace/学习/lesson3_q1_diandeng
PATH="/opt/homebrew/bin:$PATH" bash build.sh
/opt/homebrew/bin/python3.12 watch_buttons.py
```

## 这题学到的新东西

1. **GPIO 输入**：读 `IDR` 寄存器知道引脚电平（L1 是输出写 ODR/BSRR）；
2. **沿检测**：只在"松开→按下"的瞬间响应一次，避免按住不放时状态乱跳；
3. **状态机**：用一个变量记录当前状态（灭/红常亮/三色循环），按按键在状态间跳转；
4. **消抖**：25ms 采样间隔兼顾响应和抗抖动。

## 自己改

- 把循环颜色顺序改成 蓝→绿→红；
- 把循环速度调快/调慢（改 `cycle_ticks >= 8` 这个阈值）；
- 在 CYCLE 状态下按 B 也能做点什么（比如重置回红色）。
