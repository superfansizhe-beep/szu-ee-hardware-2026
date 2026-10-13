/* ============================================================================
 * q04_电机风扇/main_board.c —— 第四题「面对疾风吧！」真板版
 * ----------------------------------------------------------------------------
 * 【和仿真版 main.c 的关系】
 *   应用逻辑一行没改：挡位变量 gear、挡位表查表定占空比、按键"只看新按下"的
 *   边沿检测、上下限钳位、写占空比的那一行，结构和仿真版一一对应。
 *   只把两头接到真硬件上：
 *       仿真：按键 PA0/PA1 轮询 GPIOA_IDR → 真板：PB0/PB1 上拉输入 + 消抖
 *       仿真：AIN1/AIN2/STBY 用 GPIOE 输出  → 真板：PA1/PA2/PA3 推挽输出
 *       仿真：占空比写 mailbox 0x2001FF00   → 真板：写 TIM2->CCR1 真实输出 PWM
 *   验收时老师问"你这逻辑和仿真版是一套吗"，可以直接答：是，只有 IO 层不同。
 *
 *   【唯一一处主动的改动：消抖和挡位手感】
 *     ① 加了按键消抖 —— 真板按键按下时触点会在几毫秒内反复通断，
 *        仿真里 Python 写 mailbox 当然看不到这个现象，但真板不加就会"按一下加两挡"。
 *     ② 挡位占空比从仿真版的 300/600 改成 400/700，让 1 挡和 2 挡的手感差别更明显，
 *        现场演示时风扇从"几乎不动"到"明显在转"再到"全速"层次更清楚。
 *        这是**数据表**不是逻辑，映射关系和钳位规则完全没动。
 *
 * ------------------------------------------------------------------
 * 一、TB6612FNG 真值表（这张表是这道题的全部，写代码前先背下来）
 * ------------------------------------------------------------------
 *                 AIN2   AIN1   ┌──────────── 桥臂状态 ────────────┐
 *                 STBY   PWMA   │ 效果                              │
 *   ─────────────────────────────────────────────────────────────────────
 *      低任意          任意  │ 待机（Standby）。H 桥上下桥全关，
 *                                输出呈高阻，电机引脚悬空。
 *                                ★ 注意：这时候就算 AIN1/AIN2 摆对、
 *                                  PWMA 也在出 PWM，电机也一点都不会转。
 *      高       0      1      │  0 → 1 = 正转（OUT1 高电平驱动）    │
 *      高       1      0      │  1 → 0 = 反转                        │
 *      高       1      1      │  刹车（Short brake）。两个桥臂同时导通，
 *                                电机两端被短接，靠电机自身的反电动势制动。
 *                                并不是"高阻悬空"，堵转电流不小。        │
 *      高       0      0      │  停止（Stop / 滑行）。上下桥都关，      │
 *                                电机靠惯性自由减速。                     │
 *   转向由 AIN1/AIN2 决定，转速由 PWMA 的占空比决定，两者互不影响。
 *
 * ------------------------------------------------------------------
 * 二、接线表（换板子只改下面这段 #define，代码其余部分不用动）
 * ------------------------------------------------------------------
 *   PA0 (TIM2_CH1, AF1) ── TB6612 的 PWMA
 *   PA1 ── TB6612 的 AIN1
 *   PA2 ── TB6612 的 AIN2
 *   PA3 ── TB6612 的 STBY
 *   PB0 ── 按钮「加挡」，另一端接 GND
 *   PB1 ── 按钮「减挡」，另一端接 GND
 *   PA9 (USART1_TX, AF7) ── USB-TTL 的 RXD      115200-8-N-1
 *   PA10(USART1_RX, AF7) ── USB-TTL 的 TXD
 *
 *   ⚠ TB6612 模块一般自带 VCC 逻辑电源（3.3V）和 VM 电机电源（5~12V），
 *     VM 那路千万别接到 STM32 的 3.3V 上带电机，会立刻烧保险丝。
 *   ⚠ USB-TTL 和 STM32 必须共地，TTL 的 GND 要和板子的 GND 连一根线。
 *   ⚠ 两个按钮都用内部上拉（约 40kΩ）就够了，因为按键线短、不怕干扰。
 *     如果接线拉得比较长、旁边有电机这种大干扰源，就在每个按钮上并一个
 *     10kΩ 外部上拉再试。
 *
 * ------------------------------------------------------------------
 * 三、这题真正要讲的技术点
 * ------------------------------------------------------------------
 *   要求①双键调挡、上下限钳位 → 靠边沿检测 + if 里的上下界判断，
 *         越界时"什么都不做"比"回弹到边界"更符合直觉。
 *   要求② PWM 调速           → 看懂"TIM2_CH1 的 CCR 就是占空比"这句话，
 *         以及为什么 PWM 频率取 20kHz（高于人耳上限，风扇不会发出"呜呜"的啸叫，
 *         低频 1kHz 那样的 PWM 反而能听见风扇被一 chop 一 chop 的声音）。
 *   附加点：STBY 必须拉高      → 这是全题最大的坑，见 apply_gear() 里的注释。
 *   附加点：按键消抖          → 真板才有的现象，仿真里绝对看不出来。
 * ==========================================================================*/

