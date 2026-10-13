# L2 UART 打印 hello（STM32F407 + Renode，macOS 已验证）

目标：让固件通过 UART4 打印 "hello from L2 (UART4)"，并在 Renode 里把这句话抓到。
这是雷达固件往外发 "角度:距离"、上位机能收到的前置技能。

## 文件

| 文件 | 作用 |
|---|---|
| `main.c` | 固件：自带启动代码 + UART4 初始化 + 循环打印 |
| `stm32f407.ld` | 链接脚本（和 L1 相同） |
| `build.sh` | 两步交叉编译 |
| `watch_uart.py` | 观察手：把 UART4 输出接到文件后端，跑完读出来 |
| `uart_hello.elf/.bin/.hex` | 编译产物 |
| `uart4.log` | 运行后生成，里面就是固件从 UART4 发出的文本 |

## 怎么跑（在本目录下）

```bash
# 1) 编译
PATH="/opt/homebrew/bin:$PATH" bash build.sh

# 2) 仿真并抓串口
/opt/homebrew/bin/python3.12 watch_uart.py
```

## 你应该看到

终端打印出好几行 `hello from L2 (UART4)`，并提示"UART 打印成功"。
同时本目录下生成 `uart4.log`，里面就是固件发出的原始字节。

## 关键地址（UART4）

- UART4 基址 `0x40004C00`，挂在 APB1 总线；
- `RCC_APB1ENR` @ `0x40023840`，UART4 时钟 = 第 19 位；
- `UART4_SR` @ `0x40004C00`：第 7 位 TXE=发送寄存器空；
- `UART4_DR` @ `0x40004C04`：写一个字节到这 = 发出去；
- `UART4_BRR` @ `0x40004C08`：波特率（Renode 不校验，写 0x682）；
- `UART4_CR1` @ `0x40004C0C`：0x2008 = 使能 UART(bit13) + 使能发送(bit3)。

## 自己动手改

1. 把打印内容改成你自己的话（比如 `uart_puts("I love embedded\r\n");`），重编重跑看 `uart4.log`。
2. 把 `delay(300000)` 调大/调小，看 2 秒内抓到的行数怎么变。
3. 进阶：在循环里加一个递增计数器并打印出来（需要写一个 `uart_putdec`，参考雷达固件的写法），让每行变成 `hello 1`、`hello 2`……
