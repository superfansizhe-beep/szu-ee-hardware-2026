/* ============================================================================
 * lesson5_q9_uart_cmd/main.c —— 笔试第9题「我真得控制你了！」全仿真版
 * ----------------------------------------------------------------------------
 * 题目：电脑通过串口发命令，STM32 解析执行并回复。
 *   SETLED:X   X=0 灭灯；0<X<256 亮灯；回 OK；超范围回 ERR:RANGE
 *   FLASH:X    X=1..10 闪 X 次（亮200ms灭200ms）；回 OK；超范围/非数字回错
 *   命令格式不对（名字错/不是数字）回 ERR:FORMAT
 * 硬性要求：闪烁必须用硬件定时器(SysTick)，不得用 delay 空循环；不得卡死。
 *
 * 这题的新东西：
 *   1) SysTick 硬件定时器：每1ms进一次中断，g_ms++，得到"毫秒数"
 *   2) 串口收数据：读 SR 的 RXNE 位，有数据就读 DR
 *   3) 非阻塞：用 millis() 判断时间，不原地 delay
 *   4) 自己解析命令（不用 atoi/sscanf）
 * ==========================================================================*/

#include <stdint.h>
#define MMIO(addr) (*(volatile uint32_t *)(addr))

/* ---- RCC：开 UART4(APB1) 和 GPIOD(AHB1) ---- */
#define RCC_AHB1ENR       MMIO(0x40023800u + 0x30u)
#define RCC_AHB1ENR_GPIOD (1u << 3)
#define RCC_APB1ENR       MMIO(0x40023800u + 0x40u)
#define RCC_APB1ENR_UART4 (1u << 19)

/* ---- UART4 ---- */
#define UART4_SR   MMIO(0x40004C00u + 0x00u)
#define UART4_DR   MMIO(0x40004C00u + 0x04u)
#define UART4_BRR  MMIO(0x40004C00u + 0x08u)
#define UART4_CR1  MMIO(0x40004C00u + 0x0Cu)
#define UART_SR_RXNE (1u << 5)   /* 接收寄存器非空：收到一个字节 */
#define UART_SR_TXE  (1u << 7)  /* 发送寄存器空：可以写下一个字节 */

/* ---- GPIOD LED：PD14 ---- */
#define GPIOD_MODER MMIO(0x40020C00u + 0x00u)
#define GPIOD_BSRR  MMIO(0x40020C00u + 0x18u)
#define LED_ON   (1u << 14)
#define LED_OFF  (1u << (14u + 16u))

/* ---- SysTick（内核硬件定时器，地址在 0xE000E010）---- */
#define SYST_CSR MMIO(0xE000E010u + 0x00u)  /* 控制与状态 */
#define SYST_RVR MMIO(0xE000E010u + 0x04u)  /* 重装载值 */
#define SYST_CVR MMIO(0xE000E010u + 0x08u)  /* 当前值 */
/* CSR: bit0=ENABLE(开), bit1=TICKINT(到点产生中断), bit2=CLKSOURCE(用内核时钟) */

/* 毫秒计数器：SysTick 每 1ms 进一次中断就 +1。必须 volatile（中断和主循环共用） */
static volatile uint32_t g_ms = 0u;
static uint32_t millis(void) { return g_ms; }

/* ================= 向量表：这次第15项要指向 SysTick_Handler ================= */
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
typedef void (*isr_t)(void);
void Reset_Handler(void);
void Default_Handler(void);
void SysTick_Handler(void);
int  main(void);

