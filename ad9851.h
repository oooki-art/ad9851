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

/**
 * @brief AD9851 影子寄存器 (shadow register / 软件副本)
 *
 * 【为什么需要它】
 *   AD9851 是"只写"器件：写进芯片的寄存器读不回来。若想单独修改其中
 *   一项（例如只调相位、频率保持不变），硬件上没法做"读-改-写"。
 *   通用解法是在 MCU 内存里维护一份与芯片寄存器一一对应的副本——
 *   "影子寄存器"。
 *
 * 【怎么用】
 *   1) 要改哪一项，就改影子里对应的成员；
 *   2) 调 ad9851_update() 把整份影子编码成 40 位字重新写进芯片。
 *   好处：① 改一项不动其它项；② 随时能查"芯片当前是什么状态"，便于调试。
 *
 * 【各成员 ←→ AD9851 的 40 位控制字 W0..W39】
 *   ftw        -> W0..W31   32 位频率控制字
 *   six_x_mult -> W32       6x 参考时钟倍频使能
 *               (W33        恒为 0)
 *   power_down -> W34       掉电
 *   phase      -> W35..W39  5 位相位
 */
typedef struct {
    uint32_t ftw;         /* 32 位频率控制字 */
    uint8_t  phase;       /* 5 位相位 0..31 (0..360°, 步进 11.25°) */
    uint8_t  six_x_mult;  /* 6x 倍频使能: 0/1 */
    uint8_t  power_down;  /* 掉电: 0/1 */
} ad9851_shadow_t;

typedef struct ad9851_s ad9851_t;
struct ad9851_s {
    ad9851_io_t     io;
    ad9851_mode_t   mode;
    uint32_t        sys_clk_hz;  /* DDS 内部最终系统时钟(Hz)，见 ad9851_init */
    ad9851_shadow_t shadow;      /* 影子寄存器：芯片当前状态的软件副本 */
    /* 内部：init 时按模式绑定为串行或并行发送，运行期不再判断模式 */
    void (*send)(ad9851_t *dev, uint32_t ftw, uint8_t ctrl);
};

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
 * @brief  按影子寄存器当前内容把整份 40 位字写进芯片（刷新）。
 * @note   上面/下面的 set_* / power_* 都是"改影子字段 + 调用本函数"。
 *         你也可以直接改 dev->shadow 的多个字段后调一次本函数批量生效。
 * @param  dev  设备对象指针。
 * @return 无。
 */
void ad9851_update(ad9851_t *dev);

/**
 * @brief  改影子里的频率字并刷新芯片；相位等其它字段保持不变。
 * @param  dev      设备对象指针。
 * @param  freq_hz  目标频率(Hz)，超出 [0, sys_clk/2] 会被饱和处理。
 * @return 无。
 */
void ad9851_set_frequency(ad9851_t *dev, double freq_hz);

/**
 * @brief  改影子里的相位并刷新芯片；频率保持不变。
 * @param  dev    设备对象指针。
 * @param  phase  相位值，取 0..31，对应 0..360°，步进 11.25°（仅低 5 位有效）。
 * @return 无。
 */
void ad9851_set_phase(ad9851_t *dev, uint8_t phase);

/**
 * @brief  同时改影子里的频率与相位并刷新芯片。
 * @param  dev      设备对象指针。
 * @param  freq_hz  目标频率(Hz)。
 * @param  phase    相位值 0..31（0..360°，步进 11.25°）。
 * @return 无。
 */
void ad9851_set_freq_phase(ad9851_t *dev, double freq_hz, uint8_t phase);

/**
 * @brief  进入低功耗(power-down)：置影子的 power_down 位并刷新。
 * @note   频率字仍保留在影子里；用 ad9851_power_up() 唤醒（不会自动唤醒）。
 * @param  dev  设备对象指针。
 * @return 无。
 */
void ad9851_power_down(ad9851_t *dev);

/**
 * @brief  退出低功耗：清影子的 power_down 位并刷新，恢复原频率输出。
 * @param  dev  设备对象指针。
 * @return 无。
 */
void ad9851_power_up(ad9851_t *dev);

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