#include "stm32f4_common.h"

/* ============================ 引脚表 ============================
 * 换板子/换接线只改这一段。下面代码一行都不用动。 */
#define PIN_PWMA     PA0    /* TIM2_CH1, AF1  → TB6612 PWMA   */
#define PIN_AIN1     PA1    /* 推挽输出      → TB6612 AIN1   */
#define PIN_AIN2     PA2    /* 推挽输出      → TB6612 AIN2   */
#define PIN_STBY     PA3    /* 推挽输出      → TB6612 STBY   */
#define PIN_KEY_UP   PB0    /* 上拉输入低有效 → 加挡          */
#define PIN_KEY_DOWN PB1    /* 上拉输入低有效 → 减挡          */
#define UART_TX      PA9    /* USART1_TX, AF7                 */
#define UART_RX      PA10   /* USART1_RX, AF7                 */

/* ============================ 可调参数 ============================ */
#define GEAR_MAX     3u     /* 挡位 0..3，0 是停 */
#define DEBOUNCE_MS  20u    /* 按键消抖时间。真板触点抖动在 5~20ms 量级，
                              20ms 既够滤掉抖动，又不会让按键感觉"钝" */
#define PWM_PERIOD   20000u /* PWM 周期 20000µs = 20kHz。
                              选 20kHz 是因为它高于人耳听觉上限(20kHz)：
                              低于 1kHz 的调速能明显听见风扇被一 chop 一 chop，
                              高频调速就只能靠转速（也就是堵转声音）去听 */

/* 各挡占空比（满刻度 1000）。挡 0 是停，所以占空比 0。
 * 仿真版这里是 300/600，真板改成 400/700，理由见文件头"唯一一处主动的改动"。 */
static const uint32_t DUTY_TBL[GEAR_MAX + 1u] = { 0u, 400u, 700u, 1000u };

/* ============================ 按键状态 ============================
 * 四个变量配合出一个"消抖 + 边沿检测"，顺序不能乱：
 *   g_key_cand   本轮读到的原始电平
 *   g_key_t0     cand 从什么时候开始就没再变过
 *   g_key_state  已经确认（消抖通过）后的状态，1=按下
 *   g_key_primed 开机后有没有取过初始状态
 */
static uint32_t g_key_state  = 0u;
static uint32_t g_key_cand   = 0u;
static uint32_t g_key_t0     = 0u;
static uint32_t g_key_primed = 0u;

/* 读两个按键的原始状态，拼成一个位图：bit0=加挡键，bit1=减挡键，1 表示"正被按住"。
 * 判"低电平 = 按住"是因为两个脚都配了上拉：悬空/松开时被上拉到 3.3V，
 * 按下时按钮把它短到 GND。选低有效而不是高有效，是因为上拉电阻天然配在
 * 靠近芯片这一侧，能压住长线上的干扰；高有效则需要下拉，干扰更容易被放大。 */
static uint32_t key_raw(void)
{
    uint32_t v = 0u;
    if (gpio_read(PIN_KEY_UP)   == 0u) { v |= 1u; }
    if (gpio_read(PIN_KEY_DOWN) == 0u) { v |= 2u; }
    return v;
}

