/* ============================================================================
 * q04_电机风扇/main.c —— 第四题「面对疾风吧！」TB6612 驱动风扇，两键加减挡
 * ----------------------------------------------------------------------------
 * TB6612 一个通道怎么控制直流电机：
 *   AIN1/AIN2（两个GPIO）决定方向：
 *     10 = 正转，01 = 反转，00 = 滑行(断电)，11 = 刹车(短接)
 *   PWMA  = 速度 PWM，占空比越大转得越快；
 *   STBY  = 待机脚，必须拉高芯片才工作，拉低整个驱动休眠。
 *
 * 本题：两个独立按键 加挡/减挡；挡位 0..3（0=停）。正转，速度随挡升高。
 *
 * 仿真：按键 PA0=加挡、PA1=减挡（轮询+沿检测）；AIN1/AIN2/STBY 用 GPIOE
 *       真实输出；PWMA 的占空比写进 mailbox（真板这里写定时器 CCR）。
 * ==========================================================================*/

#include <stdint.h>
#define MMIO(addr) (*(volatile uint32_t *)(addr))

/* ---- RCC ---- */
#define RCC_AHB1ENR MMIO(0x40023830u)
#define RCC_EN_GPIOA (1u << 0)
#define RCC_EN_GPIOE (1u << 4)

/* ---- 按键 GPIOA：PA0=加挡 PA1=减挡 ---- */
#define GPIOA_IDR MMIO(0x40020000u + 0x10u)
#define KEY_UP   (1u << 0)
#define KEY_DOWN (1u << 1)

/* ---- TB6612 控制脚 GPIOE：PE0=AIN1 PE1=AIN2 PE2=STBY ---- */
#define GPIOE_MODER MMIO(0x40021000u + 0x00u)
#define GPIOE_BSRR  MMIO(0x40021000u + 0x18u)
#define PIN_AIN1 0u
#define PIN_AIN2 1u
#define PIN_STBY 2u

/* ---- PWMA 占空比 mailbox（0..1000，真板写定时器CCR）---- */
#define PWM_DUTY MMIO(0x2001FF00u)

#define GEAR_MAX 3u
/* 各挡占空比（满刻度1000），0挡=0 */
static const uint32_t DUTY_TBL[GEAR_MAX + 1u] = { 0u, 300u, 600u, 1000u };

/* 启动代码（同模板） */
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

/* 用 BSRR 把某 PE 脚拉高/拉低（低16位置位、高16位复位）*/
static void pe_set(uint32_t pin, uint32_t high)
{
    GPIOE_BSRR = 1u << (high ? pin : pin + 16u);
}

/* 按当前挡位配置 TB6612：方向脚 + STBY + PWM占空比 */
static void apply_gear(uint32_t gear)
{
    pe_set(PIN_STBY, 1u);                 /* 驱动退出待机 */
    if (gear == 0u) {
        pe_set(PIN_AIN1, 0u);
        pe_set(PIN_AIN2, 0u);             /* 00 = 滑行停车 */
    } else {
        pe_set(PIN_AIN1, 1u);
        pe_set(PIN_AIN2, 0u);             /* 10 = 正转 */
    }
    PWM_DUTY = DUTY_TBL[gear];            /* 速度（真板：TIM CCR）*/
}

int main(void)
{
    RCC_AHB1ENR |= RCC_EN_GPIOA | RCC_EN_GPIOE;

    /* PE0/PE1/PE2 配成通用输出：清两位写01 */
    for (uint32_t pin = 0u; pin <= PIN_STBY; pin++) {
        GPIOE_MODER = (GPIOE_MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
    }

    uint32_t gear = 0u;
    uint32_t prev = 0u;
    apply_gear(gear);

    for (;;) {
        uint32_t cur = GPIOA_IDR;             /* 读两个按键电平 */
        uint32_t pressed = cur & ~prev;       /* 只看这一圈新按下的（上升沿）*/

        if ((pressed & KEY_UP) && gear < GEAR_MAX) gear++;
        if ((pressed & KEY_DOWN) && gear > 0u)     gear--;

        apply_gear(gear);
        prev = cur;                           /* 沿检测：记住这一圈 */
    }
}
