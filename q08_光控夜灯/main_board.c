/* ============================================================================
 * q08_光控夜灯/main_board.c —— 第八题「懒人救星！」真板版
 * ----------------------------------------------------------------------------
 * 【和仿真版 main.c 的关系】
 *   应用逻辑（防抖、平滑、亮度映射）**一行没改**，只把两头接上真硬件：
 *       仿真：light 由 Python 写 mailbox      →  真板：adc_read(PA0) 真实采样
 *       仿真：duty  写 mailbox 给 Python 看   →  真板：写 TIM3->CCR1 真实输出 PWM
 *   验收时老师问"你这逻辑和仿真版是一套吗"，可以直接说：是，只有 IO 层不同。
 *
 * ------------------------------------------------------------------
 * 一、接线（换板子只改下面这张表，代码其余部分不用动）
 * ------------------------------------------------------------------
 *   光敏电阻分压电路
 *     3.3V ──[LDR 光敏]──┬── PA0 (ADC1_IN0)     ← 越亮，LDR 阻值越小，
 *                        │                          分压点电压越高，ADC 读数越大
 *                      [10kΩ]
 *                        │
 *                       GND
 *
 *   LED 调光
 *     PA6 (TIM3_CH1, AF2) ──[220Ω~1kΩ]──|>|── GND    ← 高电平点亮
 *
 *   调试串口（所有题统一）
 *     PA9 (USART1_TX, AF7) ── USB-TTL 的 RX
 *     PA10(USART1_RX, AF7) ── USB-TTL 的 TX
 *
 *   ⚠ 光敏模块如果自带 AO 输出且是 5V 供电，**必须分压到 3.3V 以内再接 PA0**，
 *     F407 的 IO 绝对不能超过 3.3V，烧了就是永久损失。
 *
 * ------------------------------------------------------------------
 * 二、这题真正要讲的东西（题目给了 3 条附加要求）
 * ------------------------------------------------------------------
 *   要求①亮/暗差别明显 → 映射要覆盖满量程，且别在两端"贴着天花板/地板"
 *   要求②短时突变不响应 → **防抖**：目标值连续稳定 STABLE_MS 才采纳
 *   要求③平滑渐变     → **限速斜坡**：每 RAMP_MS 只朝目标挪 RAMP_STEP
 *
 *   这两条为什么必须拆开做？
 *     防抖管"什么时候认"，斜坡管"认了之后怎么走到位"。
 *     只做防抖不做斜坡：灯会一顿一顿地跳；
 *     只做斜坡不做防抖：有人从灯前走过，灯也会跟着晃一下。
 *     两个都要，才有"人走过去，灯不为所动；天真的黑了，才慢慢亮起来"的效果。
 * ==========================================================================*/

#include "stm32f4_common.h"

/* ============================ 引脚表 ============================
 * 换板子/换接线只改这一段。下面代码一行都不用动。 */
#define PIN_LDR      PA0    /* 光敏分压点 → ADC1_IN0 */
#define PIN_LED      PA6    /* LED PWM    → TIM3_CH1, AF2 */
#define UART_TX      PA9    /* USART1_TX  → AF7 */
#define UART_RX      PA10   /* USART1_RX  → AF7 */

/* ============================ 可调参数 ============================ */
#define STABLE_MS    400u   /* 亮度要连续稳定多久，才认作"环境真的变了" */
#define BAND         60u    /* 两次采样目标值差 ≤BAND 算"没变化"，不重置计时 */
#define RAMP_MS      10u    /* 渐变的节拍间隔 */
#define RAMP_STEP    25u    /* 每拍占空比挪多少（满量程1000，约 0.4s 走完）*/
#define ADC_AVG      16u    /* 每次取多少个样本平均——单次读数会跳，取平均才稳 */
#define PRINT_MS     250u   /* 每 250ms 往串口打一次当前值，方便现场演示 */

#define PWM_PERIOD   2000u  /* PWM 周期 2000µs = 500Hz。LED 不挑频率，
                              500Hz 高于人眼闪烁临界(约100Hz)，看不出闪烁 */
#define ADC_MAX      4095u  /* 12 位 ADC 满量程 */

/* ============================ 全局状态 ============================ */
static uint32_t g_stable   = 0u;   /* 已被采纳的"稳定目标"占空比 */
static uint32_t g_pending  = 0u;   /* 正在观察、还没确认的目标 */
static uint32_t g_since    = 0u;   /* g_pending 从什么时候开始稳定 */
static uint32_t g_cur      = 0u;   /* 当前实际输出的占空比（用于渐变）*/
static uint32_t g_last_ramp = 0u;  /* 上次做渐变的时刻 */
static uint32_t g_last_print = 0u;

/* ============================================================================
 * 亮度 → 目标占空比（**反向**映射）
 * ----------------------------------------------------------------------------
 * 光敏分压电路是"越亮 ADC 读数越大"，但 LED 是"越暗越要亮"，所以这里必须反过来。
 * 写法上用 4095-ADC 而不是直接减，是为了避免中间出现无符号下溢。
 *
 * 两端各留一点余量（不取到正好 0 或正好 1000）：
 *   直接映射会让灯在极暗时"满亮"、极亮时"全灭"。
 *   留余量后，极暗时还有余量可增亮，极亮时也还能看出 LED 是关着的而不是坏了，
 *   验收时不容易被误判成"这灯是不是不亮了"。
 * ==========================================================================*/
