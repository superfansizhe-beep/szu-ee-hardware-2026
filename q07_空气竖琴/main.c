/* ============================================================================
 * q07_空气竖琴/main.c —— 第七题「空气竖琴（距离小琴）」
 * ----------------------------------------------------------------------------
 * 玩法：手在 HC-SR04 前移动，越近音调越高、越远越低；2~40cm 分 7 个区间对应
 *       Do Re Mi Fa Sol La Si；超出范围静音。
 *
 * 【有源 vs 无源蜂鸣器，别搞混】
 *   有源蜂鸣器：内部有振荡源，给高电平就"嘀——"，只能控制响/不响（Q6 那种）。
 *   无源蜂鸣器：内部没振荡源，必须由单片机给它"一定频率的方波"，频率=音高。
 *               改频率就变调；不给方波(停PWM)=静音。
 *
 * 仿真：距离由 Python 写 mailbox（假HC-SR04）；固件把距离映射成音阶，再把该音的
 *       频率写进 mailbox（真板这里是配置定时器 PWM 输出该频率到蜂鸣器脚）。
 * ==========================================================================*/

#include <stdint.h>
#define MMIO(addr) (*(volatile uint32_t *)(addr))

/* ---- mailbox：距离(传入,mm) 与 蜂鸣器频率(传出,Hz，0=静音) ---- */
#define DIST_MM   MMIO(0x2001FF00u)
#define BUZZ_FREQ MMIO(0x2001FF04u)

/* C大调音阶频率（C5..B5）：Do Re Mi Fa Sol La Si */
static const uint32_t NOTE_FREQ[7] = {
    523u, 587u, 659u, 698u, 784u, 880u, 988u
};

/* 启动代码（同模板，本题不用中断）*/
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

/* 距离(cm) -> 蜂鸣器频率；不在 2..40cm 返回 0(静音)。
 * 越近音越高：note = (40 - d_cm)*7/38，d=2 -> 6=Si，d=40 -> 0=Do。*/
static uint32_t cm_to_freq(uint32_t cm)
{
    if (cm < 2u || cm > 40u) return 0u;
    uint32_t note = (40u - cm) * 7u / 38u;   /* 0..6 */
    if (note > 6u) note = 6u;
    return NOTE_FREQ[note];
}

int main(void)
{
    BUZZ_FREQ = 0u;
    for (;;) {
        uint32_t cm = DIST_MM / 10u;        /* mm -> cm */
        BUZZ_FREQ = cm_to_freq(cm);         /* 真板：按频率配定时器PWM */
    }
}