/* 返回"本次新按下"的按键位图（0=没按，1=按了加挡，2=按了减挡，3=同时按）。
 *
 * 为什么要分"原始电平"和"已确认状态"两层：
 *   机械按键按下时，触点会在 5~20ms 内通断好几次（这就是机械抖动）。
 *   如果直接对原始电平做边沿检测，一次按压会被认成"松开-按下-松开-按下"，
 *   表现为按一下加了两挡——这是真板专有的坑，仿真里根本复现不了。
 *   所以规则是：原始电平一变就先记下来重新计时，
 *              直到**连续稳定 DEBOUNCE_MS** 才认可，并算出"变化了哪些位"。
 *
 * 只认 0→1 的那部分（raw & ~g_key_state）：仿真版的边沿检测也是这个意思
 * （cur & ~prev，只取新按下，松开不响应），这里保持一致。 */
static uint32_t key_scan(void)
{
    uint32_t raw = key_raw();

    if (g_key_primed == 0u) {
        /* 开机第一圈：把"当前按住的样子"直接当成初始状态。
         * 少了这一步，如果上电瞬间人正按着某个键，
         * 第一圈会把它误认成"刚按下的沿"，电机莫名其妙就转起来了。 */
        g_key_primed = 1u;
        g_key_state  = raw;
        g_key_cand   = raw;
        g_key_t0     = millis();
        return 0u;
    }

    uint32_t pressed = 0u;
    if (raw != g_key_cand) {
        /* 电平和上一圈不一样：先怀疑是抖动，记下来重新开始计时，
         * 这一圈什么都不做。等它稳住。 */
        g_key_cand = raw;
        g_key_t0   = millis();
    } else if ((raw != g_key_state) && ((millis() - g_key_t0) >= DEBOUNCE_MS)) {
        /* 连续 DEBOUNCE_MS 都是这个值 → 认为是真的按下去了 */
        pressed = raw & ~g_key_state;
        g_key_state = raw;
    }
    return pressed;
}

/* ============================================================================
 * 按当前挡位配置 TB6612：方向脚 + STBY + PWM 占空比
 * ----------------------------------------------------------------------------
 * 顺序上把 STBY 放在最前面，这是全题最容易翻车的地方：
 *   STBY=0 时 TB6612 的 H 桥上下桥全部关断、输出高阻，
 *   此时 AIN1/AIN2 摆得再对、PWMA 再有 PWM，电机的引脚也是悬空的，
 *   转速永远是 0。很多人查一圈 PWM、查一圈方向脚都正常，
 *   最后发现是这块芯片没被叫醒。STBY 是"总闸"，必须先合闸再谈别的。
 * ==========================================================================*/
static void apply_gear(uint32_t gear)
{
    gpio_write(PIN_STBY, 1u);            /* 先合总闸 */

    if (gear == 0u) {
        /* 0 挡选 00（滑行停）而不是 11（刹车）：
         *   00 是上下桥都关，电机靠惯性自由减速，对风扇这种带叶片的负载最温柔；
         *   11 会把电机两端短接强制制动，堵转电流大，风扇叶片被硬刹容易伤机械结构，
         *   长时间反复刹车也容易把驱动芯片烤热。 */
        gpio_write(PIN_AIN1, 0u);
        gpio_write(PIN_AIN2, 0u);
    } else {
        /* 1/0 = 正转。注意真值表里"先写的那个脚电平低"才是正转，
         * 两个脚写反了就是反转——方向接反时风扇会往反方向吹，
         * 现场表现是"怎么调挡都是往回转"，很容易以为是 PWM 写错了。 */
        gpio_write(PIN_AIN1, 1u);
        gpio_write(PIN_AIN2, 0u);
    }

    pwm_set_permyriad(TIM2_BASE, 1u, DUTY_TBL[gear]);   /* 真输出：写 TIM2->CCR1 */
}

