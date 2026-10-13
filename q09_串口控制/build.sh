#!/usr/bin/env bash
set -euo pipefail
CC=arm-none-eabi-gcc
OBJCOPY=arm-none-eabi-objcopy
$CC -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding -fno-builtin \
    -O2 -Wall -Wextra -c main.c -o main.o
$CC -mcpu=cortex-m4 -mthumb -nostdlib -T stm32f407.ld main.o -o q9.elf
$OBJCOPY -O binary q9.elf q9.bin
$OBJCOPY -O ihex   q9.elf q9.hex
echo "built:"
ls -la q9.elf q9.bin q9.hex
