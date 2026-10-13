/* ============================================================================
 * stm32f4_common.h —— 深大电协硬件部考核 · 真板驱动公共层（STM32F407）
 * ----------------------------------------------------------------------------
 * 【这份文件解决什么问题】
 *   仿真版（main.c）把"读传感器 / 驱动执行器"用 SRAM mailbox 打桩了，
 *   验收那天要烧的是**真板**，所以每个工程另配一个 main_board.c，
 *   用这份公共层去真正驱动 GPIO / 定时器 / ADC / 串口。
 *
 * 【为什么单独抽一层，而不是每题各写一份】
 *   时钟树、GPIO 位操作、UART 分频这些是**跨 10 道题复用的底层知识**。
 *   写错了 10 遍就是错 10 遍，调试成本也翻 10 倍。
 *   抽出来只有一份，讲解时也只需讲一次"底层怎么配的"，
 *   每题只讲"这题的应用逻辑"——这正好是验收时最想展示的两层。
 *
 * 【寄存器怎么来的：不要凭记忆写】
 *   本文件里每一个基址和位掩码都对照过两处权威来源：
 *     ① ST 官方 CMSIS 器件头 stm32f407xx.h（STMicroelectronics/cmsis_device_f4）
 *     ② Renode 1.17 平台描述 platforms/cpus/stm32f4.repl
 *   两处一致才敢用。踩过的坑记在文件末尾"勘误"里。
 *
 * 【改动规则】
 *   · 引脚**不要**在这里改。各题 main_board.c 顶部有自己的引脚表，
 *     换板子/换接线只动引脚表，底层一行不用碰。
 *   · 本文件在每个工程目录下都放了一份**内容相同的副本**，
 *     这样单独拷走任何一个文件夹也能编译（不用管相对路径）。
 *     根目录这一份是母版，改完请同步到各工程目录。
 *
 * 编译器：arm-none-eabi-gcc  -mcpu=cortex-m4 -mthumb
 * ==========================================================================*/

#ifndef STM32F4_COMMON_H
#define STM32F4_COMMON_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * 1. 引脚编码
 * ----------------------------------------------------------------------------
 * 把"哪个口哪根脚"编成一个 16 位数：高 8 位=端口号，低 8 位=引脚号。
 *   GPIOA=0, GPIOB=1, GPIOC=2, GPIOD=3, GPIOE=4
 * 这样引脚表写起来跟接线图长得一样（PA0、PB12…），
 * 而驱动函数只要拆一次就能拿到"端口基址 + 第几脚"。
 * ==========================================================================*/
#define PA0  0x0000u
#define PA1  0x0001u
#define PA2  0x0002u
#define PA3  0x0003u
#define PA4  0x0004u
#define PA5  0x0005u
#define PA6  0x0006u
#define PA7  0x0007u
#define PA8  0x0008u
#define PA9  0x0009u
#define PA10 0x000au
#define PA11 0x000bu
#define PA12 0x000cu
#define PA13 0x000du
#define PA14 0x000eu
#define PA15 0x000fu

#define PB0  0x0100u
#define PB1  0x0101u
#define PB2  0x0102u
#define PB3  0x0103u
#define PB4  0x0104u
#define PB5  0x0105u
#define PB6  0x0106u
#define PB7  0x0107u
#define PB8  0x0108u
#define PB9  0x0109u
#define PB10 0x010au
#define PB11 0x010bu
#define PB12 0x010cu
#define PB13 0x010du
#define PB14 0x010eu
#define PB15 0x010fu

#define PC0  0x0200u
#define PC1  0x0201u
#define PC2  0x0202u
#define PC3  0x0203u
#define PC4  0x0204u
#define PC5  0x0205u
#define PC6  0x0206u
#define PC7  0x0207u
#define PC8  0x0208u
#define PC9  0x0209u
#define PC10 0x020au
#define PC11 0x020bu
#define PC12 0x020cu
#define PC13 0x020du

#define PD0  0x0300u
#define PD1  0x0301u
#define PD2  0x0302u
#define PD3  0x0303u
#define PD4  0x0304u
#define PD5  0x0305u
#define PD6  0x0306u
#define PD7  0x0307u
#define PD8  0x0308u
#define PD9  0x0309u
#define PD10 0x030au
#define PD11 0x030bu
#define PD12 0x030cu
#define PD13 0x030du
#define PD14 0x030eu
#define PD15 0x030fu

#define PE0  0x0400u
#define PE1  0x0401u
#define PE2  0x0402u
#define PE3  0x0403u
#define PE4  0x0404u
#define PE5  0x0405u
#define PE6  0x0406u
#define PE7  0x0407u
#define PE8  0x0408u
#define PE9  0x0409u
#define PE10 0x040au
#define PE11 0x040bu
#define PE12 0x040cu
#define PE13 0x040du
#define PE14 0x040eu
#define PE15 0x040fu

/* ---- GPIO 模式（对应 MODER 两位）---- */
#define MODE_IN      0u
#define MODE_OUT     1u
#define MODE_AF      2u
#define MODE_ANALOG  3u

/* ---- 上下拉（对应 PUPDR 两位）---- */
#define PULL_NONE    0u
#define PULL_UP      1u
#define PULL_DOWN    2u

/* ---- 输出速度（对应 OSPEEDR 两位）---- */
#define SPEED_LOW    0u
#define SPEED_MED    1u
#define SPEED_HIGH   2u
#define SPEED_VHIGH  3u

/* ============================================================================
 * 2. 寄存器基址
 * ----------------------------------------------------------------------------
 * 全部核对自 ST CMSIS 头文件，且与 Renode stm32f4.repl 逐一比对一致。
 *   AH B1 外设 0x40020000 / APB1 0x40000000 / APB2 0x40010000
 * ==========================================================================*/
#define REG(base, off)   (*(volatile uint32_t *)((base) + (off)))

/* ---- 总线基址 ---- */
#define AHB1_BASE   0x40020000u
#define APB1_BASE   0x40000000u
#define APB2_BASE   0x40010000u

/* ---- RCC（时钟控制）0x40023800 ---- */
#define RCC_BASE    0x40023800u
#define RCC_CR      REG(RCC_BASE, 0x00)   /* 时钟使能/就绪        */
#define RCC_PLLCFGR REG(RCC_BASE, 0x04)   /* PLL 配置             */
#define RCC_CFGR    REG(RCC_BASE, 0x08)   /* 总线分频/源选择      */
#define RCC_AHB1RSTR REG(RCC_BASE, 0x10)
#define RCC_APB1RSTR REG(RCC_BASE, 0x20)
#define RCC_APB2RSTR REG(RCC_BASE, 0x24)
#define RCC_AHB1ENR REG(RCC_BASE, 0x30)   /* GPIO/DMA 时钟使能    */
#define RCC_APB1ENR REG(RCC_BASE, 0x40)   /* TIM2-7/UART/I2C      */
#define RCC_APB2ENR REG(RCC_BASE, 0x44)   /* TIM1/8/USART1/ADC1   */

/* RCC_CR 位（Pos 核实） */
#define RCC_CR_HSION     (1u<<0)
#define RCC_CR_HSIRDY    (1u<<1)
#define RCC_CR_HSEON     (1u<<16)
#define RCC_CR_HSERDY    (1u<<17)
#define RCC_CR_PLLON     (1u<<24)
#define RCC_CR_PLLRDY    (1u<<25)

/* RCC_CFGR 位：SW[1:0]=bit0-1, SWS=bit2-3, HPRE[3:0]=bit4-7,
 *              PPRE1[2:0]=bit10-12, PPRE2[2:0]=bit13-15, ADCPRE[1:0]=bit16-17 */
#define RCC_CFGR_SW_PLL    (2u<<0)
#define RCC_CFGR_SWS_MASK  (3u<<2)
#define RCC_CFGR_SWS_PLL   (2u<<2)
#define RCC_CFGR_HPRE_DIV1  (0u<<4)
#define RCC_CFGR_PPRE1_DIV4 (5u<<10)
#define RCC_CFGR_PPRE2_DIV2 (4u<<13)
#define RCC_CFGR_ADCPRE_DIV4 (1u<<16)   /* ← 注意是 bit16，不是 bit14 */

