/* ============================================================================
 * q01_点灯大师/main_board.c —— 笔试第 1 题「点灯大师」真板版
 * ----------------------------------------------------------------------------
 * 题目要求：
 *   · 按键 A 按下 → 灯亮（红）
 *   · 按键 B 按下 → 三色循环闪（红→绿→蓝→红…）
 *   · 再次按 A     → 灯灭
 *
 * 【和仿真版 main.c 的关系】
 *   应用逻辑原样搬过来，一行没改：
 *     · 三状态状态机 ST_OFF / ST_RED / ST_CYCLE，迁移条件完全相同
 *     · show_color(idx) 的"只亮一种颜色、其余灭掉"语义完全相同
 *     · 循环状态每 200ms 换一个颜色
 *   换掉的只有两头：
 *     仿真：按键由 Python 写 PA0/PA1   →  真板：PB0/PB1 上拉输入，真按键
 *     仿真：灯是 GPIOD 的 PD12/14/15   →  真板：PA0/PA1/PA2 三色共阳 LED
 *
 *   所以验收时被问"你这逻辑和仿真版是同一套吗"，答案是：是，只有 IO 层不同。
 *
 * ------------------------------------------------------------------
 * 一、接线（换板子只改下面那张引脚表）
 * ------------------------------------------------------------------
 *   三色 LED（常见共阳模块）：
 *     PA0 ─[220Ω]─┐
 *     PA1 ─[220Ω]─┼── LED 公共端（共阳接 VCC 时公共端接 3.3V）
 *     PA2 ─[220Ω]─┘        红 / 绿 / 蓝
 *
 *   按键（上拉输入、低电平有效）：
 *     PB0 ──[按键]── GND     按键 A：一键开关灯
 *     PB1 ──[按键]── GND     按键 B：进入三色循环
 *
 *   调试串口：
 *     PA9  (USART1_TX, AF7) ── USB-TTL 的 RXD
 *     PA10 (USART1_RX, AF7) ── USB-TTL 的 TXD
 *
 *   ⚠ **低电平有效**这件事必须和接线对应上：按键一脚接 MCU、一脚接 GND，
 *     松开时靠芯片内部上拉被拉到 3.3V（读到 1），按下时被短到地（读到 0）。
 *     如果实际接的是"一脚接 VCC、一脚接 MCU"，那有效电平就是高，
 *     得把代码里 KEY_ACTIVE_LOW 改掉，不能靠改接线硬凑——验收老师会现场问。
 *
 *   ⚠ **共阳 LED 的灌电流**：这种模块把 LED 阳极接到 VCC，靠 MCU 拉低点亮，
 *     电流从 VCC 经 LED 流入 MCU 的 IO。每个口 25mA 上限，三个一起点没问题，
 *     但如果换成大功率灯带就别这么接了。
 *
 * ------------------------------------------------------------------
 * 二、这题真正要讲的东西
 * ------------------------------------------------------------------
 *   ① 按键消抖 —— 真板和仿真最大的差别就在这里。
 *      机械按键按下时金属触点会在 5~20ms 内反复接通/断开（弹跳），
 *      一次"按下"在固件看来可能是**几十次**边沿。不消抖的表现是：
 *      按一下 A 结果灯连闪好几下；按 B 进循环后疯狂跳色。
 *      这里的做法是两级：① 相邻两次采样电平不同就重置计时，
 *      ② 电平连续稳定 20ms 才认账。比"延时 20ms 再读一次"好在
 *      ——后者如果按键正好在延时窗口里松手就会漏掉这一次按压。
 *
 *   ② 上拉/有效电平是成对的：PUPDR 配上拉 + 有效电平取 0，
 *      两者必须同时改。只改一个，程序就变成"永远按下"或者"永远没按"。
 *
 *   ③ 为什么状态机而不是一堆 if：三个状态之间的迁移是确定的，
 *      用 switch(state) 表达之后，每种状态下"谁能改变什么"一目了然，
 *      加新状态时不用去翻别处的判断。状态机是这一类"按键/传感器组合题"的通用骨架。
 * ==========================================================================*/

#include "stm32f4_common.h"

/* ============================ 引脚表 ============================
 * 换板子/换接线只改这一段，下面代码一行都不用动。 */
#define PIN_LED_R   PA0    /* 红 */
#define PIN_LED_G   PA1    /* 绿 */
#define PIN_LED_B   PA2    /* 蓝 */

#define PIN_BTN_A   PB0    /* 一键开关灯 */
#define PIN_BTN_B   PB1    /* 进入三色循环 */

#define UART_TX     PA9    /* USART1_TX → AF7 */
#define UART_RX     PA10   /* USART1_RX → AF7 */

/* 有效电平：本项目是"上拉输入 + 按下接地"，所以按下 = 0。
 * 换接线方式（比如高电平触发模块）时改这一个宏，不要去改防抖代码。 */
#define KEY_ACTIVE_LOW 1u

