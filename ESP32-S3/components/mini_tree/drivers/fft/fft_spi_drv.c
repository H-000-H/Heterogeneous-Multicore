/*
 * FFT SPI 驱动 — 委托 spi_slave_vfs 实现（dtc-lite drivers/fft 目录占位）
 */
#include "spi_slave_vfs.h"

#include "device.h"
#include "driver.h"
#include "VFS.h"
#include "compiler_compat_poison.h"

int fft_spi_probe(struct device* dev)
{
    return spi_slave_vfs_probe(dev);
}

int fft_spi_remove(struct device* dev)
{
    return spi_slave_vfs_remove(dev);
}

