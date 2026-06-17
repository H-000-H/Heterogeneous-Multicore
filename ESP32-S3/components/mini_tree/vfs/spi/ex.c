#include "spi_vfs.h"
#include "hal_spi.h"
#include "hal_spi_bus_host.h"
#include "hal_spi_bus.h"
#include "device.h"
#include "driver.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "board_config.h"
#include "dev_lifecycle.h"
#include "compiler_compat.h"
#include "osal.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "system_log.h"

#define SPI_DEVICE_COUNT DTC_GEN_COUNT_ESP32_SPI_DEVICE

struct spi_vfs_priv
{
    struct file_operations ops;
    struct hal_spi_ctx     hal;
    struct osal_mutex*     io_mutex;
    int                    pool_idx;
};

static struct spi_vfs_priv s_spi_vfs_pool[SPI_DEVICE_COUNT];
static uint8_t s_spi_vfs_used[SPI_DEVICE_COUNT];
static uint8_t s_spi_mutex_storage[SPI_DEVICE_COUNT][OSAL_MUTEX_POOL_SIZE];

static const char* const kTag = "spi_vfs";