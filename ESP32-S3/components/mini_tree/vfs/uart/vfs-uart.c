#include "vfs-uart.h"
#include "dt_config_gen.h"
#include "system_log.h"
#include "driver.h"
#define UART_COUNT DTC_GEN_COUNT_ESP32_UART1

static struct vfs_uart_prive s_uart_pool[UART_COUNT]COMPAT_ALIGNED(4);
static uint8_t               s_uart_used[UART_COUNT]COMPAT_ALIGNED(4);
static osal_pool_t           s_uart_pool_ctrl COMPAT_ALIGNED(4);
static uint8_t s_uart_mutex_storage[UART_COUNT][OSAL_MUTEX_STORAGE_SIZE]COMPAT_ALIGNED(4);

pre_execution(160)
static void uart_pool_boot_init()
{
    osal_pool_init(&s_uart_pool_ctrl,s_uart_used,UART_COUNT);
}

static const char* const kTag = "vfs-uart";

static int uart_master_bind(struct device*pdev,struct vfs_uart_prive*priv,uint8_t* mutex_storage, size_t mutex_storage_size,int pool_idx)
{
    struct hal_uart_config_t dev_cfg;
    uint32_t         baud_rate;
    hal_uart_data_bits_t data_bits;
    hal_uart_parity_t    parity;
    hal_uart_stop_bits_t stop_bits;
    hal_pin_t        tx_io;
    hal_pin_t        rx_io;
    int              uart_host;
    
    // 严格防止越界
    if(!pdev||!priv||!mutex_storage||pool_idx<0||pool_idx>=UART_COUNT)
        return VFS_ERR_INVAL;

    if (hal_pin_probe(pdev,"rx-port","rx-pin",&rx_io)||
        hal_pin_probe(pdev,"tx-port","tx-pin",&tx_io)||
        device_get_prop_int(pdev,"uart-trans-baund",(int*)&baud_rate)||
        device_get_prop_int(pdev,"host-id", (int*)&uart_host)||
        device_get_prop_int(pdev, "stop-bit", (int*)&stop_bits)||
        device_get_prop_int(pdev,"data-bit",(int*)&data_bits)||
        device_get_prop_int(pdev,"parity",(int*)&parity)) 
    {
        SYS_LOGE(kTag, "Failed to get property: %s", device_get_name(pdev));
        return VFS_ERR_INVAL;
    }
    __builtin_memset(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;
    dev_cfg.baud_rate=baud_rate;
    dev_cfg.data_bits=data_bits;
    dev_cfg.stop_bits=stop_bits;
    dev_cfg.parity=parity;
    dev_cfg.rx_io=rx_io;
    dev_cfg.tx_io=tx_io;
    dev_cfg.uart_host=uart_host;
    
    if(osal_mutex_create_static(&priv->io_mutex,mutex_storage,mutex_storage_size)!=VFS_OK)
    {
        __builtin_memset(priv,0,sizeof(*priv));
        return VFS_ERR_IO;
    }

    {
        const struct hal_uart_bus* plat = hal_uart_bus_get();
        if (!plat)
        {
            osal_mutex_destroy(priv->io_mutex);
            __builtin_memset(priv, 0, sizeof(*priv));
            return VFS_ERR_NODEV;
        }
        priv->bus               = *plat;
        priv->uart_dev.cfg      = dev_cfg;
        priv->uart_dev.pool_idx = pool_idx;
        priv->uart_dev.status   = UART_STATE_UNINIT;
    }

    device_lc_bind(pdev, priv->io_mutex);
    SYS_LOGI(kTag, "bind OK:rx-io:%d tx-io:%d baund:%d host:%d",HAL_PIN_NUM(rx_io),HAL_PIN_NUM(tx_io),baud_rate,uart_host);
    return VFS_OK;
}

static void uart_master_unbind(struct device *pdev,struct vfs_uart_prive*priv)
{
    if(!pdev||!priv)
        return ;
    if (priv->uart_dev.hw_open) 
    {
        priv->bus.deinit(&priv->uart_dev.cfg);
    }
    if(priv->io_mutex)
        osal_mutex_destroy(priv->io_mutex);

    __builtin_memset(priv,0,sizeof(*priv));
}

static int uart_xfer_session_end(struct hal_uart_dev*pdev,int io_ret)
{
    int end_ret = hal_uart_xfer_end(pdev);
    if(end_ret!=VFS_OK&&io_ret==VFS_OK)
        return VFS_ERR_IO;
    return io_ret;
}

static int uart_master_open(struct device*pdev,void*arg)
{
    struct vfs_uart_prive *priv;
    struct dev_lifecycle *lc;
    int first;
    int ret;
    COMPAT_IGNORE_RESULT(arg);
    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct vfs_uart_prive, ops);

    lc = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc,OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if(first<0)
        return first;

    ret = VFS_OK;
    if(first==1)
    {
        ret = priv->bus.open(&priv->uart_dev.cfg);
        if(ret!=VFS_OK)
            dev_lc_open_abort(lc);
    }
    if(ret==VFS_OK)
        dev_lc_open_end(lc);
    return ret;
}

