# AD9851 可移植驱动（串行 + 并行）

把"协议逻辑"和"平台 IO"分离：协议层只通过函数指针调用底层引脚操作，
换 MCU 平台时只替换一个 `port` 文件，`ad9851.c/.h` 不改。

## 文件结构

| 文件 | 作用 | 随平台改动 |
|---|---|---|
| `ad9851.h` / `ad9851.c` | 协议层：40 位字、频率换算、串/并时序、复位、扫频 | ❌ 永不改 |
| `ad9851_port_stm32f1_stdperiph.c` | STM32F103 标准库（串/并行合一） | 选其一 |
| `ad9851_port_stm32_hal.c` | STM32 HAL（G4/F1/F4，串/并行合一） | 选其一 |
| `ad9851_port_gd32.c` | GD32 固件库（串/并行合一） | 选其一 |

> 每个 port 同时导出 `ad9851_io`(串行) 和 `ad9851_io_parallel`(并行)，
> 用哪种就把哪张表传给 `ad9851_init`。串行数据线即芯片 D7，与并行总线的 D7 复用同一引脚。
| `example_main.c` | 基本/并行/阻塞扫频/非阻塞扫频示例 | — |

**编译规则**：`ad9851.c` + 目标平台对应的 **一个** `ad9851_port_*.c`。

## 串行 vs 并行
- 串行：只需 1 根数据线（芯片 D7）+ W_CLK + FQ_UD。
- 并行：需要 D0~D7 全部 8 根 + W_CLK + FQ_UD，速度更快。
- 模式在 `ad9851_init()` 传 `AD9851_MODE_SERIAL` / `AD9851_MODE_PARALLEL`。
  port 文件里串行只填 `write_data`、并行只填 `write_bus`，另一个留空即可。

## 关键参数 sys_clk_hz

频率公式 `f_out = FTW * sys_clk / 2^32`，传**倍频之后**的时钟：

- 30MHz 晶振 + 使能 6 倍频 → `ad9851_init(dev, io, mode, 180000000, 1)`
- 外部直接喂 180MHz、不倍频 → `ad9851_init(dev, io, mode, 180000000, 0)`

## 频率 / 相位

```c
ad9851_set_frequency(&dev, 10e6);   // 改频率，保留当前相位
ad9851_set_phase(&dev, 8);          // 改相位(0..31=0..360°,步进11.25°)，保留频率
ad9851_set_freq_phase(&dev, 1e6, 8);// 一起设
```
驱动内部记住 cur_ftw / cur_phase，二者互不覆盖。

## 扫频

```c
// 阻塞式：1~30MHz, 步进10kHz, 每点驻留500us（需 port 提供 delay_us）
ad9851_sweep_linear(&dev, 1e6, 30e6, 10e3, 500);

// 非阻塞：初始化一次，定时器中断里 tick 一步，不卡主循环
ad9851_sweep_t sw;
ad9851_sweep_init(&sw, &dev, 1e6, 30e6, 10e3, /*bounce=*/1); // 1=三角往复,0=锯齿
void TIMx_IRQHandler(void){ ad9851_sweep_tick(&sw); }       // 周期=驻留时间
```

## 控制字（第 5 字节）位定义

```
bit0      6x 参考时钟倍频使能
bit1      必须为 0
bit2      Power-Down
bit3..7   5 位相位（0..31 = 0..360°）
```

## 移植到新平台 3 步

1. 复制一个 `ad9851_port_*.c`，改成你平台的 GPIO 写引脚函数；
2. 填好 `ad9851_io`（串行填 write_data / 并行填 write_bus）；
3. `ad9851_init()` 之前配好 GPIO 推挽输出。
