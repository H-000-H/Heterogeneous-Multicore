#include "hal_uart.h"
#include "VFS.h"
#include "hal_pin.h"

static int hal_uart_init(const struct hal_uart_config_t *cfg)
{
    if (!cfg || !hal_pin_is_valid(cfg->rx_io) || !hal_pin_is_valid(cfg->tx_io))
        return VFS_ERR_INVAL;
    (void)cfg;
    return VFS_ERR_INVAL; /* WIP */
}

