/* ============================================================================
 * lesson7_q10_radar/main.c —— 第10题「盲人摸象」雷达扫描固件（全仿真）
 * ----------------------------------------------------------------------------
 * 【整体任务】
 *   舵机 0°→180°→0° 往复扫描（每步 5°，每步停 200ms）；
 *   每个角度测一次距离，经 UART4 发一行 "角度:距离"，例如 "45:67.0"；
 *   在 0°/90°/180° 三个关键点闪 PD12 灯，并额外发一行 "[LED] angle=90"。
 *
 * 【三块共享白板 mailbox】（RAM 顶端，链接脚本把栈挡在 0x2003FFF0 以下）
 *   地址          名字             谁写→谁读      含义
 *   0x2003FFF0  distance_mm     Python写→固件读  超声波该角度测到的距离(mm)，0=无效
 *   0x2003FFF4  servo_pulse_us  固件写→Python读  舵机当前脉宽(500..2500us)
 *   0x2003FFF8  magic 'TWIN'    固件写→Python读  开机暗号，证明固件真的跑起来了
 *
 * 【真板只改两个函数】servo_set_angle（改写成定时器 CCR）、hcsr04_read_mm
 *   （改写为发 TRIG、量 ECHO 高电平时间/58）；其余扫描/串口逻辑一行不动。
 * ==========================================================================*/

#include <stdint.h>

/* MMIO(addr)：把一个地址强转成"指向32位寄存器的指针"再解引用，
 * 于是读它=读该地址的4字节，写它=写该地址。volatile 告诉编译器别优化、每次真访问。*/
#define MMIO(addr) (*(volatile uint32_t *)(addr))

/* ---- 三块共享白板 ---- */
#define TWIN_BASE        0x2003FFF0u
#define TWIN_DISTANCE_MM MMIO(TWIN_BASE + 0u)   /* 传入：距离 */
#define TWIN_SERVO_PULSE MMIO(TWIN_BASE + 4u)   /* 传出：舵机脉宽 */
#define TWIN_MAGIC       MMIO(TWIN_BASE + 8u)   /* 暗号 */
#define TWIN_MAGIC_VALUE 0x5457494Eu            /* 4个ASCII字符拼成 'T''W''I''N' */

/* ---- RCC：各外设的"电闸"，用前必须先合闸 ---- */
#define RCC_AHB1ENR       MMIO(0x40023830u)          /* AHB1 总线使能（管GPIO）*/
#define RCC_APB1ENR       MMIO(0x40023840u)          /* APB1 总线使能（管UART4）*/
#define RCC_AHB1ENR_GPIOD (1u << 3)                 /* GPIOD 在第3位 */
#define RCC_APB1ENR_UART4 (1u << 19)                /* UART4 在第19位 */

/* ---- GPIOD：PD12 当指示灯 ---- */
#define GPIOD_MODER  MMIO(0x40020C00u + 0x00u)       /* 模式寄存器：每2位管一个脚 */
#define GPIOD_BSRR   MMIO(0x40020C00u + 0x18u)       /* 置位/复位扳机寄存器（只写）*/
/* BSRR 低16位：往第n位写1 -> 引脚n拉高；高16位：往第n+16位写1 -> 引脚n拉低。
 * 写0的位什么都不做，所以不会误伤别的脚，且一次写就生效。*/
#define LED_SET      (1u << 12)                      /* PD12 拉高=亮 */
#define LED_RESET    (1u << (12u + 16u))             /* PD12 拉低=灭（跳到高16位）*/

/* ---- UART4（雷达只发不收，所以只开 TE）---- */
#define UART4_SR    MMIO(0x40004C00u + 0x00u)        /* 状态寄存器 */
#define UART4_DR    MMIO(0x40004C00u + 0x04u)        /* 数据寄存器：写它=发1字节 */
#define UART4_BRR   MMIO(0x40004C00u + 0x08u)        /* 波特率分频 */
#define UART4_CR1   MMIO(0x40004C00u + 0x0Cu)        /* 控制寄存器1：总开关 */
#define UART_SR_TXE (1u << 7)                        /* 第7位 TXE=发送寄存器空了吗 */