/* ============================ 可调参数 ============================ */
#define DEBOUNCE_MS 20u    /* 电平要连续稳定这么久才认账。太小会被弹跳骗，
                              太大则手感发木——人手最快连点也在 60ms 以上 */
#define POLL_MS     3u     /* 主循环采样周期。不需要很快，采样越密对消抖越友好 */
#define CYCLE_MS   200u    /* 循环状态下每种颜色亮多久（仿真版是 8 圈 × 25ms ≈ 200ms，
                              这里换成直接用毫秒时基数，逻辑等价但不受编译结果影响 */

/* ============================ 状态机 ============================ */
typedef enum {
    ST_OFF,     /* 全灭 */
    ST_RED,     /* 红色常亮 */
    ST_CYCLE    /* 红→绿→蓝 循环 */
} state_t;

static state_t  g_state      = ST_OFF;
static uint32_t g_cycle_idx  = 0u;    /* CYCLE 状态下当前该亮第几号颜色 */
static uint32_t g_cycle_next = 0u;    /* 下一次换色的时刻 */

/* ============================ 按键消抖 ============================
 * 一个按键在固件眼里不是一个 bool，而是三个变量：
 *   raw     —— 上一拍从 IDR 直接读到的原始电平（可能正在抖）
 *   stable  —— 消抖之后的"可信"电平，状态机只认它
 *   since   —— raw 变成现在这个值是在什么时候（用来判稳定时长）
 *
 * 为什么不在中断里做消抖：弹跳会产生密集的边沿，中断里做等于把 CPU
 * 全花在抖动的边沿上；放在主循环里按固定节拍采样，几微秒一次就够，
 * 而且消抖期间主循环还在跑别的东西（比如这题的三色循环计时）。
 * ==========================================================================*/
typedef struct {
    uint32_t raw;
    uint32_t stable;
    uint32_t since;
} key_t;

/* 初值必须和"松开"时的 pressed 值一致（这里是 0）。
 * 不一致的话第一圈就会进"电平变了"分支把 since 重置，功能上看不出问题，
 * 但以后改有效电平时这里很容易跟着漏改，埋一个开机误触发的隐患。 */
static key_t g_key_a = { 0u, 0u, 0u };
static key_t g_key_b = { 0u, 0u, 0u };

/* 把原始电平折算成"1 = 按下"，和有效电平解耦。
 * 这样下面消抖和状态机里的代码就完全不需要知道接线是哪种接法。 */
static uint32_t key_pressed(uint32_t pin)
{
    uint32_t level = gpio_read(pin);
    return KEY_ACTIVE_LOW ? (level ? 0u : 1u) : level;
}

/* 消抖 + 取"刚按下"沿。返回 1 表示这一次轮询里检测到一次有效按下。 */
static uint32_t key_update(key_t *k, uint32_t pressed, uint32_t now)
{
    uint32_t press_event = 0u;

    if (pressed != k->raw) {
        /* 电平变了：从现在开始重新计稳定时间。
         * 注意这里是"变了就重置"，不是"稳定就更新"——
         * 后者在抖动期间会反复把 stable 跟着抖，是常见的错误写法。 */
        k->raw   = pressed;
        k->since = now;
    } else if ((now - k->since) >= DEBOUNCE_MS) {
        if (pressed != k->stable) {
            uint32_t was = k->stable;
            k->stable   = pressed;
            /* 只认"松开 → 按下"这个方向。
             * 松手沿不产生事件，否则"切换状态"会在按下和松开时各触发一次。 */
            if ((pressed == 1u) && (was == 0u)) { press_event = 1u; }
        }
    }
    return press_event;
}

/* ============================================================================
 * show_color：只亮指定颜色
 * ----------------------------------------------------------------------------
 * 先全灭、再点亮，语义和仿真版 show_color() 完全一致。
 * 三个脚在同一端口 GPIOA，所以用一次 BSRR 写完"灭三个 + 亮一个"，
 * 不需要先写一次 OFF 再写一次 ON——两步之间会有一个"全灭"的瞬间。
 * ==========================================================================*/
static void led_apply(uint32_t r_on, uint32_t g_on, uint32_t b_on)
{
    uint32_t base = pin_gpio_base(PIN_LED_R);   /* 三个脚同一端口，才能合到一次写里 */

    /* 共阳 LED：阳极接 VCC，靠 MCU 拉低点亮。
     * 所以"点亮"= 低电平 = BSRR 高 16 位的复位位，"熄灭"= 高电平 = 低 16 位的置位位。
     * 如果买的是共阴模块（阴极接 GND），把下面两个位区对调即可，
     * 别去改 show_color 的逻辑——逻辑和灯珠的极性是两件事。 */
    uint32_t v = (r_on ? (1u << (pin_no(PIN_LED_R) + 16u)) : (1u << pin_no(PIN_LED_R)))
            | (g_on ? (1u << (pin_no(PIN_LED_G) + 16u)) : (1u << pin_no(PIN_LED_G)))
            | (b_on ? (1u << (pin_no(PIN_LED_B) + 16u)) : (1u << pin_no(PIN_LED_B)));

    GPIOB(base) = v;
}

