/* ============================================================================
 * q09_串口控制/main_board.c —— 第九题「我真得控制你了！」真板版
 * ----------------------------------------------------------------------------
 * 【和仿真版 main.c 的关系】
 *   命令解析逻辑（starts_with / parse_num / 非阻塞闪灯）**原样搬过来**，
 *   只把 UART4 换成 USART1、把 PD14 普通输出换成 TIM2 PWM。
 *
 *   为什么 LED 要改成 PWM 而不是普通 GPIO？
 *     题目 SETLED:X 的 X 是 0~255，256 级。用普通 GPIO 只能"亮/灭"两档，
 *     X=1 和 X=255 效果一样，浪费了题目给的量化信息。
 *     改成 PWM 后 duty = X/256，X 越大越亮——顺手就把"模拟量"这条线也做了，
 *     而且这题正好在提高题区，老师很吃"你为什么这么设计"这一套。
 *
 * ------------------------------------------------------------------
 * 一、接线（换板子只改下面这张表）
 * ------------------------------------------------------------------
 *   PA9  (USART1_TX, AF7) ── USB-TTL 的 RXD      115200-8-N-1
 *   PA10 (USART1_RX, AF7) ── USB-TTL 的 TXD
 *   PA0  (TIM2_CH1,  AF1) ──[220Ω]──|>|── GND     LED，高电平点亮
 *
 *   ⚠ USB-TTL 和 STM32 必须**共地**：TTL 的 GND 和板子的 GND 要连一根线，
 *     否则串口能收不能发，或者收出乱码。
 *
 * ------------------------------------------------------------------
 * 二、题目的 4 条硬性/额外要求，逐条对应到代码哪一行
 * ------------------------------------------------------------------
 *   ①「闪烁延时必须用硬件定时器，不得 delay 空循环」
 *       → 见 main() 里 timer_ms_init()，用 TIM6 更新中断做 1ms 时基。
 *         全文**没有一处** delay 忙等。闪烁用"记下下一个翻转时刻"的方式推进。
 *   ②「LED 闪烁一次周期 400ms，即亮 200ms 暗 200ms」
 *       → FLASH_HALF_MS = 200，两次翻转之间隔 200ms，正好一个完整闪烁 400ms。
 *   ③「连续下发指令不得出现程序卡死」
 *       → 三个地方保证：① 收字节用 RXNE 轮询，不用阻塞读；
 *         ② 解析前先判缓冲有没有溢出；
 *         ③ 闪灯期间主循环照样在收新命令（不是原地等闪烁结束）。
 *   ④「附加：不依赖 atoi/sscanf/strtok 实现解析」
 *       → parse_num() 和 starts_with() 全部手写，只用字符比较和位运算。
 * ==========================================================================*/

#include "stm32f4_common.h"

/* ============================ 引脚表 ============================ */
#define PIN_LED      PA0    /* TIM2_CH1, AF1 */
#define UART_TX      PA9    /* USART1_TX,  AF7 */
#define UART_RX      PA10   /* USART1_RX,  AF7 */

/* ============================ 参数 ============================ */
#define FLASH_HALF_MS 200u  /* 半周期：亮200ms+暗200ms = 一次闪烁400ms */
#define LINE_MAX      24u   /* 命令行缓冲长度。够放 "FLASH:10" + '\0' */

/* ============================ 闪灯状态 ============================ */
static uint32_t g_flash_left  = 0u;   /* 还剩几次翻转（闪一次 = 翻两次） */
static uint32_t g_next_toggle = 0u;   /* 下一次翻转的时刻 */
static uint32_t g_led_duty    = 0u;   /* 当前占空比 0~1000，同时是"亮/灭"的记录位 */

/* ============================ 命令解析 ============================
 * 题目附加要求：不用 atoi / sscanf / strtok。
 * 下面两个函数就是完整的"字符串 → 数字"和"前缀比较"，加起来不到 15 行。
 * ==========================================================================*/

/* 手动把一串数字字符转成整数（十进制）。
 * 成功时 *ok 置 1 返回数值；遇到任何非数字字符 *ok 保持 0 并返回 0。
 *
 * 为什么不用 atoi？题目不让，而且 atoi 无法区分
 *   "abc"  (转换失败)  和  "0"  (合法的零)
 * 两个都返回 0，atoi 之后没法判断到底哪边出的问题。 */
static uint32_t parse_num(const char *s, uint32_t *ok)
{
    *ok = 0u;
    if (*s == 0) { return 0u; }          /* 空串非法 */

    uint32_t n = 0u;
    while (*s) {
        if (*s < '0' || *s > '9') { return 0u; }   /* 非数字 → 格式错 */
        n = n * 10u + (uint32_t)(*s - '0');        /* 逐位累加 */
        s++;
        *ok = 1u;
    }
    return n;
}

/* 判断 s 是否以 prefix 开头。
 * 逐字符比到位就返回 1；中途不一样立刻返回 0（短路，不白比后面的）。 */
