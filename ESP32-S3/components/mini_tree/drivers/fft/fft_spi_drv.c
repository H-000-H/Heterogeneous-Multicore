#include "spi_client.h"
#include "device.h"
#include "driver.h"
#include "VFS.h"

static int fft_spi_probe(struct device* dev)
{
    return spi_client_probe(dev);
}

static int fft_spi_remove(struct device* dev)
{
    return spi_client_remove(dev);
}

DRIVER_REGISTER(fft_spi, "heterogeneous,fft-spi-slave", fft_spi_probe, fft_spi_remove)
