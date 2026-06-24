#ifndef HAL_BUS_CORE_H
#define HAL_BUS_CORE_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
/*==========================================================================================================================================================*/
/*直接if(type&BUS_TYPE_XXX判断即可)*/
typedef enum 
{
    BUS_TYPE_SPI   = ((0x01) << 0),  /**< SPI 总线 -> find_type 索引: 0 */
    BUS_TYPE_IIC   = ((0x01) << 1),  /**< I2C 总线 -> find_type 索引: 1 */
    BUS_TYPE_CAN   = ((0x01) << 2),  /**< 经典 CAN 总线 -> find_type 索引: 2 */
    BUS_TYPE_CANFD = ((0x01) << 3),  /**< CAN FD 总线 -> find_type 索引: 3 */
    BUS_TYPE_USB   = ((0x01) << 4),  /**< USB 总线 -> find_type 索引: 4 */
    BUS_TYPE_I2S   = ((0X01) << 5),  /**< I2S 音频总线 -> find_type 索引: 5 */
    BUS_TYPE_PCIE  = ((0X01) << 6),  /**< PCIe 高速总线 -> find_type 索引: 6 */
} bus_type_t;
/*===========================================================================================================================================================*/

                                                            /*I2C总线独有配置*/
/*===========================================================================================================================================================*/
typedef enum {
    I2C_ADDR_7BIT  = 0,
    I2C_ADDR_10BIT = 1
} i2c_addr_type_t;

typedef struct {
    uint16_t        dev_addr;       /**< 目标设备地址(Master用) / 本机地址(Slave用) */
    i2c_addr_type_t addr_type;      /**< 7位 还是 10位 寻址 */
    uint8_t         duty_cycle;     /**< 快速模式下的占空比 (如 2:1 或 16:9) */
    uint8_t         general_call;   /**< 1: 开启广播呼叫接收, 0: 关闭 */
    uint32_t        timeout_ms;     /**< 传输超时时间，防止总线死锁卡死 */
} i2c_bus_config_t;
/*============================================================================================================================================================*/

                                                            /*SPI总线独有配置*/
/*=============================================================================================================================================================*/
typedef enum {
    SPI_MODE_0 = 0, /**< CPOL=0, CPHA=0 */
    SPI_MODE_1 = 1, /**< CPOL=0, CPHA=1 */
    SPI_MODE_2 = 2, /**< CPOL=1, CPHA=0 */
    SPI_MODE_3 = 3  /**< CPOL=1, CPHA=1 */
} spi_mode_t;

typedef enum {
    SPI_DIR_FULL_DUPLEX = 0, /**< 全双工 (MISO + MOSI) */
    SPI_DIR_HALF_DUPLEX = 1, /**< 半双工 (单根双向线) */
    SPI_DIR_SIMPLEX_RX  = 2  /**< 纯接收 */
} spi_dir_t;

typedef struct {
    spi_mode_t mode;            /**< SPI 模式 0~3 */
    spi_dir_t  direction;       /**< 全双工/半双工/单工 */
    uint8_t    data_size;       /**< 数据位宽：通常为 8 或 16 位 */
    uint8_t    first_bit;       /**< 0: MSB First, 1: LSB First */
    uint8_t    nss_mode;        /**< 片选模式：0: 软件控制 GPIO, 1: 硬件自动 NSS */
    uint8_t    wire_mode;       /**< 1: 标准3/4线, 2: 双线, 4: 四线 (QSPI) */
} bus_spi_spec_t;
/*==============================================================================================================================================================*/

                                                            /*I2S总线独有配置*/
/*===============================================================================================================================================================*/
typedef enum {
    I2S_STD_PHILIPS       = 0, /**< 飞利浦标准 */
    I2S_STD_MSB_JUSTIFIED = 1, /**< 左对齐标准 */
    I2S_STD_LSB_JUSTIFIED = 2, /**< 右对齐标准 */
    I2S_STD_PCM           = 3  /**< PCM/DSP 模式 */
} i2s_std_t;

typedef enum {
    I2S_DATAFORMAT_16B          = 0, /**< 16位数据放入16位通道 */
    I2S_DATAFORMAT_16B_EXTENDED = 1, /**< 16位数据放入32位通道 */
    I2S_DATAFORMAT_24B          = 2, /**< 24位数据放入32位通道 */
    I2S_DATAFORMAT_32B          = 3  /**< 32位数据放入32位通道 */
} i2s_format_t;