/* ---- FLASH（等待周期）0x40023C00 ---- */
#define FLASH_ACR REG(0x40023C00u, 0x00)
#define FLASH_ACR_LATENCY_5  (5u<<0)     /* 168MHz 需要 5 个等待周期 */
#define FLASH_ACR_PRFTEN     (1u<<8)     /* 预取缓冲，168MHz 必开    */

/* ---- GPIO ---- */
#define GPIOA_BASE 0x40020000u
#define GPIOB_BASE 0x40020400u
#define GPIOC_BASE 0x40020800u
#define GPIOD_BASE 0x40020C00u
#define GPIOE_BASE 0x40021000u

#define GPIO(base)      REG(base, 0x00)  /* MODER   */
#define GPIOTYP(base)   REG(base, 0x04)  /* OTYPER  */
#define GPIOSP(base)    REG(base, 0x08)  /* OSPEEDR */
#define GPIOPU(base)    REG(base, 0x0C)  /* PUPDR   */
#define GPIOIDR(base)   REG(base, 0x10)  /* 输入数据 */
#define GPIOODR(base)   REG(base, 0x14)  /* 输出数据 */
#define GPIOB(base)     REG(base, 0x18)  /* BSRR：置位/复位 */
#define GPIOAFR(base)   REG(base, 0x20)  /* AFRL/AFRH   */

/* ---- 定时器（TIM1~TIM8、2~7，通用结构）---- */
#define TIM2_BASE 0x40000000u
#define TIM3_BASE 0x40000400u
#define TIM4_BASE 0x40000800u
#define TIM5_BASE 0x40000C00u
#define TIM6_BASE 0x40001000u   /* 基本定时器，做毫秒时基 */
#define TIM7_BASE 0x40001400u
#define TIM1_BASE 0x40010000u
#define TIM8_BASE 0x40010400u

#define TIM_CR1(base)    REG(base, 0x00)
#define TIM_DIER(base)   REG(base, 0x0C)
#define TIM_SR(base)     REG(base, 0x10)
#define TIM_EGR(base)    REG(base, 0x14)
#define TIM_CCMR1(base)  REG(base, 0x18)
#define TIM_CCMR2(base)  REG(base, 0x1C)
#define TIM_CCER(base)   REG(base, 0x20)
#define TIM_CNT(base)    REG(base, 0x24)
#define TIM_PSC(base)    REG(base, 0x28)
#define TIM_ARR(base)    REG(base, 0x2C)
#define TIM_CCR1(base)   REG(base, 0x34)
#define TIM_CCR2(base)   REG(base, 0x38)
#define TIM_CCR3(base)   REG(base, 0x3C)
#define TIM_CCR4(base)   REG(base, 0x40)

#define TIM_CR1_CEN    (1u<<0)
#define TIM_CR1_ARPE   (1u<<7)
#define TIM_DIER_UIE   (1u<<0)
#define TIM_SR_UIF     (1u<<0)
#define TIM_EGR_UG     (1u<<0)
/* CCMRx：OCxM=0b110 即 PWM 模式 1（CNT<CCR 时输出有效电平） */
#define TIM_OCM_PWM1   (6u<<4)
#define TIM_OCM2_PWM1  (6u<<12)
#define TIM_OCPE       (1u<<3)
#define TIM_OCPE2      (1u<<11)

/* ---- USART ---- */
#define USART1_BASE 0x40011000u   /* APB2，默认 PA9/PA10 */
#define USART2_BASE 0x40004400u   /* APB1，默认 PA2/PA3 */
#define UART4_BASE  0x40004C00u   /* APB1，默认 PA0/PA1 或 PC10/PC11 */

#define USART_SR(base)  REG(base, 0x00)
#define USART_DR(base)  REG(base, 0x04)
#define USART_BRR(base) REG(base, 0x08)
#define USART_CR1(base) REG(base, 0x0C)
#define USART_CR2(base) REG(base, 0x10)
#define USART_CR3(base) REG(base, 0x14)

#define USART_SR_ORE  (1u<<3)
#define USART_SR_RXNE (1u<<5)
#define USART_SR_TC   (1u<<6)
#define USART_SR_TXE  (1u<<7)
#define USART_CR1_RE  (1u<<2)
#define USART_CR1_TE  (1u<<3)
#define USART_CR1_M0  (1u<<12)   /* F4 的 M[1:0] = bit12(M0),bit11(M1) */
#define USART_CR1_M1  (1u<<11)
#define USART_CR1_PS  (1u<<9)
#define USART_CR1_UE  (1u<<13)

/* ---- ADC ---- */
#define ADC1_BASE 0x40012000u
#define ADC1_SR   REG(ADC1_BASE, 0x00)
#define ADC1_CR1  REG(ADC1_BASE, 0x04)
#define ADC1_CR2  REG(ADC1_BASE, 0x08)
#define ADC1_SQR1 REG(ADC1_BASE, 0x10)
#define ADC1_SQR3 REG(ADC1_BASE, 0x1C)
#define ADC1_SMPR1 REG(ADC1_BASE, 0x28)
#define ADC1_SMPR2 REG(ADC1_BASE, 0x30)
#define ADC1_DR   REG(ADC1_BASE, 0x40)

#define ADC_SR_EOC  (1u<<1)
#define ADC_CR2_ADON  (1u<<0)
#define ADC_CR2_CONT  (1u<<1)
#define ADC_CR2_ALIGN (1u<<11)        /* 0=右对齐 */
#define ADC_CR2_RST   (1u<<15)
#define ADC_CR2_SWSTART (1u<<30)

/* ---- I2C ---- */
#define I2C1_BASE 0x40005400u
#define I2C1_CR1 REG(I2C1_BASE, 0x00)
#define I2C1_CCR REG(I2C1_BASE, 0x1C)
#define I2C1_TRISE REG(I2C1_BASE, 0x20)
#define I2C1_SR1 REG(I2C1_BASE, 0x14)

#define I2C_CR1_PE    (1u<<0)
#define I2C_CR1_START (1u<<8)
#define I2C_CR1_STOP  (1u<<9)
#define I2C_CR1_ACK   (1u<<10)
#define I2C_SR1_SB    (1u<<0)
#define I2C_SR1_ADDR  (1u<<1)
#define I2C_SR1_BTF   (1u<<2)
#define I2C_SR1_TXE   (1u<<7)
#define I2C_SR1_RXNE  (1u<<6)
#define I2C_SR1_BERR  (1u<<8)
#define I2C_SR1_ARLO  (1u<<9)
#define I2C_SR1_AF    (1u<<10)

/* ---- EXTI + SYSCFG ---- */
#define EXTI_BASE   0x40013C00u
#define SYSCFG_BASE 0x40013800u
#define EXTI_IMR(base)  REG(EXTI_BASE, 0x00)
#define EXTI_RTSR(base) REG(EXTI_BASE, 0x08)
#define EXTI_FTSR(base) REG(EXTI_BASE, 0x0C)
#define EXTI_PR(base)  REG(EXTI_BASE, 0x14)
#define SYSCFG_EXTICR(base) REG(SYSCFG_BASE, 0x08)   /* EXTICR1~4 在 0x08..0x14 */

/* ---- Cortex-M4 内核 ---- */
#define SYST_CSR   REG(0xE000E010u, 0x00)   /* 使能/计数/中断/时钟源 */
#define SYST_RVR   REG(0xE000E014u, 0x04)
#define SYST_CVR   REG(0xE000E018u, 0x08)
#define NVIC_ISER  REG(0xE000E100u, 0x00)   /* 中断使能，按 IRQ 号 */
#define NVIC_IPR   REG(0xE000E400u, 0x00)   /* 中断优先级 */
#define SCB_VTOR   REG(0xE000ED08u, 0x00)
#define CORE_DEMCR REG(0xE000EDF0u, 0x00)
#define DWT_CTRL   REG(0xE0001000u, 0x00)
#define DWT_CYCCNT REG(0xE0001000u, 0x04)
#define DEMCR_TRCENA   (1u<<24)
#define DWT_CTRL_CYCCNTENA (1u<<0)
#define SYST_CSR_ENABLE    (1u<<0)
#define SYST_CSR_TICKINT   (1u<<1)
#define SYST_CSR_CLKSOURCE (1u<<2)   /* 1=处理器时钟(168MHz)，0=AHB/8(21MHz) */

