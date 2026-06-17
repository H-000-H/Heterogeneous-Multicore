#ifndef VFS_H
#define VFS_H

/* struct file_operations 已由 device.h 统一提供 */

#include <errno.h>

#ifndef EPROBE_DEFER
#define EPROBE_DEFER 517
#endif
#ifndef EHWPOISON
#define EHWPOISON 134
#endif

/* ── Linux 内核风格: 返回负 errno ── */
#define VFS_OK            0
#define VFS_ERR_INVAL     (-EINVAL)       /* 无效参数 */
#define VFS_ERR_NOMEM     (-ENOMEM)       /* 内存不足 */
#define VFS_ERR_IO        (-EIO)          /* 物理 IO 错误 */
#define VFS_ERR_BUSY      (-EBUSY)        /* 设备忙 */
#define VFS_ERR_AGAIN     (-EAGAIN)       /* 重试 */
#define VFS_ERR_NOSPC     (-ENOSPC)       /* 无剩余空间/通道 */
#define VFS_ERR_TIMEOUT   (-ETIMEDOUT)    /* 锁获取/操作超时 */
#define VFS_ERR_HW_FATAL  (-EHWPOISON)    /* 硬件物理故障, 不可恢复 */
#define VFS_ERR_DEFER     (-EPROBE_DEFER) /* 依赖未就绪, 稍后重试 */
#define VFS_ERR_NODEV     (-ENODEV)       /* 设备已拆除或不存在 */

#endif /* VFS_H */
