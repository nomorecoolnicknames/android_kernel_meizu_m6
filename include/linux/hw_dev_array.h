#ifndef _HUAWEI_DEV_ARRAY_H_
#define _HUAWEI_DEV_ARRAY_H_
#include "hw_dev_dec.h"

typedef struct {
	int devices_id;
	char* devices_name;
}hw_dec_struct;

static hw_dec_struct hw_dec_device_array[] =
{
	{ DEV_I2C_TOUCH_PANEL,"touch_panel" },
	{ DEV_I2C_CAMERA_SLAVE,"camera_slave" },
	{ DEV_I2C_CAMERA_MAIN,"camera_main" },
	{ DEV_I2C_G_SENSOR,"g_sensor" },
	{ DEV_I2C_ALSP_SENSOR,"alsp" },
	{ DEV_I2C_CHARGER,"charge" },
    { DEV_I2C_COMPASS,"compass" },
    /*{ DEV_I2C_SAR,"sar" },*/
    { DEV_I2C_OTP_MAIN, "otp_main"},
    { DEV_I2C_OTP_SLAVE, "otp_slave"},
    { DEV_I2C_AF, "af"},
	{ DEV_I2C_MAX,"NULL" },
};
#endif
