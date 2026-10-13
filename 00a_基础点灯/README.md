# L1 点灯（STM32F407 + Renode，macOS 已验证）

目标：让 PD12（Discovery 绿色 UserLED）在 Renode 里闪烁，并从寄存器读到它翻转。
这是 renode-mcu-survey 雷达固件的极简版，只保留“点灯”这一件事。

## 文件

| 文件 | 作用 |
|---|---|
| `main.c` | 固件：自带向量表/启动代码 + 点灯主逻辑（开时钟→配输出→循环拉高/拉低） |
| `stm32f407.ld` | 链接脚本：代码放 0x08000000 Flash，变量/栈放 0x20000000 RAM |
| `build.sh` | 两步编译：main.c → main.o → blink.elf（顺带导出 .bin/.hex） |
| `watch_led.py` | 观察手：在 Renode 里跑 elf，每 0.05 虚拟秒读一次 GPIOD_ODR，打印翻转 |
| `blink.elf/.bin/.hex` | 编译产物 |

## 怎么跑（在本目录下）

```bash
# 1) 编译（arm-none-eabi-gcc 在 /opt/homebrew/bin）
PATH="/opt/homebrew/bin:$PATH" bash build.sh

# 2) 仿真并观察 PD12 翻转（复用 survey 工程里已调通的 tool 包）
/opt/homebrew/bin/python3.12 watch_led.py
```

## 你应该看到

GPIOD_ODR 在 `0x00001000`（PD12=1，亮）和 `0x00000000`（PD12=0，灭）之间来回，
约每 0.25 虚拟秒翻一次，3 个虚拟秒共翻转十几次，最后打印“点灯成功”。

## 关键地址（要记的就这几个）

- RCC_AHB1ENR `0x40023830`：第 3 位=1 打开 GPIOD 时钟
- GPIOD_MODER `0x40020C00`：PD12 占第 24~25 位，写 01=输出
- GPIOD_BSRR  `0x40020C18`：写 `1<<12` 亮，写 `1<<28` 灭
- GPIOD_ODR   `0x40020C14`：引脚当前电平，第 12 位=1 就是亮（观察用）

## 自己动手改（加深理解）

1. 把 LED_PIN 从 12 改成 13（橙色灯），想想 MODER/BSRR/ODR 分别要变哪里。
2. 把 main.c 里 `delay(200000)` 调大/调小，重新编译跑，看翻转间隔怎么变。
3. 故意注释掉 `RCC_AHB1ENR |= ...` 那行，重编译跑，观察 ODR 是否还会变
   （答案：不会——没开时钟，写 GPIO 无效。这是新手第一大坑）。
