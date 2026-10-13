/* ============================================================================
 * lesson6_servo_pwm/main.c —— 舵机控制（Q5 核心，全仿真 mailbox 打桩）
 * ----------------------------------------------------------------------------
 * 学的东西：SG90 舵机 = 用"脉冲宽度"表示角度。
 *   每 20ms 一个脉冲（50Hz），高电平宽度：
 *     0.5ms (500us)  -> 0°
 *     1.5ms (1500us) -> 90°
 *     2.5ms (2500us) -> 180°
 *   线性映射：pulse_us = 500 + deg * 2000 / 180
 *
 * 真硬件上：这个 pulse_us 要写进定时器 TIM4 的 CCR 寄存器（PWM 输出）。
 * 仿真里：我们把 pulse_us 写进 SRAM 的 mailbox，Python 读出来看角度。
 *   题目里的"电位器"也是 mailbox 打桩——Python 写目标角度，固件读。
 *
 * mailbox（RAM 顶端附近，避开栈）：
 *   0x2001FF00  target_deg   Python写 -> 固件读（这就是"电位器转到的角度"）
 *   0x2001FF04  servo_pulse  固件写 -> Python读（当前该输出的脉宽us）
 * ==========================================================================*/

#include <stdint.h>
#define MMIO(addr) (*(volatile uint32_t *)(addr))

/* ---- mailbox ---- */
#define TWIN_TARGET_DEG  MMIO(0x2001FF00u)   /* Python 写：目标角度 0..180 */
#define TWIN_SERVO_PULSE MMIO(0x2001FF04u)   /* 固件写：对应脉宽 us */

/* 启动代码（同前） */
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
typedef void (*isr_t)(void);
void Reset_Handler(void);
void Default_Handler(void);
int  main(void);
__attribute__((section(".isr_vector"), used))
isr_t vector_table[16] = {
    (isr_t)&_estack, Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler,
    0, Default_Handler, Default_Handler,
};
__attribute__((naked))
void Reset_Handler(void)
{
    __asm volatile ("ldr r0, =_estack\n\t" "mov sp, r0\n\t" : : : "r0");
    for (uint32_t *p = &_sdata, *s = &_sidata; p < &_edata;) *p++ = *s++;
    for (uint32_t *p = &_sbss; p < &_ebss;) *p++ = 0;
    (void)main();
    for (;;) ;
}
void Default_Handler(void) { for (;;) ; }

/* 角度 -> 脉宽（SG90 线性映射） */
static uint32_t deg_to_pulse(uint32_t deg)
{
    if (deg > 180u) deg = 180u;
    return 500u + deg * 2000u / 180u;   /* 500us@0° ... 2500us@180° */
}

int main(void)
{
    /* 先把脉宽设到一个安全值（0°） */
    TWIN_SERVO_PULSE = deg_to_pulse(0u);

    for (;;) {
        uint32_t deg = TWIN_TARGET_DEG;      /* 读"电位器"（Python 写的角度） */
        TWIN_SERVO_PULSE = deg_to_pulse(deg); /* 算出脉宽并输出（写 mailbox） */
        /* 真板这里：把 pulse 写进 TIM4->CCR1；仿真里写 mailbox 即可 */
    }
}
