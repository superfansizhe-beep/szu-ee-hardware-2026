/* ============================================================================
 * lesson4_q2_liushuideng/main.c —— 笔试第2题「流水灯」全仿真版
 * ----------------------------------------------------------------------------
 * 题目：用外部中断(EXTI)，按键控制流水灯方向和频率。
 *   1. 上电默认 2Hz；
 *   2. 按键1(PA0)按下 -> 流动方向反转；
 *   3. 按键2(PA1)每按一次 -> 频率 2Hz -> 1Hz -> 0.5Hz -> 2Hz...
 *
 * 硬件映射（用 Discovery 板 4 个用户 LED 当流水灯）：
 *   灯: PD12 PD13 PD14 PD15（pos=0..3）
 *   键1: PA0（方向）  键2: PA1（频率），按下=高电平(上升沿触发)
 *
 * 这题的新东西：外部中断(EXTI)。
 *   Q1 是主循环不停"问"按键（轮询）；这里按键按下时芯片自动跳进中断函数。
 * ==========================================================================*/

#include <stdint.h>

#define MMIO(addr) (*(volatile uint32_t *)(addr))

/* ---- RCC 时钟 ---- */
#define RCC_AHB1ENR   MMIO(0x40023800u + 0x30u)   /* GPIOA/GPIOD 时钟 */
#define RCC_APB2ENR   MMIO(0x40023800u + 0x44u)   /* SYSCFG 时钟 */
#define RCC_SYSCFGEN  (1u << 14)

/* ---- GPIOA 按键输入 ---- */
#define GPIOA_MODER   MMIO(0x40020000u + 0x00u)

/* ---- GPIOD 灯输出 ---- */
#define GPIOD_MODER   MMIO(0x40020C00u + 0x00u)
#define GPIOD_BSRR    MMIO(0x40020C00u + 0x18u)
/* 4 个灯的"灭"位（高16位）：PD12/13/14/15 */
#define ALL_OFF  ((1u<<(12u+16u))|(1u<<(13u+16u))|(1u<<(14u+16u))|(1u<<(15u+16u)))

/* ---- SYSCFG：把 PA0/PA1 连到 EXTI0/EXTI1 ---- */
#define SYSCFG_EXTICR1  MMIO(0x40013800u + 0x08u)

/* ---- EXTI 控制器 ---- */
#define EXTI_IMR   MMIO(0x40013C00u + 0x00u)  /* 中断屏蔽：哪条线允许触发 */
#define EXTI_RTSR  MMIO(0x40013C00u + 0x08u)  /* 上升沿触发选择 */
#define EXTI_FTSR  MMIO(0x40013C00u + 0x0Cu)  /* 下降沿触发选择 */
#define EXTI_PR    MMIO(0x40013C00u + 0x14u)  /* 挂起位：发生中断的标志，写1清除 */

/* ---- NVIC：中断控制器（Cortex-M 内核外设，地址在 0xE000E000 区）---- */
#define NVIC_ISER0  MMIO(0xE000E100u)         /* 使能外部中断：写1=使能某号IRQ */

/* ---- 中断里要改、主循环要读的变量，必须加 volatile ---- */
static volatile uint32_t direction = 0u;   /* 0=正转 1=反转 */
static volatile uint32_t freq_idx  = 0u;   /* 0=2Hz 1=1Hz 2=0.5Hz */

/* ================= 启动代码 + 向量表 =================
 * 注意：这次向量表要加长，因为外部中断也要从表里找入口地址。
 * STM32F4: EXTI0 = IRQ6, EXTI1 = IRQ7；
 * 向量表下标 = IRQ号 + 16，所以 EXTI0 在第 22 项，EXTI1 在第 23 项。 */
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
typedef void (*isr_t)(void);
void Reset_Handler(void);
void Default_Handler(void);
void EXTI0_Handler(void);   /* PA0 方向键的中断函数 */
void EXTI1_Handler(void);   /* PA1 频率键的中断函数 */
int  main(void);

