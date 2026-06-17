#ifndef COMPILER_COMPAT_H
#define COMPILER_COMPAT_H

#include <stddef.h>

/* 统一类型获取宏 */
#ifdef __cplusplus
#define TYPEOF(expr) decltype(expr)
#else
#define TYPEOF(expr) typeof(expr)
#endif

/* ── 编译器兼容抽象层 ──
 *
 * 统一 GCC / Clang / ARMCLANG (AC6) 的 __attribute__ 与内置函数差异。
 * Kconfig 选项见 Compiler Compatibility 菜单 (tools/genconfig.py)。
 */

/* ── Kconfig ── 未生成 config.h 时回退为全部启用 */
#ifndef COMPAT_HAVE_KCONFIG
#define COMPAT_HAVE_KCONFIG 0
#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#undef COMPAT_HAVE_KCONFIG
#define COMPAT_HAVE_KCONFIG 1
#endif
#endif
#endif

#define COMPAT_CFG_ENABLED(sym) \
    ((!COMPAT_HAVE_KCONFIG) || defined(CONFIG_##sym))

#define COMPAT_GNU_EXT_OK \
    (COMPAT_CFG_ENABLED(COMPILER_GNU_EXTENSIONS) && \
     (defined(__GNUC__) || defined(__clang__)) && \
     !defined(COMPAT_ARM_COMPILER_5))

#define COMPAT_WUR_ATTR_OK \
    (COMPAT_CFG_ENABLED(COMPILER_WARN_UNUSED_RESULT) && COMPAT_GNU_EXT_OK)

#define COMPAT_ALIGNED(n) __attribute__((aligned(n)))
#define COMPAT_WEAK __attribute__((weak))
#define COMPAT_TRAP()     __builtin_trap()
#define COMPAT_CTZ(x)     __builtin_ctz(x)
#define COMPAT_PACKED     __attribute__((packed))

/* Keil 5 / ARM Compiler 5 (armcc): __CC_ARM 且 __ARMCC_VERSION < 6000000
 * Keil 6 (armclang): __clang__ + __ARMCC_VERSION >= 6000000，不在此列 */
#if defined(__CC_ARM) && (!defined(__ARMCC_VERSION) || (__ARMCC_VERSION < 6000000))
#define COMPAT_ARM_COMPILER_5 1
#endif

#if COMPAT_WUR_ATTR_OK
#define COMPAT_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define COMPAT_WARN_UNUSED_RESULT
#endif

#ifdef __cplusplus
#if COMPAT_WUR_ATTR_OK
#define COMPAT_NODISCARD [[nodiscard]]
#else
#define COMPAT_NODISCARD
#endif
#else
#define COMPAT_NODISCARD COMPAT_WARN_UNUSED_RESULT
#endif

/* 显式丢弃 warn_unused_result 标注函数的返回值 (GCC 14+ 下 (void)expr 无效) */
#if COMPAT_WUR_ATTR_OK
#define COMPAT_IGNORE_RESULT(expr) \
    do { \
        TYPEOF(expr) _compat_ign_ __attribute__((unused)) = (expr); \
    } while (0)
#else
#define COMPAT_IGNORE_RESULT(expr) ((void)(expr))
#endif

/* Linux 风格 container_of */
#if COMPAT_GNU_EXT_OK
#undef container_of
#define container_of(ptr, type, member) ({                         \
    const TYPEOF(((type *)0)->member) *__mptr = (ptr);             \
    (type *)((char *)__mptr - offsetof(type, member));             \
})
#else
#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif
#endif

/* ── RAM_EXEC: 将函数置于 RAM 执行 ──
 *
 * 将高频中断或控制环函数放入 .ram_code 段, 在启动时由用户 linker script
 * 搬运到 TCM 或 SRAM, 消除 Flash Cache Miss 导致的延迟抖动。
 *
 * 用法:
 *   RAM_EXEC void motor_foc_isr(void) { ... }
 *
 * 确认芯片有足够的 RAM/ITCM, 并在 .ld 中添加:
 *
 *   .ram_code : {
 *       *(.ram_code*)
 *   } > ITCM AT> FLASH
 *
 *   _sram_code = ADDR(.ram_code);
 *   _eram_code = ADDR(.ram_code) + SIZEOF(.ram_code);
 *   _ram_code_flash = LOADADDR(.ram_code);
 *
 *   startup 中: memcpy(&_sram_code, &_ram_code_flash, _eram_code - _sram_code);
 */
#define RAM_EXEC  __attribute__((section(".ram_code")))

#endif /* COMPILER_COMPAT_H */
