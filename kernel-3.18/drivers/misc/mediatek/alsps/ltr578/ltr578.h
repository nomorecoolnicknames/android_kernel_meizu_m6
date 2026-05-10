/*
* Copyright (C) 2016 MediaTek Inc.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See http://www.gnu.org/licenses/gpl-2.0.html for more details.
*/

#ifndef __CUST_ALSPS_H__
#define __CUST_ALSPS_H__

#include <linux/types.h>

#define C_CUST_ALS_LEVEL    8
#define C_CUST_I2C_ADDR_NUM 4

#define MAX_THRESHOLD_HIGH 0xffff
#define MIN_THRESHOLD_LOW 0x0

/* LTR559-compatible register map used by the promoted LTR578 reference. */
#define LTR578_ALS_CONTR        0x80
#define LTR578_PS_CONTR         0x81
#define LTR578_PS_LED           0x82
#define LTR578_PS_N_PULSES      0x83
#define LTR578_PS_MEAS_RATE     0x84
#define LTR578_ALS_MEAS_RATE    0x85
#define LTR578_MANUFACTURER_ID  0x87

#define LTR578_INTERRUPT        0x8F
#define LTR578_PS_THRES_UP_0    0x90
#define LTR578_PS_THRES_UP_1    0x91
#define LTR578_PS_THRES_LOW_0   0x92
#define LTR578_PS_THRES_LOW_1   0x93

#define LTR578_ALS_THRES_UP_0   0x97
#define LTR578_ALS_THRES_UP_1   0x98
#define LTR578_ALS_THRES_LOW_0  0x99
#define LTR578_ALS_THRES_LOW_1  0x9A

#define LTR578_INTERRUPT_PERSIST 0x9E

#define LTR578_ALS_DATA_CH1_0   0x88
#define LTR578_ALS_DATA_CH1_1   0x89
#define LTR578_ALS_DATA_CH0_0   0x8A
#define LTR578_ALS_DATA_CH0_1   0x8B
#define LTR578_ALS_PS_STATUS    0x8C
#define LTR578_PS_DATA_0        0x8D
#define LTR578_PS_DATA_1        0x8E

#define MODE_ALS_ON_Range1      0x01
#define MODE_ALS_ON_Range2      0x05
#define MODE_ALS_ON_Range3      0x09
#define MODE_ALS_ON_Range4      0x0D
#define MODE_ALS_ON_Range5      0x19
#define MODE_ALS_ON_Range6      0x1D
#define MODE_ALS_StdBy          0x00

#define ALS_RANGE_64K           1
#define ALS_RANGE_32K           2
#define ALS_RANGE_16K           4
#define ALS_RANGE_8K            8
#define ALS_RANGE_1300          48
#define ALS_RANGE_600           96

#define MODE_PS_ON_Gain16       0x03
#define MODE_PS_ON_Gain32       0x0B
#define MODE_PS_ON_Gain64       0x0F
#define MODE_PS_StdBy           0x00

#define PS_RANGE16              1
#define PS_RANGE32              4
#define PS_RANGE64              8

#define PON_DELAY               600
#define WAKEUP_DELAY            10

#define ltr578_SUCCESS          0
#define ltr578_ERR_I2C          -1
#define ltr578_ERR_STATUS       -3
#define ltr578_ERR_SETUP_FAILURE -4
#define ltr578_ERR_GETGSENSORDATA -5
#define ltr578_ERR_IDENTIFICATION -6

/* +add by wangdongming */
enum {
	CWF_TEMP = 0,
	D65_TEMP,
	A_TEMP,
	TEMP_COUNT,
} color_t;
#define TP_COUNT 2
/* -add by wangdongming */

struct alsps_hw {
	int i2c_num;		/*!< the i2c bus used by ALS/PS */
	int power_id;		/*!< the VDD power id of the als chip */
	int power_vol;		/*!< the VDD power voltage of the als chip */
	int polling_mode;	/*!< 1: polling mode ; 0:interrupt mode */
	int polling_mode_ps;	/*!< 1: polling mode ; 0:interrupt mode */
	int polling_mode_als;	/*!< 1: polling mode ; 0:interrupt mode */
	unsigned char i2c_addr[C_CUST_I2C_ADDR_NUM];	/*!< i2c address list, some chip will have multiple address */
	/*!< (C_CUST_ALS_LEVEL-1) levels divides all range into C_CUST_ALS_LEVEL levels */
	unsigned int als_level[TP_COUNT][TEMP_COUNT][C_CUST_ALS_LEVEL];
	unsigned int als_value[C_CUST_ALS_LEVEL];	/*!< the value reported in each level */
	unsigned int ps_threshold;	/*!< the threshold of proximity sensor */
/* +add by wangdongming */
	unsigned int state_val;
	unsigned int psctrl_val;
	unsigned int alsctrl_val;
	unsigned int ledctrl_val;
	unsigned int wait_val;
/* -add by wangdongming */
	unsigned int als_window_loss;	/*!< the window loss  */
	unsigned int ps_threshold_high;
	unsigned int ps_threshold_low;
	unsigned int als_threshold_high;
	unsigned int als_threshold_low;
	int als_power_vio_id;	/*!< the VIO power id of the als chip */
	int als_power_vio_vol;	/*!< the VIO power voltage of the als chip */
	int ps_power_vdd_id;	/*!< the VDD power id of the ps chip */
	int ps_power_vdd_vol;	/*!< the VDD power voltage of the ps chip */
	int ps_power_vio_id;	/*!< the VIO power id of the ps chip */
	int ps_power_vio_vol;	/*!< the VIO power voltage of the ps chip */
	int power_lp_mode_ctrl;	/*!< 1: disable ldo low power mode when p sensor enabled ; 0: no action */
	bool is_batch_supported_ps;
	bool is_batch_supported_als;
};

int get_alsps_dts_func_ltr578(struct device_node *node,  struct alsps_hw *hw);

#endif