/* ---- SysTick：内核里的倒计数定时器，这里用"轮询"方式做 delay ----
 * 用法：RVR 设"数多少下算1ms"，启动后它从RVR倒数到0，数满时硬件把 CSR 的
 *       第16位 COUNTFLAG 置1。我们死等这一位变1，就知道1ms到了。
 * 注意：这和 Q9 的"中断方式"不同——Q9 是数满自动跳进中断函数 g_ms++，能边等边干
 *       别的活；这里扫描时本来就该原地等舵机到位，所以用更简单的轮询即可。*/
#define SYST_CSR        MMIO(0xE000E010u)
#define SYST_RVR        MMIO(0xE000E014u)
#define SYST_CVR        MMIO(0xE000E018u)
#define SYST_CSR_ENABLE (1u << 0)    /* 第0位：启动定时器 */
#define SYST_CSR_CLKSRC (1u << 2)    /* 第2位：用内核时钟当计数源 */
#define SYST_CSR_COUNT  (1u << 16)   /* 第16位：数满标志（硬件置1，读CSR会清）*/
#define SYST_TICKS_PER_MS 72000u     /* Renode 里 SysTick 按72MHz虚拟时钟数，1ms=72000下 */

#define STEP_DEG      5u             /* 每步转 5° */
#define STEP_DELAY_MS 200u           /* 每步停 200ms（等舵机到位 + 扫描节奏）*/

/* ================= 启动代码（所有 lesson 都一样，可略读）================= */
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
typedef void (*isr_t)(void);        /* "中断处理函数指针"类型 */
void Reset_Handler(void);
void Default_Handler(void);
int  main(void);

/* 向量表：第0项=初始栈顶，第1项=复位入口；其余先指向 Default_Handler。
 * 本题没用任何中断（SysTick走轮询），所以表保持最简16项。*/
__attribute__((section(".isr_vector"), used))
isr_t vector_table[16] = {
    (isr_t)&_estack, Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler,
    0, Default_Handler, Default_Handler,
};

/* 上电后CPU从这里开始：设栈指针→搬.data→清.bss→写暗号→进main */
__attribute__((naked))
void Reset_Handler(void)
{
    __asm volatile ("ldr r0, =_estack\n\t" "mov sp, r0\n\t" : : : "r0");
    for (uint32_t *p = &_sdata, *s = &_sidata; p < &_edata;) *p++ = *s++;
    for (uint32_t *p = &_sbss; p < &_ebss;) *p++ = 0;
    TWIN_MAGIC = TWIN_MAGIC_VALUE;   /* 开机第一时间写暗号，Python 据此判断"活了" */
    (void)main();
    for (;;) ;                       /* main 不该返回，万一返回就停在这 */
}
void Default_Handler(void) { for (;;) ; }   /* 意外中断/错误会落到这，卡死便于发现 */

/* ================= 基础驱动 ================= */

/* 阻塞延时 ms 毫秒 */
static void delay_ms(uint32_t ms)
{
    SYST_RVR = SYST_TICKS_PER_MS - 1u;  /* 数 72000 下（从71999数到0正好72000下）*/
    SYST_CVR = 0u;                      /* 清当前值，强制重新开始数 */
    SYST_CSR = SYST_CSR_ENABLE | SYST_CSR_CLKSRC;  /* 启动 */
    while (ms--) {
        while ((SYST_CSR & SYST_CSR_COUNT) == 0u) ;  /* 死等 COUNTFLAG 变1 = 1ms到 */
    }
    SYST_CSR = 0u;                      /* 用完关掉定时器 */
}

static void led_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOD;   /* 合 GPIOD 电闸（|= 只置这一位，不动别的）*/
    /* PD12 对应 MODER 的第24、25位：清成00再写01=通用输出 */
    GPIOD_MODER = (GPIOD_MODER & ~(3u << 24)) | (1u << 24);
}

static void uart_init(void)
{
    RCC_APB1ENR |= RCC_APB1ENR_UART4;   /* 合 UART4 电闸 */
    UART4_BRR  = 0x682u;                /* 波特率（仿真文件后端不校验，照范本写）*/
    UART4_CR1  = 0x2008u;   /* bit13 UE=开UART，bit3 TE=允许发送；没开RE因为不收 */
}

