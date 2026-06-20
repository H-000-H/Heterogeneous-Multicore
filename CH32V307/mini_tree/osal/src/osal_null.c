#ifdef CONFIG_OSAL_NULL

#define ALLOW_HEAP_ALLOC
#define ALLOW_STDIO_OUTPUT

#include "config.h"
#include "osal.h"
#include "board_config.h"
#include "compiler_compat.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "compiler_compat_poison.h"

/* ═══════════════════════════════════════════════════════════════════════════
 *  osal_null.c — 裸机适配层 (无 RTOS)
 *
 *  适用场景:
 *    1. 早期驱动移植 (先验证硬件, 后加 RTOS)
 *    2. 极简 MCU 项目 (无需多任务)
 *    3. Host 端单元测试 (配合模拟时钟)
 *
 *  约束:
 *    - 裸机无多任务, mutex/spinlock 为无操作
 *    - 队列基于静态环形缓冲区 + 忙等
 *    - osal_delay_ms 为 CPU 忙等 (精度取决于主频)
 *    - osal_task_create 固定返回失败
 *    - osal_in_isr 默认返回 0; 移植时可在中断入口/出口设置 s_isr_nest
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── 内部常量 ── */
#define OSAL_NULL_MAX_QUEUES  4       /* 最大同时存在的队列数 */
#define OSAL_NULL_QUEUE_BUF_SZ 4096   /* 单队列环形缓冲区最大字节数 */

/* ── 队列内部结构 ── */
struct osal_queue_obj
{
    uint8_t*       buf;         /* 环形缓冲区 (由 create 分配) */
    size_t         item_size;
    size_t         queue_len;
    size_t         buf_mask;    /* 环形掩码 (保证为 2^n - 1) */
    volatile size_t head;       /* 生产者写入位置 */
    volatile size_t tail;       /* 消费者读取位置 */
    bool           used;
};

/* ── 队列控制块池 ── */
static struct osal_queue_obj s_queues[OSAL_NULL_MAX_QUEUES];
static uint8_t s_queue_used[OSAL_NULL_MAX_QUEUES];

/* ── 互斥锁 (裸机单核, 无操作) ── */
struct osal_mutex
{
    osal_mutex_type_t type;
    int               dummy;
};
_Static_assert(sizeof(struct osal_mutex) <= OSAL_MUTEX_STORAGE_SIZE,
               "osal_null: OSAL_MUTEX_STORAGE_SIZE too small");

static int osal_mutex_init(struct osal_mutex* m, osal_mutex_type_t type)
{
    if (!m) return -1;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return -1;

    m->type = type;
    return 0;
}

static struct osal_mutex s_mutex_pool[OSAL_MUTEX_POOL_SIZE];
static uint8_t s_mutex_used[OSAL_MUTEX_POOL_SIZE];

/* ── 单调时钟 (由 SysTick 或定时器中断累加) ── */
static volatile uint32_t s_sys_tick_ms;

/* ── ISR 嵌套计数: 0 = 非中断上下文 ── */
static volatile int s_isr_nest;

/* ── 池操作 ── */

static int pool_claim(volatile uint8_t* used, size_t count)
{
    if (!used || count == 0) return -1;
    for (size_t i = 0; i < count; i++)
    {
        if (!used[i])
        {
            used[i] = 1;
            return (int)i;
        }
    }
    return -1;
}

static void pool_release(volatile uint8_t* used, size_t count, int idx)
{
    if (!used || idx < 0 || (size_t)idx >= count) return;
    used[idx] = 0;
}

