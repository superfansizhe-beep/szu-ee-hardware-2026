/* ============================================================================
 * q03_oled/main.c —— 第三题「觉醒吧，OLED！」全仿真版
 * ----------------------------------------------------------------------------
 * 任务：在 OLED 上显示  "<你的名字> 好想加入深大电协硬件部啊"。
 *
 * 真板做法（SSD1306 128x64，I2C）：
 *   - 上电发一串"初始化命令"（对比度、显示方向、分页方式…）；
 *   - 显存 = 8页 × 128列 = 1024 字节，每位=一个像素；
 *   - 显示字符 = 查"字库"把字符变成点阵字节写进显存（汉字16x16，用取模软件
 *     只取你用到的那几个字，每个32字节）；
 *   - 最后经 I2C 把显存刷到屏幕。
 *
 * 仿真做法（Renode 没有 SSD1306 屏幕模型）：
 *   把"要显示的4行文字"放进 SRAM 固定行缓冲，再置一个 ready 标志；
 *   Python 读到后用电脑字体把这几行渲染成一张 OLED 样子的图片。
 *   ——应用逻辑（显示什么字、第几行）是真的，"字库点阵+I2C刷屏"被打桩。
 *
 * 【你唯一要改的】把 MY_NAME 改成你的名字或昵称。
 * ==========================================================================*/

#include <stdint.h>
#define MMIO8(addr) (*(volatile uint8_t  *)(addr))
#define MMIO(addr)  (*(volatile uint32_t *)(addr))
/* ---- 显示行缓冲（SRAM 固定地址，4行 × 每行24字节）---- */
#define OLED_BASE   0x2001FE00u
#define LINE_BYTES  40u
#define OLED_READY  MMIO(0x2001FF00u)        /* 一帧准备好后置 'OLED' */
#define OLED_READY_VALUE 0x4F4C4544u

/* 改成你的名字（英文/中文都行，Python 渲染） */
#define MY_NAME "鹅耶"

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

/* 把 C 字符串拷进固定地址的行缓冲（超出23字节截断），并清掉剩余格子 */
static void oled_line(uint32_t row, const char *s)
{
    uint8_t *p = (uint8_t *)(OLED_BASE + row * LINE_BYTES);
    uint32_t i = 0;
    while (*s && i < LINE_BYTES - 1u) { p[i++] = (uint8_t)*s++; }
    while (i < LINE_BYTES) { p[i++] = 0u; }
}

/* 真板：经 I2C 发初始化命令序列；仿真：清 ready */
static void oled_init(void) { OLED_READY = 0u; }

/* 真板：把 1024 字节显存经 I2C 刷到屏幕；仿真：置 ready 让 Python 渲染 */
static void oled_refresh(void) { OLED_READY = OLED_READY_VALUE; }

int main(void)
{
    oled_init();

    /* 排版：第0行放名字，第2行放那句话（上下留点空更像屏幕居中）*/
    oled_line(0, "");
    oled_line(1, MY_NAME);
    oled_line(2, "好想加入深大电协硬件部啊");
    oled_line(3, "");

    oled_refresh();

    for (;;) ;   /* 静态显示一屏，之后不用做事 */
}
