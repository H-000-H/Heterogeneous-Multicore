#!/usr/bin/env python3
"""Convert typedef struct to plain struct in mini_tree (excludes third-party lib/)."""

import re
import sys
from pathlib import Path

TYPE_MAP = [
    ("prod_log_persist_t", "struct prod_log_persist"),
    ("prod_log_entry_t", "struct prod_log_entry"),
    ("board_task_config_t", "struct board_task_config"),
    ("ws2812_color_t", "struct ws2812_color"),
    ("dev_lifecycle_t", "struct dev_lifecycle"),
    ("vfs_storage_geometry_t", "struct vfs_storage_geometry"),
    ("vfs_storage_erase_arg_t", "struct vfs_storage_erase_arg"),
    ("vfs_storage_wp_arg_t", "struct vfs_storage_wp_arg"),
    ("osal_spinlock_t", "struct osal_spinlock"),
    ("osal_mutex_t", "struct osal_mutex"),
    ("osal_sem_t", "struct osal_sem"),
    ("bh_queue_t", "struct bh_queue"),
    ("bh_work_t", "struct bh_work"),
    ("bh_bare_t", "struct bh_bare"),
    ("bh_os_t", "struct bh_os"),
    ("bp_config_t", "struct bp_config"),
    ("hal_spi_bus_config_t", "struct hal_spi_bus_config"),
    ("hal_spi_device_config_t", "struct hal_spi_device_config"),
    ("hal_spi_bus_t", "struct hal_spi_bus"),
    ("hal_i2c_config_t", "struct hal_i2c_config"),
    ("hal_i2c_bus_t", "struct hal_i2c_bus"),
    ("hal_pulse_ws2812_hw_t", "struct hal_pulse_ws2812_hw"),
    ("hal_pwm_config_t", "struct hal_pwm_config"),
    ("hal_pwm_channel_t", "struct hal_pwm_channel"),
    ("hal_adc_config_t", "struct hal_adc_config"),
    ("hal_adc_t", "struct hal_adc"),
    ("hal_usb_config_t", "struct hal_usb_config"),
    ("hal_usb_ep_config_t", "struct hal_usb_ep_config"),
    ("hal_usb_dev_t", "struct hal_usb_dev"),
    ("hal_uart_config_t", "struct hal_uart_config"),
    ("hal_uart_t", "struct hal_uart"),
    ("hal_timer_config_t", "struct hal_timer_config"),
    ("hal_timer_channel_config_t", "struct hal_timer_channel_config"),
    ("hal_timer_t", "struct hal_timer"),
    ("hal_sdio_config_t", "struct hal_sdio_config"),
    ("hal_sdio_info_t", "struct hal_sdio_info"),
    ("hal_sdio_t", "struct hal_sdio"),
    ("hal_rtc_time_t", "struct hal_rtc_time"),
    ("hal_rtc_config_t", "struct hal_rtc_config"),
    ("hal_rtc_t", "struct hal_rtc"),
    ("hal_gpio_config_t", "struct hal_gpio_config"),
    ("hal_i2s_config_t", "struct hal_i2s_config"),
    ("hal_i2s_bus_t", "struct hal_i2s_bus"),
    ("hal_dma_config_t", "struct hal_dma_config"),
    ("hal_dma_chan_t", "struct hal_dma_chan"),
    ("hal_dac_config_t", "struct hal_dac_config"),
    ("hal_dac_t", "struct hal_dac"),
    ("hal_can_msg_t", "struct hal_can_msg"),
    ("hal_can_config_t", "struct hal_can_config"),
    ("hal_can_filter_t", "struct hal_can_filter"),
    ("hal_can_t", "struct hal_can"),
    ("storage_geometry_t", "struct storage_geometry"),
    ("storage_erase_arg_t", "struct storage_erase_arg"),
    ("storage_wp_arg_t", "struct storage_wp_arg"),
    ("spi_read_arg_t", "struct spi_read_arg"),
    ("i2c_rw_arg_t", "struct i2c_rw_arg"),
    ("i2c_wr_arg_t", "struct i2c_wr_arg"),
    ("uart_read_arg_t", "struct uart_read_arg"),
    ("i2s_write_arg_t", "struct i2s_write_arg"),
    ("adc_read_arg_t", "struct adc_read_arg"),
    ("gpio_isr_arg_t", "struct gpio_isr_arg"),
    ("gpio_level_arg_t", "struct gpio_level_arg"),
    ("FIFO_Type_Def", "struct fifo_spsc"),
    ("safety_pin_t", "struct safety_pin"),
    ("cs_entry_t", "struct cs_entry"),
    ("bp_t", "struct bp_pool"),
]