/* ── 查找队列索引 ── */
static int queue_index_of(osal_queue_handle_t q)
{
    if (!q) return -1;
    for (int i = 0; i < OSAL_NULL_MAX_QUEUES; i++)
    {
        if (s_queue_used[i] && &s_queues[i] == (struct osal_queue_obj*)q)
        {
            return i;
        }
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  上下文检测
 * ═══════════════════════════════════════════════════════════════════════════ */

int osal_in_isr(void)
{
    return s_isr_nest > 0;
}

/*
 * 以下两个函数由移植层在中断入口/出口调用.
 * 典型用法 (startup_xxx.s 或 C 中断处理):
 *   void SysTick_Handler(void)
 {
 *       osal_null_isr_enter();
 *       // ... 中断处理 ...
 *       osal_null_isr_exit();
 *   }
 */
void osal_null_isr_enter(void)
{
    __atomic_add_fetch(&s_isr_nest, 1, __ATOMIC_RELAXED);
}
void osal_null_isr_exit(void)
{
    __atomic_sub_fetch(&s_isr_nest, 1, __ATOMIC_RELAXED);
}

/*
 * 系统滴答中断 (默认 1 ms) 应从 SysTick_Handler 调用.
 * 移植时在中断处理中加: osal_null_systick_handler();
 */
void osal_null_systick_handler(void)
{
    __atomic_add_fetch(&s_sys_tick_ms, 1, __ATOMIC_RELAXED);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Spinlock (裸机关中断)
 * ═══════════════════════════════════════════════════════════════════════════ */

struct osal_spinlock
{ volatile int locked; };

void osal_spinlock_init(struct osal_spinlock* lock)
{
    if (!lock) return;
    ((struct osal_spinlock*)lock)->locked = 0;
}

void osal_spinlock_lock(struct osal_spinlock* lock)
{
    if (!lock) return;
    /* 裸机单核: 无需真正自旋, 标记即可 */
    ((struct osal_spinlock*)lock)->locked = 1;
}

void osal_spinlock_unlock(struct osal_spinlock* lock)
{
    if (!lock) return;
    ((struct osal_spinlock*)lock)->locked = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  时间
 * ═══════════════════════════════════════════════════════════════════════════ */

uint32_t osal_time_ms(void)
{
    return __atomic_load_n(&s_sys_tick_ms, __ATOMIC_RELAXED);
}

void osal_delay_ms(uint32_t ms)
{
    if (ms == 0) return;
    uint32_t start = osal_time_ms();
    while ((osal_time_ms() - start) < ms)
    {
        /* 忙等 — 移植者可在循环中插入 WFI 以降低功耗 */
    }
}

osal_tick_t osal_ticks_from_ms(uint32_t ms)
{
    return ms;  /* 裸机默认 1 tick = 1 ms */
}

osal_tick_t osal_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAIT_FOREVER)
        return UINT32_MAX;
    return timeout_ms;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  内存
 * ═══════════════════════════════════════════════════════════════════════════ */

void* osal_calloc(size_t count, size_t size)
{
    void* ptr = calloc(count, size);
    if (!ptr)
    {
        ptr = NULL;
    }
    return ptr;
}

void osal_free(void* ptr)
{
    if (ptr)
    {
        free(ptr);
        ptr = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  互斥锁 (裸机单核, 均为无操作)
 * ═══════════════════════════════════════════════════════════════════════════ */

int osal_mutex_create_typed(struct osal_mutex** out, osal_mutex_type_t type)
{
    if (!out) return -1;
    if (osal_in_isr()) return -1;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return -1;
    *out = NULL;

    int idx = pool_claim(s_mutex_used, OSAL_MUTEX_POOL_SIZE);
    if (idx < 0) return -1;

    if (osal_mutex_init(&s_mutex_pool[idx], type) != 0)
    {
        pool_release(s_mutex_used, OSAL_MUTEX_POOL_SIZE, idx);
        return -1;
    }
    *out = (struct osal_mutex*)&s_mutex_pool[idx];
    return 0;
}

int osal_mutex_create_static_typed(struct osal_mutex** out, void* storage,
                                 size_t storage_size, osal_mutex_type_t type)
{
    if (!out || !storage || storage_size < sizeof(struct osal_mutex)) return -1;
    if (osal_in_isr()) return -1;
    if (type != OSAL_MUTEX_RECURSIVE && type != OSAL_MUTEX_PLAIN) return -1;

    struct osal_mutex* m = (struct osal_mutex*)storage;
    if (osal_mutex_init(m, type) != 0) return -1;
    *out = m;
    return 0;
}

int osal_mutex_create(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

int osal_mutex_create_static(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

int osal_mutex_create_recursive(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_RECURSIVE);
}

int osal_mutex_create_static_recursive(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_RECURSIVE);
}

int osal_mutex_create_plain(struct osal_mutex** out)
{
    return osal_mutex_create_typed(out, OSAL_MUTEX_PLAIN);
}

int osal_mutex_create_static_plain(struct osal_mutex** out, void* storage, size_t storage_size)
{
    return osal_mutex_create_static_typed(out, storage, storage_size, OSAL_MUTEX_PLAIN);
}

void osal_mutex_destroy(struct osal_mutex* mutex)
{
    if (!mutex) return;
    if (osal_in_isr()) return;
    for (int i = 0; i < OSAL_MUTEX_POOL_SIZE; i++)
    {
        if (&s_mutex_pool[i] == (struct osal_mutex*)mutex)
        {
            pool_release(s_mutex_used, OSAL_MUTEX_POOL_SIZE, i);
            break;
        }
    }
}

int osal_mutex_lock(struct osal_mutex* mutex, uint32_t timeout_ms)
{
    if (!mutex) return -1;
    if (osal_in_isr()) return -1;
    (void)timeout_ms;
    return 0;  /* 裸机单核, 无需同步 */
}

int osal_mutex_unlock(struct osal_mutex* mutex)
{
    if (!mutex) return -1;
    if (osal_in_isr()) return -1;
    return 0;
}

/* ── 池操作 (对外接口, 供 osal_pool_claim/release 宏或外部调用) ── */
int osal_pool_claim(volatile uint8_t* used_slots, size_t slot_count)
{
    return pool_claim(used_slots, slot_count);
}

void osal_pool_release(volatile uint8_t* used_slots, size_t slot_count, int slot_index)
{
    pool_release(used_slots, slot_count, slot_index);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  任务 (裸机不支持多任务, 创建固定失败)
 * ═══════════════════════════════════════════════════════════════════════════ */

int osal_task_create(const char* name, uint32_t stack_size,
                     uint32_t priority, osal_task_entry_t entry,
                     void* param, int core_id)
{
    (void)name; (void)stack_size; (void)priority;
    (void)entry; (void)param; (void)core_id;
    return -1;  /* 裸机不能创建任务 */
}

int osal_task_create_handle(const char* name, uint32_t stack_size,
                            uint32_t priority, osal_task_entry_t entry,
                            void* param, int core_id,
                            osal_task_handle_t* out_handle)
{
    if (!out_handle) return -1;
    (void)name; (void)stack_size; (void)priority;
    (void)entry; (void)param; (void)core_id;
    *out_handle = NULL;
    return -1;
}

void osal_task_self_delete(void)
{
    /* 裸机无任务, 死循环 */
    while (1)
    { ; }
}

void osal_task_delete(osal_task_handle_t task)
{
    (void)task;
}

bool osal_task_is_running(osal_task_handle_t task)
{
    (void)task;
    return false;
}

const char* osal_task_get_name(osal_task_handle_t task)
{
    (void)task;
    return "baremetal";
}

uint32_t osal_task_get_stack_watermark(osal_task_handle_t task)
{
    (void)task;
    return 0;
}

/* ── 二值信号量 (裸机: 单 bit 标志) ── */
struct osal_sem
{
    volatile uint8_t signaled;
    bool             from_pool;
};

_Static_assert(sizeof(struct osal_sem) <= OSAL_SEM_STORAGE_SIZE,
               "OSAL_SEM_STORAGE_SIZE too small");

static struct osal_sem s_sem_pool[OSAL_SEM_POOL_SIZE];
static uint8_t s_sem_used[OSAL_SEM_POOL_SIZE];

int osal_sem_create_binary(struct osal_sem** out)
{
    if (!out)
        return -1;

    int idx = pool_claim(s_sem_used, OSAL_SEM_POOL_SIZE);
    if (idx < 0)
        return -1;

    struct osal_sem* sem = &s_sem_pool[idx];
    sem->signaled = 0U;
    sem->from_pool = true;
    *out = sem;
    return 0;
}

int osal_sem_create_binary_static(struct osal_sem** out, void* storage, size_t storage_size)
{
    if (!out || !storage || storage_size < sizeof(struct osal_sem))
        return -1;

    struct osal_sem* sem = (struct osal_sem*)storage;
    sem->signaled = 0U;
    sem->from_pool = false;
    *out = sem;
    return 0;
}

void osal_sem_destroy(struct osal_sem* sem)
{
    if (!sem)
        return;

    sem->signaled = 0U;
    if (sem->from_pool)
    {
        for (size_t i = 0; i < OSAL_SEM_POOL_SIZE; i++)
        {
            if (&s_sem_pool[i] == sem)
            {
                pool_release(s_sem_used, OSAL_SEM_POOL_SIZE, (int)i);
                break;
            }
        }
    }
}

int osal_sem_wait(struct osal_sem* sem, uint32_t timeout_ms)
{
    if (!sem)
        return -1;

    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
        while (!sem->signaled)
        {
            /* 裸机忙等 */
        }
    }
    else if (timeout_ms > 0U)
    {
        uint32_t start = osal_time_ms();
        while (!sem->signaled)
        {
            if ((osal_time_ms() - start) >= timeout_ms)
                return -1;
        }
    }
    else if (!sem->signaled)
    {
        return -1;
    }

    sem->signaled = 0U;
    return 0;
}

bool osal_sem_post(struct osal_sem* sem)
{
    if (!sem)
        return false;
    sem->signaled = 1U;
    return true;
}

bool osal_sem_post_from_isr(struct osal_sem* sem, bool* px_yield_required)
{
    (void)px_yield_required;
    return osal_sem_post(sem);
}

void osal_yield_from_isr(bool yield_required)
{
    (void)yield_required;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  队列 (基于环形缓冲区, ISR 安全)
 *
 *  环形缓冲区使用 2^n 大小 + 掩码, 避免取模.
 *  队列满时 osal_queue_send 返回 false, 无阻塞写入.
 *  队列空时 osal_queue_receive 支持 OSAL_WAIT_FOREVER 忙等.
 * ═══════════════════════════════════════════════════════════════════════════ */

osal_queue_handle_t osal_queue_create(size_t queue_len, size_t item_size)
{
    size_t needed = item_size * queue_len;
    if (needed == 0 || needed > OSAL_NULL_QUEUE_BUF_SZ) return NULL;

    int idx = pool_claim(s_queue_used, OSAL_NULL_MAX_QUEUES);
    if (idx < 0) return NULL;

    struct osal_queue_obj* q = &s_queues[idx];

    /* 分配环形缓冲区 */
    q->buf = (uint8_t*)osal_calloc(1, needed);
    if (!q->buf)
    {
        pool_release(s_queue_used, OSAL_NULL_MAX_QUEUES, idx);
        return NULL;
    }

    q->item_size = item_size;
    q->queue_len = queue_len;
    q->buf_mask  = queue_len - 1;  /* 调用方应保证 queue_len 为 2^n */
    q->head = 0;
    q->tail = 0;
    q->used = true;

    return (osal_queue_handle_t)q;
}

void osal_queue_delete(osal_queue_handle_t queue)
{
    int idx = queue_index_of(queue);
    if (idx < 0) return;

    struct osal_queue_obj* q = &s_queues[idx];
    if (q->buf)
    {
        osal_free(q->buf);
        q->buf = NULL;
    }
    q->used = false;
    pool_release(s_queue_used, OSAL_NULL_MAX_QUEUES, idx);
}

bool osal_queue_send(osal_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    (void)timeout_ms;

    if (osal_in_isr())
        return false;

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) return false;

    struct osal_queue_obj* q = &s_queues[idx];
    size_t item_size = q->item_size;
    size_t mask      = q->buf_mask;
    size_t head      = q->head;
    size_t tail      = q->tail;

    /* 判满 */
    if ((head - tail) >= q->queue_len) return false;

    memcpy(&q->buf[(head & mask) * item_size], item, item_size);
    q->head = head + 1;

    return true;
}

bool osal_queue_send_from_isr(osal_queue_handle_t queue, const void* item,
                              bool* px_yield_required)
{
    (void)px_yield_required;

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) return false;

    struct osal_queue_obj* q = &s_queues[idx];
    size_t item_size = q->item_size;
    size_t mask      = q->buf_mask;
    size_t head      = q->head;
    size_t tail      = q->tail;

    if ((head - tail) >= q->queue_len) return false;

    memcpy(&q->buf[(head & mask) * item_size], item, item_size);
    q->head = head + 1;

    return true;
}

bool osal_queue_receive(osal_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    if (osal_in_isr())
        return false;

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) return false;

    struct osal_queue_obj* q = &s_queues[idx];
    size_t item_size = q->item_size;
    size_t mask      = q->buf_mask;

    /* 等待直到有数据 */
    if (timeout_ms == OSAL_WAIT_FOREVER)
    {
        while (q->head == q->tail)
        {
            /* 忙等 — 无 RTOS 无法阻塞 */
        }
    } else if (timeout_ms > 0)
    {
        uint32_t start = osal_time_ms();
        while (q->head == q->tail)
        {
            if ((osal_time_ms() - start) >= timeout_ms) return false;
        }
    }

    size_t tail = q->tail;
    if (q->head == tail) return false;  /* 超时后仍空 */

    memcpy(item, &q->buf[(tail & mask) * item_size], item_size);
    q->tail = tail + 1;

    return true;
}

bool osal_queue_receive_from_isr(osal_queue_handle_t queue, void* item,
                                 bool* px_yield_required)
{
    (void)px_yield_required;

    int idx = queue_index_of(queue);
    if (idx < 0 || !item) return false;

    struct osal_queue_obj* q = &s_queues[idx];
    size_t item_size = q->item_size;
    size_t mask      = q->buf_mask;
    size_t tail      = q->tail;

    if (q->head == tail) return false;

    memcpy(item, &q->buf[(tail & mask) * item_size], item_size);
    q->tail = tail + 1;

    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  硬件安全关断 & 日志 (与其他后端相同)
 * ═══════════════════════════════════════════════════════════════════════════ */

COMPAT_WEAK void safety_hardware_shutdown(void)
{
    COMPAT_TRAP();
}

COMPAT_WEAK void osal_panic_interlock(void)
{
}

/* ── 调度器挂起 / 中断禁用 (裸机无操作) ── */
void osal_sched_suspend(void) {}
void osal_int_disable(void)   {}

/* ── 日志 ── */
void osal_log(osal_log_level_t level, const char* tag, const char* fmt, ...)
{
    (void)level;
    if (!fmt) fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("[%s] ", tag ? tag : "drv");
    vprintf(fmt, args);
    my_printf_output("\n");
    va_end(args);
}

void osal_log_fatal(const char* fmt, ...)
{
    if (!fmt) fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[FATAL ERROR] ");
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

void osal_log_critical_assert(const char* file, int line, const char* fmt, ...)
{
    if (!fmt) fmt = "(null)";

    va_list args;
    va_start(args, fmt);
    my_printf_output("\r\n[CRITICAL_ASSERT FAILED] %s:%d: ", file ? file : "?", line);
    vprintf(fmt, args);
    my_printf_output("\r\n");
    va_end(args);
}

#endif /* CONFIG_OSAL_NULL */
