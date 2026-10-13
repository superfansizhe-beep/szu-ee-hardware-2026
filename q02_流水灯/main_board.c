/* ============================================================================
 * q02_流水灯/main_board.c —— 笔试第 2 题「流水灯」真板版
 * ----------------------------------------------------------------------------
 * 题目：用**外部中断 EXTI**，按键控制流水灯的方向和频率。
 *   1) 上电默认 2Hz；
 *   2) 按键 1 按下 → 流动方向反转；
 *   3) 按键 2 每按一次 → 频率 2Hz → 1Hz → 0.5Hz → 2Hz 循环
 *
 * 【和仿真版 main.c 的关系】
 *   应用逻辑原样搬过来，一行没改：
 *     · 两个 volatile 标志 direction / freq_idx，中断里改、主循环用，语义相同；
 *     · freq_idx 取模 3 循环，三档周期的对应关系完全一致；
 *     · pos 在 4 个位置之间按 direction +1 / +3 前进，%4 取模也一致。
 *   换掉的只有两头：
 *     仿真：按键由 Python 拉 PA0/PA1，上升沿触发 → 真板：PB0/PB1 下降沿 + 上拉
 *     仿真：delay(period) 忙等             → 真板：millis()（TIM6 1ms 中断）计时
 *
 *   所以验收时被问"你这逻辑和仿真版是同一套吗"，答案是：是，只有 IO 层不同。
 *
 * ------------------------------------------------------------------
 * 一、接线（换板子只改下面那张引脚表）
 * ------------------------------------------------------------------
 *   流水灯 PD12~PD15（正好是 TIM4_CH1~CH4，AF2）：
 *     PD12 ─[220Ω~1kΩ]─|>|─ GND   高电平点亮
 *     PD13 ─[220Ω~1kΩ]─|>|─ GND
 *     PD14 ─[220Ω~1kΩ]─|>|─ GND
 *     PD15 ─[220Ω~1kΩ]─|>|─ GND
 *
 *   按键（**上拉输入、低电平有效**，exti_init() 内部已经配好内部上拉）：
 *     PB0 ──[按键]── GND     按键 1：方向反转（EXTI0）
 *     PB1 ──[按键]── GND     按键 2：频率切换（EXTI1）
 *
 *   调试串口：
 *     PA9  (USART1_TX, AF7) ── USB-TTL 的 RXD
 *     PA10 (USART1_RX, AF7) ── USB-TTL 的 TXD
 *
 *   ⚠ EXTI 线号是**全局的 0~15，不是每端口各 15 根**。
 *     也就是说 EXTI0 只能接一根引脚，而且同一个时刻只能有一个端口
 *     挂在 EXTI0 上。所以这题两根按键分别占 EXTI0 和 EXTI1 是必须的，
 *     不能都塞进 EXTI0 再靠软件区分端口。
 *     另外 EXTI 线号和 NVIC 的 IRQ 号**不是一回事**：
 *     EXTI0 → IRQ6、EXTI1 → IRQ7、EXTI9_5 → IRQ23、EXTI15_10 → IRQ40。
 *     public层 exti_init() 里 exti_irq_of() 就是干这个换算的，别自己硬编。
 *
 *   ⚠ 机械按键一次按下在 EXTI 看来是**一串**下降沿（弹跳 5~20ms）。
 *     如果中断里直接"每进来一次就翻一次方向"，一次按下会翻十几次，
 *     看起来就是灯抽风。处理办法见下面 wait_release_and_settle()。
 *
 * ------------------------------------------------------------------
 * 二、这题真正要讲的东西
 * ------------------------------------------------------------------
 *   ① **EXTI 的两段映射，缺一不可**：
 *      EXTI 控制器只知道"第 0 号线上有边沿"，它**不知道是哪个 GPIO 口**；
 *      必须再去 SYSCFG 的 EXTICR1 把 EXTI0 指明成 PAx/PBx（本题是 GPIOB）。
 *      只配 EXTI 不配 SYSCFG，中断永远进不来——这是"代码看着对、按键没反应"
 *      排第二的原因（第一是忘了开 EXTI 自己的时钟）。
 *
 *   ② **中断里只置标志，主循环才干活**（这条我在代码里也标出来了）：
 *      在中断里直接点灯其实能跑，但那样会把三个问题一起掩盖掉——
 *        · ISR 该有多短没有约束，以后加个串口打印进去也不知道自己踩了线；
 *        · 方向和频率的"生效"时刻被中断推迟，串口打印的顺序会和灯的变化对不上，
 *          现场演示时说不清到底是按键先到还是打印先到；
 *        · 真出问题（弹跳）时排查无从下手。
 *      现在 ISR 里只有"清挂起位 + 置一个 volatile 标志"两件事，
 *      ISR 长度是**固定的、与按键弹跳次数无关**的，这本身就是一种健壮性。
 *      代价是响应延迟变长了（等到主循环跑到），对这道题完全够用。
 *
 *   ③ **为什么用 TIM4 的 PWM 输出、而不是直接 GPIO 翻转**：
 *      PWM 的波形由硬件在 CNT<CCR 期间自己拉高，CPU 只在"换灯/换亮度"时写一次
 *      CCR。所以哪怕此刻主循环正忙着串口打印（115200 下一个字符 87µs），
 *      灯的波形也不会抖一下——纯 GPIO 手写电平时序会被打断。
 *      附带好处是亮度变成连续可调的（下面 led_head 用满占空比、led_tail 用低占空比，
 *      拖出一条渐亮的尾巴），方向反转时"哪边在跑"一眼就看得出。
 *
 *   ④ **为什么频率节拍不走 TIM4 的周期，而走 TIM6 的 1ms 时基**：
 *      这不是随便选的，是算出来的：
 *        - 公共层 pwm_init() 的约定是"1 个计数 = 1µs"，ARR 是 16 位，
 *          所以 TIM4 能表达的最长周期是 65535µs ≈ 65ms，装不下 500ms / 1s / 2s；
 *        - 想用 ARR 直接表示节拍就得把计数周期放大到毫秒级，
 *          那是改公共层 pwm 封装的语义，验收时"只有 IO 层不同"这句话就不成立了；
 *        - 于是分工：**时间**交给 TIM6 的 1ms 中断去数（millis），
 *          **电平**交给 TIM4 的 PWM 波形去产生。两者互不干扰，
 *          而且 TIM6 已经为了整个工程在跑了，不额外占资源。
 * ==========================================================================*/

