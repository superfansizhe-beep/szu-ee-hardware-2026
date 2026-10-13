/* ============================================================================
 * q03_oled/main_board.c —— 笔试第 3 题「觉醒吧，OLED！」真板版
 * ----------------------------------------------------------------------------
 * 任务：在 SSD1306 OLED（I2C，128x64）上显示「鹅耶」和「好想加入深大电协硬件部啊」。
 *
 * 【和仿真版 main.c 的关系】
 *   应用层——显示什么、第几行、一帧显示完就不再动——完全一致，
 *   仿真版的排版也是「第 1 行名字、第 2 行那句话」，这里照搬：
 *       仿真：oled_line(row, s) 把字符串写进 SRAM 行缓冲，Python 渲染成图
 *       真板：cn_draw/ascii_draw 把字库字节写进 1024 字节显存，再经 I2C 刷屏
 *   换掉的只有 IO 层：SRAM mailbox → SSD1306 + I2C1。
 *
 * ------------------------------------------------------------------
 * 一、接线（换板子只改下面那张引脚表）
 * ------------------------------------------------------------------
 *   I2C1（AF4）：
 *     PB6 ── SCL ──┐                  OLED 模块（0.96" 128x64 SSD1306）
 *     PB7 ── SDA ──┼── 模块的 SCL/SDA    7 位地址 0x3C（写时左移 1 位送主机）
 *          GND ────┘── 模块的 GND        VCC 接 3.3V
 *
 *   调试串口：
 *     PA9  (USART1_TX, AF7) ── USB-TTL 的 RXD
 *     PA10 (USART1_RX, AF7) ── USB-TTL 的 TXD
 *
 *   ⚠ **I2C 的两根线必须有外部上拉电阻**（典型 4.7kΩ 到 VCC）。
 *     I2C 是开漏输出：器件只能把线拉低、拉高必须靠上拉电阻。
 *     很多模块板上已经焊了上拉，先用万用表量一下 SCL-SCL 之间
 *     通断情况和阻值；没有上拉时总线永远读到低电平，表现为"初始化失败"。
 *
 *   ⚠ **接 5V 会烧 MCU**：多数 0.96" 模块标称 1~5V，但它的 SCL/SDA 上拉
 *     接到的是模块自己的 VCC。用 5V 供电时这两根线会被拉到 5V，
 *     而 F407 的 PB6/PB7 是 3.3V 域，超压会打穿 IO。
 *     稳妥做法：**模块也用 3.3V 供电**，或者给两根数据线各串 1kΩ + 
 *     加 3.3V 钳位。这一条是老师最爱追问的电气细节。
 *
 *   ⚠ 有些模块是 0x3D 而不是 0x3C（背面焊盘决定）。0x3C 不应答就试 0x3D。
 *
 * ------------------------------------------------------------------
 * 二、这题真正要讲的东西
 * ------------------------------------------------------------------
 *   ① **SSD1306 的显存结构和"页"**：128x64 但显存不是 1024 个点，
 *      而是 8 页 × 128 列 = 1024 **字节**，每字节 = 一列上竖着叠的 8 个像素，
 *      bit0 在最上面。地址 = 页号 × 128 + 列号。
 *      所以"画一个点"要先算出它在第几页第几列——
 *      页 = y/8、列 = x、字节内位 = y%8。这三个数算错，画面就是斜的。
 *
 *   ② **字库的字节顺序必须和显存结构一致**：
 *      oled_font.h 里 16x16 汉字 = 32 字节 = 2 页 × 16 列，页优先、页内按列。
 *      拷贝时是 `for page in 0..1: for col in 0..15: fb[page*128+x+col] = glyph[page*16+col]`。
 *      这里最容易出的错是"按行拷"——那样字会横过来或者变成一堆横线。
 *
 *   ③ **I2C 的控制字节**：SSD1306 上电后每个字节的含义由它前面一个
 *      控制字节决定：Co=0,D/C=1 → 后面是**命令**；Co=0,D/C=0 → 后面是**数据**。
 *      所以刷新一帧要先发一个 0x40（后面跟数据），再连续发 1024 个字节。
 *      初始化那一串命令则要先发 0x00。
 *
 *   ④ **开漏 + 上拉 = 线与**：I2C 规定"所有器件都拉低才是低"，
 *      所以器件只能拉低不能拉高。这也是为什么必须等 BTF 而不是只等 TXE——
 *      不等 BTF 就塞下一个字节，数据会错位（送进了还在移位的那个寄存器）。
 * ==========================================================================*/