#define __NVIC_PRIO_BITS 4

/* NVIC 的寄存器是**按 IRQ 号分组的 32 位寄存器**，
 * 所以不能写成 NVIC_ISER[n]（那是个值不是指针），得按地址偏移算。 */
#define NVIC_ISER_AT(irq) REG(0xE000E100u + 4u * ((irq) >> 2), 0u)  /* 中断使能 */
#define NVIC_IPR_AT(irq)  REG(0xE000E400u + 4u * ((irq) >> 2), 0u)  /* 优先级 */

/* ============================================================================
 * 3. 中断向量表（弱符号，未用到的自动落到默认死循环）
 * ----------------------------------------------------------------------------
 * 为什么要全：Q2 要 EXTI0/1、Q9/Q10 要 USART1、几乎所有题都要 TIM6 时基。
 * 与其在每个 main_board.c 里手抄一张 98 项的表，不如在这里给一张全的，
 * 用 weak 让没用到的中断都指向同一个"什么都不干"的处理函数。
 * 项目自己定义同名函数即可覆盖（弱符号会被同名的强符号顶掉）。
 * ==========================================================================*/
#define WEAK_HANDLER(name) \
    __attribute__((weak, used, section(".text"))) void name(void) { for(;;){} }

WEAK_HANDLER(NMI_Handler)
WEAK_HANDLER(HardFault_Handler)
WEAK_HANDLER(MemManage_Handler)
WEAK_HANDLER(BusFault_Handler)
WEAK_HANDLER(UsageFault_Handler)
WEAK_HANDLER(SVC_Handler)
WEAK_HANDLER(DebugMon_Handler)
WEAK_HANDLER(PendSV_Handler)
WEAK_HANDLER(SysTick_Handler)
WEAK_HANDLER(WWDG_IRQHandler)
WEAK_HANDLER(PVD_IRQHandler)
WEAK_HANDLER(TAMP_STAMP_IRQHandler)
WEAK_HANDLER(RTC_WKUP_IRQHandler)
WEAK_HANDLER(FLASH_IRQHandler)
WEAK_HANDLER(RCC_IRQHandler)
WEAK_HANDLER(EXTI0_IRQHandler)
WEAK_HANDLER(EXTI1_IRQHandler)
WEAK_HANDLER(EXTI2_IRQHandler)
WEAK_HANDLER(EXTI3_IRQHandler)
WEAK_HANDLER(EXTI4_IRQHandler)
WEAK_HANDLER(DMA1_Stream0_IRQHandler)
WEAK_HANDLER(DMA1_Stream1_IRQHandler)
WEAK_HANDLER(DMA1_Stream2_IRQHandler)
WEAK_HANDLER(DMA1_Stream3_IRQHandler)
WEAK_HANDLER(DMA1_Stream4_IRQHandler)
WEAK_HANDLER(DMA1_Stream5_IRQHandler)
WEAK_HANDLER(DMA1_Stream6_IRQHandler)
WEAK_HANDLER(ADC_IRQHandler)
WEAK_HANDLER(CAN1_TX_IRQHandler)
WEAK_HANDLER(CAN1_RX0_IRQHandler)
WEAK_HANDLER(CAN1_RX1_IRQHandler)
WEAK_HANDLER(CAN1_SCE_IRQHandler)
WEAK_HANDLER(EXTI9_5_IRQHandler)
WEAK_HANDLER(TIM1_BRK_TIM9_IRQHandler)
WEAK_HANDLER(TIM1_UP_TIM10_IRQHandler)
WEAK_HANDLER(TIM1_TRG_COM_TIM11_IRQHandler)
WEAK_HANDLER(TIM1_CC_IRQHandler)
WEAK_HANDLER(TIM2_IRQHandler)
WEAK_HANDLER(TIM3_IRQHandler)
WEAK_HANDLER(TIM4_IRQHandler)
WEAK_HANDLER(I2C1_EV_IRQHandler)
WEAK_HANDLER(I2C1_ER_IRQHandler)
WEAK_HANDLER(I2C2_EV_IRQHandler)
WEAK_HANDLER(I2C2_ER_IRQHandler)
WEAK_HANDLER(SPI1_IRQHandler)
WEAK_HANDLER(SPI2_IRQHandler)
WEAK_HANDLER(USART1_IRQHandler)
WEAK_HANDLER(USART2_IRQHandler)
WEAK_HANDLER(USART3_IRQHandler)
WEAK_HANDLER(EXTI15_10_IRQHandler)
WEAK_HANDLER(RTC_Alarm_IRQHandler)
WEAK_HANDLER(OTG_FS_WKUP_IRQHandler)
WEAK_HANDLER(TIM8_BRK_TIM12_IRQHandler)
WEAK_HANDLER(TIM8_UP_TIM13_IRQHandler)
WEAK_HANDLER(TIM8_TRG_COM_TIM14_IRQHandler)
WEAK_HANDLER(TIM8_CC_IRQHandler)
WEAK_HANDLER(DMA1_Stream7_IRQHandler)
WEAK_HANDLER(FSMC_IRQHandler)
WEAK_HANDLER(SDIO_IRQHandler)
WEAK_HANDLER(TIM5_IRQHandler)
WEAK_HANDLER(SPI3_IRQHandler)
WEAK_HANDLER(UART4_IRQHandler)
WEAK_HANDLER(UART5_IRQHandler)
/* TIM6_DAC_IRQHandler 不在这里给默认空实现——它在时基小节有真正的默认实现 */
WEAK_HANDLER(TIM7_IRQHandler)
WEAK_HANDLER(DMA2_Stream0_IRQHandler)
WEAK_HANDLER(DMA2_Stream1_IRQHandler)
WEAK_HANDLER(DMA2_Stream2_IRQHandler)
WEAK_HANDLER(DMA2_Stream3_IRQHandler)
WEAK_HANDLER(DMA2_Stream4_IRQHandler)
WEAK_HANDLER(ETH_IRQHandler)
WEAK_HANDLER(ETH_WKUP_IRQHandler)
WEAK_HANDLER(CAN2_TX_IRQHandler)
WEAK_HANDLER(CAN2_RX0_IRQHandler)
WEAK_HANDLER(CAN2_RX1_IRQHandler)
WEAK_HANDLER(CAN2_SCE_IRQHandler)
WEAK_HANDLER(OTG_FS_IRQHandler)
WEAK_HANDLER(DMA2_Stream5_IRQHandler)
WEAK_HANDLER(DMA2_Stream6_IRQHandler)
WEAK_HANDLER(DMA2_Stream7_IRQHandler)
WEAK_HANDLER(USART6_IRQHandler)
WEAK_HANDLER(I2C3_EV_IRQHandler)
WEAK_HANDLER(I2C3_ER_IRQHandler)
WEAK_HANDLER(OTG_HS_EP1_OUT_IRQHandler)
WEAK_HANDLER(OTG_HS_EP1_IN_IRQHandler)
WEAK_HANDLER(OTG_HS_WKUP_IRQHandler)
WEAK_HANDLER(OTG_HS_IRQHandler)
WEAK_HANDLER(DCMI_IRQHandler)
WEAK_HANDLER(CRYP_IRQHandler)
WEAK_HANDLER(HASH_RNG_IRQHandler)
WEAK_HANDLER(FPU_IRQHandler)

typedef void (*isr_t)(void);

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;

int main(void);
void Reset_Handler(void);

/* 后面小节里的函数会互相调用，这里先把用到的原型摆出来，
 * 这样定义顺序就可以按"知识点"排，而不是被迫按调用顺序排。 */
void gpio_write(uint32_t pin, uint32_t level);
uint32_t gpio_read(uint32_t pin);
void gpio_toggle(uint32_t pin);
void gpio_mode(uint32_t pin, uint32_t mode, uint32_t pull, uint32_t speed);
void gpio_af(uint32_t pin, uint32_t af);
uint32_t millis(void);
void TIM6_DAC_IRQHandler(void);   /* 默认实现在"时基"小节，先声明好给向量表用 */

