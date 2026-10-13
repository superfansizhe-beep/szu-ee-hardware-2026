/* ============================================================================
 * q06_红外避障/main.c —— 第六题「此路不通！」避障报警器（基础+附加分级报警）
 * ----------------------------------------------------------------------------
 * 反射式红外模块怎么工作：红外管发光，碰到物体反射回来，接收管收到的"模拟强度 AO"
 *   越大 = 反射越强 = 物体越近。模块上有个比较器，把 AO 和电位器设定的阈值比，
 *   超过阈值就把数字脚 DO 拉为有效（这就是基础题用的开关量）。
 *
 * 基础要求：有遮挡 -> 有源蜂鸣器响 + 红灯亮；无遮挡 -> 静音 + 绿灯亮。
 * 附加要求：AO 接 ADC 读强度；越强(越近)蜂鸣器响得越急、红灯闪得越快（分级）。
 *
 * 仿真：AO 强度由 Python 写进 mailbox（0..4095，模拟ADC读数）；固件用阈值比较
 *       得到"有没有障碍"（复刻模块比较器），再按强度分3级，用 SysTick 非阻塞地
 *       让蜂鸣器+红灯以不同节奏响。有源蜂鸣器/红绿灯用 GPIO 真实输出。
 * 真板：AO 换成读 ADC；蜂鸣器/红绿灯脚照常用 GPIO。
 * ==========================================================================*/

#include <stdint.h>
#define MMIO(addr) (*(volatile uint32_t *)(addr))

/* ---- RCC ---- */
#define RCC_AHB1ENR MMIO(0x40023830u)
#define RCC_EN_GPIOD (1u << 3)
#define RCC_EN_GPIOE (1u << 4)

/* ---- GPIOD：PD12=绿灯 PD14=红灯 ---- */
#define GPIOD_MODER MMIO(0x40020C00u + 0x00u)
#define GPIOD_BSRR  MMIO(0x40020C00u + 0x18u)
#define PIN_GREEN 12u
#define PIN_RED   14u

/* ---- GPIOE：PE0=有源蜂鸣器（高电平响）---- */
#define GPIOE_MODER MMIO(0x40021000u + 0x00u)
#define GPIOE_BSRR  MMIO(0x40021000u + 0x18u)
#define PIN_BUZZER 0u

/* ---- AO 反射强度 mailbox（0..4095，模拟ADC）---- */
#define IR_AO MMIO(0x2001FF00u)

#define AO_THRESHOLD 800u   /* 超过它判定"有障碍"（真板对应模块上电位器阈值）*/

/* ---- SysTick 中断方式做毫秒计时（Renode 72MHz，1ms=72000下）---- */
#define SYST_CSR MMIO(0xE000E010u)
#define SYST_RVR MMIO(0xE000E014u)
#define SYST_CVR MMIO(0xE000E018u)
static volatile uint32_t g_ms = 0u;
static uint32_t millis(void) { return g_ms; }

/* 启动代码 + 向量表（第15项=SysTick）*/
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
typedef void (*isr_t)(void);
void Reset_Handler(void);
void Default_Handler(void);
void SysTick_Handler(void);
int  main(void);
__attribute__((section(".isr_vector"), used))
isr_t vector_table[16] = {
    (isr_t)&_estack, Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler,
    0, Default_Handler, SysTick_Handler,
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
void SysTick_Handler(void) { g_ms++; }

static void d_set(uint32_t pin, uint32_t hi) { GPIOD_BSRR = 1u << (hi ? pin : pin + 16u); }
static void e_set(uint32_t pin, uint32_t hi) { GPIOE_BSRR = 1u << (hi ? pin : pin + 16u); }

/* 把一个脚配成通用输出 */
static void make_output_d(uint32_t pin) {
    GPIOD_MODER = (GPIOD_MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
}
static void make_output_e(uint32_t pin) {
    GPIOE_MODER = (GPIOE_MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
}

int main(void)
{
    RCC_AHB1ENR |= RCC_EN_GPIOD | RCC_EN_GPIOE;
    make_output_d(PIN_GREEN); make_output_d(PIN_RED);
    make_output_e(PIN_BUZZER);

    /* SysTick：1ms 中断 */
    SYST_RVR = 72000u - 1u;
    SYST_CVR = 0u;
    SYST_CSR = (1u << 0) | (1u << 1) | (1u << 2);

    /* 报警节奏表：level 0=安全；1/2/3 的"半周期ms"（响这么久、停这么久）。
     * 越强半周期越短 -> 响得越急。*/
    static const uint32_t HALF_MS[4] = { 0u, 300u, 150u, 75u };

    uint32_t alarm = 0u, out = 0u, next_flip = 0u;

    /* 开机先按"安全"显示绿灯 */
    d_set(PIN_GREEN, 1u); d_set(PIN_RED, 0u); e_set(PIN_BUZZER, 0u);

    for (;;) {
        uint32_t ao = IR_AO;
        uint32_t level;
        if      (ao < AO_THRESHOLD) level = 0u;   /* 无障碍 */
        else if (ao < 1800u)       level = 1u;   /* 远：慢 */
        else if (ao < 3000u)       level = 2u;   /* 中 */
        else                       level = 3u;   /* 近：急 */

        if (level == 0u) {
            /* 安全：绿灯常亮，红灯/蜂鸣器关 */
            alarm = 0u;
            d_set(PIN_GREEN, 1u);
            d_set(PIN_RED, 0u);
            e_set(PIN_BUZZER, 0u);
        } else {
            if (!alarm) { alarm = 1u; out = 0u; next_flip = millis(); }
            if (millis() >= next_flip) {
                out ^= 1u;                       /* 翻转 响/停 */
                d_set(PIN_GREEN, 0u);
                d_set(PIN_RED, out);             /* 红灯跟着闪 */
                e_set(PIN_BUZZER, out);          /* 蜂鸣器跟着响 */
                next_flip = millis() + HALF_MS[level];
            }
        }
    }
}
