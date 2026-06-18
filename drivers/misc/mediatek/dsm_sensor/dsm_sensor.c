#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/atomic.h>
#include <linux/ioctl.h>

#include "dsm_sensor.h"
struct dsm_client *dsm_sensorhub_dclient = NULL;

static struct dsm_dev dsm_sensorhub = {
    .name      = CLIENT_NAME_SENSORHUB,  // dsm client name
    .fops      = NULL,                // options
    .buff_size = DSM_SENSOR_BUF_MAX, // buffer size
};

static int dsm_sensor_probe(void)
{
    DSM_SENSOR_LOG("+++++++++++++dsm_sensor_probe!!\n");

    if(!dsm_sensorhub_dclient){
        dsm_sensorhub_dclient = dsm_register_client(&dsm_sensorhub);
    }

    return 0;
}
static int dsm_sensor_remove(void)
{
    DSM_SENSOR_LOG("+++++++++++++dsm_sensor_remove!!\n");
    if(dsm_sensorhub_dclient){
        dsm_unregister_client(dsm_sensorhub_dclient,&dsm_sensorhub);
        dsm_sensorhub_dclient=NULL;
    }

    return 0;
}

static int __init dsm_sensor_init(void)
{
    DSM_SENSOR_LOG("+++++dsm_sensor_init\n");

    if (dsm_sensor_probe()) {
        DSM_SENSOR_ERR("failed to register  dsm_sensor driver\n");
        return -ENODEV;
    }

    return 0;
}

static void __exit dsm_sensor_exit(void)
{
    dsm_sensor_remove();
}
module_init(dsm_sensor_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DSM_SENSOR device driver");
MODULE_AUTHOR("Mediatek");
