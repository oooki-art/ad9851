/**
 * @file  ad9851.c
 * @brief AD9851 协议层实现，不含任何平台相关代码。
 *
 * 40 位字结构（W0 为最低位）：
 *   W0..W31   32 位频率控制字 FTW（低位在前）
 *   W32..W39  8 位控制字：
 *     bit0 (W32) 6x REFCLK 倍频使能
 *     bit1 (W33) 必须为 0
 *     bit2 (W34) Power-Down
 *     bit3..bit7 (W35..W39) 5 位相位
 *
 * 发送顺序：
 *   串行 - 按 W0..W39 顺序逐位移入(LSB first)，每位一个 W_CLK 脉冲。
 *   并行 - 分 5 个字节: freq[7:0],freq[15:8],freq[23:16],freq[31:24],ctrl，
 *          每个字节一个 W_CLK 脉冲锁存。
 *   两种方式最后都用 FQ_UD 上升沿把结果输出。
 *
 * 频率公式: f_out = FTW * f_sysclk / 2^32
 *           FTW   = f_out * 2^32 / f_sysclk
 */
#include "ad9851.h"

#define AD9851_CTRL_6X_MULT   0x01u  /* bit0: 6 倍频使能 */
#define AD9851_CTRL_PWRDOWN   0x04u  /* bit2: 掉电 */
#define AD9851_PHASE_SHIFT    3u     /* 相位位于 bit3..bit7 */

#define TWO_POW_32  4294967296.0     /* 2^32 */

/* ----------------- 内部小工具 ----------------- */

static inline void io_delay(const ad9851_t *dev)
{
    if (dev->io.delay_us) {
        dev->io.delay_us(1);
    }
}

static void w_clk_pulse(const ad9851_t *dev)
{
    dev->io.write_w_clk(1);
    io_delay(dev);
    dev->io.write_w_clk(0);
    io_delay(dev);
}

static void fq_ud_pulse(const ad9851_t *dev)
{
    dev->io.write_fq_ud(1);
    io_delay(dev);
    dev->io.write_fq_ud(0);
    io_delay(dev);
}

/* 把影子寄存器的控制部分编码成 8 位控制字 W32..W39 */
static uint8_t ad9851_encode_ctrl(const ad9851_shadow_t *s)
{
    uint8_t ctrl = 0u;
    if (s->six_x_mult) { ctrl |= AD9851_CTRL_6X_MULT; }  /* bit0 = W32 */
    if (s->power_down) { ctrl |= AD9851_CTRL_PWRDOWN; }  /* bit2 = W34 */
    ctrl |= (uint8_t)((s->phase & 0x1Fu) << AD9851_PHASE_SHIFT); /* bit3..7 = W35..W39 */
    return ctrl;
}

/* ----------------- 串行 / 并行底层发送 ----------------- */

static void send_serial(ad9851_t *dev, uint32_t ftw, uint8_t ctrl)
{
    int i;
    for (i = 0; i < 32; i++) {           /* 32 位频率字, LSB first */
        dev->io.write_data((uint8_t)(ftw & 1u));
        w_clk_pulse(dev);
        ftw >>= 1;
    }
    for (i = 0; i < 8; i++) {            /* 8 位控制字, LSB first */
        dev->io.write_data((uint8_t)(ctrl & 1u));
        w_clk_pulse(dev);
        ctrl >>= 1;
    }
}

static void send_parallel(ad9851_t *dev, uint32_t ftw, uint8_t ctrl)
{
    /* 5 个字节: 频率低字节在前, 控制字最后 */
    dev->io.write_bus((uint8_t)(ftw & 0xFFu));        w_clk_pulse(dev);
    dev->io.write_bus((uint8_t)((ftw >> 8) & 0xFFu)); w_clk_pulse(dev);
    dev->io.write_bus((uint8_t)((ftw >> 16) & 0xFFu));w_clk_pulse(dev);
    dev->io.write_bus((uint8_t)((ftw >> 24) & 0xFFu));w_clk_pulse(dev);
    dev->io.write_bus(ctrl);                          w_clk_pulse(dev);
}

/* ----------------- 对外接口 ----------------- */

void ad9851_write_word(ad9851_t *dev, uint32_t ftw, uint8_t control_byte)
{
    dev->send(dev, ftw, control_byte);  /* init 时已绑定串/并行，运行期不判断 */
    fq_ud_pulse(dev);                   /* 锁存输出 */
}

void ad9851_reset(ad9851_t *dev)
{
    if (dev->io.write_reset) {
        dev->io.write_reset(1);
        io_delay(dev);
        dev->io.write_reset(0);
        io_delay(dev);
    }
    /* 复位后芯片默认在并行模式；
       串行模式需再补一个 W_CLK 脉冲 + 一个 FQ_UD 脉冲来切入串行。 */
    if (dev->mode == AD9851_MODE_SERIAL) {
        w_clk_pulse(dev);
        fq_ud_pulse(dev);
    }
}