#include "stm32f4_common.h"

/* oled_font.h 是脚本生成的表初始化器，写法是 `uint8_t t[N][M] = { v, v, ... }`。
 * C 标准允许省略内层花括号，但 gcc 的 -Wmissing-braces 会为这种写法报警告，
 * 而验收要求零警告。这个文件是**生成物、不手改**，所以在 include 前后
 * 局部关掉这一条检查，而不是去动生成脚本或表本身。 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
#include "oled_font.h"     /* 字库：ascii_8x16[95][16]、cn_16x16[14][32] */
#pragma GCC diagnostic pop

/* ============================ 引脚表 ============================
 * 换板子只改这一段。I2C1 的 AF4 固定在 PB6/PB7。 */
#define PIN_SCL      PB6    /* I2C1_SCL → AF4 */
#define PIN_SDA      PB7    /* I2C1_SDA → AF4 */

#define UART_TX      PA9    /* USART1_TX → AF7 */
#define UART_RX      PA10   /* USART1_RX → AF7 */

/* 7 位地址。写成 0x3C 而不是 0x78：0x78 是"已经左移一位"的写法，
 * 两种都对，但 7 位地址是 SSD1306 手册上的原话，讲解时不容易被问住。
 * 公共层的 i2c1_write() 内部会做 dev7<<1。 */
#define OLED_ADDR7   0x3Cu

/* ============================ 显存 ============================
 * 8 页 × 128 列 = 1024 字节 = 恰好 8KB 的 1/8。
 * 放 .bss（初值为 0 = 全黑），不占 Flash。
 * 声明成 static 是为了只在本文件可见、不进符号表；
 * 用 const 就不行——显存要写。 */
#define FB_PAGES  8u
#define FB_WIDTH 128u
static uint8_t g_fb[FB_PAGES * FB_WIDTH];   /* 页优先：fb[page*128 + col] */

/* 控制字节：Co=0（一串里只有一个控制字节，不是每字节一个），
 * D/C=1 → 命令；D/C=0 → 显示数据。位序是 Co(bit7) D/C(bit6) 0(bit5)… */
#define CTRL_CMD    0x00u
#define CTRL_DATA   0x40u

/* ============================================================================
 * oled_cmd_list：按 Co=0,D/C=1 发一串初始化命令
 * ----------------------------------------------------------------------------
 * 一次 I2C 事务里"控制字节 + N 条命令"，比一条命令发一个事务快得多，
 * 而且命令之间的时序间隔由 I2C 时钟自然保证，比插 delay 稳。
 * ==========================================================================*/
static uint8_t oled_cmd_list(const uint8_t *cmds, uint16_t n)
{
    uint8_t buf[32];   /* 1 个控制字节 + 最多 31 条命令；下面 init_seq 是 25 条，一批就够 */
    uint16_t sent = 0u;
    uint8_t ok = 1u;

    /* 超过一批就再开一个事务。不这么限制的话，
     * 一旦命令数超上限就静默失败——调用方如果不查返回值，
     * 现象只是"屏一直黑"，看不出是哪一步出的问题。 */
    while (sent < n) {
        uint16_t chunk = n - sent;
        if (chunk > (uint16_t)(sizeof(buf) - 1u)) { chunk = (uint16_t)(sizeof(buf) - 1u); }

        buf[0] = CTRL_CMD;
        for (uint16_t i = 0u; i < chunk; ++i) { buf[i + 1u] = cmds[sent + i]; }

        if (!i2c1_write(OLED_ADDR7, buf, (uint16_t)(chunk + 1u))) { ok = 0u; break; }
        sent = (uint16_t)(sent + chunk);
    }
    return ok;
}

