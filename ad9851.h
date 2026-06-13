/**
 * @file    ad9851.h
 * @brief   AD9851 DDS 平台无关驱动（支持串行 / 并行两种加载模式）
 *
 * 设计原则：协议层（本文件 + ad9851.c）不依赖任何 MCU 头文件。
 * 所有底层引脚操作由调用者通过 ad9851_io_t 里的函数指针提供，
 * 因此换平台（STM32 标准库 / HAL / GD32）时本文件无需改动，
 * 只替换一个 port 实现文件即可。
 *
 * 接线：
 *   W_CLK  字时钟，每个上升沿移入 1 bit(串行) / 锁存 1 字节(并行)
 *   FQ_UD  频率更新，上升沿把移位寄存器内容锁存输出
 *   串行模式: DATA = 芯片 D7 脚，单根数据线
 *   并行模式: BUS  = 芯片 D0..D7，8 位总线（多数便宜模块已硬配成串行，
 *                    并行需要把 D0~D7 全部引出）
 *   RESET  主复位（可选，没接就传 NULL）
 */
#ifndef AD9851_H
#define AD9851_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AD9851_MODE_SERIAL = 0,   /* 串行：用 write_data 逐位发 */
    AD9851_MODE_PARALLEL = 1  /* 并行：用 write_bus 逐字节发 */
} ad9851_mode_t;

/**
 * 底层引脚操作接口。level: 0=低电平, 非0=高电平。
 *   - 串行 port 只需填 write_data，write_bus 留空(NULL)；
 *   - 并行 port 只需填 write_bus，write_data 留空(NULL)；
 *   - 没接 RESET 引脚时 write_reset 传 NULL；
 *   - delay_us 是为了兼容时序限制，但MCU置位一般足够慢，所以一般传 NULL 即可。
 */
typedef struct {
    void (*write_w_clk)(uint8_t level);
    void (*write_fq_ud)(uint8_t level);
    void (*write_data) (uint8_t level);   /* 串行: 单 bit -> D7 */
    void (*write_bus)  (uint8_t byte);    /* 并行: 一次写 D0..D7 */
    void (*write_reset)(uint8_t level);   /* 可为 NULL */
    void (*delay_us)   (uint32_t us);     /* 可为 NULL */
} ad9851_io_t;

typedef struct {
    ad9851_io_t   io;
    ad9851_mode_t mode;
    uint32_t      sys_clk_hz;   /* DDS 内部最终系统时钟(Hz)，见 ad9851_init */
    uint8_t       use_6x_mult;  /* 1 = 使能片内 6 倍参考时钟倍频器 */
    /* 当前状态，使频率/相位互不覆盖 */
    uint32_t      cur_ftw;
    uint8_t       cur_phase;    /* 0..31 */
} ad9851_t;

/**
 * @brief  初始化 AD9851 设备对象：设置引脚空闲电平并按模式执行复位序列。
 * @param  dev          设备对象指针，由调用者分配，后续接口都用它。
 * @param  io           底层引脚操作接口表（内部会拷贝一份保存）。
 * @param  mode         加载模式：AD9851_MODE_SERIAL 或 AD9851_MODE_PARALLEL。
 * @param  sys_clk_hz   DDS 内部系统时钟(Hz)，即倍频之后的频率：
 *                        - 30MHz 晶振 + 使能 6 倍频 -> 180000000, use_6x_mult=1
 *                        - 外部直接喂 180MHz 不倍频 -> 180000000, use_6x_mult=0
 * @param  use_6x_mult  是否使能芯片内部 6 倍频位（控制字 bit0）：0=关, 1=开。
 * @return 无。
 */
void ad9851_init(ad9851_t *dev, const ad9851_io_t *io, ad9851_mode_t mode,
                 uint32_t sys_clk_hz, uint8_t use_6x_mult);

/**
 * @brief  复位芯片。串行模式会附带"进入串行模式"序列(W_CLK 脉冲 + FQ_UD 脉冲)。
 * @param  dev  设备对象指针。
 * @return 无。
 */
void ad9851_reset(ad9851_t *dev);

/**
 * @brief  设置输出频率，保留当前相位。
 * @param  dev      设备对象指针。
 * @param  freq_hz  目标频率(Hz)，超出 [0, sys_clk/2] 会被饱和处理。
 * @return 无。
 */
