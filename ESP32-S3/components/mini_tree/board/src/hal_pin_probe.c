#include "hal_pin_probe.h"
#include "device.h"
#include "compiler_compat_poison.h"

int hal_pin_probe(const struct device* dev, const char* port_key, const char* pin_key,
                  hal_pin_t* out)
{
    int port = 0;
    int pin  = -1;

    if (!dev || !pin_key || !out)
        return VFS_ERR_INVAL;

    if (device_get_prop_int(dev, pin_key, &pin))
        return VFS_ERR_INVAL;

    if (port_key)
        COMPAT_IGNORE_RESULT(device_get_prop_int(dev, port_key, &port));

    *out = hal_pin_from_parts(port, pin);
    return VFS_OK;
}