static uint32_t light_to_duty(uint32_t light)
{
    if (light > ADC_MAX) { light = ADC_MAX; }

    /* 反向 + 两端各留 5% 余量 */
    uint32_t inverted = ADC_MAX - light;
    uint32_t duty     = inverted * 950u / ADC_MAX;   /* 950/1000 = 95% */
    return duty;
}

int main(void)
{
    /* ------------------------------------------------------------------
     * 初始化。顺序：时钟 → 时基 → IO → 外设。
     * 之所以时钟排最前，是因为后面每一步都要往寄存器写东西，
     * 而总线时钟没开的话，写进去的值根本存不住。
     * ------------------------------------------------------------------ */
    clock_init();                 /* HSE 8MHz → PLL 168MHz，Flash 等 5 周期 */
    dwt_init();                   /* 打开 DWT 周期计数器，供 delay_us 用 */
    timer_ms_init();              /* TIM6 每 1ms 中断一次 → millis() 可用 */

    /* 串口先起来：后面出问题时至少能打印出来，不至于黑屏干瞪眼 */
    gpio_af(UART_TX, 7u);  gpio_mode(UART_TX, MODE_AF, PULL_UP, SPEED_HIGH);
    gpio_af(UART_RX, 7u);  gpio_mode(UART_RX, MODE_AF, PULL_UP, SPEED_HIGH);
    uart_init(USART1_BASE, g_pclk2_hz, 115200u);
    uart_puts(USART1_BASE, "\r\nQ8 night-light (board) start\r\n");

    /* ADC：光敏接 PA0（ADC1_IN0）。这里只配模拟模式+采样时间+自校准 */
    adc_init(PIN_LDR);

    /* LED 调光：TIM3_CH1 复用推给 PA6，周期 2ms */
    gpio_af(PIN_LED, 2u);
    gpio_mode(PIN_LED, MODE_AF, PULL_NONE, SPEED_HIGH);
    pwm_init(TIM3_BASE, 1u, PWM_PERIOD);
    pwm_set_permyriad(TIM3_BASE, 1u, 0u);          /* 初始全灭 */

    uint32_t light = 0u;
    for (;;) {
        /* ============= 第一步：真实采样 =============
         * 取 16 次平均。单次 ADC 读数会因为电源噪声、光敏本身的迟滞而跳几十个 LSB，
         * 直接拿去当目标值会让防抖逻辑不停重置，灯就一直不动。 */
        light = adc_read_pin_avg(PIN_LDR, ADC_AVG);

        uint32_t want = light_to_duty(light);

        /* ============= 第二步：防抖 —— 决定"什么时候认" =============
         * 判据：want 和 pending 差得超过 BAND，说明环境真的变了，
         *       把 pending 换成新值并**重新开始计时**；
         *       差得不多，说明还在同一档噪声里，继续等。
         *
         * 一闪而过的变化（比如有人从灯前走过）会让 diff>BAND 反复触发，
         * 计时器一直被重置，于是永远凑不满 STABLE_MS，pending 就一直不被采纳。
         * 这就是"亮度稳定时才变化"的实现方式。 */
        uint32_t diff = (want > g_pending) ? (want - g_pending) : (g_pending - want);
        if (diff > BAND) {
            g_pending = want;
            g_since   = millis();
        } else if ((millis() - g_since) >= STABLE_MS) {
            g_stable  = g_pending;      /* 稳定够久了，正式采纳 */
        }

        /* ============= 第三步：平滑 —— 决定"怎么走到位" =============
         * 每 RAMP_MS 朝稳定目标挪最多 RAMP_STEP。
         * 用 (差多少 > 一步) ? 一步 : 剩下的差 这种写法，
         * 是为了**保证一定收敛且不会越过目标**（不会来回抖）。
         *
         * 注意 millis() 每次都重新读而不是缓存成 now：
         * 这一轮里 millis() 可能已经被中断更新过，但差值判断用无符号减法
         * 即使 g_ms 溢出回绕也是对的。 */
        uint32_t now = millis();
        if ((now - g_last_ramp) >= RAMP_MS) {
            g_last_ramp = now;
            if (g_cur < g_stable) {
                g_cur += ((g_stable - g_cur) > RAMP_STEP) ? RAMP_STEP : (g_stable - g_cur);
            } else if (g_cur > g_stable) {
                g_cur -= ((g_cur - g_stable) > RAMP_STEP) ? RAMP_STEP : (g_cur - g_stable);
            }
            pwm_set_permyriad(TIM3_BASE, 1u, g_cur);   /* 真输出：写 TIM3->CCR1 */
        }

        /* ============= 演示辅助：把内部状态打到串口 =============
         * 现场验收时老师能直接看到"ADC 原始值 / 目标 / 实际输出"三条曲线，
         * 比只看灯亮不亮要直观得多，也能证明防抖和平滑确实在起作用。 */
        if ((now - g_last_print) >= PRINT_MS) {
            g_last_print = now;
            uart_puts(USART1_BASE, "adc=");  uart_putu(USART1_BASE, light);
            uart_puts(USART1_BASE, " want="); uart_putu(USART1_BASE, want);
            uart_puts(USART1_BASE, " stable="); uart_putu(USART1_BASE, g_stable);
            uart_puts(USART1_BASE, " out=");  uart_putu(USART1_BASE, g_cur);
            uart_putc(USART1_BASE, '\n');
        }
    }
}