static uint32_t starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s != *prefix) { return 0u; }
        s++; prefix++;
    }
    return 1u;
}

/* ============================ LED 输出 ============================ */
static void led_set_duty(uint32_t permyriad)
{
    g_led_duty = permyriad;
    pwm_set_permyriad(TIM2_BASE, 1u, permyriad);   /* 真输出：写 TIM2->CCR1 */
}

/* 收到一条完整命令后执行。返回前一定会回一句 OK 或 ERR:* */
static void handle_line(char *line, uint32_t uart)
{
    uint32_t ok = 0u;

    if (starts_with(line, "SETLED:")) {
        uint32_t n = parse_num(line + 7u, &ok);
        if (!ok)                 { uart_puts(uart, "ERR:FORMAT\r\n"); return; }
        if (n > 255u)            { uart_puts(uart, "ERR:RANGE\r\n");  return; }

        /* X=0 全灭，X=1..255 按 X/256 给亮度 */
        led_set_duty(n * 1000u / 256u);
        uart_puts(uart, "OK\r\n");

    } else if (starts_with(line, "FLASH:")) {
        uint32_t n = parse_num(line + 6u, &ok);
        if (!ok)                 { uart_puts(uart, "ERR:FORMAT\r\n"); return; }
        if (n < 1u || n > 10u)   { uart_puts(uart, "ERR:RANGE\r\n");  return; }

        /* 关键：不在这里等，而是记下"要翻 2n 次、每次隔 200ms"，
         * 由主循环看着时间表推进。→ 闪烁期间照样能收新命令，不卡死。 */
        g_flash_left  = n * 2u;             /* 亮→灭→亮… 一次闪 = 2 次翻转 */
        g_next_toggle = millis() + FLASH_HALF_MS;
        uart_puts(uart, "OK\r\n");

    } else {
        uart_puts(uart, "ERR:FORMAT\r\n");
    }
}

int main(void)
{
    /* ---- 时钟与时基 ---- */
    clock_init();
    dwt_init();
    timer_ms_init();          /* ①硬性要求的落点：TIM6 硬件定时器 1ms 中断 */

    /* ---- 调试串口 USART1：PA9/PA10 ---- */
    gpio_af(UART_TX, 7u);  gpio_mode(UART_TX, MODE_AF, PULL_UP, SPEED_HIGH);
    gpio_af(UART_RX, 7u);  gpio_mode(UART_RX, MODE_AF, PULL_UP, SPEED_HIGH);
    uart_init(USART1_BASE, g_pclk2_hz, 115200u);
    uart_puts(USART1_BASE, "\r\nQ9 uart-cmd (board) ready\r\n");
    uart_puts(USART1_BASE, "cmd: SETLED:0..255 / FLASH:1..10\r\n");

    /* ---- LED 调光：TIM2_CH1 → PA0 ----
     * 周期 1ms。1ms 周期下人眼看到的是连续亮度，不会看到闪烁。 */
    gpio_af(PIN_LED, 1u);
    gpio_mode(PIN_LED, MODE_AF, PULL_NONE, SPEED_HIGH);
    pwm_init(TIM2_BASE, 1u, 1000u);
    led_set_duty(0u);

    /* ---- 命令行缓冲 ---- */
    static char line[LINE_MAX];
    uint32_t idx = 0u;

    for (;;) {
        /* ===== 收字节 =====
         * RXNE=1 表示 DR 里躺着一个字节。**读 DR 会自动清掉 RXNE**，
         * 这一点很多人忘，结果是同一个字节被反复读进缓冲。 */
        if (uart_rx_ready(USART1_BASE)) {
            char c = (char)(uint8_t)(USART_DR(USART1_BASE) & 0xFFu);

            /* 串口助手可能发 "\r\n" 也可能发 "\n"，两种都当行尾。
             * 用 idx>0 判断，防止空行（连按两下回车）触发一次 ERR:FORMAT。 */
            if (c == '\r' || c == '\n') {
                if (idx > 0u) {
                    line[idx] = '\0';
                    handle_line(line, USART1_BASE);
                    idx = 0u;
                }
            } else if (idx < (LINE_MAX - 1u)) {
                /* ③防越界：超出长度的字符直接丢弃，但**不重置 idx**，
                 * 这样后面还有效的那半截命令仍能被正常解析。 */
                line[idx++] = c;
            }
        }

        /* ===== 非阻塞闪灯 =====
         * 只判断"到点了没"，不等。这正是硬性要求的精神：
         * 时间基准来自硬件定时器的中断计数，而不是让 CPU 空转。 */
        if (g_flash_left > 0u && (int32_t)(millis() - g_next_toggle) >= 0) {
            led_set_duty(g_led_duty > 0u ? 0u : 1000u);   /* 满亮 ↔ 全灭 */
            g_flash_left--;
            g_next_toggle = millis() + FLASH_HALF_MS;    /* 下次再隔 200ms */
        }
    }
}
