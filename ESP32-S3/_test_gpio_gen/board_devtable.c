#include "board_nodes.h"
#include "board_devtable.h"
#include "device.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* ===== 属性表 (静态 .rodata) ===== */

/* / */
static const struct device_property DEV__props[] = {
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"model", "Espressif ESP32-S3-DevKitC-1"},
};

/* /cpus/cpu@0 */
static const struct device_property DEV_cpu_0_props[] = {
    {"device_type", "cpu"},
    {"reg", "0x0"},
    {"clock-frequency", "0xe4e1c00"},
};

/* /soc */
static const struct device_property DEV_soc_props[] = {
    {"ranges", "true"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
};

/* /soc/gpio@0 */
static const struct device_property DEV_gpio_0_props[] = {
    {"reg", "0x0"},
    {"gpio-controller", "true"},
    {"#gpio-cells", "0x2"},
};

/* /soc/gpio-pin@0 */
static const struct device_property DEV_gpio_pin_0_props[] = {
    {"reg", "0x0"},
    {"gpio-port", "0x0"},
    {"gpio-pin", "0x0"},
    {"gpio-mode", "0x1"},
    {"gpio-pull", "0x0"},
    {"gpio-intr", ""},
    {"default-level", "0x0"},
};

/* /soc/led@0 */
static const struct device_property DEV_led_0_props[] = {
    {"reg", "0x0"},
    {"brightness", "0x20"},
    {"bytes-per-led", "0x3"},
    {"color-order", "grb"},
    {"default-timeout-ms", "0x64"},
    {"rmt-resolution-hz", "0x989680"},
    {"rmt-mem-block", "0x40"},
    {"rmt-queue-depth", "0x4"},
    {"t0h-ticks", "0x4"},
    {"t0l-ticks", "0x9"},
    {"t1h-ticks", "0x8"},
    {"t1l-ticks", "0x5"},
    {"reset-ticks", "0x1f4"},
    {"gpio", "0x30"},
    {"num-leds", "0x1"},
};

/* /soc/spi@0 */
static const struct device_property DEV_spi_0_props[] = {
    {"reg", "0x0"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"host-id", "0x2"},
    {"dma-chan", "-0x1"},
    {"mosi-pin", "0xb"},
    {"miso-pin", "0xd"},
    {"sclk-pin", "0xc"},
    {"max-trans-buffer", "0x800"},
};

/* /soc/spi@0/fft@0 */
static const struct device_property DEV_fft_0_props[] = {
    {"reg", "0x0"},
    {"spi-mode", "0x0"},
    {"spi-max-frequency", "0x2625a00"},
    {"queue-size", "0x8"},
    {"cs-pin", "0xa"},
};

/* /soc/spi@1 */
static const struct device_property DEV_spi_1_props[] = {
    {"reg", "0x1"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"host-id", "0x3"},
    {"dma-chan", "-0x1"},
    {"mosi-pin", "0xe"},
    {"miso-pin", "0xf"},
    {"sclk-pin", "0x10"},
    {"max-trans-buffer", "0x200"},
};

/* /soc/spi@1/w25q64@0 */
static const struct device_property DEV_w25q64_0_props[] = {
    {"reg", "0x0"},
    {"spi-mode", "0x0"},
    {"spi-max-frequency", "0x2625a00"},
    {"queue-size", "0x8"},
    {"cs-pin", "0x14"},
};

/* ===== 依赖表 ===== */

static const device_id_t DEV_fft_0_deps[] = {
    DEV_ID_SPI_0,
};

static const device_id_t DEV_w25q64_0_deps[] = {
    DEV_ID_SPI_1,
};

/* ===== reg 分组表 ===== */

static const uint32_t DEV_cpu_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_gpio_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_gpio_pin_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_led_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_spi_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_fft_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_spi_1_REG_DATA[] = {
    0x1,
};
static const uint32_t DEV_w25q64_0_REG_DATA[] = {
    0x0,
};
static const struct device_reg DEV_cpu_0_REGS[] = {
    { .addr = &DEV_cpu_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_gpio_0_REGS[] = {
    { .addr = &DEV_gpio_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_gpio_pin_0_REGS[] = {
    { .addr = &DEV_gpio_pin_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_led_0_REGS[] = {
    { .addr = &DEV_led_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_spi_0_REGS[] = {
    { .addr = &DEV_spi_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_fft_0_REGS[] = {
    { .addr = &DEV_fft_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_spi_1_REGS[] = {
    { .addr = &DEV_spi_1_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_w25q64_0_REGS[] = {
    { .addr = &DEV_w25q64_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

/* ===== irq 表 ===== */

/* ===== 主节点表 ===== */
static const struct device_node s_nodes[DEV_ID_COUNT] = {
    [DEV_ID_] = {
        .name       = "",
        .label      = "",
        .compatible = "espressif,esp32-s3-devkitc-1",
        .path       = "/",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV__props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 0,
        .regs       = (const struct device_reg*)NULL,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_CPU_0] = {
        .name       = "cpu@0",
        .label      = "",
        .compatible = "espressif,xtensa-lx7",
        .path       = "/cpus/cpu@0",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_cpu_0_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_cpu_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_SOC] = {
        .name       = "soc",
        .label      = "soc",
        .compatible = "simple-bus",
        .path       = "/soc",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_soc_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 0,
        .regs       = (const struct device_reg*)NULL,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_GPIO_0] = {
        .name       = "gpio@0",
        .label      = "gpio",
        .compatible = "esp32,gpio",
        .path       = "/soc/gpio@0",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_gpio_0_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_gpio_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_GPIO_PIN_0] = {
        .name       = "gpio-pin@0",
        .label      = "gpio_pin",
        .compatible = "heterogeneous,gpio",
        .path       = "/soc/gpio-pin@0",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpio_pin_0_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_gpio_pin_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_LED_0] = {
        .name       = "led@0",
        .label      = "ws2812",
        .compatible = "esp32,ws2812",
        .path       = "/soc/led@0",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 15,
        .props      = DEV_led_0_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_led_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_SPI_0] = {
        .name       = "spi@0",
        .label      = "spi1",
        .compatible = "esp32,spi",
        .path       = "/soc/spi@0",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 9,
        .props      = DEV_spi_0_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_spi_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_FFT_0] = {
        .name       = "fft@0",
        .label      = "fft_slave",
        .compatible = "heterogeneous,fft-spi-slave",
        .path       = "/soc/spi@0/fft@0",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 5,
        .props      = DEV_fft_0_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_fft_0_deps,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_fft_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_SPI_1] = {
        .name       = "spi@1",
        .label      = "spi2",
        .compatible = "esp32,spi-master",
        .path       = "/soc/spi@1",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 9,
        .props      = DEV_spi_1_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_spi_1_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_W25Q64_0] = {
        .name       = "w25q64@0",
        .label      = "w25q64_master",
        .compatible = "heterogeneous,w25q64-master",
        .path       = "/soc/spi@1/w25q64@0",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 5,
        .props      = DEV_w25q64_0_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_w25q64_0_deps,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_w25q64_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
};

const struct device_node* board_node_get(device_id_t id) {
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;
    return &s_nodes[id];
}

int board_dev_count(void) { return DEV_ID_COUNT; }

device_id_t board_dev_find(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < DEV_ID_COUNT; i++) {
        if (strcmp(s_nodes[i].name, name) == 0)
            return (device_id_t)i;
    }
    return -1;
}

device_id_t board_dev_find_by_compat(const char* compatible) {
    if (!compatible) return -1;
    for (int i = 0; i < DEV_ID_COUNT; i++) {
        if (s_nodes[i].compatible[0] &&
            strcmp(s_nodes[i].compatible, compatible) == 0)
            return (device_id_t)i;
    }
    return -1;
}

device_id_t board_dev_find_by_label(const char* label) {
    if (!label || !label[0]) return -1;
    for (int i = 0; i < DEV_ID_COUNT; i++) {
        if (s_nodes[i].label[0] &&
            strcmp(s_nodes[i].label, label) == 0)
            return (device_id_t)i;
    }
    return -1;
}
