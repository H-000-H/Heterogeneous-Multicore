#ifndef W25Q64_DRV_H
#define W25Q64_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ── 容量 / 几何 (应用层可引用) ── */
#define W25Q64_PAGE_SIZE             256U
#define W25Q64_SECTOR_SIZE           4096U
#define W25Q64_BLOCK_SIZE_32K        32768U
#define W25Q64_BLOCK_SIZE_64K        65536U
#define W25Q64_FLASH_SIZE            (8U * 1024U * 1024U)
#define W25Q64_JEDEC_ID_LEN          3U

/* W25Q64JV 典型 JEDEC ID (Winbond) */
#define W25Q64_JEDEC_MANUFACTURER    0xEFU
#define W25Q64_JEDEC_MEMORY_TYPE     0x40U
#define W25Q64_JEDEC_CAPACITY        0x17U

/*
 * 应用层用法 (Linux MTD 风格):
 *   device_open(dev, NULL);
 *   device_ioctl(dev, W25Q64_CMD_SEEK, &offset, sizeof(offset), ...);
 *   device_read(dev, buf, len, ...);   // 从当前偏移读, 并递增 f_pos
 *   device_write(dev, buf, len, ...);  // 从当前偏移写 (需先擦除)
 *   device_ioctl(dev, W25Q64_CMD_SECTOR_ERASE, &addr, sizeof(addr), ...);
 *   device_ioctl(dev, W25Q64_CMD_READ_JEDEC_ID, &jedec, sizeof(jedec), ...);
 *   device_close(dev);
 */

/* ── VFS ioctl (控制类, 数据走 read/write) ── */
#define W25Q64_CMD_BASE              (COMPAT_MAGIC(SPI) + 0x80)
#define W25Q64_CMD_SEEK              (W25Q64_CMD_BASE + 0x01)
#define W25Q64_CMD_SECTOR_ERASE      (W25Q64_CMD_BASE + 0x02)
#define W25Q64_CMD_READ_JEDEC_ID     (W25Q64_CMD_BASE + 0x03)

struct w25q64_jedec_arg
{
    uint8_t id[W25Q64_JEDEC_ID_LEN];
};

static inline int w25q64_jedec_match_w25q64jv(const uint8_t id[W25Q64_JEDEC_ID_LEN])
{
    return id && id[0] == W25Q64_JEDEC_MANUFACTURER &&
           id[1] == W25Q64_JEDEC_MEMORY_TYPE &&
           id[2] == W25Q64_JEDEC_CAPACITY;
}

#ifdef __cplusplus
}
#endif

#endif /* W25Q64_DRV_H */