__attribute__((section(".isr_vector"), used))
isr_t vector_table[] = {
    (isr_t)&_estack, Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler,
    0, Default_Handler, SysTick_Handler,   /* 第15项 = SysTick 中断 */
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

/* SysTick 中断：到点自动进来，只干一件事——毫秒数+1 */
void SysTick_Handler(void) { g_ms++; }

/* ================= 串口收发小函数 ================= */
static void uart_putc(char c)
{
    while ((UART4_SR & UART_SR_TXE) == 0u) ;
    UART4_DR = (uint32_t)(uint8_t)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

/* ================= 命令解析 =================
 * 手动把一串数字字符转成整数，不用 atoi。
 * 成功返回数字并把 *ok 置1；遇到非数字字符则 *ok=0 */
static uint32_t parse_num(const char *s, uint32_t *ok)
{
    *ok = 0;
    uint32_t n = 0;
    if (*s == 0) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;   /* 有非数字 -> 格式错 */
        n = n * 10u + (uint32_t)(*s - '0');
        s++;
        *ok = 1;
    }
    return n;
}

/* 比较两个字符串开头是否相等（手写 strncmp，不依赖库） */
static int starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

/* ================= 闪灯状态（非阻塞） ================= */
static uint32_t flash_left   = 0u;   /* 还剩几次翻转（一次闪=翻转两次：亮->灭） */
static uint32_t next_toggle  = 0u;   /* 下一次翻转的毫秒时刻 */
static uint32_t led_on       = 0u;   /* 记录当前灯是亮还是灭 */

static void led_set(uint32_t on)
{
    led_on = on;
    GPIOD_BSRR = on ? LED_ON : LED_OFF;
}

/* 处理一整条命令。在主循环里遇到一行结束才调用 */
static void handle_line(char *line)
{
    uint32_t ok = 0;

    if (starts_with(line, "SETLED:")) {
        uint32_t n = parse_num(line + 7, &ok);
        if (!ok) { uart_puts("ERR:FORMAT\r\n"); return; }
        if (n > 255u) { uart_puts("ERR:RANGE\r\n"); return; }
        if (n == 0u) led_set(0u);
        else         led_set(1u);
        uart_puts("OK\r\n");

    } else if (starts_with(line, "FLASH:")) {
        uint32_t n = parse_num(line + 6, &ok);
        if (!ok) { uart_puts("ERR:FORMAT\r\n"); return; }
        if (n < 1u || n > 10u) { uart_puts("ERR:RANGE\r\n"); return; }
        /* 不原地等待，而是记下"要闪 2n 次、200ms 翻一次"，主循环自己转 */
        flash_left  = n * 2u;
        next_toggle = millis() + 200u;
        uart_puts("OK\r\n");

    } else {
        uart_puts("ERR:FORMAT\r\n");
    }
}

int main(void)
{
    /* ---- 初始化 UART4：这次既要发(TE)也要收(RE) ---- */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOD;
    RCC_APB1ENR |= RCC_APB1ENR_UART4;

    /* PD14 输出 */
    GPIOD_MODER = (GPIOD_MODER & ~(3u << 28)) | (1u << 28);
    GPIOD_BSRR = LED_OFF;

    UART4_BRR = 0x682u;
    /* CR1: UE(bit13) + TE(bit3发) + RE(bit2收) = 0x200C */
    UART4_CR1 = 0x200Cu;

    /* ---- 初始化 SysTick：每 1ms 一次中断 ----
     * RVR 设 168000-1（对应168MHz下1ms），开中断并使能 */
    SYST_RVR = 168000u - 1u;
    SYST_CVR = 0u;
    SYST_CSR = (1u << 0) | (1u << 1) | (1u << 2);   /* ENABLE | TICKINT | CLKSOURCE */

    /* ---- 主循环：收命令 + 非阻塞闪灯，两不耽误 ---- */
    char line[24];
    uint32_t idx = 0;

    for (;;) {
        /* 1) 收串口：RXNE=1 说明来了一个字节 */
        if (UART4_SR & UART_SR_RXNE) {
            char c = (char)(uint8_t)UART4_DR;   /* 读 DR 会自动清 RXNE */
            if (c == '\r' || c == '\n') {
                if (idx > 0u) { line[idx] = 0; handle_line(line); idx = 0; }
            } else if (idx < sizeof(line) - 1u) {
                line[idx++] = c;                /* 存进行缓冲 */
            }
        }

        /* 2) 非阻塞闪灯：到点才翻一下，不等 */
        if (flash_left > 0u && millis() >= next_toggle) {
            led_set(led_on ^ 1u);            /* 翻转亮灭 */
            flash_left--;
            next_toggle = millis() + 200u;
        }
    }
}
