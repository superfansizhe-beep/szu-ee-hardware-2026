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
NAME="${2:-q1_board}"

CC=${CC:-arm-none-eabi-gcc}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
command -v "$CC" >/dev/null 2>&1 || { echo "找不到 arm-none-eabi-gcc，请先装 GNU Arm Embedded Toolchain"; exit 1; }

# -mcpu=cortex-m4 -mthumb : 目标核。F407 是 Cortex-M4 带 FPU，浮点可用
# -nostdlib -ffreestanding: 不链 libc，我们只有裸机启动代码
# -O2 -Wall -Wextra      : 优化 + 把所有警告当错误看，验收前不留隐患
"$CC" -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding -fno-builtin \
      -O2 -Wall -Wextra -std=c11 -c "$SRC" -o main_board.o

"$CC" -mcpu=cortex-m4 -mthumb -nostdlib -T stm32f407.ld \
      main_board.o -o "${NAME}.elf"

"$OBJCOPY" -O binary "${NAME}.elf" "${NAME}.bin"
"$OBJCOPY" -O ihex   "${NAME}.elf" "${NAME}.hex"

echo "built: ${NAME}.elf / ${NAME}.bin / ${NAME}.hex"
arm-none-eabi-size "${NAME}.elf" 2>/dev/null || true