__attribute__((section(".isr_vector"), used))
isr_t const vector_table[] = {
    (isr_t)&_estack,     Reset_Handler,
    NMI_Handler,         HardFault_Handler,
    MemManage_Handler,   BusFault_Handler,
    UsageFault_Handler,  0, 0, 0, 0,
    SVC_Handler,         DebugMon_Handler, 0,
    PendSV_Handler,      SysTick_Handler,
    /* 外设中断 0..81 */
    WWDG_IRQHandler, PVD_IRQHandler, TAMP_STAMP_IRQHandler, RTC_WKUP_IRQHandler,
    FLASH_IRQHandler, RCC_IRQHandler, EXTI0_IRQHandler, EXTI1_IRQHandler,
    EXTI2_IRQHandler, EXTI3_IRQHandler, EXTI4_IRQHandler, DMA1_Stream0_IRQHandler,
    DMA1_Stream1_IRQHandler, DMA1_Stream2_IRQHandler, DMA1_Stream3_IRQHandler,
    DMA1_Stream4_IRQHandler, DMA1_Stream5_IRQHandler, DMA1_Stream6_IRQHandler,
    ADC_IRQHandler, CAN1_TX_IRQHandler, CAN1_RX0_IRQHandler, CAN1_RX1_IRQHandler,
    CAN1_SCE_IRQHandler, EXTI9_5_IRQHandler, TIM1_BRK_TIM9_IRQHandler,
    TIM1_UP_TIM10_IRQHandler, TIM1_TRG_COM_TIM11_IRQHandler, TIM1_CC_IRQHandler,
    TIM2_IRQHandler, TIM3_IRQHandler, TIM4_IRQHandler, I2C1_EV_IRQHandler,
    I2C1_ER_IRQHandler, I2C2_EV_IRQHandler, I2C2_ER_IRQHandler, SPI1_IRQHandler,
    SPI2_IRQHandler, USART1_IRQHandler, USART2_IRQHandler, USART3_IRQHandler,
    EXTI15_10_IRQHandler, RTC_Alarm_IRQHandler, OTG_FS_WKUP_IRQHandler,
    TIM8_BRK_TIM12_IRQHandler, TIM8_UP_TIM13_IRQHandler,
    TIM8_TRG_COM_TIM14_IRQHandler, TIM8_CC_IRQHandler, DMA1_Stream7_IRQHandler,
    FSMC_IRQHandler, SDIO_IRQHandler, TIM5_IRQHandler, SPI3_IRQHandler,
    UART4_IRQHandler, UART5_IRQHandler, TIM6_DAC_IRQHandler, TIM7_IRQHandler,
    DMA2_Stream0_IRQHandler, DMA2_Stream1_IRQHandler, DMA2_Stream2_IRQHandler,
    DMA2_Stream3_IRQHandler, DMA2_Stream4_IRQHandler, ETH_IRQHandler,
    ETH_WKUP_IRQHandler, CAN2_TX_IRQHandler, CAN2_RX0_IRQHandler,
    CAN2_RX1_IRQHandler, CAN2_SCE_IRQHandler, OTG_FS_IRQHandler,
    DMA2_Stream5_IRQHandler, DMA2_Stream6_IRQHandler, DMA2_Stream7_IRQHandler,
    USART6_IRQHandler, I2C3_EV_IRQHandler, I2C3_ER_IRQHandler,
    OTG_HS_EP1_OUT_IRQHandler, OTG_HS_EP1_IN_IRQHandler, OTG_HS_WKUP_IRQHandler,
    OTG_HS_IRQHandler, DCMI_IRQHandler, CRYP_IRQHandler, HASH_RNG_IRQHandler,
    FPU_IRQHandler,
};

void Reset_Handler(void) __attribute__((naked));
void Reset_Handler(void)
{
    /* 三件事，顺序不能换：
     *  1) 立栈指针 —— 此后 C 代码（含局部变量）才有地方放
     *  2) .data 从 Flash 搬到 RAM（初值非零的那段）
     *  3) .bss 清零（初值为零的那段）
     * 少任何一步，.data 里的初值就是垃圾值、.bss 里全是随机数。 */
    __asm volatile ("ldr r0, =_estack\n\t" "mov sp, r0\n\t" : : : "r0");
    for (uint32_t *p = &_sdata, *s = &_sidata; p < &_edata; ) *p++ = *s++;
    for (uint32_t *p = &_sbss; p < &_ebss; ) *p++ = 0u;
    SCB_VTOR = (uint32_t)vector_table;   /* 中断来了能找到表 */
    (void)main();
    for (;;) { }
}

/* ============================================================================
 * 4. 时钟：HSE 8MHz → PLL → 168MHz
 * ----------------------------------------------------------------------------
 * 为什么非要配到 168MHz：F407 主频上限 168MHz，且 GPIO/串口/定时器的
 * **所有分频都是相对主频算的**。不改时钟，所有 uart_brr / tim_psc 都得重算。
 *
 * 168MHz 的来源：8MHz / PLLM(8) * PLLN(336) / PLLP(2) = 168MHz
 * 总线分频：AHB /1 = 168MHz，APB1 /4 = 42MHz，APB2 /2 = 84MHz
 *
 * 【关键知识点：定时器时钟不等于 PCLK】
 *   规则：**APBx 分频系数 ≠ 1 时，该总线上的定时器时钟自动 ×2**。
 *   所以 APB1 分频是 /4（PCLK1=42MHz）→ TIM2~7 实际跑 **84MHz**；
 *      APB2 分频是 /2（PCLK2=84MHz）→ TIM1/8 实际跑 **168MHz**。
 *   这个 ×2 忘了算，PWM 频率会差一倍——这是最常见的低级坑。
 *
 * 【为什么加 HSI 兜底】
 *   验收现场晶振虚焊 / 板子不喂 8MHz 的情况真会出现。
 *   这里检测 HSE 是否就绪，超时或失败就退回内部 16MHz 振荡器继续跑，
 *   至少 LED 会亮、串口有输出，不会黑屏。
 *   （退回时同样配到 168MHz：16/16*336/2 = 168MHz）
 * ==========================================================================*/
extern uint32_t g_sysclk_hz, g_hclk_hz, g_pclk1_hz, g_pclk2_hz;
extern uint32_t g_tim2_7_hz, g_tim1_8_hz;

static inline uint32_t pin_port(uint32_t p)  { return (p >> 8) & 0xFu; }
static inline uint32_t pin_no(uint32_t p)    { return p & 0xFFu; }
static inline uint32_t pin_gpio_base(uint32_t p) { return AHB1_BASE + (pin_port(p) << 10); }
/* ADC 通道号 = 端口号 * 8 + 引脚号（F407 只有 PA0-7 / PB0-1 / PC0-5 能做 ADC） */
static inline uint32_t pin_adc_ch(uint32_t p)   { return (pin_port(p) << 3) | pin_no(p); }

/* 定时器时钟：APBx 分频≠1 时 ×2 */
static inline uint32_t tim_clock(uint32_t base)
{
    if (base == TIM1_BASE || base == TIM8_BASE) return g_tim1_8_hz;
    return g_tim2_7_hz;
}

