/*
 * SPI1 板级初始化 — MounRiver/Cube 风格
 *
 * PA5=SCK, PA6=MISO, PA7=MOSI (与 board/dtsi/ch32v307-spi.dtsi 一致)
 * CS (PA4) 由 mini_tree hal_gpio 在运行时控制.
 */
#include "spi.h"
#include "ch32v30x_conf.h"

void MX_SPI1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef  SPI_InitStructure  = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode            = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize        = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL            = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA            = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS             = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    SPI_InitStructure.SPI_FirstBit        = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial   = 7;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);
}
