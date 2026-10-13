#!/usr/bin/env bash
set -euo pipefail
CC=arm-none-eabi-gcc
OBJCOPY=arm-none-eabi-objcopy
$CC -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding -fno-builtin \
    -O2 -Wall -Wextra -c main.c -o main.o
$CC -mcpu=cortex-m4 -mthumb -nostdlib -T stm32f407.ld main.o -o servo.elf
$OBJCOPY -O binary servo.elf servo.bin
$OBJCOPY -O ihex   servo.elf servo.hex
echo "built:"
ls -la servo.elf servo.bin servo.hex