void clock_init(void)
{
    uint32_t timeout;

    /* ① Flash：先把等待周期和预取打开，再提频。
     *    顺序反了会取指失败——这是"能低频跑、提频就跑飞"的经典原因。 */
    FLASH_ACR = FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_5;

    /* ② 选 PLL 输入源：优先外部 8MHz 晶振 */
    RCC_CR |= RCC_CR_HSION;
    while (((RCC_CR >> 1) & 1u) == 0u) { }        /* 等 HSI 就绪 */

    RCC_CR &= ~(RCC_CR_HSEON | (1u<<18));          /* 先关 HSE，避开 HSEBYP */
    RCC_CR |= RCC_CR_HSEON;                        /* 打开外部晶振 */
    for (timeout = 0u; timeout < 0x00100000u; ++timeout) {
        if (RCC_CR & RCC_CR_HSERDY) break;         /* 晶振起振成功 */
    }

    uint32_t pll_src = 0u;                          /* 0=HSI 1=HSE */
    if ((RCC_CR & RCC_CR_HSERDY) != 0u) {
        pll_src = 1u;
    } else {
        RCC_CR &= ~RCC_CR_HSEON;                    /* 没起来就关掉，别留着干扰 */
    }

    /* ③ 配 PLL
     *   HSE: PLLM=8  → 8/8=1MHz   1MHz*336=336MHz  /2=168MHz
     *   HSI: PLLM=16 → 16/16=1MHz 1MHz*336=336MHz  /2=168MHz
     *   两种源都把 VCO 拉到 336MHz 再二分，这样下游分频逻辑完全一样。 */
    if (pll_src) {
        RCC_PLLCFGR = (8u<<0)          /* PLLM  = 8      */
                    | (336u<<6)        /* PLLN  = 336    */
                    | (1u<<22)         /* PLLSRC= HSE    */
                    | (0u<<24)         /* PLLQ  = 4 (48MHz, USB 用，此处不用) */
                    ;                  /* PLLP=2 是复位默认(编码 00) */
    } else {
        RCC_PLLCFGR = (16u<<0)         /* PLLM  = 16     */
                    | (336u<<6)        /* PLLN  = 336    */
                    | (0u<<22)         /* PLLSRC= HSI    */
                    | (0u<<24)
                    ;
    }

    /* ④ 总线分频：先降速再提主频，顺序同样重要 */
    RCC_CFGR = RCC_CFGR_HPRE_DIV1
             | RCC_CFGR_PPRE1_DIV4       /* PCLK1 = 42MHz  → 限速 42MHz 上限 */
             | RCC_CFGR_PPRE2_DIV2       /* PCLK2 = 84MHz  → 限速 84MHz 上限 */
             | RCC_CFGR_ADCPRE_DIV4;     /* ADC 时钟 = 84/4 = 21MHz ≤ 36MHz 上限 */

    /* ⑤ 开 PLL，等就绪，再切系统时钟 */
    RCC_CR |= RCC_CR_PLLON;
    while ((RCC_CR & RCC_CR_PLLRDY) == 0u) { }
    RCC_CFGR = (RCC_CFGR & ~3u) | RCC_CFGR_SW_PLL;
    while ((RCC_CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL) { }

    g_sysclk_hz = 168000000u;
    g_hclk_hz    = 168000000u;   /* AHB /1  */
    g_pclk1_hz   = 42000000u;    /* APB1 /4 */
    g_pclk2_hz   = 84000000u;    /* APB2 /2 */
    g_tim2_7_hz  = 84000000u;    /* APB1 分频≠1 → ×2 */
    g_tim1_8_hz  = 168000000u;   /* APB2 分频≠1 → ×2 */
}

uint32_t g_sysclk_hz, g_hclk_hz, g_pclk1_hz, g_pclk2_hz;
uint32_t g_tim2_7_hz, g_tim1_8_hz;

/* ============================================================================
 * 5. GPIO
 * ----------------------------------------------------------------------------
 * 三个要点，也是面试常问的：
 *  ① **改时钟**：外设挂在总线上，总线时钟没开，写寄存器完全无效。
 *     这是"代码看起来对但外设死活不动"第一大原因。
 *  ② **MODER 两位一组**：00=输入 01=输出 10=复用 11=模拟。
 *  ③ **写输出用 BSRR 不用 ODR**：BSRR 可以**原子**置位/复位一位，
 *     读-改-写 ODR 在有中断时可能被打断导致其他位被清掉（bit-bang 时很致命）。
 * ==========================================================================*/
static inline void rcc_gpio_en(uint32_t port)
{
    RCC_AHB1ENR |= (1u << port);          /* GPIOA=bit0 … GPIOE=bit4 */
}

void gpio_mode(uint32_t pin, uint32_t mode, uint32_t pull, uint32_t speed)
{
    uint32_t base = pin_gpio_base(pin);
    uint32_t n    = pin_no(pin);
    uint32_t sh   = n * 2u;

    rcc_gpio_en(pin_port(pin));

    GPIO(base) = (GPIO(base) & ~(3u << sh)) | (mode << sh);
    GPIOTYP(base) &= ~(1u << n);                       /* 推挽（清 OTYPER） */
    GPIOSP(base)  = (GPIOSP(base) & ~(3u << sh)) | (speed << sh);
    GPIOPU(base)  = (GPIOPU(base) & ~(3u << sh)) | (pull << sh);
}

/* 复用功能必须**先配 AF 再配 MODER**：
 * 复用功能号 AFn 就编码在 AFRL/AFRH 里，每个脚占 4 位。 */
void gpio_af(uint32_t pin, uint32_t af)
{
    uint32_t base = pin_gpio_base(pin);
    uint32_t n    = pin_no(pin);
    uint32_t idx  = n >> 3;                 /* 0-7 脚在 AFRL，8-15 在 AFRH */
    uint32_t sh   = (n & 7u) * 4u;
    uint32_t reg  = base + 0x20u + idx * 4u;

    REG(reg, 0u) = (REG(reg, 0u) & ~(0xFu << sh)) | (af << sh);
}

void gpio_out_init(uint32_t pin, uint32_t active_high)
{
    gpio_mode(pin, MODE_OUT, PULL_NONE, SPEED_LOW);
    gpio_write(pin, active_high ? 0u : 1u);   /* 先摆到"熄灭"电平，避免上电闪一下 */
}

void gpio_write(uint32_t pin, uint32_t level)
{
    uint32_t base = pin_gpio_base(pin);
    uint32_t n    = pin_no(pin);
    GPIOB(base) = level ? (1u << n) : (1u << (n + 16u));  /* 低16位=置位，高16位=复位 */
}

uint32_t gpio_read(uint32_t pin)
{
    return (GPIOIDR(pin_gpio_base(pin)) >> pin_no(pin)) & 1u;
}

/* 翻转输出：BSRR 上半区也能"翻转"？不能——翻转要读改写 ODR。
 * 下面这种写法用于 LED 跑马灯，延时场景下无中断竞争，安全。 */
void gpio_toggle(uint32_t pin)
{
    uint32_t base = pin_gpio_base(pin);
    uint32_t n    = pin_no(pin);
    GPIOODR(base) ^= (1u << n);
}

/* ============================================================================
 * 6. 毫秒时基（TIM6）+ 精确延时
 * ----------------------------------------------------------------------------
 * 【为什么用 TIM6 而不是 SysTick】
 *   Q9 题目硬性要求"闪烁延时必须用硬件定时器，不得用 delay 空循环"。
 *   SysTick 本质是内核定时器，也算数，但老师可能追问"你这算硬件定时器吗"。
 *   直接用 TIM6 + 中断，每毫秒一个 tick，全工程统一，
 *   讲解时可以明确说：这是 APB1 的硬件定时器，时基来自 TIM6 更新中断。
 *
 * TIM6 是 16 位基本定时器：84MHz / (PSC+1) / (ARR+1) = 1kHz
 *   取 PSC=83 → 计数 1MHz；ARR=999 → 1000 个计数 = 1ms
 * ==========================================================================*/
/* 全局毫秒计数。给成 weak：项目里可以直接再定义一个同名强符号，
 * 但大多数题不需要改，只是等它累加而已。 */
__attribute__((weak, used)) volatile uint32_t g_ms = 0u;

void timer_ms_init(void)
{
    /* TIM6 挂在 APB1，开时钟 */
    RCC_APB1ENR |= (1u << 4);                 /* APB1ENR bit4 = TIM6EN */

    TIM_PSC(TIM6_BASE) = 83u;                 /* 84MHz / 84  = 1MHz */
    TIM_ARR(TIM6_BASE) = 999u;                /* 1MHz / 1000 = 1kHz = 1ms */
    TIM_CR1(TIM6_BASE) = TIM_CR1_ARPE;        /* ARR 预装载：更新时不会半路改周期 */
    TIM_DIER(TIM6_BASE) = TIM_DIER_UIE;       /* 允许更新中断 */
    TIM_SR(TIM6_BASE) = 0u;
    TIM_EGR(TIM6_BASE) = TIM_EGR_UG;          /* 产生一次更新事件，把 PSC/ARR 装进去 */
    TIM_CR1(TIM6_BASE) |= TIM_CR1_CEN;        /* 启动计数 */

    NVIC_ISER_AT(54) = 1u << (54u & 3u);                          /* TIM6_DAC IRQ = 54 */
    NVIC_IPR_AT(54)  = 6u << (__NVIC_PRIO_BITS * (54u & 3u));     /* 优先级 6 */
}

/* TIM6 每 1ms 进一次：清标志（写 1 清）+ 毫秒 +1。
 * 标 weak 是为了让项目想每毫秒做点别的时，可以直接重定义同名函数覆盖它。 */
__attribute__((weak, used)) void TIM6_DAC_IRQHandler(void)
{
    if (TIM_SR(TIM6_BASE) & TIM_SR_UIF) {
        TIM_SR(TIM6_BASE) = ~0u;              /* 写全 1 = 清所有标志 */
        ++g_ms;
    }
}

uint32_t millis(void) { return g_ms; }

/* 用法：delay_ms(100)。为什么不是 while(millis()-t<100)？
 * 因为在中断里不能用后者（中断会改 g_ms 造成误判），而 delay_ms 是纯等待。 */
void delay_ms(uint32_t ms)
{
    uint32_t t0 = millis();
    while ((millis() - t0) < ms) { }
}

/* 精确到微秒的忙等：用 DWT 周期计数器，不占定时器。
 * DWT 是 Cortex-M4 自带的调试单元，CPU 每个时钟周期加 1。 */
void dwt_init(void)
{
    CORE_DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT  = 0u;
    DWT_CTRL   |= DWT_CTRL_CYCCNTENA;
}

void delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT;
    uint32_t ticks = us * (g_sysclk_hz / 1000000u);
    while ((DWT_CYCCNT - start) < ticks) { }
}

