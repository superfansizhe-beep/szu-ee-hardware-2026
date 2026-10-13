#!/usr/bin/env bash
# build.sh —— 两步交叉编译
set -euo pipefail
CC=arm-none-eabi-gcc
OBJCOPY=arm-none-eabi-objcopy

$CC -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding -fno-builtin \
    -O2 -Wall -Wextra -c main.c -o main.o
$CC -mcpu=cortex-m4 -mthumb -nostdlib -T stm32f407.ld main.o -o q1.elf
$OBJCOPY -O binary q1.elf q1.bin
$OBJCOPY -O ihex   q1.elf q1.hex
echo "built:"
ls -la q1.elf q1.bin q1.hex
