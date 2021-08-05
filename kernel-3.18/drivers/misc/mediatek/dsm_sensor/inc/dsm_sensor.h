#ifndef __DSM_SENSOR_H__
#define __DSM_SENSOR_H__

#include <dsm/dsm_pub.h>
#include <asm-generic/gpio.h>
#include <mt-plat/mt_boot.h>

#define DSM_SENSOR_TAG                      "<DSM_SENSOR>"
#define DSM_SENSOR_ERR(fmt, args...)        pr_err(DSM_SENSOR_TAG fmt, ##args)
#define DSM_SENSOR_LOG(fmt, args...)        pr_err(DSM_SENSOR_TAG fmt, ##args)
#define DSM_SENSOR_VER(fmt, args...)        pr_err(DSM_SENSOR_TAG fmt, ##args)

#define CLIENT_NAME_SENSORHUB               "dsm_sensorhub"

#endif
