/* ============================================================================
 * 00a_基础点灯/main_board.c —— L1 点灯 · 真板版
 * ----------------------------------------------------------------------------
 * 【和仿真版 main.c 的关系】
 *   仿真版 main.c：手写 RCC/GPIO 寄存器，PD13 短亮长灭，PC 端读 ODR 看电平。
 *   真板版        ：GPIO 和寄存器换用公共层 stm32f4_common.h，PD12~PD15 走流水；
 *                   延时从"数 n 个 nop"换成 TIM6 的 1ms 中断时基。
 *
 *   应用层（灯按什么节奏走、亮几个）保持一致；**换掉的只有两样**：
 *     ① IO 层：寄存器地址 → 公共层封装好的 gpio_write/gpio_read
 *     ② 时间：忙等 → 硬件定时器
 *   所以验收时被问"你这逻辑和仿真版是同一套吗"，答案是：是，只有 IO 层不同。
 *
 * ------------------------------------------------------------------
 * 一、接线（换板子只改下面那张引脚表）
 * ------------------------------------------------------------------
 *   PD12 ─[220Ω~1kΩ]─|>|─ GND    ①
 *   PD13 ─[220Ω~1kΩ]─|>|─ GND    ②
 *   PD14 ─[220Ω~1kΩ]─|>|─ GND    ③
 *   PD15 ─[220Ω~1kΩ]─|>|─ GND    ④
 *   PA9  (USART1_TX, AF7) ─ USB-TTL 的 RXD      115200-8-N-1
 *   PA10 (USART1_RX, AF7) ─ USB-TTL 的 TXD
 *
 *   ⚠ LED 串 220Ω~1kΩ 不是可选项：F407 单脚灌电流上限约 25mA，
 *     直接把 LED 接到 3.3V 会在点亮瞬间把引脚驱动级烧掉，救不回来。
 *   ⚠ 极性：下面按"高电平点亮"写（LED 阳极接 MCU、阴极接地）。
 *     如果手上是低电平点亮的模块，把 led_polarity 宏改过来，不要去改代码逻辑。
 *
 * ------------------------------------------------------------------
 * 二、这题真正要讲的东西
 * ------------------------------------------------------------------
 *   ① 上电之后 CPU 什么状态都不对：主频还是 16MHz 内部 RC、Flash 没有等待周期、
 *      栈顶是随机值。这一题的意义是把"时钟 → Flash 等待周期 → GPIO 时钟 → 引脚模式"
 *      这条链子完整走一遍，后面 9 道题都在复用它。
 *   ② 为什么每个外设用之前都要先在 RCC 里给它开时钟：
 *      总线时钟没开的时候，往外设寄存器写的值**存不住**（读回来恒为 0），
 *      这是"代码看着对、灯死活不亮"排第一的原因。
 *   ③ 为什么点亮一个灯要碰 3 个寄存器而不是 1 个：时钟、方向（MODER）、电平（BSRR）。
 * ==========================================================================*/

#include "stm32f4_common.h"

/* ============================ 引脚表 ============================
 * 换板子只改这一段。PD12~PD15 既是 F407-Discovery 的板载四色 LED，
 * 也是 TIM4 的 CH1~CH4（AF2），后面 q02 会复用同一组脚去做 PWM。 */
#define LED0        PD12
#define LED1        PD13
#define LED2        PD14
#define LED3        PD15

#define UART_TX     PA9    /* USART1_TX → AF7 */
#define UART_RX     PA10   /* USART1_RX → AF7 */

/* ============================ 参数 ============================ */
/* 仿真版是 delay(100000) 亮 + delay(300000) 灭，也就是"短亮长灭"。
 * 真板上这两个 delay() 的圈数等于多少毫秒取决于编译结果，不可控也不可复现，
 * 所以只保留**短亮长灭这个比例**，具体时间换成毫秒时基来数。 */
#define ON_MS       120u   /* 每盏灯亮多久 */
#define OFF_MS      380u   /* 每盏灯灭多久（明显长于 ON_MS，肉眼看得出"跑"）*/
#define PRINT_MS   1000u   /* 每秒往串口打一次当前状态，验收时用得上 */

#define LED_COUNT    4u

static const uint32_t g_led[LED_COUNT] = { LED0, LED1, LED2, LED3 };

