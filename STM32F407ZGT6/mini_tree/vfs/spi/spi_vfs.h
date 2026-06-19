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
struct hal_spi_bus;

/* VFS ioctl 命令 (每条总线 namespace 0x100, 命令在 base 内递增) */
#define SPI_CMD_BASE             COMPAT_MAGIC(SPI) + 0x40
#define SPI_CMD_DEINIT           SPI_CMD_BASE+0x01
#define SPI_CMD_READ             SPI_CMD_BASE+0x02
#define SPI_CMD_QUEUE_TX         SPI_CMD_BASE+0x03  /* write_top_half: 只入队, 等主机 */
#define SPI_CMD_GET_TRANS_RESULT SPI_CMD_BASE+0x04  /* 下半部: 等待 queue 的事务完成 */

struct spi_read_arg
{
    uint8_t* data;
    size_t len;
};

/* SPI_CMD_QUEUE_TX */
struct spi_queue_arg
{
    const uint8_t* data;
    size_t len;
};

/* SPI_CMD_GET_TRANS_RESULT (trans_len 输出实际接收字节数, 可为 NULL) */
struct spi_trans_result_arg
{
    uint8_t* data;
    size_t len;
    size_t* trans_len;
};

int device_get_spi_bus(struct device* dev, struct hal_spi_bus** out) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* SPI_VFS_H */