#include "stm32f4_common.h"

/* ============================ 引脚表 ============================
 * 换板子只改这一段。注意 PD12~PD15 既是普通 GPIO，又是 TIM4_CH1~CH4（AF2）。 */
#define PIN_LED0    PD12   /* TIM4_CH1, AF2 */
#define PIN_LED1    PD13   /* TIM4_CH2, AF2 */
#define PIN_LED2    PD14   /* TIM4_CH3, AF2 */
#define PIN_LED3    PD15   /* TIM4_CH4, AF2 */

#define PIN_BTN_DIR PB0    /* EXTI0：方向反转 */
#define PIN_BTN_FRQ PB1    /* EXTI1：频率切换 */

#define UART_TX     PA9    /* USART1_TX → AF7 */
#define UART_RX     PA10   /* USART1_RX → AF7 */

/* ============================ 参数 ============================ */
/* PWM 周期 2ms = 500Hz。人眼分辨不出 500Hz 的闪烁，比相机慢门下的频闪干净得多；
 * 又是整数 µs，正好符合公共层"1 计数 = 1µs"的约定，ARR 直接就是 1999。 */
#define PWM_PERIOD_US 2000u
#define DUTY_HEAD     1000u   /* 千分比：当前位置的灯，满亮 */
#define DUTY_TAIL      220u   /* 千分比：上一位置的余晖，让方向可读 */

/* 三档频率对应的节拍周期。仿真版是 delay(400000/800000/1600000) 的圈数，
 * 真板上圈数换不成时间，所以直接写毫秒值，逻辑等价、结果可复现。 */
