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
    {"model", "WCHDTS_UART_HOSTID    0CH32V307"},
};

/* /cpus/cpu@0 */
static const struct device_property DEV_cpu_0_props[] = {
    {"device_type", "cpu"},
    {"reg", "0x0"},
    {"clock-frequency", "0x8954400"},
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

/* /soc/gpios-pin@0 */
static const struct device_property DEV_gpios_pin_0_props[] = {
    {"reg", "0x0"},
    {"gpio-port", "0x0"},
    {"gpio-pin", "0x0"},
    {"gpio-mode", "0x1"},
    {"gpio-pull", "0x0"},
    {"gpio-intr", ""},
    {"default-level", "0x0"},
};

/* /soc/spi@0 */
static const struct device_property DEV_spi_0_props[] = {
    {"reg", "0x0"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"host-id", "0x0"},
    {"dma-chan", "-0x1"},
    {"mosi-port", "0x0"},
    {"mosi-pin", "0x7"},
    {"miso-port", "0x0"},
    {"miso-pin", "0x6"},
    {"sclk-port", "0x0"},
    {"sclk-pin", "0x5"},
};

/* /soc/spi@0/w25q64@0 */
static const struct device_property DEV_w25q64_0_props[] = {
    {"reg", "0x0"},
    {"spi-mode", "0x0"},
    {"spi-max-frequency", "0x989680"},
    {"queue-size", "0x4"},
    {"cs-port", "0x0"},
    {"cs-pin", "0x4"},
};

/* /soc/uart@0 */
static const struct device_property DEV_uart_0_props[] = {
    {"reg", "0x0"},
    {"host-id", "0x0"},
    {"tx-port", "0x0"},
    {"tx-pin", "0x9"},
    {"rx-port", "0x0"},
    {"rx-pin", "0xa"},
    {"uart-trans-baund", "0x1c200"},
    {"data-bit", "0x8"},
    {"stop-bit", "0x0"},
    {"parity", "0x0"},
};

/* ===== 依赖表 ===== */

static const device_id_t DEV_w25q64_0_deps[] = {
    DEV_ID_SPI_0,
};

/* ===== reg 分组表 ===== */

static const uint32_t DEV_cpu_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_gpio_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_gpios_pin_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_spi_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_w25q64_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_uart_0_REG_DATA[] = {
    0x0,
};
static const struct device_reg DEV_cpu_0_REGS[] = {
    { .addr = &DEV_cpu_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_gpio_0_REGS[] = {
    { .addr = &DEV_gpio_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_gpios_pin_0_REGS[] = {
    { .addr = &DEV_gpios_pin_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_spi_0_REGS[] = {
    { .addr = &DEV_spi_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_w25q64_0_REGS[] = {
    { .addr = &DEV_w25q64_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_uart_0_REGS[] = {
    { .addr = &DEV_uart_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

/* ===== irq 表 ===== */

/* ===== 主节点表 ===== */
static const struct device_node s_nodes[DEV_ID_COUNT] = {
    [DEV_ID_] = {
        .name       = "",
        .label      = "",
        .compatible = "wch,ch32v307-mini-tree",
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
        .compatible = "wch,riscv",
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
        .compatible = "ch32,gpio",
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
    [DEV_ID_GPIOS_PIN_0] = {
        .name       = "gpios-pin@0",
        .label      = "gpios_pin",
        .compatible = "heterogeneous,gpios",
        .path       = "/soc/gpios-pin@0",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpios_pin_0_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_gpios_pin_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_SPI_0] = {
        .name       = "spi@0",
        .label      = "spi1",
        .compatible = "ch32,spi-master",
        .path       = "/soc/spi@0",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 11,
        .props      = DEV_spi_0_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_spi_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_W25Q64_0] = {
        .name       = "w25q64@0",
        .label      = "w25q64_master",
        .compatible = "heterogeneous,w25q64-master",
        .path       = "/soc/spi@0/w25q64@0",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_w25q64_0_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_w25q64_0_deps,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_w25q64_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_UART_0] = {
        .name       = "uart@0",
        .label      = "uart1",
        .compatible = "ch32,uart1",
        .path       = "/soc/uart@0",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 10,
        .props      = DEV_uart_0_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_uart_0_REGS,
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