/* ============================================================================
 * oled_init：SSD1306 上电初始化
 * ----------------------------------------------------------------------------
 * 下面这串命令的顺序不是随便排的，一步依赖一步：
 *   1. 关显示 → 先别急着开，省电且避免半初始化时被看到花屏
 *   2. 设时钟分频 / COM 扫描方向 / SEG 重映射 → 决定"左上角"对应哪个物理引脚
 *   3. 设对比度、预充电、VCOM → 决定亮度
 *   4. 设页寻址模式 + 起始页列 → 后面刷屏时列/页指针会自动 +1，不用逐行重设
 *   5. 退出全开 / 反显 → 让 GDDRAM 里的 0=黑、1=亮
 *   6. 开显示
 * 少一条的结果：少对比度→偏灰，少重映射→上下或左右颠倒，
 * 少页寻址→整屏只显示一页。这些都是新手最常遇到的现象。
 * ==========================================================================*/
static void oled_init(void)
{
    static const uint8_t init_seq[] = {
        0xAE,             /* display OFF，先别显示 */
        0xD5, 0x80,       /* 时钟分频 / 振荡频率：0x80 是 SSD1306 手册的典型值 */
        0xA8, 0x3F,       /* 多路复用比 = 64 行（0x3F+1），64 行的屏必须写 0x3F */
        0xD3, 0x00,       /* 显示偏移 0 */
        0x40,             /* 起始行 = 0 */
        0xA1,             /* SEG 重映射：把列 127 映到 SEG0，画面左右不反 */
        0xC8,             /* COM 扫描方向倒置：把行 63 映到 COM0，画面上下不反 */
        0xDA, 0x12,       /* COM 引脚配置：128x64 用 0x12（交替式） */
        0x81, 0xCF,       /* 对比度 0xCF（约 80%），越大越亮但功耗也越大 */
        0xD9, 0xF1,       /* 预充电周期 */
        0xDB, 0x40,       /* VCOMH 电平 */
        0x8D, 0x14,       /* 开启内部电荷泵（模块没有外部升压时必须靠它） */
        0x20, 0x02,       /* **页寻址模式**：写完 128 列自动跳到下一页 */
        0xA4,             /* 跟随 GDDRAM（退出"全开点亮"）*/
        0xA6,             /* 正常显示（不是反白） */
        0xAF              /* display ON */
    };

    /* 面板上电后需要一点时间内部电荷泵才稳，紧接着发命令可能被忽略，
     * 所以这里必须留一段延时。delay_ms 用的是毫秒时基（TIM6 中断），
     * 不是空转，所以在等的时候 CPU 是闲着的、串口时基还在走。 */
    delay_ms(100u);
    (void)oled_cmd_list(init_seq, (uint16_t)sizeof(init_seq));
}

/* ============================================================================
 * fb_clear：清显存（写 0 = 灭像素）
 * ==========================================================================*/
static void fb_clear(void)
{
    for (uint32_t i = 0u; i < sizeof(g_fb); ++i) { g_fb[i] = 0u; }
}

/* ============================================================================
 * 点阵搬运：把字库里的字节搬到显存
 * ----------------------------------------------------------------------------
 * 【行列对应关系，这是全题最容易搞错的地方】
 *   字库的一个字节 = 一列上竖着叠 8 个像素，bit0 在最上面。
 *   显存的一个字节 = 也是一列上竖着叠 8 个像素，bit0 在最上面。
 *   两边定义完全一致，所以**不需要任何位运算转换**，直接整字节拷。
 *   真正要小心的只是"第几个字节放到显存第几列"。
 *
 *   汉字 16x16：字库 32 字节 = 上页 16 列 + 下页 16 列
 *       glyph[page*16 + col] → fb[(y_page+page)*128 + x + col]
 *   ASCII 8x16：字库 16 字节 = 上页 8 列 + 下页 8 列
 *       glyph[page*8 + col] → fb[(y_page+page)*128 + x + col]
 * ==========================================================================*/

