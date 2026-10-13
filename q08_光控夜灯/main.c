/* ============================================================================
 * q08_光控夜灯/main.c —— 第八题「懒人救星！」光控小夜灯（基础+附加防抖/平滑）
 * ----------------------------------------------------------------------------
 * 功能：环境变暗 LED 自动变亮；变亮 LED 自动变暗。
 * 附加：
 *   1) 亮/暗环境 LED 亮度有明显差别；
 *   2) 防抖：短暂的亮度变化不响应，只有亮度"稳定"一段时间才改 LED；
 *   3) 平滑：LED 亮度是慢慢渐变到目标，不是一下子跳。
 *
 * 思路（三段）：
 *   光敏ADC值 light(0黑..4095亮) -> 想要的目标占空比 want（反向：越暗占空比越大）；
 *   【防抖】want 必须连续 STABLE_MS 保持在一个小范围(BAND)内，才采纳为稳定目标；
 *          一闪而过的变化会不断重置计时器，永远不被采纳。
 *   【平滑】当前占空比 cur 每 RAMP_MS 朝稳定目标挪一小步，于是出现渐变。
 *
 * 仿真：light 由 Python 写 mailbox（假ADC）；LED 占空比 cur 写 mailbox（真板写定时器CCR）。
 * ==========================================================================*/

#include <stdint.h>
#define MMIO(addr) (*(volatile uint32_t *)(addr))

/* ---- mailbox：环境亮度(传入0..4095) 与 LED占空比(传出0..1000) ---- */
#define LIGHT_IN MMIO(0x2001FF00u)
#define LED_DUTY MMIO(0x2001FF04u)

/* 防抖/平滑参数 */
#define STABLE_MS 400u    /* 亮度需稳定多久才采纳 */
#define BAND      60u     /* 目标值落在 ±BAND 内算"没变" */
#define RAMP_MS   10u     /* 渐变的节拍 */
#define RAMP_STEP 25u     /* 每拍占空比挪多少（满量程1000，约0.4s走完）*/

/* ---- SysTick 毫秒计时（中断，Renode 72MHz）---- */
#define SYST_CSR MMIO(0xE000E010u)
#define SYST_RVR MMIO(0xE000E014u)
#define SYST_CVR MMIO(0xE000E018u)
static volatile uint32_t g_ms = 0u;
static uint32_t millis(void) { return g_ms; }

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
typedef void (*isr_t)(void);
void Reset_Handler(void);
void Default_Handler(void);
void SysTick_Handler(void);
int  main(void);
__attribute__((section(".isr_vector"), used))
isr_t vector_table[16] = {
    (isr_t)&_estack, Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, 0, 0, 0, 0, Default_Handler, Default_Handler,
    0, Default_Handler, SysTick_Handler,
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
void SysTick_Handler(void) { g_ms++; }

/* 亮度 -> 目标占空比（反向）：全黑->1000，全亮->0 */
static uint32_t light_to_duty(uint32_t light)
{
    if (light > 4095u) light = 4095u;
    return (4095u - light) * 1000u / 4095u;
}

int main(void)
{
    /* SysTick 1ms 中断 */
    SYST_RVR = 72000u - 1u;
    SYST_CVR = 0u;
    SYST_CSR = (1u << 0) | (1u << 1) | (1u << 2);

    uint32_t stable = 0u;    /* 已采纳的稳定目标 */
    uint32_t cur = 0u;       /* 当前实际输出 */
    uint32_t pending = 0u;   /* 待确认目标 */
    uint32_t since = 0u;
    uint32_t last_ramp = 0u;

    LED_DUTY = 0u;

    for (;;) {
        uint32_t want = light_to_duty(LIGHT_IN);

        /* 防抖：和待确认目标差太多 -> 重新开始计时；接近且持续够久 -> 采纳 */
        uint32_t diff = (want > pending) ? (want - pending) : (pending - want);
        if (diff > BAND) {
            pending = want;
            since = millis();
        } else if (millis() - since >= STABLE_MS) {
            stable = pending;
        }

        /* 平滑：每拍朝稳定目标挪一小步 */
        if (millis() - last_ramp >= RAMP_MS) {
            last_ramp = millis();
            if (cur < stable) {
                cur += ((stable - cur) > RAMP_STEP) ? RAMP_STEP : (stable - cur);
            } else if (cur > stable) {
                cur -= ((cur - stable) > RAMP_STEP) ? RAMP_STEP : (cur - stable);
            }
            LED_DUTY = cur;
        }
    }
}
