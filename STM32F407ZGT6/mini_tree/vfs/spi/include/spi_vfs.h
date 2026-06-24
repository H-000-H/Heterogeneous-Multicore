#ifndef SPI_VFS_H
#define SPI_VFS_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct device;
struct bus_device;

/* VFS ioctl 命令 (namespace 0x100, 命令在 base 内递增) */
#define SPI_CMD_BASE             COMPAT_MAGIC(SPI)
#define SPI_CMD_DEINIT           SPI_CMD_BASE+0x01
#define SPI_CMD_READ             SPI_CMD_BASE+0x02
#define SPI_CMD_QUEUE_TX         SPI_CMD_BASE+0x03  /* write_top_half: 只入队 */
#define SPI_CMD_GET_TRANS_RESULT SPI_CMD_BASE+0x04  /* 下半部: 等待 queue 完成 */
#define SPI_CMD_TRANSFER         SPI_CMD_BASE+0x05  /* Master 全双工 */

#ifndef SPI_VFS_PUBLIC_POISON_DEINIT
#define SPI_VFS_PUBLIC_POISON_DEINIT 1
#endif
#if SPI_VFS_PUBLIC_POISON_DEINIT
#ifdef __GNUC__
#pragma GCC poison SPI_CMD_DEINIT
#endif
#endif

struct spi_read_arg
{
    uint8_t* data;
    size_t len;
};

struct spi_queue_arg
{
    const uint8_t* data;
    size_t len;
};

struct spi_trans_result_arg
{
    uint8_t* data;
    size_t len;
    size_t* trans_len;
};

struct spi_transfer_arg
{
    const uint8_t* tx;
    uint8_t*       rx;
    size_t         len;
};

#ifdef __cplusplus
}
#endif

#endif /* SPI_VFS_H */
