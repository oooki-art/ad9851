/**
 * @file  ad9851_port_stm32_hal.c
 * @brief 平台移植层：STM32 HAL（G4/F1/F4，串行 + 并行合一）
 *
 * 同时导出两张 IO 表：
 *   ad9851_io           串行（数据线 = 芯片 D7）
 *   ad9851_io_parallel  并行（D0..D7 八位总线）
 *
 * 控制脚建议在 CubeMX 命名为 AD9851_W_CLK / AD9851_FQ_UD / AD9851_RESET /
 * AD9851_DATA，CubeMX 会生成 *_Pin 与 *_GPIO_Port 宏。GPIO 时钟/方向由
 * CubeMX 的 MX_GPIO_Init() 配置，本文件不再单独初始化。
 *
 * 并行总线假设 D0..D7 连续在一个端口（下例 PB0..PB7），用 BSRR 一次写。
 */
#include "main.h"      /* CubeMX 生成的引脚宏 + HAL 头 */
#include "ad9851.h"

/* 并行总线所在端口与 D0 对应的引脚号 */
#define AD9851_BUS_PORT   GPIOB
#define AD9851_BUS_SHIFT  0       /* D0 = PB0 -> 起始位 0 */

static inline void wr(GPIO_TypeDef *port, uint16_t pin, uint8_t level)
{
    HAL_GPIO_WritePin(port, pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void w_clk(uint8_t lv) { wr(AD9851_W_CLK_GPIO_Port, AD9851_W_CLK_Pin, lv); }
static void fq_ud(uint8_t lv) { wr(AD9851_FQ_UD_GPIO_Port, AD9851_FQ_UD_Pin, lv); }
static void reset(uint8_t lv) { wr(AD9851_RESET_GPIO_Port, AD9851_RESET_Pin, lv); }
static void data (uint8_t lv) { wr(AD9851_DATA_GPIO_Port,  AD9851_DATA_Pin,  lv); }

/* 并行：一次写 D0..D7，BSRR 低 16 位置位/高 16 位复位，原子操作 */
static void bus(uint8_t b)
{
    uint32_t set = ((uint32_t)b & 0xFFu) << AD9851_BUS_SHIFT;
    uint32_t rst = ((uint32_t)(~b) & 0xFFu) << AD9851_BUS_SHIFT;
    AD9851_BUS_PORT->BSRR = set | (rst << 16);
}

const ad9851_io_t ad9851_io = {            /* 串行 */
    .write_w_clk = w_clk,
    .write_fq_ud = fq_ud,
    .write_data  = data,
    .write_bus   = 0,
    .write_reset = reset,
    .delay_us    = 0,
};

const ad9851_io_t ad9851_io_parallel = {   /* 并行 */
    .write_w_clk = w_clk,
    .write_fq_ud = fq_ud,
    .write_data  = 0,
    .write_bus   = bus,
    .write_reset = reset,
    .delay_us    = 0,
};