void ad9851_set_frequency(ad9851_t *dev, double freq_hz);

/**
 * @brief  只改相位，保留当前频率。
 * @param  dev    设备对象指针。
 * @param  phase  相位值，取 0..31，对应 0..360°，步进 11.25°（仅低 5 位有效）。
 * @return 无。
 */
void ad9851_set_phase(ad9851_t *dev, uint8_t phase);

/**
 * @brief  同时设置频率与相位。
 * @param  dev      设备对象指针。
 * @param  freq_hz  目标频率(Hz)。
 * @param  phase    相位值 0..31（0..360°，步进 11.25°）。
 * @return 无。
 */
void ad9851_set_freq_phase(ad9851_t *dev, double freq_hz, uint8_t phase);

/**
 * @brief  进入低功耗(power-down)，保留当前频率字。再次调用 set_frequency 唤醒。
 * @param  dev  设备对象指针。
 * @return 无。
 */
void ad9851_power_down(ad9851_t *dev);

/**
 * @brief  直接写 40 位字，需要自定义控制字时使用。
 * @param  dev           设备对象指针。
 * @param  ftw           32 位频率控制字。
 * @param  control_byte  8 位控制字：bit0=6x倍频, bit2=掉电, bit3..7=相位。
 * @return 无。
 */
void ad9851_write_word(ad9851_t *dev, uint32_t ftw, uint8_t control_byte);

/**
 * @brief  把频率换算成 32 位频率控制字，供显示/调试。
 * @param  dev      设备对象指针（提供 sys_clk_hz）。
 * @param  freq_hz  频率(Hz)。
 * @return 对应的 32 位频率控制字 FTW = round(freq * 2^32 / sys_clk)，已做饱和。
 */
uint32_t ad9851_calc_ftw(const ad9851_t *dev, double freq_hz);

/* ===================== 扫频（上层封装） ===================== */

/**
 * @brief  阻塞式线性扫频：从 f_start 扫到 f_stop，逐点输出并驻留。
 * @param  dev       设备对象指针。
 * @param  f_start   起始频率(Hz)。
 * @param  f_stop    终止频率(Hz)。
 * @param  f_step    步进(Hz)，可为负实现反向扫频；为 0 直接返回。
 * @param  dwell_us  每个频点驻留时间(微秒)，依赖 io.delay_us，必须提供。
 * @return 无（函数返回时已扫完一遍）。
 */
void ad9851_sweep_linear(ad9851_t *dev, double f_start, double f_stop,
                         double f_step, uint32_t dwell_us);

/**
 * 非阻塞扫频对象：在定时器中断/主循环里反复调用 ad9851_sweep_tick()，
 * 每调一次输出一个频点并前进一步，不会阻塞。
 */
typedef struct {
    ad9851_t *dev;
    double    start;
    double    stop;
    double    step;     /* 始终为正，方向由 dir 决定 */
    double    cur;
    int8_t    dir;      /* +1 上扫, -1 下扫 */
    uint8_t   bounce;   /* 1=到端点折返往复, 0=到顶回到起点重来 */
} ad9851_sweep_t;

/**
 * @brief  初始化非阻塞扫频对象。
 * @param  sw       扫频对象指针，由调用者分配。
 * @param  dev      已初始化的设备对象指针。
 * @param  f_start  起始频率(Hz)。
 * @param  f_stop   终止频率(Hz)。
 * @param  f_step   步进(Hz)，内部取绝对值，方向由起止大小自动判定。
 * @param  bounce   端点处理：1=往复(三角扫), 0=锯齿(到顶跳回起点)。
 * @return 无。
 */
void ad9851_sweep_init(ad9851_sweep_t *sw, ad9851_t *dev,
                       double f_start, double f_stop, double f_step,
                       uint8_t bounce);

/**
 * @brief  输出当前频点并前进一步，循环不停止（适合放进定时器中断）。
 * @param  sw  扫频对象指针。
 * @return 本次实际输出的频率(Hz)。
 */
double ad9851_sweep_tick(ad9851_sweep_t *sw);

/**
 * @brief  把扫频复位到起点。
 * @param  sw  扫频对象指针。
 * @return 无。
 */
void ad9851_sweep_rewind(ad9851_sweep_t *sw);

#ifdef __cplusplus
}
#endif

#endif /* AD9851_H */