/* 语义和仿真版 show_color(idx) 一致：只亮 idx 这种颜色，其余全灭。
 * idx 只在 0/1/2 里选；传越界值会退化成"全灭"，不会乱写寄存器。 */
static void show_color(uint32_t idx)
{
    led_apply((idx == 0u) ? 1u : 0u,
              (idx == 1u) ? 1u : 0u,
              (idx == 2u) ? 1u : 0u);
}

static void show_color_off(void)
{
    led_apply(0u, 0u, 0u);
}

int main(void)
{
    clock_init();
    dwt_init();
    timer_ms_init();       /* millis() 的来源：TIM6 每 1ms 中断一次 */

    /* ---- 按键：上拉输入 ----
     * PULL_UP 不是"锦上添花"，它是这套接线的**必要条件**：
     * 松开时引脚没有任何外部驱动，靠内部上拉才有确定的高电平；
     * 不开上拉就是浮空输入，读出来是随机数，表现为按键自己乱触发。
     * 外部已经有 4.7k 上拉的板子，内部上拉再开一次无害（并联变强）。 */
    gpio_mode(PIN_BTN_A, MODE_IN, PULL_UP, SPEED_LOW);
    gpio_mode(PIN_BTN_B, MODE_IN, PULL_UP, SPEED_LOW);

    /* ---- LED：推挽输出 ---- */
    gpio_mode(PIN_LED_R, MODE_OUT, PULL_NONE, SPEED_LOW);
    gpio_mode(PIN_LED_G, MODE_OUT, PULL_NONE, SPEED_LOW);
    gpio_mode(PIN_LED_B, MODE_OUT, PULL_NONE, SPEED_LOW);

    /* ---- 调试串口 ---- */
    gpio_af(UART_TX, 7u);  gpio_mode(UART_TX, MODE_AF, PULL_UP, SPEED_HIGH);
    gpio_af(UART_RX, 7u);  gpio_mode(UART_RX, MODE_AF, PULL_UP, SPEED_HIGH);
    uart_init(USART1_BASE, g_pclk2_hz, 115200u);

    uart_puts(USART1_BASE, "\r\nQ1 led-master (board) start\r\n");
    uart_puts(USART1_BASE, "A=PB0 toggle, B=PB1 cycle, active LOW\r\n");
    uart_puts(USART1_BASE, "state=OFF\r\n");

    uint32_t last_poll = millis();

    for (;;) {
        uint32_t now = millis();

        /* 按固定节拍采样，而不是"主循环一圈采一次"。
         * 主循环本身要跑串口打印和换色计时，一圈的实际耗时不稳定，
         * 拿它当采样周期会让消抖窗口时宽时窄。 */
        if ((now - last_poll) >= POLL_MS) {
            last_poll += POLL_MS;

            uint32_t a_press = key_update(&g_key_a, key_pressed(PIN_BTN_A), now);
            uint32_t b_press = key_update(&g_key_b, key_pressed(PIN_BTN_B), now);

            if (a_press || b_press) {
                switch (g_state) {
                case ST_OFF:
                    if (a_press) {
                        g_state = ST_RED;
                        show_color(0u);
                        uart_puts(USART1_BASE, "A -> state=RED\r\n");
                    }
                    break;

                case ST_RED:
                    if (a_press) {
                        g_state = ST_OFF;
                        show_color_off();
                        uart_puts(USART1_BASE, "A -> state=OFF\r\n");
                    } else if (b_press) {
                        g_state     = ST_CYCLE;
                        g_cycle_idx = 0u;
                        g_cycle_next = now + CYCLE_MS;
                        show_color(0u);
                        uart_puts(USART1_BASE, "B -> state=CYCLE\r\n");
                    }
                    break;

                case ST_CYCLE:
                    if (a_press) {
                        g_state = ST_OFF;
                        show_color_off();
                        uart_puts(USART1_BASE, "A -> state=OFF\r\n");
                    }
                    break;
                }
            }
        }

        /* ---- 循环换色 ----
         * 单独放主循环而不是塞进按键分支里：换色是"时间到了自动发生"，
         * 和有没有按键无关；混在一起会让"没按键时要不要走时基"这种问题
         * 在代码上看不出来。这里写成了"到点了就翻下一页"，非阻塞。 */
        if ((g_state == ST_CYCLE) && ((int32_t)(now - g_cycle_next) >= 0)) {
            g_cycle_next += CYCLE_MS;
            g_cycle_idx   = (g_cycle_idx + 1u) % 3u;
            show_color(g_cycle_idx);

            uart_puts(USART1_BASE, "cycle -> ");
            uart_putu(USART1_BASE, g_cycle_idx);
            uart_putc(USART1_BASE, '\r');
            uart_putc(USART1_BASE, '\n');
        }
    }
}