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
    {"model", "STMicro STM32F407ZGT6"},
};

/* /cpus/cpu@0 */
static const struct device_property DEV_cpu_0_props[] = {
    {"device_type", "cpu"},
    {"reg", "0x0"},
    {"clock-frequency", "0xa037a00"},
};

/* /soc */
static const struct device_property DEV_soc_props[] = {
    {"ranges", "true"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
};

/* /soc/spi@0 */
static const struct device_property DEV_spi_0_props[] = {
    {"reg", "0x0"},
    {"host-id", "0x1"},
    {"dma-chan", "-0x1"},
};

/* /soc/spi@0/device@0 */
static const struct device_property DEV_device_0_props[] = {
    {"reg", "0x0"},
    {"spi-mode", "0x0"},
    {"spi-max-frequency", "0x989680"},
    {"queue-size", "0x4"},
};

/* ===== 依赖表 ===== */

static const device_id_t DEV_device_0_deps[] = {
    DEV_ID_SPI_0,
};

/* ===== reg 分组表 ===== */

static const uint32_t DEV_cpu_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_spi_0_REG_DATA[] = {
    0x0,
};
static const uint32_t DEV_device_0_REG_DATA[] = {
    0x0,
};
static const struct device_reg DEV_cpu_0_REGS[] = {
    { .addr = &DEV_cpu_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_spi_0_REGS[] = {
    { .addr = &DEV_spi_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const struct device_reg DEV_device_0_REGS[] = {
    { .addr = &DEV_device_0_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

/* ===== irq 表 ===== */

/* ===== 主节点表 ===== */
static const struct device_node s_nodes[DEV_ID_COUNT] = {
    [DEV_ID_] = {
        .name       = "",
        .label      = "",
        .compatible = "st,stm32f407zgt6",
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
        .compatible = "arm,cortex-m4",
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
    [DEV_ID_SPI_0] = {
        .name       = "spi@0",
        .label      = "spi1",
        .compatible = "stm32,spi-host",
        .path       = "/soc/spi@0",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_spi_0_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_spi_0_REGS,
        .irq_count  = 0,
        .irqs       = (const struct device_irq*)NULL,
    },
    [DEV_ID_DEVICE_0] = {
        .name       = "device@0",
        .label      = "spi_dev0",
        .compatible = "stm32,spi-device",
        .path       = "/soc/spi@0/device@0",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 4,
        .props      = DEV_device_0_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_device_0_deps,
        .reg_count  = 1,
        .regs       = (const struct device_reg*)DEV_device_0_REGS,
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