typedef struct {
    i2s_std_t    audio_std;     /**< 音频标准 (飞利浦/左/右/PCM) */
    i2s_format_t data_format;   /**< 数据和通道位宽格式 */
    uint32_t     sample_rate;   /**< 音频采样率 (如 44100, 48000 Hz) */
    uint8_t      mode;          /**< 0: Master TX, 1: Master RX, 2: Slave TX, 3: Slave RX */
    uint8_t      mclk_output;   /**< 1: 输出主时钟 MCLK, 0: 关闭 */
} i2s_bus_config_t;
/*================================================================================================================================================================*/

                                                        /*USB独有配置*/
/*================================================================================================================================================================*/
typedef enum {
    USB_SPEED_LOW   = 0, /**< 低速 1.5 Mbps */
    USB_SPEED_FULL  = 1, /**< 全速 12 Mbps */
    USB_SPEED_HIGH  = 2, /**< 高速 480 Mbps */
    USB_SPEED_SUPER = 3  /**< 超高速 5 Gbps+ */
} usb_speed_t;

typedef enum {
    USB_MODE_DEVICE = 0,
    USB_MODE_HOST   = 1,
    USB_MODE_OTG    = 2
} usb_mode_t;

typedef struct {
    usb_mode_t  mode;           /**< Device / Host / OTG */
    usb_speed_t speed;          /**< 速度等级 */
    uint8_t     dev_class;      /**< 设备类: 0x02=CDC(串口), 0x03=HID, 0x08=MSC(U盘) */
    uint8_t     max_packet_size;/**< 端点0最大包大小 (8, 32, 64) */
    uint16_t    vid;            /**< Vendor ID */
    uint16_t    pid;            /**< Product ID */
} usb_bus_config_t;
/*======================================================================================================================================================================*/

                                                        /*CAN独有配置*/
/*=====================================================================================================================================================================*/
typedef struct {
    uint16_t prescaler;         /**< 时钟分频 (BRP) */
    uint8_t  time_seg1;         /**< Phase Segment 1 */
    uint8_t  time_seg2;         /**< Phase Segment 2 */
    uint8_t  sjw;               /**< 同步跳转宽度 */
} can_fd_timing_t;

typedef struct {
    uint32_t filter_id;
    uint32_t filter_mask;
    uint8_t  filter_bank;
    uint8_t  enable;
} can_bus_fd_filter_cfg_t;

/**
 * @brief CAN FD 总线独有配置
 */
typedef struct {
    can_fd_timing_t nominal_timing; /**< 仲裁段位定时 (250k~1Mbps) */
    can_fd_timing_t data_timing;    /**< 数据段位定时 (2M~5Mbps) */
    uint8_t         mode;           /**< 0: Normal, 1: Loopback */
    bool            fd_iso_mode;    /**< true: ISO 标准; false: Non-ISO 标准 */
    bool            tdc_enable;     /**< 是否开启发送延迟补偿 (Transmitter Delay Compensation) */
    uint8_t         tdc_offset;     /**< TDC 偏移量 */
    can_bus_fd_filter_cfg_t *filters;/**< 过滤器阵列指针 */
    size_t          filter_num;     /**< 过滤器数量 */
} can_bus_fd_config_t;

/**
 * @brief 经典 CAN 总线独有配置
 */
typedef struct {
    uint32_t filter_id;     /**< 硬件接收过滤ID */
    uint32_t filter_mask;   /**< 掩码 */
    uint16_t brp_sjw_seg;   /**< 压缩在一起的位定时参数 */
} can_bus_config_t;
/*================================================================================================================================================================*/

                                                        /*PCle独有配置*/
/*================================================================================================================================================================*/
typedef enum 
{
    PCIE_GEN_1 = 1,
    PCIE_GEN_2 = 2,
    PCIE_GEN_3 = 3,
    PCIE_GEN_4 = 4
} pcie_gen_t;