/* 画一个 16x16 汉字。y_page 以"页"为单位（0..3，每页 16 像素高）。 */
static void cn_draw(uint32_t x, uint32_t y_page, uint32_t glyph_idx)
{
    if (glyph_idx >= CN_COUNT) { return; }

    const uint8_t *g = &cn_16x16[glyph_idx][0];

    /* 越界保护：x+16 > 128 时如果还照写，fb 下标会跑到下一页的列，
     * 屏幕上就是右边多出一道竖线。返回比画坏要好排查。 */
    if (x + 16u > FB_WIDTH) { return; }
    if (y_page + 2u > FB_PAGES) { return; }

    for (uint32_t page = 0u; page < 2u; ++page) {
        uint8_t *dst = &g_fb[(y_page + page) * FB_WIDTH + x];
        const uint8_t *src = &g[page * 16u];
        for (uint32_t col = 0u; col < 16u; ++col) { dst[col] = src[col]; }
    }
}

/* 画一个 8x16 ASCII 字符。 */
static void ascii_draw(uint32_t x, uint32_t y_page, char c)
{
    uint32_t code = (uint32_t)(uint8_t)c;

    /* 字库只收了 0x20~0x7E。下标越界会读到表外的内存，
     * 在 Flash 里表现为乱码或者干脆跑飞——这里先挡掉。 */
    if (code < ASCII_FIRST || code > ASCII_LAST) { return; }

    const uint8_t *g = &ascii_8x16[code - ASCII_FIRST][0];

    if (x + 8u > FB_WIDTH) { return; }
    if (y_page + 2u > FB_PAGES) { return; }

    for (uint32_t page = 0u; page < 2u; ++page) {
        uint8_t *dst = &g_fb[(y_page + page) * FB_WIDTH + x];
        const uint8_t *src = &g[page * 8u];
        for (uint32_t col = 0u; col < 8u; ++col) { dst[col] = src[col]; }
    }
}

/* ============================================================================
 * oled_flush：把 1024 字节显存刷到屏幕
 * ----------------------------------------------------------------------------
 * 页寻址模式下，列指针写满 128 会自动回到 0 并把页号 +1，
 * 所以 1024 个字节一口气连续发过去就行，不用分段、不用逐页重设地址。
 *
 * 【为什么不能分 8 次发、每次 128 字节】
 *   不是不能，是每段之间都要 STOP 再 START，多 7 个事务，
 *   而 STOP 之后 SSD1306 的列/页指针行为在不同固件版本上不一致，
 *   一次推 1024 字节最稳。
 *
 * 缓冲区放 1 个控制字节 + 1024 个数据 = 1025 字节。
 * 这里用一个局部数组一次性拼好再发，是因为 i2c1_write() 是"给指针+长度"
 * 的接口，它不替你发控制字节。用局部数组而不是 memcpy 是因为工程是
 * -nostdlib 编译的，不能依赖 libc。
 * ==========================================================================*/
static uint8_t oled_flush(void)
{
    static uint8_t tx[1u + FB_PAGES * FB_WIDTH];

    tx[0] = CTRL_DATA;                       /* 后面这些字节全是显示数据 */
    for (uint32_t i = 0u; i < sizeof(g_fb); ++i) { tx[i + 1u] = g_fb[i]; }

    return i2c1_write(OLED_ADDR7, tx, (uint16_t)(sizeof(tx)));
}

/* ============================ 要显示的内容 ============================
 * 用 oled_font.h 里已有的 CN_xxxx 索引表引用字形：
 * 「鹅耶」= CN_9E45、CN_8036
 * 「好想加入深大电协硬件部啊」= CN_597D 起共 12 个字，逐个列出来。
 * 想换内容：改 gen_oled_font.py 里的 NAME/PHRASE 重跑脚本生成字库，
 * 然后把下面两张表同步改掉。
 * ==========================================================================*/
static const uint8_t NAME_CN[] = { CN_9E45, CN_8036 };

static const uint8_t PHRASE_A[] = {           /* 前 8 个字，16*8=128 列刚好一行 */
    CN_597D, CN_60F3, CN_52A0, CN_5165,
    CN_6DF1, CN_5927, CN_7535, CN_534F
};
static const uint8_t PHRASE_B[] = {           /* 后 4 个字 */
    CN_786C, CN_4EF6, CN_90E8, CN_554A
};