static const uint32_t g_period_ms[3] = { 500u, 1000u, 2000u };  /* 2Hz / 1Hz / 0.5Hz */
static const char    *g_freq_name[3] = { "2Hz", "1Hz", "0.5Hz" };

#define BOUNCE_MS   40u     /* 一次有效操作之间的最小间隔，挡住弹跳连发 */
#define LED_COUNT    4u

/* ============================ 跨中断共享的状态 ============================
 * volatile 不是可选的：这些变量在中断里被写、在主循环里被读，
 * 不加 volatile 编译器会把主循环里的读优化成"寄存器里的旧值"，
 * 表现就是"按了没反应"。仿真版也是这么写的，逻辑一致。
 *
 * 【为什么 ISR 在另一个文件 q2_exti_isr.c 里，不在本文件】
 *   公共层 stm32f4_common.h 已经用 weak 符号给了全部 98 个默认中断处理函数，
 *   而向量表也在那个头文件里。weak 符号只能被**另一个目标文件**里的
 *   同名强符号顶掉；在同一个文件里再写一遍同名函数是"重定义"直接编译报错。
 *   所以真正的 EXTI0/EXTI1 处理函数必须单独成文件（q2_exti_isr.c），
 *   链接时由强符号覆盖公共层的弱符号。这是这套公共层设计的预期用法，
 *   不是绕路。
 * ==========================================================================*/
volatile uint32_t g_dir_event = 0u;   /* 方向键事件计数（在 q2_exti_isr.c 里写） */
volatile uint32_t g_frq_event = 0u;   /* 频率键事件计数 */
volatile uint32_t g_last_edge = 0u;   /* 最近一次 EXTI 的时刻，用于二次防抖 */

/* 供 q2_exti_isr.c 调用：那个文件刻意不 include 公共层（会和本文件重复定义），
 * 所以清挂起位这件事由这里代做，寄存器宏只在本文件出现一次。 */
void exti_line_clear(uint32_t line)
{
    exti_clear(line);   /* 公共层的 exti_clear()：EXTI_PR 写 1 清 */
}

/* direction / freq_idx 只在主循环里改，所以不用 volatile —— 这也是
 * "ISR 只置标志"带来的直接好处：共享变量的写者只有一个。 */
static uint32_t g_direction = 0u;            /* 0=正转(+1) 1=反转(+3) */
static uint32_t g_freq_idx   = 0u;           /* 0/1/2 → 2Hz/1Hz/0.5Hz */

static const uint32_t g_led_pin[LED_COUNT] = { PIN_LED0, PIN_LED1, PIN_LED2, PIN_LED3 };

/* ============================================================================
 * take_press：等按键松开、且抖动窗口过去，才算"一次按压"
 * ----------------------------------------------------------------------------
 * 这是按键 + EXTI 的经典难点，也是这题技术上最值得讲的一点。
 *
 * 机械触点按下后的 5~20ms 里会反复接通断开，EXTI 看到的是一串下降沿。
 * 如果每次下降沿都执行一次操作，一次按压会被当成十几次。
 *
 * 这里用两个条件一起把关：
 *   ① 松手沿必须出现 —— 读 IDR 发现引脚回到高电平，说明这次按压真的结束了；
 *   ② 距离上一次沿至少 BOUNCE_MS —— 即使触点已经松开，最后一次弹跳
 *      也可能发生在几十微秒后，第二个条件把它们全挡在外面。
 *
 * 【为什么放在主循环而不是中断里】在中断里等松手意味着 ISR 会阻塞几十毫秒，
 * TIM6 的毫秒中断会被拖延，millis() 直接走不准——这是真板调试里
 * 最常见的"串口时间戳对不上"的原因。等按键这种动作天然属于主循环。
 * ==========================================================================*/
static uint32_t take_press(volatile uint32_t *event, uint32_t pin)
{
    if (*event == 0u) { return 0u; }

    if (gpio_read(pin) == 0u) {           /* 还按着：不是一次完整按压，先记账不回退 */
        return 0u;
    }
    if ((millis() - g_last_edge) < BOUNCE_MS) {
        return 0u;                        /* 还在抖动窗口里，这次不算 */
    }

    *event = 0u;                          /* 消费掉这批事件 */
    return 1u;
}

