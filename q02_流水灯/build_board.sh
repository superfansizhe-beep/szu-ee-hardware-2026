#!/usr/bin/env bash
# build_board.sh —— 编译「真板版」固件 main_board.c
#
# 和 build.sh 的区别：
#   build.sh        编译仿真版 main.c     → 产物给 Renode 跑，main.c 必须原封不动
#   build_board.sh  编译真板版 main_board.c → 产物用 ST-Link / J-Link 烧进芯片
#
# 两者共用同一个链接脚本 stm32f407.ld（Flash 0x08000000 / RAM 128K）。
# 真板版不用 mailbox，所以栈可以老老实实用 RAM 顶端那 128K。
#
# 用法：
#   PATH="/opt/homebrew/bin:$PATH" bash build_board.sh
# 产物：<name>.elf / .bin / .hex
#   .hex  给 STM32CubeProgrammer / ST-Link Utility
#   .bin  给部分烧录工具或 Bootloader 用
set -euo pipefail

SRC="${1:-main_board.c}"
NAME="${2:-q2_board}"

# 【本工程特有】q2 必须编两个 .c：
#   main_board.c    include 公共层 stm32f4_common.h，里面已经有 98 个 weak 中断入口和向量表
#   q2_exti_isr.c   只声明 extern，不 include 公共层，用强符号顶掉 EXTI0/EXTI1 两个 weak 入口
# 之所以要拆两个文件：weak 符号只能被**另一个目标文件**的同名强符号覆盖，
# 在同一个 .c 里重定义同名函数会直接编译报错。
# 其它题没有自定义中断入口，所以 EXTRA_SRCS 是空的。

CC=${CC:-arm-none-eabi-gcc}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
command -v "$CC" >/dev/null 2>&1 || { echo "找不到 arm-none-eabi-gcc，请先装 GNU Arm Embedded Toolchain"; exit 1; }

# -mcpu=cortex-m4 -mthumb : 目标核。F407 是 Cortex-M4 带 FPU，浮点可用
# -nostdlib -ffreestanding: 不链 libc，我们只有裸机启动代码
# -O2 -Wall -Wextra      : 优化 + 把所有警告当错误看，验收前不留隐患
"$CC" -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding -fno-builtin \
      -O2 -Wall -Wextra -std=c11 -c "$SRC" -o main_board.o

EXTRA_SRCS="${EXTRA_SRCS:-q2_exti_isr.c}"
EXTRA_OBJS=""
for f in $EXTRA_SRCS; do
    obj="${f%.c}.o"
    "$CC" -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding -fno-builtin \
          -O2 -Wall -Wextra -std=c11 -c "$f" -o "$obj"
    EXTRA_OBJS="$EXTRA_OBJS $obj"
done

"$CC" -mcpu=cortex-m4 -mthumb -nostdlib -T stm32f407.ld \
      main_board.o $EXTRA_OBJS -o "${NAME}.elf"

"$OBJCOPY" -O binary "${NAME}.elf" "${NAME}.bin"
"$OBJCOPY" -O ihex   "${NAME}.elf" "${NAME}.hex"

echo "built: ${NAME}.elf / ${NAME}.bin / ${NAME}.hex"
arm-none-eabi-size "${NAME}.elf" 2>/dev/null || true