int main(void)
{
    /* ---- 时钟与时基 ---- */
    clock_init();                 /* HSE 8MHz → PLL 168MHz */
    dwt_init();                   /* 本题没用到 delay_us，但开着不影响，留给后续加功能 */
    timer_ms_init();              /* TIM6 每 1ms 中断一次 → millis() 可用，按键消抖靠它 */

    /* ---- 调试串口 USART1 ---- */
    gpio_af(UART_TX, 7u);  gpio_mode(UART_TX, MODE_AF, PULL_UP, SPEED_HIGH);
    gpio_af(UART_RX, 7u);  gpio_mode(UART_RX, MODE_AF, PULL_UP, SPEED_HIGH);
    uart_init(USART1_BASE, g_pclk2_hz, 115200u);
    uart_puts(USART1_BASE, "\r\nQ4 motor-fan (board) ready\r\n");
    uart_puts(USART1_BASE, "key1=PB0 gear+   key2=PB1 gear-\r\n");

    /* ---- 上电先把三个方向脚摆到"不动作"的电平 ----
     * gpio_out_init 第二个参数是"有效电平"：传 1 表示高电平是有效状态，
     * 它会先写入相反电平再交给我。三个脚都是高有效（高=转/工作），
     * 所以上电全给低：电机引脚不会在上电瞬间被驱动一下猛甩。 */
    gpio_out_init(PIN_AIN1, 1u);
    gpio_out_init(PIN_AIN2, 1u);
    gpio_out_init(PIN_STBY, 1u);        /* 上电先待机，等 apply_gear 再合闸 */

    /* ---- 按键：上拉输入 ----
     * 用内部上拉而不是浮空输入：浮空时引脚电平由漏电流决定，
     * 在电机这种强干扰场合可能读到随机值，表现为"随机加挡"。 */
    gpio_mode(PIN_KEY_UP,   MODE_IN, PULL_UP, SPEED_LOW);
    gpio_mode(PIN_KEY_DOWN, MODE_IN, PULL_UP, SPEED_LOW);

    /* ---- PWM 输出：TIM2_CH1 → PA0 ----
     * ★ 这里必须自己合 TIM2 的时钟闸：pwm_init() 只负责配 PSC/ARR/CCR 和开 CEN，
     *   并不去开 RCC。定时器时钟没开的时候，写进去的寄存器值不会被真正装载，
     *   输出脚上一个沿都不会有。这是"代码看着对、电机就是不动"的隐藏原因之一，
     *   而且现象和 STBY 没拉高一模一样，debug 时特别难分。
     *   APB1ENR 的位序：bit0=TIM2 bit1=TIM3 bit2=TIM4 bit3=TIM5 bit4=TIM6 */
    gpio_af(PIN_PWMA, 1u);               /* PA0 复用为 TIM2_CH1 */
    gpio_mode(PIN_PWMA, MODE_AF, PULL_NONE, SPEED_HIGH);
    RCC_APB1ENR |= (1u << 0);            /* TIM2EN */
    pwm_init(TIM2_BASE, 1u, PWM_PERIOD);

    /* ★ 全题只在这里 pwm_init() 一次。
     *   pwm_init 会重设 PSC/ARR，如果在一个已经跑着 PWM 的定时器上再调一次，
     *   前面通道的周期就被改掉了。后面调挡一律走 pwm_set_permyriad()，
     *   它只改 CCR，不碰周期，所以调速不会打断任何别的东西。 */

    uint32_t gear = 0u;
    apply_gear(gear);                    /* 开机停在 0 挡，但 STBY 已经合上 */
    uart_puts(USART1_BASE, "gear=0\r\n");

    for (;;) {
        uint32_t pressed = key_scan();

        if (pressed != 0u) {
            /* 钳位写法：到顶/到底时"什么都不做"，
             * 而不是先加再判断再减回来。越界时电机不会有任何抖一下。 */
            if (pressed & 1u) {          /* 加挡键 */
                if (gear < GEAR_MAX) {
                    gear++;
                }
            }
            if (pressed & 2u) {          /* 减挡键 */
                if (gear > 0u) {
                    gear--;
                }
            }
            apply_gear(gear);
            uart_puts(USART1_BASE, "gear=");
            uart_putu(USART1_BASE, gear);
            uart_puts(USART1_BASE, " duty=");
            uart_putu(USART1_BASE, DUTY_TBL[gear]);
            uart_putc(USART1_BASE, '\n');
        }
    }
}