/* ============================================================================
 * LED 输出
 * ----------------------------------------------------------------------------
 * 用 TIM4 的四个 PWM 通道：
 *   · 当前位置 → 满占空比（灯全亮）
 *   · 上一位置 → 低占空比（余晖）
 *   · 其余     → 0 占空比（灭）
 *
 * 为什么留余晖：题目只要求"流水"，但纯硬边沿的流水在方向反转的那一刻
 * 视觉上很难一眼看出转向（因为亮的位置和走的方向没有视觉关联）。
 * 加一条渐亮的尾巴之后，"尾巴在哪边"就是方向，这条信息免费从硬件里长出来。
 * 这是"用 PWM 而不是 GPIO"的直接收益，不是为了炫技。
 * ==========================================================================*/
static void led_show(uint32_t pos)
{
    for (uint32_t i = 0u; i < LED_COUNT; ++i) {
        uint32_t duty = 0u;
        if (i == pos)     { duty = DUTY_HEAD; }
        else if ((pos + LED_COUNT - 1u) % LED_COUNT == i) { duty = DUTY_TAIL; }
        pwm_set_permyriad(TIM4_BASE, (uint8_t)(i + 1u), duty);
    }
}

/* ============================================================================
 * LED 的 PWM 初始化
 * ----------------------------------------------------------------------------
 * 标准三步：开时钟 → 配 AF2 → pwm_init。
 *
 * 【这里有个真实的坑，值得记下来】
 *  TIMx_CCMRx 里每个通道占 8 位，OCxM（输出模式）在其中的位置**逐通道不同**：
 *      CH1 → bit 4-7        CH2 → bit 12-15   （都在 CCMR1）
 *      CH3 → bit 4-7        CH4 → bit 12-15   （都在 CCMR2）
 *  规律是"同一个 CCMR 寄存器里，CH1/CH3 用低半、CH2/CH4 用高半"。
 *  写偏移的时候如果图省事写成 `(ch<=2) ? 0 : 8`，
 *  CH1 和 CH4 会碰巧正确，而 **CH2 的模式被写进 OC1M、CH3 的被写进 OC4M**——
 *  编译不报错、串口打印也正常，但这两路就是不出 PWM。
 *  本题四个通道全用，所以这个坑正好会踩。公共层的 pwm_init() 已经按上表算对了。
 * ==========================================================================*/
static void led_pwm_init(void)
{
    /* TIM4 的时钟由公共层 pwm_init() 内部开（APB1ENR bit2 = TIM4EN）。
     * 这里没有自己再开一遍，但**要点名说一下**：timer_ms_init() 只开了 TIM6，
     * TIM4 的时钟是 pwm_init() 顺手开的。如果哪天真换成手写寄存器配 PWM，
     * 忘了开这个钟的表现是"PSC/ARR 写进去读回来还是 0、示波器上一个毛都没有"——
     * 因为时钟没合闸时定时器寄存器是**静默失效**的，不报错。 */
    for (uint32_t i = 0u; i < LED_COUNT; ++i) {
        /* 必须先 gpio_af() 再 gpio_mode(MODE_AF)：前者写 AFRL/AFRH（选择哪个外设），
         * 后者写 MODER（把脚交给复用功能）。顺序反了也可能对，但反过来一定不对。 */
        gpio_af(g_led_pin[i], 2u);                        /* AF2 = TIM4 */
        gpio_mode(g_led_pin[i], MODE_AF, PULL_NONE, SPEED_HIGH);

        /* PWM 输出是开漏还是推挽由外设决定，但**速度等级要选高**：
         * 2ms 周期对应 500Hz 翻转，速率不够时边沿会有肉眼可见的台阶。 */
        pwm_init(TIM4_BASE, (uint8_t)(i + 1u), PWM_PERIOD_US);
    }
}