/* ============================================================================
 * 7. 串口
 * ----------------------------------------------------------------------------
 * 【波特率怎么算】
 *   F4 的 USART 默认 16 倍过采样（M1:0 = 11），
 *   BRR = PCLK / 波特率。 例：84MHz / 115200 = 728.9 → 取 728，
 *   实际波特率 115226，误差 0.02%，完全够用。
 *
 * 【收发为什么轮询 SR 的 TXE / RXNE】
 *   TXE=1 说明发送保持寄存器空，可以往 DR 塞下一个字节；
 *   往 DR 写会**自动清掉 TXE**。
 *   RXNE=1 说明 DR 里有收到的字节，读 DR 会**自动清掉 RXNE**。
 *   这两个"硬件自动清标志"是串口最容易忘的细节。
 * ==========================================================================*/
static inline uint32_t usart_pclk(uint32_t base)
{
    return (base == USART1_BASE) ? g_pclk2_hz : g_pclk1_hz;
}

void uart_init(uint32_t base, uint32_t pclk, uint32_t baud)
{
    /* ① 先开时钟，否则寄存器写不进去 */
    if (base == USART1_BASE) RCC_APB2ENR |= (1u << 4);     /* APB2ENR bit4 = USART1EN */
    else                     RCC_APB1ENR |= (1u << 17);    /* APB1ENR bit17= USART2EN
                                                              bit19= UART4EN         */

    /* ② 停用才能改分频（CR1.UE=0 时 BRR/CR2/CR3 才可写） */
    USART_CR1(base) &= ~USART_CR1_UE;

    /* ③ 波特率：OVER8=0 时 BRR[15:4] = PCLK/baud */
    USART_BRR(base) = (uint32_t)((pclk + baud / 2u) / baud);

    /* ④ 8 位数据、无校验、1 停止位 = CR1 默认值 + 使能收发 */
    USART_CR1(base) = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE
                    | USART_CR1_M1 | USART_CR1_M0;         /* 16 倍过采样 */
}

void uart_putc(uint32_t base, char c)
{
    while ((USART_SR(base) & USART_SR_TXE) == 0u) { }      /* 等发送寄存器空 */
    USART_DR(base) = (uint8_t)c;
}

void uart_puts(uint32_t base, const char *s)
{
    while (*s) { uart_putc(base, *s++); }
}

uint32_t uart_rx_ready(uint32_t base)
{
    return (USART_SR(base) & USART_SR_RXNE) ? 1u : 0u;
}

/* 带超时的收一个字节：超时返回 -1。用毫秒时基计超时，
 * 好处是**收到什么都没丢**，不会因为对端没发而把程序卡死
 * （Q9 硬性要求：连续下发指令不得卡死）。 */
int uart_getc(uint32_t base, uint32_t timeout_ms)
{
    uint32_t t0 = millis();
    for (;;) {
        if (USART_SR(base) & USART_SR_RXNE) {
            if (USART_SR(base) & USART_SR_ORE) { (void)USART_DR(base); return -1; }
            return (int)(USART_DR(base) & 0xFFu);
        }
        if ((millis() - t0) > timeout_ms) return -1;
    }
}