HEADER_STRUCT_FIXES = {
    "osal/include/osal.h": [
        (r"typedef struct osal_mutex osal_mutex_t;\r?\n", ""),
        (r"typedef struct osal_spinlock osal_spinlock_t;\r?\n", ""),
        (r"typedef struct osal_sem osal_sem_t;\r?\n", ""),
    ],
    "board/include/dev_lifecycle.h": [
        (r"typedef struct dev_lifecycle \{", "struct dev_lifecycle {"),
        (r"\} dev_lifecycle_t;", "};"),
    ],
    "core/include/bh/bh.h": [
        (r"typedef struct bh_work \{", "struct bh_work {"),
        (r"\} bh_work_t;", "};"),
        (r"typedef struct bh_queue \{", "struct bh_queue {"),
        (r"\} bh_queue_t;", "};"),
    ],
    "core/include/bh/bh_os.h": [
        (r"typedef struct bh_os \{", "struct bh_os {"),
        (r"\} bh_os_t;", "};"),
    ],
    "core/include/bh/bh_bare.h": [
        (r"typedef struct bh_bare \{", "struct bh_bare {"),
        (r"\} bh_bare_t;", "};"),
    ],
    "drivers/ws2812/ws2812_drv.h": [
        (r"typedef struct\r?\n\{", "struct ws2812_color\r\n{"),
        (r"\} ws2812_color_t;", "};"),
    ],
    "core/include/buffer_pool.h": [
        (r"typedef struct\r?\n\{", "struct bp_config\r\n{", 1),
        (r"\} bp_config_t;", "};"),
        (r"typedef struct bp_pool bp_t;\r?\n\r?\n", "struct bp_pool;\r\n\r\n"),
    ],
    "core/include/production_log.h": [
        (r"typedef struct\r?\n\{", "struct prod_log_entry\r\n{"),
        (r"\} prod_log_entry_t;", "};"),
    ],
    "board/include/task_config.h": [
        (r"typedef struct\r?\n\{", "struct board_task_config\r\n{"),
        (r"\} board_task_config_t;", "};"),
    ],
    "algorithm/buffer/m_buffer.h": [
        (r"typedef struct\r?\n\{", "struct fifo_spsc\r\n{"),
        (r"\} FIFO_Type_Def;", "};"),
    ],
    "board/src/board_driver.c": [
        (r"typedef struct\r?\n\{", "struct safety_pin\r\n{"),
        (r"\} safety_pin_t;", "};"),
    ],
    "board/src/config_store.c": [
        (r"typedef struct\r?\n\{", "struct cs_entry\r\n{"),
        (r"\} cs_entry_t;", "};"),
    ],
    "core/src/production_log.c": [
        (r"typedef struct\r?\n\{", "struct prod_log_persist\r\n{"),
        (r"\} prod_log_persist_t;", "};"),
    ],
    "board/include/vfs_storage.h": [
        (r"typedef struct\r?\n\{", "struct vfs_storage_geometry\r\n{", 1),
        (r"\} vfs_storage_geometry_t;", "};"),
        (r"typedef struct\r?\n\{", "struct vfs_storage_erase_arg\r\n{", 1),
        (r"\} vfs_storage_erase_arg_t;", "};"),
        (r"typedef struct\r?\n\{", "struct vfs_storage_wp_arg\r\n{", 1),
        (r"\} vfs_storage_wp_arg_t;", "};"),
    ],
    "board/include/vfs_gpio.h": [
        (r"typedef struct\r?\n\{", "struct hal_gpio_config\r\n{", 1),
        (r"\} hal_gpio_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct gpio_isr_arg\r\n{", 1),
        (r"\} gpio_isr_arg_t;", "};"),
        (r"typedef struct\r?\n\{", "struct gpio_level_arg\r\n{", 1),
        (r"\} gpio_level_arg_t;", "};"),
    ],
    "board/include/vfs_adc.h": [
        (r"typedef struct\r?\n\{", "struct adc_read_arg\r\n{"),
        (r"\} adc_read_arg_t;", "};"),
    ],
    "hal_bus/spi/hal_spi_bus.h": [
        (r"typedef struct hal_spi_bus hal_spi_bus_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_spi_bus_config\r\n{", 1),
        (r"\} hal_spi_bus_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct hal_spi_device_config\r\n{", 1),
        (r"\} hal_spi_device_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct spi_read_arg\r\n{", 1),
        (r"\} spi_read_arg_t;", "};"),
    ],
    "hal_bus/i2c/hal_i2c_bus.h": [
        (r"typedef struct hal_i2c_bus hal_i2c_bus_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_i2c_config\r\n{", 1),
        (r"\} hal_i2c_config_t;", "};"),
    ],
    "hal_bus/i2c/hal_i2c.h": [
        (r"typedef struct\r?\n\{", "struct i2c_rw_arg\r\n{", 1),
        (r"\} i2c_rw_arg_t;", "};"),
        (r"typedef struct\r?\n\{", "struct i2c_wr_arg\r\n{", 1),
        (r"\} i2c_wr_arg_t;", "};"),
    ],
    "hal_if/pulse/hal_pulse_engine.h": [
        (r"typedef struct\r?\n\{", "struct hal_pulse_ws2812_hw\r\n{"),
        (r"\} hal_pulse_ws2812_hw_t;", "};"),
    ],
    "hal_if/storage/hal_storage.h": [
        (r"typedef struct\r?\n\{", "struct storage_geometry\r\n{", 1),
        (r"\} storage_geometry_t;", "};"),
        (r"typedef struct\r?\n\{", "struct storage_erase_arg\r\n{", 1),
        (r"\} storage_erase_arg_t;", "};"),
        (r"typedef struct\r?\n\{", "struct storage_wp_arg\r\n{", 1),
        (r"\} storage_wp_arg_t;", "};"),
    ],
    "hal_if/pwm/hal_pwm.h": [
        (r"typedef struct hal_pwm_channel hal_pwm_channel_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_pwm_config\r\n{"),
        (r"\} hal_pwm_config_t;", "};"),
    ],
    "hal_if/analog/hal_adc.h": [
        (r"typedef struct hal_adc hal_adc_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_adc_config\r\n{", 1),
        (r"\} hal_adc_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct adc_read_arg\r\n{", 1),
        (r"\} adc_read_arg_t;", "};"),
    ],
    "hal_bus/usb/hal_usb_bus.h": [
        (r"typedef struct hal_usb_bus hal_usb_bus_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_usb_config\r\n{", 1),
        (r"\} hal_usb_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct hal_usb_ep_config\r\n{", 1),
        (r"\} hal_usb_ep_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct hal_usb_bus\r\n{", 1),
        (r"\};\r?\n\r?\nvoid hal_usb_bus_init_struct", "};\r\n\r\nvoid hal_usb_bus_init_struct"),
    ],
    "hal_if/uart/hal_uart.h": [
        (r"typedef struct hal_uart hal_uart_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_uart_config\r\n{", 1),
        (r"\} hal_uart_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct uart_read_arg\r\n{", 1),
        (r"\} uart_read_arg_t;", "};"),
    ],
    "hal_if/system/hal_timer.h": [
        (r"typedef struct hal_timer hal_timer_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_timer_config\r\n{", 1),
        (r"\} hal_timer_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct hal_timer_channel_config\r\n{", 1),
        (r"\} hal_timer_channel_config_t;", "};"),
    ],
    "hal_if/system/hal_sdio.h": [
        (r"typedef struct hal_sdio hal_sdio_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_sdio_config\r\n{", 1),
        (r"\} hal_sdio_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct hal_sdio_info\r\n{", 1),
        (r"\} hal_sdio_info_t;", "};"),
    ],
    "hal_if/system/hal_rtc.h": [
        (r"typedef struct hal_rtc hal_rtc_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_rtc_time\r\n{", 1),
        (r"\} hal_rtc_time_t;", "};"),
        (r"typedef struct\r?\n\{", "struct hal_rtc_config\r\n{", 1),
        (r"\} hal_rtc_config_t;", "};"),
    ],
    "hal_if/gpio/hal_gpio.h": [
        (r"typedef struct\r?\n\{", "struct hal_gpio_config\r\n{", 1),
        (r"\} hal_gpio_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct gpio_isr_arg\r\n{", 1),
        (r"\} gpio_isr_arg_t;", "};"),
        (r"typedef struct\r?\n\{", "struct gpio_level_arg\r\n{", 1),
        (r"\} gpio_level_arg_t;", "};"),
    ],
    "hal_bus/i2s/hal_i2s_bus.h": [
        (r"typedef struct hal_i2s_bus hal_i2s_bus_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_i2s_config\r\n{", 1),
        (r"\} hal_i2s_config_t;", "};"),
    ],
    "hal_bus/i2s/hal_i2s.h": [
        (r"typedef struct\r?\n\{", "struct i2s_write_arg\r\n{", 1),
        (r"\} i2s_write_arg_t;", "};"),
    ],
    "hal_if/system/hal_dma.h": [
        (r"typedef struct hal_dma_chan hal_dma_chan_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_dma_config\r\n{"),
        (r"\} hal_dma_config_t;", "};"),
    ],
    "hal_if/analog/hal_dac.h": [
        (r"typedef struct hal_dac hal_dac_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_dac_config\r\n{"),
        (r"\} hal_dac_config_t;", "};"),
    ],
    "hal_bus/can/hal_can_bus.h": [
        (r"typedef struct hal_can_bus hal_can_bus_t;\r?\n\r?\n", ""),
        (r"typedef struct\r?\n\{", "struct hal_can_msg\r\n{", 1),
        (r"\} hal_can_msg_t;", "};"),
        (r"typedef struct\r?\n\{", "struct hal_can_config\r\n{", 1),
        (r"\} hal_can_config_t;", "};"),
        (r"typedef struct\r?\n\{", "struct hal_can_filter\r\n{", 1),
        (r"\} hal_can_filter_t;", "};"),
    ],
}