static int uart_close(struct device*pdev)
{
    struct vfs_uart_prive*priv;
    struct dev_lifecycle *lc;
    int last;
    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;
    priv = container_of(pdev->ops,struct vfs_uart_prive,ops);

    lc = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc,OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if(last<0)
        return last;

    if(last)
    {
        if(priv->bus.close(&priv->uart_dev.cfg)!=VFS_OK)
        {
            return VFS_ERR_NODEV;
        }
    }
        
    dev_lc_close_end(lc);
    return VFS_OK;
}

static int uart_write(struct device* pdev, const void* buffer, size_t len, uint32_t timeout_ms)
{
    struct vfs_uart_prive*priv;
    struct dev_lifecycle*lc;
    int ret;

    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops,struct vfs_uart_prive, ops);
    lc = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc,OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if(ret!=VFS_OK)
        return ret;

    if(len==0)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    ret = hal_uart_xfer_begin(&priv->uart_dev,timeout_ms);
    if(ret==VFS_OK)
    {
        int write_bytes =priv->bus.write(&priv->uart_dev,(const uint8_t*)buffer,len);

        if(write_bytes>0)
            ret = VFS_OK;
        else
            ret = VFS_ERR_IO;

        ret = uart_xfer_session_end(&priv->uart_dev,ret);
    }

    dev_lc_io_end(lc);
    return ret;
}

static int uart_read(struct device* pdev, void* buffer, size_t len, uint32_t timeout_ms)
{
    struct vfs_uart_prive*priv;
    struct dev_lifecycle*lc;
    int ret;

    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops,struct vfs_uart_prive, ops);
    lc =device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc,OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if(ret!=VFS_OK)
        return  ret;

    if(len == 0)
    {
        dev_lc_io_end(lc);
        return VFS_OK;
    }

    if(!buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    ret = hal_uart_xfer_begin(&priv->uart_dev,timeout_ms);
    if(ret == VFS_OK)
    {
        ret = priv->bus.read(&priv->uart_dev,(uint8_t*)buffer,len);
        ret = uart_xfer_session_end(&priv->uart_dev,ret);
    }

    dev_lc_io_end(lc);
    return ret;
}

static int uart_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct vfs_uart_prive*priv;
    struct dev_lifecycle*lc;
    int ret;
    int bytes = VFS_ERR_IO; 
    struct uart_transfer_arg* t_arg = (struct uart_transfer_arg*)arg;
    
    if(!pdev||!pdev->ops||!t_arg||arg_len!=sizeof(*t_arg))    
        return VFS_ERR_INVAL;
        
    priv = container_of(pdev->ops,struct vfs_uart_prive,ops);
    lc = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc,OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if(ret!=VFS_OK)
        return ret;

    if(!t_arg->rx||!t_arg->tx)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }
    
    if(t_arg->rx_len<0||t_arg->tx_len<0)
    {
        dev_lc_io_end(lc);
        return VFS_OK;
    }

    ret = hal_uart_xfer_begin(&priv->uart_dev,timeout_ms);
    if(ret != VFS_OK)
    {
        dev_lc_io_end(lc); 
        return VFS_ERR_IO;
    }

    bytes = priv->bus.transmit(&priv->uart_dev,(uint8_t*)t_arg->rx,(uint8_t*)t_arg->tx,t_arg->rx_len,t_arg->tx_len);
    
    // 释放硬件总线会话锁
    COMPAT_IGNORE_RESULT(uart_xfer_session_end(&priv->uart_dev, VFS_OK));
    
    dev_lc_io_end(lc);
    return bytes;
}

static const struct file_operations uart_fops =
{
    .open       = uart_master_open,
    .close      = uart_close,
    .ioctl      = uart_ioctl,
    .read       = uart_read,
    .write      = uart_write,
};

static int uart_master_probe(struct device*pdev)
{
    struct vfs_uart_prive*priv;
    int pool_idx;
    int ret;

    pool_idx = osal_pool_claim(&s_uart_pool_ctrl);
    if(pool_idx<0)
    {
        SYS_LOGE(kTag, "Failed to claim UART client pool");
        return VFS_ERR_NOMEM;
    }

    priv        =  &s_uart_pool[pool_idx];
    ret         = uart_master_bind(pdev,priv,s_uart_mutex_storage[pool_idx],sizeof(s_uart_mutex_storage[pool_idx]),pool_idx);
    if(ret!=VFS_OK)
    {
        osal_pool_release(&s_uart_pool_ctrl,pool_idx);
        return ret;
    }
    priv->ops   = uart_fops;
    pdev->ops   = &priv->ops;

    if(device_set_priv(pdev,priv)!=VFS_OK)
    {
        osal_pool_release(&s_uart_pool_ctrl,pool_idx);
        return VFS_ERR_IO;
    }
    return VFS_OK;
}

static int uart_master_remove(struct device*pdev)
{
    struct vfs_uart_prive*priv;
    struct dev_lifecycle*lc;
    int pool_idx;

    if(!pdev||!pdev->ops)
        return VFS_ERR_INVAL;
    priv = container_of(pdev->ops,struct vfs_uart_prive,ops);
    lc = device_lc(pdev);
    if(IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if(dev_lc_remove_drain(lc,OSAL_WAIT_FOREVER)!=VFS_OK)
    {
        SYS_LOGE(kTag, "remove drain failed");
        return VFS_ERR_IO;
    }

    uart_master_unbind(pdev,priv);
    osal_pool_release(&s_uart_pool_ctrl, pool_idx);
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(uart,"esp32,uart1",uart_master_probe, uart_master_remove)