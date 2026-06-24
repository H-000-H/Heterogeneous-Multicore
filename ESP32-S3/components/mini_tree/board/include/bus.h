#ifndef BOARD_BUS_H
#define BOARD_BUS_H

#include "board_devtable.h"
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" 
{
#endif

                                                            /*总线类型*/
/*===========================================================================================================================================================*/
struct device;

struct bus_type
{
    const char* name;
};
/*===========================================================================================================================================================*/

                                                            /*总线控制器与客户端实体*/
/*===========================================================================================================================================================*/
struct bus_controller
{
    struct device*         dev;
    const struct bus_type* type;
    void*                  hw_priv;
};

struct bus_client
{
    struct device* dev;
    struct device* controller;
    void*          client_priv;
};
/*===========================================================================================================================================================*/

                                                            /*Controller 绑定 API*/
/*===========================================================================================================================================================*/
int bus_controller_bind(struct device* dev, const struct bus_type* type, void* hw_priv)
    COMPAT_WARN_UNUSED_RESULT;

int bus_controller_of(const struct device* dev, struct bus_controller** out)
    COMPAT_WARN_UNUSED_RESULT;

void bus_controller_unbind(struct device* dev);
/*===========================================================================================================================================================*/

                                                            /*Client 绑定 API*/
/*===========================================================================================================================================================*/
int bus_client_bind(struct device* child, struct device* controller, void* client_priv)
    COMPAT_WARN_UNUSED_RESULT;

int bus_client_priv(const struct device* child, void** out) COMPAT_WARN_UNUSED_RESULT;

void bus_client_unbind(const struct device* child);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* BOARD_BUS_H */