/* 发一个字符：先等发送寄存器空(TXE=1)，再把字节写进 DR，硬件自动移位发出 */
static void uart_putc(char c)
{
    while ((UART4_SR & UART_SR_TXE) == 0u) ;   /* 没空就等，否则会冲掉没发完的 */
    UART4_DR = (uint32_t)(uint8_t)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

/* 把一个非负整数按十进制发出去。不能直接发数字（UART只认字符），
 * 技巧：先反复 %10 取个位放进 buf（得到的是倒序），再倒着发出来。
 * 例：670 -> 依次取出 0,7,6 存入 buf，再倒序发 '6''7''0'。用 do-while 保证 0 也发。*/
static void uart_putdec(uint32_t v)
{
    char buf[10];
    int i = 0;
    do {
        buf[i++] = (char)('0' + v % 10u);  /* 个位数字 + '0' = 该数字的ASCII字符 */
        v /= 10u;                           /* 去掉个位 */
    } while (v);
    while (i--) uart_putc(buf[i]);          /* i 先自减再当下标，实现倒序输出 */
}

/* ================= 两个"外设"（真板只换这两个函数内部）================= */

/* 舵机转到 deg：SG90 用脉宽表示角度，500us@0° … 2500us@180°，线性插值。
 * 仿真：把脉宽写进 mailbox 给 Python 看；真板：这里改成 TIM4->CCR1 = 脉宽对应值。*/
static void servo_set_angle(uint32_t deg)
{
    TWIN_SERVO_PULSE = 500u + deg * 2000u / 180u;
}

/* 读超声波距离(mm)。仿真：直接读 Python 写好的 mailbox；
 * 真板：给 TRIG 一个10us高脉冲，再量 ECHO 高电平持续的微秒数 / 58 得厘米。*/
static uint32_t hcsr04_read_mm(void)
{
    return TWIN_DISTANCE_MM;
}

/* LED 亮 ms 毫秒再灭（关键点提示用）*/
static void led_pulse(uint32_t ms)
{
    GPIOD_BSRR = LED_SET;
    delay_ms(ms);
    GPIOD_BSRR = LED_RESET;
}

/* ================= 扫描一步：转到 angle → 测距 → 串口上报 ================= */
static void scan_step(uint32_t angle)
{
    uint32_t mm;

    servo_set_angle(angle);    /* 1) 舵机转到这一步的角度 */
    delay_ms(STEP_DELAY_MS);   /* 2) 等它转到位（也固定了扫描节奏）*/
    mm = hcsr04_read_mm();     /* 3) 读这一方向的距离(mm) */

    /* 4) 发 "角度:厘米.毫米位"。距离存的是整数mm，670mm 要显示成 67.0：
     *    mm/10=67（整厘米），mm%10=0（小数点后那一位）。*/
    uart_putdec(angle);
    uart_putc(':');
    uart_putdec(mm / 10u);
    uart_putc('.');
    uart_putdec(mm % 10u);
    uart_puts("\r\n");         /* \r\n = 回车换行，一行结束 */

    /* 5) 角度是 0/90/180 时额外打点：发标记 + 闪灯50ms */
    if (angle % 90u == 0u) {
        uart_puts("[LED] angle=");
        uart_putdec(angle);
        uart_puts("\r\n");
        led_pulse(50u);
    }
}

int main(void)
{
    led_init();
    uart_init();

    /* 永远往复扫描。外层 dir 选方向：
     *   dir=0：从 0° 开始，每步 +5，走到 180° 停；
     *   dir=1：从180°开始，每步 -5，走到 0° 停。
     * step 用有符号 int32_t 才能为负（往回扫）。*/
    for (;;) {
        for (uint32_t dir = 0u; dir < 2u; dir++) {
            uint32_t a = dir ? 180u : 0u;                 /* 这一趟的起点 */
            int32_t step = dir ? -(int32_t)STEP_DEG      /* 回程：-5 */
                               :  (int32_t)STEP_DEG;      /* 去程：+5 */
            for (;;) {
                scan_step(a);
                /* 走到这一趟的端点就跳出，否则继续加减 */
                if (a == (dir ? 0u : 180u)) break;
                a = (uint32_t)((int32_t)a + step);
            }
        }
    }
}
