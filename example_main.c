/**
 * @file  example_main.c
 * @brief 使用示例。编译时只链接【一个】port 文件即可换平台。
 */
#include "ad9851.h"

/* 串行 port(标准库/HAL/GD32) 导出的 IO 表 */
extern const ad9851_io_t ad9851_io;
/* 并行 port 导出的 IO 表（如果用并行） */
extern const ad9851_io_t ad9851_io_parallel;

/* 标准库 / GD32 的 port 提供；HAL(CubeMX) 工程用 MX_GPIO_Init 代替 */
void ad9851_gpio_init(void);

static ad9851_t       g_dds;
static ad9851_sweep_t g_sweep;

/* ---------- 基本用法（串行） ---------- */
void demo_basic(void)
{
    ad9851_gpio_init();   /* HAL 工程改成 MX_GPIO_Init() */

    /* 30MHz 晶振 + 片内 6 倍频 = 180MHz */
    ad9851_init(&g_dds, &ad9851_io, AD9851_MODE_SERIAL, 180000000u, 1u);

    ad9851_set_frequency(&g_dds, 10000000.0);  /* 10 MHz */

    /* 频率/相位互不覆盖：先设 90°(≈8)，再改频率，相位仍保持 */
    ad9851_set_phase(&g_dds, 8);
    ad9851_set_frequency(&g_dds, 1000000.0);   /* 1 MHz, 相位还是 90° */
}

/* ---------- 并行模式 ---------- */
void demo_parallel(void)
{
    /* GPIO(含 D0..D7) 初始化由你的工程完成 */
    ad9851_init(&g_dds, &ad9851_io_parallel, AD9851_MODE_PARALLEL,
                180000000u, 1u);
    ad9851_set_frequency(&g_dds, 5000000.0);
}

/* ---------- 阻塞式扫频 ---------- */
void demo_sweep_blocking(void)
{
    ad9851_init(&g_dds, &ad9851_io, AD9851_MODE_SERIAL, 180000000u, 1u);
    /* 1MHz -> 30MHz, 每步 10kHz, 每点驻留 500us。需 port 提供 delay_us */
    ad9851_sweep_linear(&g_dds, 1000000.0, 30000000.0, 10000.0, 500);
}

/* ---------- 非阻塞扫频：在定时器中断里走步 ---------- */
void demo_sweep_nonblocking_setup(void)
{
    ad9851_init(&g_dds, &ad9851_io, AD9851_MODE_SERIAL, 180000000u, 1u);
    /* 1~30MHz, 步进 10kHz, 往复(三角扫) */
    ad9851_sweep_init(&g_sweep, &g_dds, 1000000.0, 30000000.0, 10000.0, 1u);
}

/* 把它放进你的定时器中断回调，定时器周期 = 每个频点的驻留时间 */
void on_sweep_timer_irq(void)
{
    ad9851_sweep_tick(&g_sweep);
}