int main(void)
{
    clock_init();
    dwt_init();
    timer_ms_init();       /* millis() 的来源：TIM6 每 1ms 中断一次 */

    /* ---- 流水灯 ---- */
    led_pwm_init();

    /* ---- 按键：外部中断 ----
     * exti_init(pin, mode, prio) 一次做完四件事，别自己拆开写：
     *   ① 开 GPIOB 时钟  ② 开 SYSCFG 时钟（EXTICR 在 SYSCFG 里，不开时钟写不进去）
     *   ③ 把脚配成**上拉输入**  ④ SYSCFG 指明来源端口 + EXTI 选边沿 + 清残留挂起 + NVIC 使能
     *
     * mode=2 是**下降沿**。按键低电平有效，所以"按下"是从高(松开)掉到低(按下)，
     * 触发下降沿；上升沿触发对应的是高电平有效的接法，这里不能照抄仿真版的 1。
     * 只配单边沿是有意的：双触发会把松手也当成一次事件，操作次数翻倍。
     *
     * 优先级给 7（数值越大越不优先）。TIM6 的毫秒时基是 6 优先级，
     * 按键应该让位给时基——时基被拖延会直接让 millis() 走不准。 */
    exti_init(PIN_BTN_DIR, 2u, 7u);
    exti_init(PIN_BTN_FRQ, 2u, 7u);

    /* ---- 调试串口 ---- */
    gpio_af(UART_TX, 7u);  gpio_mode(UART_TX, MODE_AF, PULL_UP, SPEED_HIGH);
    gpio_af(UART_RX, 7u);  gpio_mode(UART_RX, MODE_AF, PULL_UP, SPEED_HIGH);
    uart_init(USART1_BASE, g_pclk2_hz, 115200u);

    uart_puts(USART1_BASE, "\r\nQ2 running-light (board) start\r\n");
    uart_puts(USART1_BASE, "PB0=EXTI0 direction, PB1=EXTI1 freq, active LOW\r\n");
    uart_puts(USART1_BASE, "freq=2Hz dir=forward\r\n");

    uint32_t pos       = 0u;
    uint32_t next_step = millis() + g_period_ms[g_freq_idx];
    led_show(pos);

    for (;;) {
        uint32_t now = millis();

        /* ---- 消费方向事件 ---- */
        if (take_press(&g_dir_event, PIN_BTN_DIR)) {
            g_direction ^= 1u;                     /* 0↔1 翻转，等价于仿真版的 ^=1 */
            uart_puts(USART1_BASE, "KEY1 -> dir=");
            uart_puts(USART1_BASE, g_direction ? "reverse\r\n" : "forward\r\n");
        }

        /* ---- 消费频率事件 ----
         * 立刻用新档位重算下一个 step 的时刻，而不是让旧周期跑完再换。
         * 否则从 0.5Hz 切回 2Hz 之后，还要等最多 2 秒才看得出变化，
         * 现场演示会让人觉得"按键没生效"。 */
        if (take_press(&g_frq_event, PIN_BTN_FRQ)) {
            g_freq_idx = (g_freq_idx + 1u) % 3u;
            next_step  = now + g_period_ms[g_freq_idx];
            uart_puts(USART1_BASE, "KEY2 -> freq=");
            uart_puts(USART1_BASE, g_freq_name[g_freq_idx]);
            uart_putc(USART1_BASE, '\r');
            uart_putc(USART1_BASE, '\n');
        }

        /* ---- 流水推进 ----
         * (int32_t)(now - next_step) >= 0 这种写法：millis() 是 32 位计数器，
         * 约 49 天后会回绕。无符号减法在回绕前后都成立，
         * 写成 now >= next_step 就会在回绕那一刻卡住将近 49 天。 */
        if ((int32_t)(now - next_step) >= 0) {
            /* 用"下一次该亮的时刻 + 周期"而不是 now + 周期：
             * 这样周期误差不会在每一步累积，几百步之后节拍依然准。 */
            next_step += g_period_ms[g_freq_idx];

            pos = (g_direction == 0u) ? ((pos + 1u) % LED_COUNT)
                                      : ((pos + LED_COUNT - 1u) % LED_COUNT);
            led_show(pos);
        }
    }
}