int main(void)
{
    clock_init();
    dwt_init();
    timer_ms_init();

    /* ---- 调试串口：先起来，I2C 出问题时还能说话 ---- */
    gpio_af(UART_TX, 7u);  gpio_mode(UART_TX, MODE_AF, PULL_UP, SPEED_HIGH);
    gpio_af(UART_RX, 7u);  gpio_mode(UART_RX, MODE_AF, PULL_UP, SPEED_HIGH);
    uart_init(USART1_BASE, g_pclk2_hz, 115200u);

    uart_puts(USART1_BASE, "\r\nQ3 oled (board) start\r\n");
    uart_puts(USART1_BASE, "SSD1306 128x64, I2C1 PB6/PB7, addr=0x3C\r\n");

    /* ---- I2C1 ----
     * i2c1_init() 里已经把 PB6/PB7 配成 AF4 开漏，并设了 CCR/TRISE。
     * 400kHz 对杜邦线接的面包板可能偏快，接线长的话把 CCR 改成 40（100kHz）
     * 更稳——I2C 的上升沿由 R×C 决定，线越长电容越大，能跑的最高频率越低。 */
    i2c1_init();

    oled_init();

    /* ---- 画一帧 ----
     * 排版和仿真版一致：第 0 行留空、第 1 行名字、第 2 行那句话。
     * 128 列一行放 8 个 16x16 汉字，所以那句话 12 个字必须拆成两行，
     * 这不是设计选择，是屏宽的物理限制——讲的时候可以直接说清楚。 */
    fb_clear();

    /* 第 0 行：一行 ASCII，顺带把 8x16 字库这条路径也走通。
     * 题目只要求两行中文，这行是为了"汉字 / ASCII 两种字模都验证过"，
     * 现场手指点一下就知道屏幕和 I2C 是好的。不想要就删掉这一段。 */
    {
        static const char CAPTION[] = "SSD1306 128x64";
        uint32_t cap_x = (FB_WIDTH - 8u * (uint32_t)(sizeof(CAPTION) - 1u)) / 2u;
        for (uint32_t i = 0u; i < (uint32_t)(sizeof(CAPTION) - 1u); ++i) {
            ascii_draw(cap_x + i * 8u, 0u, CAPTION[i]);
        }
    }

    /* 名字「鹅耶」：2 个字 = 32 列，居中放（(128-32)/2 = 48 列起步） */
    uint32_t name_x = (FB_WIDTH - 16u * (uint32_t)(sizeof(NAME_CN))) / 2u;
    for (uint32_t i = 0u; i < (uint32_t)(sizeof(NAME_CN)); ++i) {
        cn_draw(name_x + i * 16u, 1u, NAME_CN[i]);
    }

    /* 「好想加入深大电协」8 个字占满 128 列，从第 0 列开始 */
    for (uint32_t i = 0u; i < (uint32_t)(sizeof(PHRASE_A)); ++i) {
        cn_draw(i * 16u, 2u, PHRASE_A[i]);
    }

    /* 「硬件部啊」4 个字 = 64 列，同样居中 */
    uint32_t tail_x = (FB_WIDTH - 16u * (uint32_t)(sizeof(PHRASE_B))) / 2u;
    for (uint32_t i = 0u; i < (uint32_t)(sizeof(PHRASE_B)); ++i) {
        cn_draw(tail_x + i * 16u, 3u, PHRASE_B[i]);
    }

    /* ---- 刷屏 ----
     * 返回值必须查：总线出错（没接、地址错、上拉缺失）时 i2c1_write() 返回 0，
     * 而不查的话屏幕就是黑的，现场根本看不出是固件错了还是屏坏了。
     * 这一点也是"总线型 IO 必须有返回值检查"的典型理由。 */
    uint8_t ok = oled_flush();
    uart_puts(USART1_BASE, ok ? "flush OK\r\n"
                              : "flush FAILED (check 3.3V / pull-up / 0x3C vs 0x3D)\r\n");

    for (;;) {
        /* 静态显示一屏，之后不用做事。
         * 这里特意空转而不是补别的功能：一屏文字 1024 字节 = 8KB Flash 常量 +
         * 1KB 显存，正好留给后面想加的功能。空转时 CPU 白跑不影响任何时基，
         * 因为 millis() 来自 TIM6 中断而不是轮询。 */
    }
}