SKIP_PARTS = {"/lib/", "\\lib\\", "/Drivers/", "\\Drivers\\"}


def should_skip(path: Path) -> bool:
    s = str(path)
    return any(part in s for part in SKIP_PARTS)


def convert_tree(root: Path) -> list[str]:
    exts = {".c", ".h", ".cpp", ".hpp"}
    changed = []
    for path in root.rglob("*"):
        if path.suffix not in exts or should_skip(path):
            continue
        rel = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8")
        orig = text
        if rel in HEADER_STRUCT_FIXES:
            for item in HEADER_STRUCT_FIXES[rel]:
                if len(item) == 3:
                    pat, rep, count = item
                    text = re.sub(pat, rep, text, count=count)
                else:
                    pat, rep = item
                    text = re.sub(pat, rep, text)
        for old, new in TYPE_MAP:
            text = re.sub(r"\b" + re.escape(old) + r"\b", new, text)
        if text != orig:
            path.write_text(text, encoding="utf-8")
            changed.append(rel)
    return changed


def main() -> int:
    targets = sys.argv[1:] or [
        r"d:\can_project\CH32V307\mini_tree",
        r"d:\can_project\STM32F407ZGT6\mini_tree",
    ]
    for target in targets:
        root = Path(target)
        print(f"=== {root} ===")
        changed = convert_tree(root)
        print(f"Updated {len(changed)} files")
        leftover = []
        for path in root.rglob("*"):
            if path.suffix not in {".c", ".h", ".cpp", ".hpp"} or should_skip(path):
                continue
            if "typedef struct" in path.read_text(encoding="utf-8"):
                leftover.append(str(path.relative_to(root)))
        if leftover:
            print("Remaining typedef struct:")
            for item in sorted(leftover)[:20]:
                print(" ", item)
            if len(leftover) > 20:
                print(f"  ... and {len(leftover) - 20} more")
        else:
            print("No typedef struct left in mini_tree (excluding lib/)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
