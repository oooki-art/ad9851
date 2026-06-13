/**
 * @file  ad9851_port_stm32f1_stdperiph.c
 * @brief 平台移植层：STM32F103 标准库（串行 + 并行合一）
 *
 * 同时导出两张 IO 表，用哪种模式就把哪张传给 ad9851_init：
 *   ad9851_io           串行（数据线 = 芯片 D7）
 *   ad9851_io_parallel  并行（D0..D7 八位总线）
 *
 * 本例接线（按需改顶部宏）：
 *   D0..D7 = PB0..PB7（连续 8 脚），串行数据复用 D7=PB7
 *   W_CLK=PB8  FQ_UD=PB9  RESET=PB10
 */
#include "stm32f10x.h"
#include "ad9851.h"

#define AD9851_RCC_GPIO   RCC_APB2Periph_GPIOB
#define AD9851_PORT       GPIOB
#define AD9851_BUS_PINS   (GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3| \
                           GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_7)
#define PIN_DATA          GPIO_Pin_7   /* 串行数据 = D7 */
#define PIN_W_CLK         GPIO_Pin_8
#define PIN_FQ_UD         GPIO_Pin_9
#define PIN_RESET         GPIO_Pin_10

static inline void wr(uint16_t pin, uint8_t level)
{
    if (level) {
        GPIO_SetBits(AD9851_PORT, pin);
    } else {
        GPIO_ResetBits(AD9851_PORT, pin);
    }
}

static void w_clk(uint8_t lv) { wr(PIN_W_CLK, lv); }
static void fq_ud(uint8_t lv) { wr(PIN_FQ_UD, lv); }
static void reset(uint8_t lv) { wr(PIN_RESET, lv); }
static void data (uint8_t lv) { wr(PIN_DATA,  lv); }   /* 串行用 */

/* 并行：一次写 D0..D7，BSRR 低 16 位置位/高 16 位复位，原子操作 */
static void bus(uint8_t b)
{
    AD9851_PORT->BSRR =
        ((uint32_t)b & 0xFFu) | (((uint32_t)(~b) & 0xFFu) << 16);
}

/** 配置所有引脚为推挽输出（串/并行通用，调用 ad9851_init 前先调）。 */
void ad9851_gpio_init(void)
{
    GPIO_InitTypeDef gi;
    RCC_APB2PeriphClockCmd(AD9851_RCC_GPIO, ENABLE);
    gi.GPIO_Pin   = AD9851_BUS_PINS | PIN_W_CLK | PIN_FQ_UD | PIN_RESET;
    gi.GPIO_Mode  = GPIO_Mode_Out_PP;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(AD9851_PORT, &gi);
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
