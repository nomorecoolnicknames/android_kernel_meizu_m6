#ifndef _HUAWEI_DEV_DCT_H_
#define _HUAWEI_DEV_DCT_H_

/* hw device list */
enum hw_device_type {
    DEV_I2C_START,
    DEV_I2C_TOUCH_PANEL = DEV_I2C_START,
    DEV_I2C_CAMERA_SLAVE,
    DEV_I2C_CAMERA_MAIN,
	DEV_I2C_G_SENSOR,
    DEV_I2C_ALSP_SENSOR,
    DEV_I2C_CHARGER,
    DEV_I2C_COMPASS,
    /*DEV_I2C_SAR,*/
    DEV_I2C_OTP_MAIN,
    DEV_I2C_OTP_SLAVE,
    DEV_I2C_AF,
    DEV_I2C_MAX,
};

/* set a device flag as true */
void set_hw_dev_flag( int dev_id );

#endif
