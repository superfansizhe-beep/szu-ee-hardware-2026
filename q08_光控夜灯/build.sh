#!/usr/bin/env bash
set -euo pipefail
CC=arm-none-eabi-gcc
OBJCOPY=arm-none-eabi-objcopy
$CC -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding -fno-builtin \
    -O2 -Wall -Wextra -c main.c -o main.o
$CC -mcpu=cortex-m4 -mthumb -nostdlib -T stm32f407.ld main.o -o nightlight.elf
$OBJCOPY -O binary nightlight.elf nightlight.bin
$OBJCOPY -O ihex   nightlight.elf nightlight.hex
echo built
ls -la nightlight.elf nightlight.bin
