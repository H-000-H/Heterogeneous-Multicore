/* SPI 默认参数 (dt-bindings, 仅供 dtsi #include <dt-bindings/...> 引用)
 *
 * 只放 #define 常量, 不写设备节点.
 * 板级引脚在 board *.dts &spi1 { } / &fft_slave { } 中覆盖.
 * 注意: 仅供 dtc-lite 预处理, 勿加 #ifndef guard (会破坏宏展开).
 */

#define DTS_SPI_DEFAULT_HOST_ID           2
#define DTS_SPI_FLASH_HOST_ID             3
#define DTS_SPI_DEFAULT_MAX_FREQUENCY_HZ  40000000 /*fft传输去主机imx的spi波特率*/
#define DTS_SPI_DEFAULT_MODE              0/*CPOL低 CPHA 低 上升沿采样*/
#define DTS_SPI_DEFAULT_BITS_PER_WORD     8/*8bit模式*/
#define DTS_SPI_DEFAULT_QUEUE_SIZE        8/*最多可以有8个传输*/
#define DTS_SPI_DEFAULT_DMA_CHAN          (-1)/*idf自动分配dma通道*/
