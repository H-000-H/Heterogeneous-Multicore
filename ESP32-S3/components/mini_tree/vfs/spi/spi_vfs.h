#ifndef SPI_VFS_H
#define SPI_VFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct hal_spi_bus;

/* VFS ioctl 命令 */
#define SPI_CMD_DEINIT           0x40
#define SPI_CMD_READ             0x41
#define SPI_CMD_QUEUE_TX         0x42  /* write_top_half: 只入队, 等主机 */
#define SPI_CMD_GET_TRANS_RESULT 0x43  /* 下半部: 等待 queue 的事务完成 */

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

struct hal_spi_bus* device_get_spi_bus(struct device* dev);

#ifdef __cplusplus
}
#endif

#endif /* SPI_VFS_H */