/* ============================================================================
 * led_show：只点亮第 idx 盏灯
 * ----------------------------------------------------------------------------
 * 写法上值得讲的一点：**一次 BSRR 写完"灭掉其余三盏 + 点亮这一盏"**，
 * 而不是先 ALL_OFF 再置位那一位。
 *   BSRR 的低 16 位是置位位、高 16 位是复位位，一次写入里两组同时生效；
 *   拆成两次写，中间会出现一个"全灭"的瞬间，流水灯看起来每步都闪一下。
 *   这也正是公共层 gpio_write() 坚持写 BSRR 而不读写 ODR 的原因：
 *   ODR 是读-改-写，被中断打断时会把别人改过的位一起清掉。
 * ==========================================================================*/
static void led_show(uint32_t idx)
{
    uint32_t base = pin_gpio_base(g_led[idx]);
    uint32_t bsrr = 1u << pin_no(g_led[idx]);        /* 置位：点亮 */

    bsrr |= 1u << (pin_no(g_led[(idx + 1u) % LED_COUNT]) + 16u);   /* 其余复位 */
    bsrr |= 1u << (pin_no(g_led[(idx + 2u) % LED_COUNT]) + 16u);
    bsrr |= 1u << (pin_no(g_led[(idx + 3u) % LED_COUNT]) + 16u);

    GPIOB(base) = bsrr;
}

int main(void)
{
    /* ------------------------------------------------------------------
     * 初始化顺序：时钟 → 延时/时基 → IO → 串口。
     * 把 clock_init() 放第一位是因为后面每一步都在往寄存器写东西，
     * 而总线时钟没配好时这些写的时序是不确定的。
     * ------------------------------------------------------------------ */
    clock_init();      /* HSE 8MHz → PLL → 168MHz，同时给 Flash 开 5 个等待周期。
                        * 顺序不能反：先把等待周期配好再提频，反了会在 168MHz 下取指失败，
                        * 表现就是"低速能跑、提频就跑飞"。 */
    dwt_init();        /* 打开 DWT 周期计数器，后面 delay_us() 要用 */
    timer_ms_init();   /* TIM6 每 1ms 中断一次，millis() 从这里开始准 */

    /* 四个 LED 配成推挽输出。先摆到熄灭电平再让主循环跑，
     * 否则复位后到第一次写 BSRR 之间那几个周期里引脚是浮空的，
     * 表现为上电瞬间闪一下。 */
    for (uint32_t i = 0u; i < LED_COUNT; ++i) {
        gpio_out_init(g_led[i], 1u);
    }

    /* 串口：先把"嘴"打开，后面出问题时至少能打印出来，不至于黑屏干瞪眼。
     * AF7 和 MODER=复用两件事都要配，少任何一件 USART1 的信号都出不了芯片。 */
    gpio_af(UART_TX, 7u);  gpio_mode(UART_TX, MODE_AF, PULL_UP, SPEED_HIGH);
    gpio_af(UART_RX, 7u);  gpio_mode(UART_RX, MODE_AF, PULL_UP, SPEED_HIGH);
    uart_init(USART1_BASE, g_pclk2_hz, 115200u);

    uart_puts(USART1_BASE, "\r\nL1 blink (board) start\r\n");
    uart_puts(USART1_BASE, "PD12..PD15 = 4 steps, short-on long-off\r\n");

    uint32_t idx        = 0u;
    uint32_t on_phase   = 1u;                  /* 上电先亮 LED0 */
    uint32_t next_ms    = millis() + ON_MS;    /* 下一次动作的时刻 */
    uint32_t last_print = millis();
    led_show(idx);

    for (;;) {
        uint32_t now = millis();

        /* 时间判断用无符号减法 (now - deadline >= 0)：millis() 是 32 位计数器，
         * 大约 49 天后会回绕，无符号减法在回绕前后都成立，
         * 写成 now >= deadline 就会在回绕那一刻停 49 天。 */
        if ((int32_t)(now - next_ms) >= 0) {
            if (on_phase) {
                /* 亮相结束：灯还亮着，只是把"下一跳"的时间往后推。
                 * 灭的时间明显长于亮的时间，肉眼才看得出这是"流水"而不是"频闪"。 */
                next_ms = now + OFF_MS;
            } else {
                idx = (idx + 1u) % LED_COUNT;
                led_show(idx);
                next_ms = now + ON_MS;
            }
            on_phase ^= 1u;
        }

        /* 每秒打一次状态。串口本身是阻塞轮询 TXE，
         * 但一秒钟才发几十字节，占用时间不到千分之一，
         * 不影响流水节奏——这也是为什么"调试输出"要限频而不是每圈都打。 */
        if ((now - last_print) >= PRINT_MS) {
            last_print = now;
            uart_puts(USART1_BASE, "uptime=");
            uart_putu(USART1_BASE, now);
            uart_puts(USART1_BASE, "ms  led=");
            uart_putu(USART1_BASE, idx);
            uart_putc(USART1_BASE, '\n');
        }
    }
}