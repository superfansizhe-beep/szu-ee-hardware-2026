#!/usr/bin/env bash
# build.sh —— 两步交叉编译（和 L1 一样）
# 用法：PATH="/opt/homebrew/bin:$PATH" bash build.sh
set -euo pipefail

CC=arm-none-eabi-gcc
OBJCOPY=arm-none-eabi-objcopy

# 第1步：编译 main.c -> main.o（只编译、不链接）
$CC -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding -fno-builtin \
    -O2 -Wall -Wextra -c main.c -o main.o

# 第2步：按链接脚本把 main.o 链成 uart_hello.elf
$CC -mcpu=cortex-m4 -mthumb -nostdlib -T stm32f407.ld main.o -o uart_hello.elf

# 顺带导出 bin/hex（可选，方便对照）
$OBJCOPY -O binary uart_hello.elf uart_hello.bin
$OBJCOPY -O ihex   uart_hello.elf uart_hello.hex

echo "built:"
ls -la uart_hello.elf uart_hello.bin uart_hello.hex
