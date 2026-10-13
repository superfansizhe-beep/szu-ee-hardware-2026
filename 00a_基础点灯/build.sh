#!/bin/sh
# 编译 L1 点灯固件：bash build.sh
# 需要 arm-none-eabi-gcc（本机 homebrew 已装，在 /opt/homebrew/bin）。
# 不在 PATH 时可用环境变量覆盖： ARM_GCC=/完整/路径/arm-none-eabi-gcc bash build.sh
set -e
cd "$(dirname "$0")"
CC="${ARM_GCC:-arm-none-eabi-gcc}"
OBJCOPY="${OBJCOPY:-arm-none-eabi-objcopy}"

# 第 1 步：编译。把 main.c 翻译成 Cortex-M4 的目标文件 main.o（还没排地址）
#   -mcpu=cortex-m4 -mthumb  目标是 M4 内核、用 Thumb 指令集
#   -nostdlib -ffreestanding -fno-builtin  裸机：不链接标准库/操作系统
#   -O2                      开优化；-Wall -Wextra 打开全部常用警告
#   -c                       只编译不链接，输出 main.o
"$CC" -mcpu=cortex-m4 -mthumb \
      -nostdlib -ffreestanding -fno-builtin \
      -O2 -Wall -Wextra \
      -c main.c -o main.o

# 第 2 步：链接。按 stm32f407.ld 这张“平面图”把 main.o 排进固定地址，得到 elf
#   -T stm32f407.ld  使用我们的链接脚本
"$CC" -mcpu=cortex-m4 -mthumb -nostdlib \
      -T stm32f407.ld \
      main.o -o blink.elf

rm -f main.o

# 第 3 步（可选，仅用于对照）：从 elf 导出纯机器码 .bin 和带地址文本 .hex
"$OBJCOPY" -O binary blink.elf blink.bin
"$OBJCOPY" -O ihex   blink.elf blink.hex

echo "built:"; ls -la blink.elf blink.bin blink.hex
