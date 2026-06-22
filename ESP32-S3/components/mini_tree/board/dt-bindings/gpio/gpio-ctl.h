/* GPIO 默认参数 (dt-bindings, 仅供 dtsi #include <dt-bindings/...> 引用)
 *
 * 只放 #define 常量, 不写设备节点.
 * 数值与 hal_if/include/hal_gpio.h 中 hal_gpio_*_t 枚举一致.
 * 板级 port/pin/mode 在 board *.dts &gpio_pin { } 中覆盖.
 *
 * 端口: STM32/CH32 用 GPIOA..; ESP32-S3 固定 GPIO_PORT_0, gpio-pin = SoC 编号 (GPIO0..GPIO60).
 * 注意: 仅供 dtc-lite 预处理, 勿加 #ifndef guard (会破坏宏展开).
 */

/* ── 方向模式 (hal_gpio_mode_t) ── */
#define GPIO_MODE_INPUT                 0
#define GPIO_MODE_OUTPUT                1
#define GPIO_MODE_INPUT_OUTPUT          2
#define GPIO_MODE_OPEN_DRAIN            3

/* ── 上下拉 (hal_gpio_pull_t) ── */
#define GPIO_PULL_NONE                  0
#define GPIO_PULL_UP                    1
#define GPIO_PULL_DOWN                  2

/* ── 中断触发 (hal_gpio_intr_t) ── */
#define GPIO_INTR_DISABLE               0
#define GPIO_INTR_RISING                1
#define GPIO_INTR_FALLING               2
#define GPIO_INTR_ANY_EDGE              3

/* ── 输出默认电平 / 有效电平 (Linux gpio 语义) ── */
#define GPIO_LEVEL_LOW                  0
#define GPIO_LEVEL_HIGH                 1
#define GPIO_ACTIVE_LOW                 2
#define GPIO_ACTIVE_HIGH                3

/* ── 端口 (hal_pin_t [31:16]) ── */
#define GPIO_PORT_0                     -1
#define GPIOA                           0
#define GPIOB                           1
#define GPIOC                           2
#define GPIOD                           3
#define GPIOE                           4
#define GPIOF                           5
#define GPIOG                           6
#define GPIOH                           7
#define GPIOI                           8

/* ── 端口内引脚位 (0..15, STM32/CH32 单端口) ── */
#define GPIO_PIN_0                      0
#define GPIO_PIN_1                      1
#define GPIO_PIN_2                      2
#define GPIO_PIN_3                      3
#define GPIO_PIN_4                      4
#define GPIO_PIN_5                      5
#define GPIO_PIN_6                      6
#define GPIO_PIN_7                      7
#define GPIO_PIN_8                      8
#define GPIO_PIN_9                      9
#define GPIO_PIN_10                     10
#define GPIO_PIN_11                     11
#define GPIO_PIN_12                     12
#define GPIO_PIN_13                     13
#define GPIO_PIN_14                     14
#define GPIO_PIN_15                     15
#define GPIO_PIN_16                     16
#define GPIO_PIN_17                     17
#define GPIO_PIN_18                     18
#define GPIO_PIN_19                     19
#define GPIO_PIN_20                     20
#define GPIO_PIN_21                     21
#define GPIO_PIN_22                     22
#define GPIO_PIN_23                     23
#define GPIO_PIN_24                     24
#define GPIO_PIN_25                     25
#define GPIO_PIN_26                     26
#define GPIO_PIN_27                     27
#define GPIO_PIN_28                     28
#define GPIO_PIN_29                     29
#define GPIO_PIN_30                     30

/* ── ESP32 SoC GPIO 编号 (0..60, gpio-pin 直接填此值) ── */
#define GPIO0                           0
#define GPIO1                           1
#define GPIO2                           2
#define GPIO3                           3
#define GPIO4                           4
#define GPIO5                           5
#define GPIO6                           6
#define GPIO7                           7
#define GPIO8                           8
#define GPIO9                           9
#define GPIO10                          10
#define GPIO11                          11
#define GPIO12                          12
#define GPIO13                          13
#define GPIO14                          14
#define GPIO15                          15
#define GPIO16                          16
#define GPIO17                          17
#define GPIO18                          18
#define GPIO19                          19
#define GPIO20                          20
#define GPIO21                          21
#define GPIO22                          22  /* ESP32-S3: NC */
#define GPIO23                          23  /* ESP32-S3: NC */
#define GPIO24                          24  /* ESP32-S3: NC */
#define GPIO25                          25  /* ESP32-S3: NC */
#define GPIO26                          26
#define GPIO27                          27
#define GPIO28                          28
#define GPIO29                          29
#define GPIO30                          30
#define GPIO31                          31
#define GPIO32                          32
#define GPIO33                          33
#define GPIO34                          34
#define GPIO35                          35
#define GPIO36                          36
#define GPIO37                          37
#define GPIO38                          38
#define GPIO39                          39
#define GPIO40                          40
#define GPIO41                          41
#define GPIO42                          42
#define GPIO43                          43
#define GPIO44                          44
#define GPIO45                          45
#define GPIO46                          46
#define GPIO47                          47
#define GPIO48                          48
#define GPIO49                          49  /* ESP32-S3: NC */
#define GPIO50                          50  /* ESP32-S3: NC */
#define GPIO51                          51  /* ESP32-S3: NC */
#define GPIO52                          52  /* ESP32-S3: NC */
#define GPIO53                          53  /* ESP32-S3: NC */
#define GPIO54                          54  /* ESP32-S3: NC */
#define GPIO55                          55  /* ESP32-S3: NC */
#define GPIO56                          56  /* ESP32-S3: NC */
#define GPIO57                          57  /* ESP32-S3: NC */
#define GPIO58                          58  /* ESP32-S3: NC */
#define GPIO59                          59  /* ESP32-S3: NC */
#define GPIO60                          60  /* ESP32-S3: NC */

/* ── 常用默认组合 (dtsi 可直接引用) ── */
#define GPIO_DEFAULT_OUTPUT             GPIO_MODE_OUTPUT
#define GPIO_DEFAULT_OUTPUT_PULL        GPIO_PULL_NONE
#define GPIO_DEFAULT_OPEN_DRAIN         GPIO_MODE_OPEN_DRAIN
#define GPIO_DEFAULT_OPEN_DRAIN_PULL    GPIO_PULL_NONE
#define GPIO_DEFAULT_INPUT              GPIO_MODE_INPUT
#define GPIO_DEFAULT_INPUT_PULL         GPIO_PULL_NONE
#define GPIO_DEFAULT_INPUT_PULLUP       GPIO_PULL_UP
#define GPIO_DEFAULT_INPUT_PULLDOWN     GPIO_PULL_DOWN
#define GPIO_DEFAULT_INTR               GPIO_INTR_DISABLE
#define GPIO_DEFAULT_LEVEL              GPIO_LEVEL_LOW