void ad9851_init(ad9851_t *dev, const ad9851_io_t *io, ad9851_mode_t mode,
                 uint32_t sys_clk_hz, uint8_t use_6x_mult)
{
    dev->io          = *io;
    dev->mode        = mode;
    dev->sys_clk_hz  = sys_clk_hz;
    /* 按模式一次性绑定发送函数，之后 write_word 直接调用，不再判断 */
    dev->send = (mode == AD9851_MODE_PARALLEL) ? send_parallel : send_serial;

    /* 影子寄存器初值：频率 0、相位 0、6x 倍频按入参、不掉电 */
    dev->shadow.ftw        = 0;
    dev->shadow.phase      = 0;
    dev->shadow.six_x_mult = use_6x_mult ? 1u : 0u;
    dev->shadow.power_down = 0;

    /* 引脚空闲电平 */
    dev->io.write_w_clk(0);
    dev->io.write_fq_ud(0);
    if (mode == AD9851_MODE_PARALLEL) {
        dev->io.write_bus(0);
    } else {
        dev->io.write_data(0);
    }

    ad9851_reset(dev);
}

uint32_t ad9851_calc_ftw(const ad9851_t *dev, double freq_hz)
{
    double ftw;
    if (freq_hz < 0.0) {
        freq_hz = 0.0;
    }
    ftw = (freq_hz * TWO_POW_32) / (double)dev->sys_clk_hz;
    if (ftw > 4294967295.0) {
        ftw = 4294967295.0;          /* 饱和到最大 */
    }
    return (uint32_t)(ftw + 0.5);    /* 四舍五入 */
}

/* 影子寄存器 -> 芯片：所有 set_* / power_* 都最终走这里刷新 */
void ad9851_update(ad9851_t *dev)
{
    ad9851_write_word(dev, dev->shadow.ftw, ad9851_encode_ctrl(&dev->shadow));
}

void ad9851_set_freq_phase(ad9851_t *dev, double freq_hz, uint8_t phase)
{
    dev->shadow.ftw   = ad9851_calc_ftw(dev, freq_hz);
    dev->shadow.phase = phase & 0x1Fu;
    ad9851_update(dev);
}

void ad9851_set_frequency(ad9851_t *dev, double freq_hz)
{
    dev->shadow.ftw = ad9851_calc_ftw(dev, freq_hz);
    ad9851_update(dev);
}

void ad9851_set_phase(ad9851_t *dev, uint8_t phase)
{
    dev->shadow.phase = phase & 0x1Fu;
    ad9851_update(dev);
}

void ad9851_power_down(ad9851_t *dev)
{
    dev->shadow.power_down = 1u;
    ad9851_update(dev);
}

void ad9851_power_up(ad9851_t *dev)
{
    dev->shadow.power_down = 0u;
    ad9851_update(dev);
}

/* ===================== 扫频 ===================== */

void ad9851_sweep_linear(ad9851_t *dev, double f_start, double f_stop,
                         double f_step, uint32_t dwell_us)
{
    double f;
    if (f_step == 0.0) {
        return;
    }
    if (f_step > 0.0) {
        for (f = f_start; f <= f_stop; f += f_step) {
            ad9851_set_frequency(dev, f);
            if (dev->io.delay_us) {
                dev->io.delay_us(dwell_us);
            }
        }
    } else {
        for (f = f_start; f >= f_stop; f += f_step) {
            ad9851_set_frequency(dev, f);
            if (dev->io.delay_us) {
                dev->io.delay_us(dwell_us);
            }
        }
    }
}

void ad9851_sweep_init(ad9851_sweep_t *sw, ad9851_t *dev,
                       double f_start, double f_stop, double f_step,
                       uint8_t bounce)
{
    sw->dev    = dev;
    sw->start  = f_start;
    sw->stop   = f_stop;
    sw->step   = (f_step < 0.0) ? -f_step : f_step;  /* 取绝对值 */
    sw->cur    = f_start;
    sw->dir    = (f_stop >= f_start) ? 1 : -1;
    sw->bounce = bounce ? 1u : 0u;
}

void ad9851_sweep_rewind(ad9851_sweep_t *sw)
{
    sw->cur = sw->start;
    sw->dir = (sw->stop >= sw->start) ? 1 : -1;
}

double ad9851_sweep_tick(ad9851_sweep_t *sw)
{
    double out = sw->cur;
    double lo  = (sw->start < sw->stop) ? sw->start : sw->stop;
    double hi  = (sw->start < sw->stop) ? sw->stop : sw->start;

    ad9851_set_frequency(sw->dev, out);

    /* 前进一步 */
    sw->cur += (double)sw->dir * sw->step;

    /* 到达端点的处理 */
    if (sw->cur > hi || sw->cur < lo) {
        if (sw->bounce) {
            sw->dir = (int8_t)-sw->dir;                 /* 折返 */
            sw->cur = out + (double)sw->dir * sw->step;
        } else {
            sw->cur = sw->start;                        /* 跳回起点 */
            sw->dir = (sw->stop >= sw->start) ? 1 : -1;
        }
    }
    return out;
}