typedef struct 
{
    pcie_gen_t gen;             /**< PCIe 世代 (Gen1 ~ Gen4) */
    uint8_t    lanes;           /**< 通道数: 1=x1, 4=x4, 16=x16 等 */
    uint8_t    mode;            /**< 0: RC (Root Complex), 1: EP (Endpoint) */
    uint32_t   bar_size[6];     /**< BAR 空间大小，用于内存树映射 */
    uint16_t   vendor_id;
    uint16_t   device_id;
} pcie_config_cfg_t;
/*===========================================================================================================================================================================*/

                                            /*BUS总线配置结构体*/
/*============================================================================================================================================================================*/
typedef struct 
{
    uint32_t clock_speed_hz;    /**< 公共配置：总线基本主频/波特率 (如 SPI 10M, I2C 400K) */
    
    union {
        bus_spi_spec_t      spi;    /**< SPI 独有配置空间 */
        i2c_bus_config_t    i2c;    /**< I2C 独有配置空间 */
        i2s_bus_config_t    i2s;    /**< I2S 独有配置空间 */
        can_bus_fd_config_t can_fd; /**< CAN FD 独有配置空间 */
        can_bus_config_t    can;    /**< 经典 CAN 独有配置空间 */
        usb_bus_config_t    usb;    /**< USB 独有配置空间 */
        pcie_config_cfg_t   pcie;   /**< PCIe 独有配置空间 */
    } spec;                         /**< 特异性配置共享内存联合体 */
} bus_config_t;

typedef struct bus_device bus_device_t; 

typedef struct 
{
    int (*open)(bus_device_t *bus);
    int (*close)(bus_device_t *bus);
    int (*ioctl)(bus_device_t *bus, uint32_t cmd, void *arg);

    /* 同步阻塞传输 */
    int (*write)(bus_device_t *bus, const uint8_t *buf, size_t len);
    int (*read)(bus_device_t *bus, uint8_t *buf, size_t len);
    int (*transfer)(bus_device_t *bus, const uint8_t *tx_buf, size_t tx_len, uint8_t *rx_buf, size_t rx_len);

    /* 异步非阻塞传输 */
    int (*transfer_async)(bus_device_t *bus, const uint8_t *tx_buf, size_t tx_len, uint8_t *rx_buf, size_t rx_len, void (*callback)(void *arg));
} bus_ops_t;

/**
 * @brief 总线运行时状态机
 */
typedef enum {
    BUS_STATE_UNINIT = 0,
    BUS_STATE_READY,
    BUS_STATE_BUSY,
    BUS_STATE_ERROR
} bus_status_t;

/**
 * @brief 终极统一总线设备结构体 (宿主对象)
 */
struct bus_device {
    /* 属性与路由 */
    bus_type_t          type;           /**< 核心：总线类型位掩码 */
    const bus_ops_t     *ops;           /*多态：指向虚函数表的指针*/
    
    /* 硬件与配置 */
    void                *hw_handle;     /**< 裸指针：挂载原生硬件句柄(如原厂HAL_Handle) */
    bus_config_t        config;         /**< 属性：配置大联合体 */
    
    /* OS 并发及同步控制 (对接通用指针，解耦具体操作系统) */
    void                *mutex;         /**< 防止多任务同时抢占总线 */
    void                *sync_sem;      /**< 信号量：用于中断/DMA异步传输的同步挂起与唤醒 */
    
    /* 运行时上下文 */
    volatile uint8_t    status;         /**< 当前运行状态 (bus_status_t) */
    uint32_t            error_code;     /**< 运行时底层硬件传回的详细错误码 */
    void                *user_data;     /**< 预留：给上层应用扩展的私有上下文 */
};

/**
 * @brief 依靠硬件指令级 CTZ 瞬间找出总线索引
 * @param bus 指向目标总线设备的指针
 * @return int32_t 成功返回 0~6 的连续数组索引，失败返回 -1
 */
 static inline int32_t find_type(bus_device_t *bus)
 {
     if (bus == NULL || bus->type == 0) 
     {
         return -1;
     }
     // COMPAT_CTZ 由 compiler_compat.h 提供，映射到内置 __builtin_ctz 或硬件汇编 CLZ
     return (int32_t)COMPAT_CTZ((uint32_t)bus->type);
 }

#ifdef __cplusplus
}
#endif

#endif /* HAL_BUS_CORE_H */
