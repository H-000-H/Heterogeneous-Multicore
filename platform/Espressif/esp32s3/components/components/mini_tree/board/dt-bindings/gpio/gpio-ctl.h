/* GPIO 默认参数 (dt-bindings, 仅供 dtsi #include <dt-bindings/...> 引用)
 *
 * 只放 #define 常量, 不写设备节点.
 * 数值与 hal_if/gpio/hal_gpio.h 中 hal_gpio_*_t 枚举一致.
 * 板级 port/pin/mode 在 board *.dts &gpio_pin { } 中覆盖.
 * ESP32: DTS_GPIOZERO=0, gpio-pin = SoC 编号 (DTS_GPIO0..DTS_GPIO60).
 * dtc-lite 仅做 #define 展开; 勿加 #ifndef guard (会破坏宏展开).
 */

/* 方向模式 (hal_gpio_mode_t) */
#define DTS_GPIO_MODE_INPUT          0
#define DTS_GPIO_MODE_OUTPUT         1
#define DTS_GPIO_MODE_INPUT_OUTPUT   2
#define DTS_GPIO_MODE_OPEN_DRAIN     3

/* 上下拉 (hal_gpio_pull_t) */
#define DTS_GPIO_PULL_NONE           0
#define DTS_GPIO_PULL_UP             1
#define DTS_GPIO_PULL_DOWN           2

/* 中断触发 (hal_gpio_intr_t) */
#define DTS_GPIO_INTR_DISABLE        0
#define DTS_GPIO_INTR_RISING         1
#define DTS_GPIO_INTR_FALLING        2
#define DTS_GPIO_INTR_ANY_EDGE       3

/* 输出默认电平 / 有效电平 */
#define DTS_GPIO_LEVEL_LOW           0
#define DTS_GPIO_LEVEL_HIGH          1
#define DTS_GPIO_ACTIVE_LOW          2
#define DTS_GPIO_ACTIVE_HIGH         3

/* 逻辑端口 (hal_pin_t.v[0]) */
#define DTS_GPIOZERO                 0
/* ESP32 SoC GPIO 编号 (gpio-pin 直接填此值) */
#define DTS_GPIO0                    0
#define DTS_GPIO1                    1
#define DTS_GPIO2                    2
#define DTS_GPIO3                    3
#define DTS_GPIO4                    4
#define DTS_GPIO5                    5
#define DTS_GPIO6                    6
#define DTS_GPIO7                    7
#define DTS_GPIO8                    8
#define DTS_GPIO9                    9
#define DTS_GPIO10                   10
#define DTS_GPIO11                   11
#define DTS_GPIO12                   12
#define DTS_GPIO13                   13
#define DTS_GPIO14                   14
#define DTS_GPIO15                   15
#define DTS_GPIO16                   16
#define DTS_GPIO17                   17
#define DTS_GPIO18                   18
#define DTS_GPIO19                   19
#define DTS_GPIO20                   20
#define DTS_GPIO21                   21
#define DTS_GPIO22                   22  /* ESP32-S3: NC */
#define DTS_GPIO23                   23  /* ESP32-S3: NC */
#define DTS_GPIO24                   24  /* ESP32-S3: NC */
#define DTS_GPIO25                   25  /* ESP32-S3: NC */
#define DTS_GPIO26                   26
#define DTS_GPIO27                   27
#define DTS_GPIO28                   28
#define DTS_GPIO29                   29
#define DTS_GPIO30                   30
#define DTS_GPIO31                   31
#define DTS_GPIO32                   32
#define DTS_GPIO33                   33
#define DTS_GPIO34                   34
#define DTS_GPIO35                   35
#define DTS_GPIO36                   36
#define DTS_GPIO37                   37
#define DTS_GPIO38                   38
#define DTS_GPIO39                   39
#define DTS_GPIO40                   40
#define DTS_GPIO41                   41
#define DTS_GPIO42                   42
#define DTS_GPIO43                   43
#define DTS_GPIO44                   44
#define DTS_GPIO45                   45
#define DTS_GPIO46                   46
#define DTS_GPIO47                   47
#define DTS_GPIO48                   48
#define DTS_GPIO49                   49  /* ESP32-S3: NC */
#define DTS_GPIO50                   50  /* ESP32-S3: NC */
#define DTS_GPIO51                   51  /* ESP32-S3: NC */
#define DTS_GPIO52                   52  /* ESP32-S3: NC */
#define DTS_GPIO53                   53  /* ESP32-S3: NC */
#define DTS_GPIO54                   54  /* ESP32-S3: NC */
#define DTS_GPIO55                   55  /* ESP32-S3: NC */
#define DTS_GPIO56                   56  /* ESP32-S3: NC */
#define DTS_GPIO57                   57  /* ESP32-S3: NC */
#define DTS_GPIO58                   58  /* ESP32-S3: NC */
#define DTS_GPIO59                   59  /* ESP32-S3: NC */
#define DTS_GPIO60                   60  /* ESP32-S3: NC */

/* 常用默认组合 */
#define DTS_GPIO_DEFAULT_OUTPUT          DTS_GPIO_MODE_OUTPUT
#define DTS_GPIO_DEFAULT_OUTPUT_PULL     DTS_GPIO_PULL_NONE
#define DTS_GPIO_DEFAULT_OPEN_DRAIN      DTS_GPIO_MODE_OPEN_DRAIN
#define DTS_GPIO_DEFAULT_OPEN_DRAIN_PULL DTS_GPIO_PULL_NONE
#define DTS_GPIO_DEFAULT_INPUT           DTS_GPIO_MODE_INPUT
#define DTS_GPIO_DEFAULT_INPUT_PULL      DTS_GPIO_PULL_NONE
#define DTS_GPIO_DEFAULT_INPUT_PULLUP    DTS_GPIO_PULL_UP
#define DTS_GPIO_DEFAULT_INPUT_PULLDOWN  DTS_GPIO_PULL_DOWN
#define DTS_GPIO_DEFAULT_INTR            DTS_GPIO_INTR_DISABLE
#define DTS_GPIO_DEFAULT_LEVEL           DTS_GPIO_LEVEL_LOW