__attribute__((section(".isr_vector"), used))
isr_t vector_table[] = {
    /* 0~15：内核系统异常 */
    (isr_t)&_estack, Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler,
    0, Default_Handler, Default_Handler,
    /* 16 起：外部 IRQ。指定 EXTI0/EXTI1 的入口，其它默认 0(不用) */
    [22] = EXTI0_Handler,
    [23] = EXTI1_Handler,
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

/* ================= 两个中断函数（ISR）=================
 * 按键按下、硬件触发中断时，CPU 自动跳进这里，不用主循环管。
 * 记住：ISR 要短，做完事马上返回；并且必须清挂起位，否则会立刻再触发。 */

/* PA0（方向键）中断：翻转方向 */
void EXTI0_Handler(void)
{
    if (EXTI_PR & (1u << 0)) {      /* 确认是 EXTI0 触发的 */
        EXTI_PR = (1u << 0);        /* 写1清除挂起位（不清会一直触发） */
        direction ^= 1u;            /* 0变1、1变0：方向反转 */
    }
}

/* PA1（频率键）中断：切换频率档 */
void EXTI1_Handler(void)
{
    if (EXTI_PR & (1u << 1)) {
        EXTI_PR = (1u << 1);
        freq_idx = (freq_idx + 1u) % 3u;   /* 0->1->2->0 循环 */
    }
}

/* ================= 用户代码 ================= */

static void delay(volatile uint32_t n)
{
    while (n--) __asm__("nop");
}

int main(void)
{
    /* ---- 初始化：开时钟 ---- */
    RCC_AHB1ENR |= (1u << 0) | (1u << 3);   /* GPIOA + GPIOD */
    RCC_APB2ENR  |= RCC_SYSCFGEN;           /* SYSCFG（EXTI 要用到） */

    /* PA0/PA1 配成输入 */
    GPIOA_MODER &= ~((3u << 0) | (3u << 2));

    /* PD12~15 配成输出 */
    GPIOD_MODER = (GPIOD_MODER & ~((3u<<24)|(3u<<26)|(3u<<28)|(3u<<30)))
                | (1u<<24) | (1u<<26) | (1u<<28) | (1u<<30);
    GPIOD_BSRR = ALL_OFF;

    /* ---- EXTI 配置四步 ----
     * 1) SYSCFG：把 EXTI0 连到 PA0、EXTI1 连到 PA1（都选端口A=0x0） */
    SYSCFG_EXTICR1 &= ~((0xFu << 0) | (0xFu << 4));
    /* 2) 不屏蔽 EXTI0/EXTI1 */
    EXTI_IMR  |= (1u << 0) | (1u << 1);
    /* 3) 上升沿触发（按下=拉高时触发），不要下降沿 */
    EXTI_RTSR |= (1u << 0) | (1u << 1);
    EXTI_FTSR &= ~((1u << 0) | (1u << 1));
    /* 4) NVIC 使能 IRQ6(EXTI0) 和 IRQ7(EXTI1) */
    NVIC_ISER0 = (1u << 6) | (1u << 7);

    /* ---- 主循环：流水灯（完全不管按键，按键交给中断）----
     * pos: 0..3 对应 PD12..PD15；direction 决定往哪边挪；
     * freq_idx 决定停多久（2Hz=0.5s, 1Hz=1s, 0.5Hz=2s）。 */
    uint32_t pos = 0u;
    static const uint32_t period[3] = { 400000u, 800000u, 1600000u };

    for (;;) {
        GPIOD_BSRR = ALL_OFF;                 /* 先全灭 */
        GPIOD_BSRR = (1u << (12u + pos));     /* 亮当前 pos 那个灯 */

        /* 移动到下一个位置 */
        if (direction == 0u) pos = (pos + 1u) % 4u;   /* 正转 +1 */
        else                 pos = (pos + 3u) % 4u;  /* 反转 -1（等价+3） */

        delay(period[freq_idx]);              /* 按当前频率停一会儿 */
    }
}