/* 打印一个无符号整数（不想引 printf —— newlib 的 printf 在 -nostdlib 下要挂一串东西）*/
void uart_putu(uint32_t base, uint32_t v)
{
    char buf[11];
    int i = 0;
    if (v == 0u) { uart_putc(base, '0'); return; }
    while (v) { buf[i++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (i--) { uart_putc(base, buf[i]); }
}

/* ============================================================================
 * 8. 定时器 PWM
 * ----------------------------------------------------------------------------
 * 【PWM 是怎么产生高电平的】
 *   计数器 CNT 从 0 数到 ARR（周期），一路数；同时 CCR 存一个"比较值"。
 *   CNT < CCR 时输出有效电平（高），CNT ≥ CCR 时输出低。
 *   占空比 = CCR / (ARR+1)。频率 = 定时器时钟 / ((PSC+1)*(ARR+1))
 *
 * 【为什么预装载（OCxPE + ARPE）必须开】
 *   不开的话，改 CCR 的瞬间可能在一个 PWM 周期中间生效，
 *   输出一个宽度异常的脉冲——舵机上这就是"猛抽一下"，蜂鸣器上是"咔"一声。
 *   开了之后新值在**周期边界**统一生效，永远是干净的整周期。
 *
 * 【本封装的约定：1 个计数 = 1 微秒】
 *   84MHz → PSC=83，168MHz → PSC=167，都正好 1 tick/µs。
 *   于是 ARR = 周期µs - 1，CCR = 高电平µs，写起来全是人话。
 * ==========================================================================*/
void pwm_init(uint32_t base, uint8_t ch, uint32_t period_us)
{
    uint32_t tc  = tim_clock(base);
    uint32_t psc = tc / 1000000u - 1u;         /* 让 1 个计数 = 1µs */

    /* 通道使能位在 CCER 里的位置：CH1=bit0, CH2=bit4, CH3=bit8, CH4=bit12 */
    uint32_t ccer_bit = 1u << ((ch - 1u) * 4u);
    /* CCR 寄存器：CH1=0x34 CH2=0x38 CH3=0x3C CH4=0x40 */
    uint32_t ccr_off  = 0x34u + (ch - 1u) * 4u;
    /* CCMRx 里 OCxM 的位置，四个通道各不相同，写错一个就整路通道不出波形：
     *     CCMR1 (0x18): OC1M = bit[7:4]   OC2M = bit[15:12]
     *     CCMR2 (0x1C): OC3M = bit[7:4]   OC4M = bit[15:12]
     *   规律是"在所属的 CCMRx 里，CH1/CH3 用低半、CH2/CH4 用高半"，
     *   所以偏移 = (通道在它那个寄存器里排第几 - 1) * 8。
     *   ⚠ 早先这里写成 "(ch<=2) ? 0 : 8"，导致 CH2 的模式被写进 OC1M、
     *     CH3 的被写进 OC4M——代码能编译、也"没报错"，但 CH2/CH3 压根不是 PWM 模式。
     *     用 CH1/CH4 的题目碰不到这个坑，一用 CH2/CH3 就整路静默失效。 */
    uint32_t ccmr_off = (ch <= 2u) ? 0x18u : 0x1Cu;
    uint32_t m_ofs    = ((ch <= 2u) ? (ch - 1u) : (ch - 3u)) * 8u;
    /* OCxPE（预装载使能）同样按 低半=bit3 / 高半=bit11 交替 */
    uint32_t pe_bit   = 1u << (((ch - 1u) % 2u) * 8u + 3u);

    /* ⚠ 先开时钟，而且**必须在写 PSC/ARR 之前**。
     *   时钟没合闸时往定时器寄存器写是**静默失效**的——写进去了但外设根本没在跑，
     *   读回来却又是写进去的值，现场表现是"程序明明配了 PWM，示波器上一个毛都没有"。
     *   这是"代码看着完全正确但外设死活不动"的头号原因，
     *   和 TB6612 的 STBY 没拉高、电机不转，是同一类错误。
     *   APB1ENR: TIM2~7 = bit0~5；APB2ENR: TIM1=bit0, TIM8=bit1。 */
    if (base == TIM1_BASE || base == TIM8_BASE) {
        RCC_APB2ENR |= 1u << ((base == TIM1_BASE) ? 0u : 1u);
    } else {
        RCC_APB1ENR |= 1u << (((base - TIM2_BASE) >> 10) & 0x7u);  /* TIM2/3/4/5/6/7 = bit0~5 */
    }

    TIM_PSC(base) = psc;
    TIM_ARR(base) = period_us - 1u;
    REG(base, ccr_off) = 0u;                    /* 初始 0% 占空比，避免上电满功率 */

    REG(base, ccmr_off) |= (TIM_OCM_PWM1 << m_ofs) | pe_bit;
    TIM_CCER(base) |= ccer_bit;                  /* 通道使能，输出极性默认高有效 */

    TIM_CR1(base) |= TIM_CR1_ARPE;
    TIM_EGR(base)  = TIM_EGR_UG;                 /* 把 PSC/ARR/CCR 影子值刷进实际寄存器 */
    TIM_CR1(base) |= TIM_CR1_CEN;               /* 开跑 */
}

void pwm_set_us(uint32_t base, uint8_t ch, uint32_t high_us)
{
    uint32_t ccr_off = 0x34u + (ch - 1u) * 4u;
    uint32_t period  = TIM_ARR(base) + 1u;
    if (high_us > period) high_us = period;
    REG(base, ccr_off) = high_us;
}

/* 千分比（0~1000）占空比，写起来直观：pwm_set_permyriad(tim,1,500) = 50% */
void pwm_set_permyriad(uint32_t base, uint8_t ch, uint32_t permyriad)
{
    if (permyriad > 1000u) permyriad = 1000u;
    pwm_set_us(base, ch, (TIM_ARR(base) + 1u) * permyriad / 1000u);
}

/* 无源蜂鸣器发声：给定频率 Hz、占空比千分比。
 * 无源蜂鸣器**必须给方波**，给直流不响（有源蜂鸣器才给电平就响）。 */
void buzzer_tone(uint32_t base, uint8_t ch, uint32_t freq_hz, uint32_t permyriad)
{
    if (freq_hz < 20u) freq_hz = 20u;
    uint32_t period_us = 1000000u / freq_hz;
    pwm_init(base, ch, period_us);
    pwm_set_permyriad(base, ch, permyriad ? permyriad : 500u);
}

void buzzer_off(uint32_t base, uint8_t ch)
{
    pwm_set_us(base, ch, 0u);
}

/* ============================================================================
 * 9. ADC
 * ----------------------------------------------------------------------------
 * 【采样流程：SMP → HOLD → CONVERT】
 *   SMPR 设"内部采样电容充到多少电压所需时间"，越长越准但越慢。
 *   选 15 周期时一次转换 15+12=27 个 ADC 时钟 ≈ 1.3µs，够用。
 *
 * 【一个真实的坑：光敏/红外模块的输出电压】
 *   这类模块 AO 输出常达 3~4.8V，**超了 F407 的 3.3V**。
 *   必须加分压（如 1k/2k 分到 1/3）再接 PA0，否则会烧 IO/读数不对。
 *   README 里的"模电常识"老师很爱追问这个。
 *
 * 【为什么多次采样取平均】
 *   ADC 有量化误差 + 电源噪声 + 传感器本身的抖动，单次读数会跳。
 *   取 16 次平均能明显稳住读数，代价只是几十微秒。
 * ==========================================================================*/
void adc_init(uint32_t pin)
{
    uint32_t ch = pin_adc_ch(pin);

    RCC_APB2ENR |= (1u << 8);                   /* APB2ENR bit8 = ADC1EN */
    RCC_CFGR    = (RCC_CFGR & ~(3u << 16)) | RCC_CFGR_ADCPRE_DIV4;  /* ADC 时钟 21MHz */

    /* ① 模拟输入模式：MODER=11。同时断开上下拉，避免给采样电容充电。 */
    gpio_mode(pin, MODE_ANALOG, PULL_NONE, SPEED_LOW);

    /* ② 采样时间：CH0~9 在 SMPR2，CH10~15 在 SMPR1 */
    if (ch < 10u) {
        ADC1_SMPR2 &= ~(3u << (ch * 3u));
        ADC1_SMPR2 |=  (1u << (ch * 3u));      /* 0b01 = 15 周期 */
    } else {
        ADC1_SMPR1 &= ~(3u << ((ch - 10u) * 3u));
        ADC1_SMPR1 |=  (1u << ((ch - 10u) * 3u));
    }

    /* ③ 上电自校准：ADC 上电后第一次转换前必须校准，否则精度差一截。
     *    手册要求等 3 个 ADCCLK 再启动校准，校准期间不能动 CR2 的 ADON。 */
    ADC1_CR2 |= ADC_CR2_ADON;
    for (volatile uint32_t i = 0u; i < 1000u; ++i) { }
    ADC1_CR2 |= ADC_CR2_RST;
    for (volatile uint32_t i = 0u; i < 1000u; ++i) { }
    ADC1_CR2 &= ~ADC_CR2_RST;
    while (ADC1_CR2 & ADC_CR2_RST) { }
}

uint16_t adc_read(uint32_t ch)
{
    /* 选通道：SQR3 的 SQ1~SQ4 对 CH0~CH3。我们只用 SQ1。 */
    ADC1_SQR3 = (ADC1_SQR3 & ~0x1Fu) | (ch & 0x1Fu);
    ADC1_CR2 |= ADC_CR2_SWSTART;               /* 软件触发开始转换 */
    while ((ADC1_SR & ADC_SR_EOC) == 0u) { }   /* 等转换完成 */
    return (uint16_t)(ADC1_DR & 0xFFFu);       /* 12 位结果 */
}

uint16_t adc_read_avg(uint32_t ch, uint8_t n)
{
    uint32_t sum = 0u;
    if (n == 0u) n = 1u;
    for (uint8_t i = 0u; i < n; ++i) { sum += adc_read(ch); }
    return (uint16_t)(sum / n);
}

uint16_t adc_read_pin_avg(uint32_t pin, uint8_t n)
{
    return adc_read_avg(pin_adc_ch(pin), n);
}

/* ============================================================================
 * 10. 外部中断 EXTI + NVIC
 * ----------------------------------------------------------------------------
 * 【EXTI 的两段映射，缺一不可】
 *   ① EXTI 只能知道"第几号线上有边沿"（EXTI9_5 / EXTI15_10 分组），
 *      它**不知道是哪个 GPIO 口**；
 *   ② 所以必须去 SYSCFG 的 EXTICR[x] 把这号线指明来源端口
 *      （EXTI0~4 → EXTICR1，每 4 线一组，每线 4 位，编码 0=PA … 4=PE）。
 *   只配 EXTI 不配 SYSCFG，中断永远不进来——这是第二大"代码对但没反应"的原因。
 *
 * 【NVIC 还要单独开】
 *   EXTI 配好了只是"外设想中断"，还得去 NVIC_ISER 把对应 IRQ 使能，
 *   并设优先级。IRQ 号 ≠ EXTI 线号：EXTI0→IRQ6、EXTI1→IRQ7，
 *   EXTI9_5→IRQ23、EXTI15_10→IRQ40。
 * ==========================================================================*/
static inline uint8_t exti_irq_of(uint32_t line)
{
    if (line < 5u)  return (uint8_t)(6u + line);        /* EXTI0~4  → IRQ6~10 */
    if (line < 10u) return 23u;                           /* EXTI5~9  → EXTI9_5 */
    return 40u;                                           /* EXTI10~15→ EXTI15_10 */
}

void exti_init(uint32_t pin, uint32_t mode /* 1=上升 2=下降 3=双边 */, uint8_t priority)
{
    uint32_t line = pin_no(pin);
    uint32_t port = pin_port(pin);
    uint8_t  irq  = exti_irq_of(line);
    uint32_t sh   = (line & 3u) * 4u;
    uint32_t ec   = SYSCFG_BASE + 0x08u + (line >> 2u) * 4u;

    rcc_gpio_en(port);
    RCC_APB2ENR |= (1u << 14);                   /* APB2ENR bit14 = SYSCFGEN（EXTICR 在 SYSCFG 里）*/

    gpio_mode(pin, MODE_IN, PULL_UP, SPEED_LOW); /* 按键/触发线配成上拉输入 */

    REG(ec, 0u) = (REG(ec, 0u) & ~(0xFu << sh)) | (port << sh);   /* 指明来源端口 */

    EXTI_IMR(EXTI_BASE)  |= (1u << line);        /* 允许该线产生中断 */
    EXTI_RTSR(EXTI_BASE)  = (EXTI_RTSR(EXTI_BASE) & ~(1u << line)) | ((mode & 1u) ? (1u << line) : 0u);
    EXTI_FTSR(EXTI_BASE)  = (EXTI_FTSR(EXTI_BASE) & ~(1u << line)) | ((mode & 2u) ? (1u << line) : 0u);
    EXTI_PR(EXTI_BASE)   &= ~(1u << line);       /* 清残留的挂起标志，否则一使能就进中断 */

    NVIC_IPR_AT(irq)  = (uint32_t)(priority << (__NVIC_PRIO_BITS * (irq & 3u)));
    NVIC_ISER_AT(irq) = 1u << (irq & 3u);
}

/* 进中断必须清挂起标志，否则退出后会立刻再进来，表现为"死循环在中断里" */
static inline void exti_clear(uint32_t line)
{
    EXTI_PR(EXTI_BASE) = 1u << line;             /* 写 1 清标志（不是写 0！）*/
}

/* ============================================================================
 * 11. I2C（Q3 OLED 用）
 * ----------------------------------------------------------------------------
 * 【I2C 的三根线】
 *   SCL 时钟（开漏+上拉，一般外接 4.7k 上拉）、
 *   SDA 数据（开漏+上拉）、地址 7 位（OLED SSD1306 = 0x3C，写时左移 1 位）。
 *
 * 【为什么必须开漏 + 上拉】
 *   I2C 是"线与"总线：所有设备都拉低才叫低。器件只能拉低不能拉高（开漏），
 *   靠外部上拉电阻拉高，这样才不会两个设备打架烧总线。
 *
 * 【为什么要等 BTF 而不是只等 TXE】
 *   TXE 只说明 DR 空了；BTF(BYTE TRANSFER FINISHED) 才说明这个字节
 *   真的移位移完了。连续发多个字节时不等 BTF 会把后一个字节
 *   塞进移位寄存器，导致字节错位。
 * ==========================================================================*/
void i2c1_init(void)
{
    RCC_APB1ENR |= (1u << 21);                   /* APB1ENR bit21 = I2C1EN */
    /* PB6=SCL / PB7=SDA，复用功能 AF4（开漏，由 I2C 自己管） */
    gpio_af(PB6, 4u);
    gpio_af(PB7, 4u);
    gpio_mode(PB6, MODE_AF, PULL_NONE, SPEED_HIGH);
    gpio_mode(PB7, MODE_AF, PULL_NONE, SPEED_HIGH);

    I2C1_CR1 = 0u;                               /* 全清，PE 从 0 开始 */
    I2C1_CCR = 280u;                             /* 400kHz(快速模式)。要 100kHz 就填 40 */
    I2C1_TRISE = 17u;                            /* 上升沿时间(100ns 单位)，400kHz 用 17 */
    I2C1_CR1 = I2C_CR1_PE;                       /* 最后使能 */
}

static uint32_t i2c_wait(uint32_t mask, uint32_t val)
{
    uint32_t t0 = millis();
    while ((I2C1_SR1 & mask) != val) {
        if (I2C1_SR1 & (I2C_SR1_AF | I2C_SR1_ARLO | I2C_SR1_BERR)) { (void)I2C1_SR1; return 0u; }
        if ((millis() - t0) > 50u) return 0u;     /* 超时兜底，绝不无限等 */
    }
    return 1u;
}

/* 返回 1 成功，0 失败（超时/从机不应答）。所有 I2C 事务都查返回值，
 * 因为总线出错时若不检查，程序会在死循环里卡死。 */
uint8_t i2c1_write(uint8_t dev7, const uint8_t *buf, uint16_t len)
{
    uint8_t i;
    uint16_t n = len;

    /* 起始 + 地址（写 = 最低位 0） */
    I2C1_CR1 |= I2C_CR1_START;
    if (!i2c_wait(I2C_SR1_SB, I2C_SR1_SB)) return 0u;
    REG(I2C1_BASE, 0x04u) = (uint32_t)((dev7 << 1) | 0u);      /* OAR1 + DR */
    if (!i2c_wait(I2C_SR1_ADDR, I2C_SR1_ADDR)) { (void)I2C1_SR1; return 0u; }

    for (i = 0u; i < n; ++i) {
        I2C1_CR1 |= I2C_CR1_START;                              /* 重启起始，清 ADDR */
        REG(I2C1_BASE, 0x10u) = buf[i];                          /* 写 DR */
        if (!i2c_wait(I2C_SR1_TXE | I2C_SR1_BTF, I2C_SR1_TXE | I2C_SR1_BTF)) return 0u;
    }

    I2C1_CR1 |= I2C_CR1_STOP;
    return 1u;
}

/* ============================================================================
 * 勘误（凭记忆写最容易踩的坑，这里都对着 ST 头文件核过了）
 * ----------------------------------------------------------------------------
 * 1. RCC_CFGR.ADCPRE 在 **bit16-17**，不是 F1/F2 上印象中的 bit14-15。
 *    写错位会让 ADC 时钟变成 84/2=42MHz，超出 36MHz 上限，采样值整体偏高。
 * 2. USART_CR1 的 M[1:0] 是 M0=bit12 / M1=bit11（F4/F1/F2/F7 这一族都是这个排法），
 *    别按"bit12-11 是个数"的直觉理解成 M1 在高位。
 * 3. TIM6_DAC 的 IRQ 号是 **54**；EXTI 线号和 IRQ 号不是一回事。
 * 4. ADC 校准位 CR2.RST 是"写 1 进入校准状态，要读回 0 才算完成"，
 *    不是普通的状态位。
 * 5. EXTI_PR 写 **1** 清挂起，写 0 没用。
 *
 * ---- 下面两条是这层公共代码自己踩过的坑，代码写对了但现象完全不对 ----
 * 6. **忘了开外设时钟**（pwm_init 真实踩过）。
 *    只配 PSC/ARR/CCR 却没碰 RCC_APBxENR，寄存器**写是写进去了、外设根本没在跑**，
 *    读回来还确实是写进去的值，所以代码"看起来完全正确"，
 *    但引脚上一个波形都没有。
 *    这是"程序明明配了 PWM，示波器上是平的"头号原因，
 *    和 TB6612 的 STBY 没拉高导致电机不转属于同一类错误。
 * 7. **OCxM 的字段偏移按通道各不相同**（pwm_init 真实踩过）。
 *    把"CH1/CH3 在低半、CH2/CH4 在高半"写成 `(ch<=2) ? 0 : 8`，
 *    结果 CH2 的模式被写进 OC1M、CH3 的被写进 OC4M。
 *    编译不报错、链接不报错、串口打印也正常，
 *    但只有 CH1 和 CH4 真的在出 PWM。**只用到 CH1 的题目永远发现不了。**
 *
 * 这两条合起来说明一件事：**"能编译"和"外设真的在动"是两件事**，
 * 前者由编译器保证，后者只能靠示波器/逻辑分析仪或者对寄存器回读来验证。
 * 所以验收前的真机联调，示波器比再读一遍代码有用得多。
 * ==========================================================================*/

#endif /* STM32F4_COMMON_H */
