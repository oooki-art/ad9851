/**
 * @file  ad9851_port_gd32.c
 * @brief 平台移植层：GD32 固件库(以 GD32F30x 为例，串行 + 并行合一)
 *
 * 同时导出两张 IO 表：
 *   ad9851_io           串行（数据线 = 芯片 D7）
 *   ad9851_io_parallel  并行（D0..D7 八位总线）
 *
 * 本例接线（按需改顶部宏）：
 *   D0..D7 = PB0..PB7（连续 8 脚），串行数据复用 D7=PB7
 *   W_CLK=PB8  FQ_UD=PB9  RESET=PB10
 * GD32 位操作寄存器是 GPIO_BOP：低 16 位置位/高 16 位复位（同 STM32 BSRR）。
 */
#include "gd32f30x.h"
#include "ad9851.h"

#define AD9851_RCU_GPIO   RCU_GPIOB
#define AD9851_PORT       GPIOB
#define AD9851_BUS_PINS   (GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3| \
                           GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7)
#define PIN_DATA          GPIO_PIN_7   /* 串行数据 = D7 */
#define PIN_W_CLK         GPIO_PIN_8
#define PIN_FQ_UD         GPIO_PIN_9
#define PIN_RESET         GPIO_PIN_10

static inline void wr(uint32_t pin, uint8_t level)
{
    if (level) {
        gpio_bit_set(AD9851_PORT, pin);
    } else {
        gpio_bit_reset(AD9851_PORT, pin);
    }
}

static void w_clk(uint8_t lv) { wr(PIN_W_CLK, lv); }
static void fq_ud(uint8_t lv) { wr(PIN_FQ_UD, lv); }
static void reset(uint8_t lv) { wr(PIN_RESET, lv); }
static void data (uint8_t lv) { wr(PIN_DATA,  lv); }   /* 串行用 */

/* 并行：一次写 D0..D7，BOP 低 16 位置位/高 16 位复位，原子操作 */
static void bus(uint8_t b)
{
    GPIO_BOP(AD9851_PORT) =
        ((uint32_t)b & 0xFFu) | (((uint32_t)(~b) & 0xFFu) << 16);
}

void ad9851_gpio_init(void)
{
    rcu_periph_clock_enable(AD9851_RCU_GPIO);
    gpio_init(AD9851_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              AD9851_BUS_PINS | PIN_W_CLK | PIN_FQ_UD | PIN_RESET);
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
