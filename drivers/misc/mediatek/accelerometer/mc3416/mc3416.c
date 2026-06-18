/*
* Copyright(C)2014 MediaTek Inc.
* Modification based on code covered by the below mentioned copyright
* and/or permission notice(S).
*/

/*****************************************************************************
 *
 * Copyright (c) 2016 mCube, Inc.  All rights reserved.
 *
 * This source is subject to the mCube Software License.
 * This software is protected by Copyright and the information and source code
 * contained herein is confidential. The software including the source code
 * may not be copied and the information contained herein may not be used or
 * disclosed except with the written permission of mCube Inc.
 *
 * All other rights reserved.
 *
 * This code and information are provided "as is" without warranty of any
 * kind, either expressed or implied, including but not limited to the
 * implied warranties of merchantability and/or fitness for a
 * particular purpose.
 *
 * The following software/firmware and/or related documentation ("mCube Software")
 * have been modified by mCube Inc. All revisions are subject to any receiver's
 * applicable license agreements with mCube Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 *****************************************************************************/

/*****************************************************************************
 *** HEADER FILES
 *****************************************************************************/

#include "cust_acc.h"
#include "accel.h"
#include "mc3416.h"
#include <misc/app_info.h>
#ifdef CONFIG_HUAWEI_HW_I2C_DCT
#include <linux/hw_dev_dec.h>
#endif
#ifdef CONFIG_HUAWEI_DSM
#include "dsm_sensor.h"
#endif
/*****************************************************************************
 *** CONFIGURATION
 *****************************************************************************/
#define _MC3XXX_SUPPORT_CONCURRENCY_PROTECTION_
//#define MC3XXX_SUPPORT_MOTION
#define C_MAX_FIR_LENGTH	(32)

/*****************************************************************************
 *** CONSTANT / DEFINITION
 *****************************************************************************/
/**************************
 *** CONFIGURATION
 **************************/
#define MC3XXX_DEV_NAME						"MC3416"
#define MC3XXX_MOTION						   "MCXXX_MOTION"
#define MC3XXX_DEV_DRIVER_VERSION			  "2.1.6"

/**************************
 *** COMMON
 **************************/
#define MC3XXX_AXIS_X	  0
#define MC3XXX_AXIS_Y	  1
#define MC3XXX_AXIS_Z	  2
#define MC3XXX_AXES_NUM	3
#define MC3XXX_DATA_LEN	6

#define MC3XXX_LOW_REOLUTION_DATA_SIZE	 3
#define MC3XXX_HIGH_REOLUTION_DATA_SIZE	6
#define MC3XXX_INIT_SUCC	(0)
#define MC3XXX_INIT_FAIL	(-1)
#define MC3XXX_REGMAP_LENGTH		(75)//for mensa
//#define MC3XXX_REGMAP_LENGTH	(64)
#define DEBUG_SWITCH		1
#define C_I2C_FIFO_SIZE	 8

/**************************
 *** DEBUG
 **************************/
#if DEBUG_SWITCH
    #define GSE_TAG                  "[Gsensor] "
    #define GSE_FUN(f)               printk(KERN_INFO GSE_TAG"%s\n", __FUNCTION__)
    #define GSE_ERR(fmt, args...)    printk(KERN_ERR GSE_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)
    #define GSE_LOG(fmt, args...)    printk(KERN_NOTICE GSE_TAG fmt, ##args)
#else
    #define GSE_TAG
    #define GSE_FUN(f)               do {} while (0)
    #define GSE_ERR(fmt, args...)    do {} while (0)
    #define GSE_LOG(fmt, args...)    do {} while (0)
#endif

/*****************************************************************************
 *** DATA TYPE / STRUCTURE DEFINITION / ENUM
 *****************************************************************************/
enum MCUBE_TRC {
	MCUBE_TRC_FILTER  = 0x01,
	MCUBE_TRC_RAWDATA = 0x02,
	MCUBE_TRC_IOCTL   = 0x04,
	MCUBE_TRC_CALI	= 0X08,
	MCUBE_TRC_INFO	= 0X10,
	MCUBE_TRC_REGXYZ  = 0X20,
};

struct scale_factor {
	u8	whole;
	u8	fraction;
};

struct data_resolution {
	struct scale_factor	scalefactor;
	int					sensitivity;
};

struct data_filter {
	s16	raw[C_MAX_FIR_LENGTH][MC3XXX_AXES_NUM];
	int	sum[MC3XXX_AXES_NUM];
	int	num;
	int	idx;
};

struct mc3xxx_i2c_data {
	/* ================================================ */
	struct i2c_client		  *client;
	struct acc_hw			  *hw;
	struct hwmsen_convert	   cvt;

	/* ================================================ */
	struct data_resolution	 *reso;
	atomic_t					trace;
	atomic_t					suspend;
	atomic_t					selftest;
	atomic_t					filter;
	s16						 cali_sw[MC3XXX_AXES_NUM + 1];

	/* ================================================ */
	s16						 offset[MC3XXX_AXES_NUM + 1];
	s16						 data[MC3XXX_AXES_NUM + 1];

	/* ================================================ */
};

#ifdef CONFIG_HUAWEI_DSM
extern struct dsm_client *dsm_sensorhub_dclient;
#endif

/*****************************************************************************
 *** EXTERNAL FUNCTION
 *****************************************************************************/
/* extern struct acc_hw*	mc3xxx_get_cust_acc_hw(void); */

/*****************************************************************************
 *** STATIC FUNCTION
 *****************************************************************************/
static int mc3xxx_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int mc3xxx_i2c_remove(struct i2c_client *client);
//static int _mc3xxx_i2c_auto_probe(struct i2c_client *client);
static int mc3xxx_suspend(struct i2c_client *client, pm_message_t msg);
static int mc3xxx_resume(struct i2c_client *client);
static int mc3xxx_local_init(void);
static int mc3xxx_remove(void);
static int MC3XXX_SetPowerMode(struct i2c_client *client, bool enable);
static int MC3XXX_WriteCalibration(struct i2c_client *client, int dat[MC3XXX_AXES_NUM]);
#ifdef MC3XXX_SUPPORT_MOTION
static int mc3xxx_motion_local_init(void);
static int mc3xxx_motion_local_uninit(void);
#endif
static void MC3XXX_SetGain(void);
/*****************************************************************************
 *** STATIC VARIBLE & CONTROL BLOCK DECLARATION
 *****************************************************************************/
static unsigned char	s_bPCODE	  = 0x00;

static unsigned char	s_bPCODER	 = 0x00;
static unsigned char	s_bHWID	   = 0x00;
static unsigned char	s_bMPOL	   = 0x00;
static int	s_nInitFlag = MC3XXX_INIT_FAIL;
/* Maintain  cust info here */
struct acc_hw accel_cust_mc3416;
static struct acc_hw *hw = &accel_cust_mc3416;

/* For  driver get cust info */
static struct acc_hw *get_cust_acc(void)
{
	return &accel_cust_mc3416;
}
#ifdef MC3XXX_SUPPORT_MOTION
static struct step_c_init_info  mc3xxx_motion_init_info = {
	.name   = MC3XXX_MOTION,
	.init   = mc3xxx_motion_local_init,
	.uninit = mc3xxx_motion_local_uninit,
};
#endif
static struct acc_init_info  mc3xxx_init_info = {
	.name   = MC3XXX_DEV_NAME,
	.init   = mc3xxx_local_init,
	.uninit = mc3xxx_remove,
	};
#ifdef CONFIG_OF
static const struct of_device_id accel_of_match[] = {
	{.compatible = "mediatek,gsensor"},
	{},
};
#endif
static const struct i2c_device_id mc3xxx_i2c_id[] = { {MC3XXX_DEV_NAME, 0}, {} };
/* static struct i2c_board_info __initdata mc3xxx_i2c_board_info = { I2C_BOARD_INFO(MC3XXX_DEV_NAME, 0x4C) }; */
static unsigned short mc3xxx_i2c_auto_probe_addr[] = { 0x4C, 0x6C, 0x4D, 0x4E, 0x4F, 0x6D, 0x6E, 0x6F };
static struct i2c_driver	mc3xxx_i2c_driver = {
							.driver = {
								  .name = MC3XXX_DEV_NAME,
						   #ifdef CONFIG_OF
								  .of_match_table = accel_of_match,
						   #endif
								  },
							.probe  = mc3xxx_i2c_probe,
							.remove = mc3xxx_i2c_remove,

							.suspend = mc3xxx_suspend,
							.resume  = mc3xxx_resume,

							.id_table = mc3xxx_i2c_id,
						};

static struct i2c_client		*mc3xxx_i2c_client;
static struct mc3xxx_i2c_data   *mc3xxx_obj_i2c_data;
//static struct data_resolution	mc3xxx_offset_resolution = { {7, 8}, 256 };
static bool	mc3xxx_sensor_power;
#ifdef MC3XXX_SUPPORT_MOTION
static bool motion_enable_status;
#endif
static struct GSENSOR_VECTOR3D	gsensor_gain, gsensor_offset;
static char	selftestRes[10] = {0};
//static struct file *fd_file;
//static mm_segment_t oldfs;
static unsigned char offset_buf[6];
static signed int	offset_data[3];
//static signed int	gain_data[3];
static unsigned char	s_baOTP_OffsetData[6] = { 0 };
static DEFINE_MUTEX(MC3XXX_i2c_mutex);
#ifdef _MC3XXX_SUPPORT_CONCURRENCY_PROTECTION_
	static struct semaphore	s_tSemaProtect;
#endif
static signed char	s_bAccuracyStatus = SENSOR_STATUS_ACCURACY_MEDIUM;
static int mc3xxx_mutex_lock(void);
static void mc3xxx_mutex_unlock(void);
static void mc3xxx_mutex_init(void);
/*****************************************************************************
 *** MACRO
 *****************************************************************************/
#ifdef _MC3XXX_SUPPORT_CONCURRENCY_PROTECTION_
static void mc3xxx_mutex_init(void)
{
	sema_init(&s_tSemaProtect, 1);
}

static int mc3xxx_mutex_lock(void)
{
	if (down_interruptible(&s_tSemaProtect))
			return (-ERESTARTSYS);
	return 0;
}

static void mc3xxx_mutex_unlock(void)
{
	up(&s_tSemaProtect);
}
#else
	#define mc3xxx_mutex_lock()				do {} while (0)
	#define mc3xxx_mutex_lock()				do {} while (0)
	#define mc3xxx_mutex_unlock()			  do {} while (0)
#endif

#define IS_MCFM12()	((0xC0 <= s_bHWID) && (s_bHWID <= 0xCF))
#define IS_MCFM3X()	((0x20 == s_bHWID) || ((0x22 <= s_bHWID) && (s_bHWID <= 0x2F)))
//#define IS_MENSA()	((0xA0 == s_bHWID))
#define IS_MENSA()	((0xA0 == 0xA0))

#ifdef CONFIG_HUAWEI_DSM
static int gsensor_report_dsm_err(int type)
{
	int size = 0;
	int total_size = 0;
	if (FACTORY_BOOT == get_boot_mode()||(dsm_sensorhub_dclient == NULL))
	return 0;
	/* try to get permission to use the buffer */
	if(dsm_client_ocuppy(dsm_sensorhub_dclient))
	{
		/* buffer is busy */
		GSE_ERR("%s: buffer is busy!", __func__);
		return -EBUSY;
	}
	switch(type)
	{
		case DSM_SHB_ERR_GSENSOR_I2C_ERR:
			/* report i2c infomation */
		    size = dsm_client_record(dsm_sensorhub_dclient,"gsensor I2C error:%d\n",type);
			break;
		case DSM_SHB_ERR_GSENSOR_DATA_ABNORMAL:
			size = dsm_client_record(dsm_sensorhub_dclient,"gsensor data error:%d\n",type);
			break;
		case DSM_SHB_ERR_GSENSOR_DATA_ALL_ZERO:
			size = dsm_client_record(dsm_sensorhub_dclient,"gsensor xyz all zero error:%d\n",type);
			break;
		default:
			GSE_ERR("%s:unsupported dsm report type.\n",__func__);
			break;
	}
    total_size += size;

	dsm_client_notify(dsm_sensorhub_dclient, type);


	return total_size;
}
#endif
/*****************************************************************************
 *** FUNCTION
 *****************************************************************************/

/**************I2C operate API*****************************/
/*
static int MC3XXX_i2c_read_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
	u8 beg = addr;
	int err;
	struct i2c_msg msgs[2] = {{0}, {0} };

	mutex_lock(&MC3XXX_i2c_mutex);

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].ext_flag |= I2C_RS_FLAG;
	msgs[0].len = 1;
	msgs[0].buf = &beg;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = data;

	if (!client) {
		mutex_unlock(&MC3XXX_i2c_mutex);
		return -EINVAL;
	} else if (len > C_I2C_FIFO_SIZE) {
		GSE_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		mutex_unlock(&MC3XXX_i2c_mutex);
		return -EINVAL;
	}
	err = i2c_transfer(client->adapter, msgs, sizeof(msgs)/sizeof(msgs[0]));
	if (err != 2) {
		GSE_ERR("i2c_transfer error: (%d %p %d) %d\n", addr, data, len, err);
		err = -EIO;
	} else
		err = 0;

	mutex_unlock(&MC3XXX_i2c_mutex);
	return err;

}
*/
static int MC3XXX_i2c_read_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
    int err = 0;
    char buf[C_I2C_FIFO_SIZE];
    mutex_lock(&MC3XXX_i2c_mutex);
    if (!client)
    {
        mutex_unlock(&MC3XXX_i2c_mutex);
        return -EINVAL;
    }
    else if (len > C_I2C_FIFO_SIZE)
    {
        GSE_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
        mutex_unlock(&MC3XXX_i2c_mutex);
        return -EINVAL;
    }

	memset(buf, 0, C_I2C_FIFO_SIZE);
	buf[0] = addr;

	client->addr &= I2C_MASK_FLAG;
	client->addr |= I2C_WR_FLAG;
	client->addr |= I2C_RS_FLAG;

    err = i2c_master_send(client, buf, len << 8 | 1);

    if (err < 0)
    {
		#ifdef CONFIG_HUAWEI_DSM
		gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_I2C_ERR);
		#endif
        GSE_ERR("i2c_transfer error: (%d %p %d) %d\n",addr, data, len, err);
        err = -EIO;
    }
	else
    {
        err = 0;
    }

	memcpy(data, buf, len);

	client->addr &= I2C_MASK_FLAG;
    mutex_unlock(&MC3XXX_i2c_mutex);
    return err;
}

static int MC3XXX_i2c_write_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{   /*because address also occupies one byte, the maximum length for write is 7 bytes*/
	int err, idx, num;
	char buf[C_I2C_FIFO_SIZE];

	err = 0;
	mutex_lock(&MC3XXX_i2c_mutex);
	if (!client) {
		mutex_unlock(&MC3XXX_i2c_mutex);
		return -EINVAL;
	} else if (len >= C_I2C_FIFO_SIZE) {
		GSE_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		mutex_unlock(&MC3XXX_i2c_mutex);
		return -EINVAL;
	}

	num = 0;
	buf[num++] = addr;
	for (idx = 0; idx < len; idx++)
		buf[num++] = data[idx];

	err = i2c_master_send(client, buf, num);
	if (err < 0) {
		GSE_ERR("send command error!!\n");
		mutex_unlock(&MC3XXX_i2c_mutex);
        #ifdef CONFIG_HUAWEI_DSM
        gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_I2C_ERR);
        #endif
		return -EFAULT;
	}
	err = 0;

	mutex_unlock(&MC3XXX_i2c_mutex);
	return err;
}
#ifdef MC3XXX_SUPPORT_MOTION
/*******************Motion*******************************/

static int mc3xxx_motion_c_open_report_data(int open)
{
	return 0;
}
static int mc3xxx_motion_c_enable_nodata(int en)
{
    return 0;
}
static int mc3xxx_motion_c_enable_motion_detect(int en)
{
	unsigned char	DataBuf[2] = { 0 };
	int err=0;
	struct mc3xxx_i2c_data *obj = mc3xxx_obj_i2c_data;

	if(en == 1)
	{
		motion_enable_status = true;
		DataBuf[0] = 0x04;

		err = MC3XXX_i2c_write_block(obj->client, MC3XXX_REG_INTERRUPT_ENABLE, DataBuf, 1);
		if (err < 0) {
			GSE_LOG("MC3XXX set motion enable failed!\n");
			return MC3XXX_RETCODE_ERROR_I2C;
		} else
			GSE_LOG("mc3xxx set motion enable ok %d!\n", DataBuf[1]);
	}
	else
	{
		motion_enable_status = false;
		DataBuf[0] = 0x04;

		err = MC3XXX_i2c_write_block(obj->client, MC3XXX_REG_INTERRUPT_ENABLE, DataBuf, 1);
		if (err < 0) {
			GSE_LOG("MC3XXX set motion enable failed!\n");
			return MC3XXX_RETCODE_ERROR_I2C;
		} else
			GSE_LOG("mc3xxx set motion enable ok %d!\n", DataBuf[1]);
	}

	GSE_LOG("mc3xxx_motion_c_enable_nodata OK!\n");
	return MC3XXX_RETCODE_SUCCESS;
}

static int mc3xxx_motion_c_set_delay(u64 delay)
{
    return 0;
}
static int mc3xxx_motion_d_set_delay(u64 delay)
{
	return 0;
}
static int mc3xxx_motion_c_get_data(uint32_t *value, int *status)
{
	return 0;
}
static int mc3xxx_motion_c_get_data_motion_d(uint32_t *value, int *status)
{
	return 0;
}
static int mc3xxx_motion_c_get_data_significant(uint32_t *value, int *status)
{
	unsigned char	DataBuf[2] = { 0 };
	int err=0;
	struct mc3xxx_i2c_data *obj = mc3xxx_obj_i2c_data;

	GSE_LOG("[%s]\n", __func__);

	err = MC3XXX_i2c_read_block(obj->client, MC3XXX_REG_STATUS_1, DataBuf, 2);
	if(err)
		return MC3XXX_RETCODE_ERROR_I2C;
	else{
		if(DataBuf[0] | 0x04){
			*value = 1;
			*status = 1;
		}else{
			*value = 0;
			*status = 0;
		}
	}

	return MC3XXX_RETCODE_SUCCESS;
}
static int mc3xxx_motion_c_enable_significant(int en)
{
	unsigned char	DataBuf[2] = { 0 };
	int err = 0;
	struct mc3xxx_i2c_data *obj = mc3xxx_obj_i2c_data;

	DataBuf[0] = 0x04;
	err = MC3XXX_i2c_write_block(obj->client, MC3XXX_REG_INTERRUPT_ENABLE, DataBuf, 1);
	if (err < 0) {
		GSE_LOG("MC3XXX set motion enable failed!\n");
		return MC3XXX_RETCODE_ERROR_I2C;
	} else
		GSE_LOG("mc3xxx set motion enable ok %d!\n", DataBuf[1]);

    return MC3XXX_RETCODE_SUCCESS;
}
static int mc3xxx_motion_local_init(void)
{
	int res = 0;
	struct step_c_control_path motion_ctl={0};
	struct step_c_data_path motion_data={0};

	GSE_LOG("%s start \n", __FUNCTION__);

	motion_ctl.open_report_data= mc3xxx_motion_c_open_report_data;
	motion_ctl.enable_nodata = mc3xxx_motion_c_enable_nodata;
	motion_ctl.enable_step_detect  = mc3xxx_motion_c_enable_motion_detect;
	motion_ctl.step_c_set_delay = mc3xxx_motion_c_set_delay;
	motion_ctl.step_d_set_delay = mc3xxx_motion_d_set_delay;
	motion_ctl.is_report_input_direct = false;
	motion_ctl.is_support_batch = false;
	motion_ctl.enable_significant = mc3xxx_motion_c_enable_significant;

	res = step_c_register_control_path(&motion_ctl);
	if(res)
	{
		 GSE_ERR("register step counter control path err\n");
		goto mc3xxx_motion_c_local_init_failed;
	}

	motion_data.get_data = mc3xxx_motion_c_get_data;
	motion_data.get_data_step_d = mc3xxx_motion_c_get_data_motion_d;
	motion_data.get_data_significant = mc3xxx_motion_c_get_data_significant;

	motion_data.vender_div = 1;
	res = step_c_register_data_path(&motion_data);
	if(res)
	{
		GSE_ERR("register step counter data path err= %d\n", res);
		goto mc3xxx_motion_c_local_init_failed;
	}

	return 0;

mc3xxx_motion_c_local_init_failed:

	GSE_ERR("%s init failed!\n", __FUNCTION__);
	return res;

}

static int mc3xxx_motion_local_uninit(void)
{
    return 0;
}
#endif
/*****************************************
 *** MC3XXX_power
 *****************************************/
static void MC3XXX_power(struct acc_hw *hw, unsigned int on)
{
}
/*****************************************
*** MC3XXX_ValidateSensorIC
*****************************************/
static int MC3XXX_ValidateSensorIC(unsigned char *pbPCode, unsigned char *pbHwID)
{
#if 0
    GSE_LOG("[%s] *pbPCode: 0x%02X, *pbHwID: 0x%02X\n", __func__, *pbPCode, *pbHwID);
    if ((0x02 == *pbHwID) || (0x21 == *pbHwID)
        || ((0x10 <= *pbHwID) && (*pbHwID <= 0x1F))) {
            if (
                (MC3XXX_PCODE_3410 == *pbPCode) || (MC3XXX_PCODE_3410N == *pbPCode)
                || (MC3XXX_PCODE_3430 == *pbPCode) || (MC3XXX_PCODE_3430N == *pbPCode))
            return MC3XXX_RETCODE_SUCCESS;
                
    } else if ((0x20 == *pbHwID) || ((0x22 <= *pbHwID) && (*pbHwID <= 0x2F))) {
            *pbPCode = (*pbPCode & 0xF1);

            if ((MC3XXX_PCODE_RESERVE_1 == *pbPCode)
                || (MC3XXX_PCODE_RESERVE_2 == *pbPCode) || (MC3XXX_PCODE_RESERVE_3 == *pbPCode)
                || (MC3XXX_PCODE_RESERVE_4 == *pbPCode) || (MC3XXX_PCODE_RESERVE_5 == *pbPCode)
                || (MC3XXX_PCODE_RESERVE_6 == *pbPCode) || (MC3XXX_PCODE_RESERVE_7 == *pbPCode)
                || (MC3XXX_PCODE_RESERVE_8 == *pbPCode) || (MC3XXX_PCODE_RESERVE_9 == *pbPCode))
            return MC3XXX_RETCODE_SUCCESS;

        }
#endif
//For MENSA
	if (0xA0 == *pbHwID) {
		GSE_LOG("the chip is Mensa pbPCode = %d.\n",*pbPCode);
		return MC3XXX_RETCODE_SUCCESS;
	}
    return MC3XXX_RETCODE_ERROR_IDENTIFICATION;
}
/*****************************************
 *** MC3XXX_Read_Chip_ID
 *****************************************/
static int MC3XXX_Read_Chip_ID(struct i2c_client *client, char *buf)
{
	u8	 _bChipID[4] = { 0 };

	GSE_LOG("[%s]\n", __func__);

	if (!buf || !client)
		return -EINVAL;

	if (MC3XXX_i2c_read_block(client, 0x3C, _bChipID, 4)) {
		GSE_ERR("[%s] i2c read fail\n", __func__);
		_bChipID[0] = 0;
		_bChipID[1] = 0;
		_bChipID[2] = 0;
		_bChipID[3] = 0;
	}

	GSE_LOG("[%s] %02X-%02X-%02X-%02X\n", __func__, _bChipID[3], _bChipID[2], _bChipID[1], _bChipID[0]);

	return sprintf(buf, "%02X-%02X-%02X-%02X\n", _bChipID[3], _bChipID[2], _bChipID[1], _bChipID[0]);
}
static int MC3XXX_app_set_info(void)
{
	struct mc3xxx_i2c_data	 *_pt_i2c_data = mc3xxx_obj_i2c_data;
	u8	 _bChipID[4] = { 0 };
	int err=0;

	GSE_LOG("[%s]\n", __func__);

	if (MC3XXX_i2c_read_block(_pt_i2c_data->client, 0x3C, _bChipID, 4)) {
		GSE_ERR("[%s] i2c read fail\n", __func__);
		_bChipID[0] = 0;
		_bChipID[1] = 0;
		_bChipID[2] = 0;
		_bChipID[3] = 0;
	}
	err = app_info_set("G-Sensor", "MC3416");
	if(err < 0){
	    GSE_LOG("mc3416 failed to add app_info\n");
		}
#ifdef CONFIG_HUAWEI_HW_I2C_DCT
    set_hw_dev_flag(DEV_I2C_G_SENSOR);
#endif
	GSE_LOG("[%s] %02X-%02X-%02X-%02X\n", __func__, _bChipID[3], _bChipID[2], _bChipID[1], _bChipID[0]);
	return err;

}
/*****************************************
 *** MC3XXX_Read_Reg_Map
 *****************************************/
static int MC3XXX_Read_Reg_Map(struct i2c_client *p_i2c_client, u8 *pbUserBuf)
{
	u8	 _baData[MC3XXX_REGMAP_LENGTH] = { 0 };
	int	_nIndex = 0;

	GSE_LOG("[%s]\n", __func__);

	if (NULL == p_i2c_client)
		return (-EINVAL);

	for (_nIndex = 0; _nIndex < MC3XXX_REGMAP_LENGTH; _nIndex++) {
		MC3XXX_i2c_read_block(p_i2c_client, _nIndex, &_baData[_nIndex], 1);

		if (NULL != pbUserBuf)
			pbUserBuf[_nIndex] = _baData[_nIndex];

		GSE_LOG("[Gsensor] REG[0x%02X] = 0x%02X\n", _nIndex, _baData[_nIndex]);
	}

	//mcube_write_log_data(p_i2c_client, _baData);

	return 0;
}
/*****************************************
 *** MC3XXX_SaveDefaultOffset
 *****************************************/
//static void MC3XXX_SaveDefaultOffset(struct i2c_client *p_i2c_client)
//{
//	GSE_LOG("[%s]\n", __func__);
//
//	MC3XXX_i2c_read_block(p_i2c_client, 0x21, &s_baOTP_OffsetData[0], 3);
//	MC3XXX_i2c_read_block(p_i2c_client, 0x24, &s_baOTP_OffsetData[3], 3);
//
//	GSE_LOG("s_baOTP_OffsetData: 0x%02X - 0x%02X - 0x%02X - 0x%02X - 0x%02X - 0x%02X\n",
//		s_baOTP_OffsetData[0], s_baOTP_OffsetData[1], s_baOTP_OffsetData[2],
//		s_baOTP_OffsetData[3], s_baOTP_OffsetData[4], s_baOTP_OffsetData[5]);
//}
/*****************************************
 *** MC3XXX_ReadData
 *****************************************/
static int	MC3XXX_ReadData(struct i2c_client *pt_i2c_client, s16 waData[MC3XXX_AXES_NUM])
{
	u8	_baData[MC3XXX_DATA_LEN] = { 0 };

	if (MC3XXX_i2c_read_block(pt_i2c_client, MC3XXX_REG_XOUT_EX_L, _baData,
	MC3XXX_HIGH_REOLUTION_DATA_SIZE)) {
		GSE_ERR("ERR: fail to read data via I2C!\n");

	return MC3XXX_RETCODE_ERROR_I2C;
	}

	waData[MC3XXX_AXIS_X] = ((signed short) ((_baData[0]) | (_baData[1]<<8)));
	waData[MC3XXX_AXIS_Y] = ((signed short) ((_baData[2]) | (_baData[3]<<8)));
	waData[MC3XXX_AXIS_Z] = ((signed short) ((_baData[4]) | (_baData[5]<<8)));

	GSE_LOG("[%s][high] X: %d, Y: %d, Z: %d\n",
			__func__, waData[MC3XXX_AXIS_X], waData[MC3XXX_AXIS_Y], waData[MC3XXX_AXIS_Z]);

	if (s_bMPOL & 0x01)
		waData[MC3XXX_AXIS_X] = -waData[MC3XXX_AXIS_X];
	if (s_bMPOL & 0x02)
		waData[MC3XXX_AXIS_Y] = -waData[MC3XXX_AXIS_Y];
	#ifdef CONFIG_HUAWEI_DSM
	if(waData[MC3XXX_AXIS_X] == 0 && waData[MC3XXX_AXIS_Y] == 0 && waData[MC3XXX_AXIS_Z] == 0)
	{
		gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ALL_ZERO);
	}
	#endif
	return MC3XXX_RETCODE_SUCCESS;
}

/*****************************************
 *** MC3XXX_ReadOffset
 *****************************************/
//static int MC3XXX_ReadOffset(struct i2c_client *client, s16 ofs[MC3XXX_AXES_NUM])
//{
//	int err = 0;
//	u8 off_data[6] = {0};
//
//		err = MC3XXX_i2c_read_block(client, MC3XXX_REG_XOUT_EX_L, off_data, MC3XXX_DATA_LEN);
//		if (err) {
//			GSE_ERR("error: %d\n", err);
//			return err;
//		}
//		ofs[MC3XXX_AXIS_X] = ((s16)(off_data[0]))|((s16)(off_data[1])<<8);
//		ofs[MC3XXX_AXIS_Y] = ((s16)(off_data[2]))|((s16)(off_data[3])<<8);
//		ofs[MC3XXX_AXIS_Z] = ((s16)(off_data[4]))|((s16)(off_data[5])<<8);
//
//
//	GSE_LOG("MC3XXX_ReadOffset %d %d %d\n", ofs[MC3XXX_AXIS_X] , ofs[MC3XXX_AXIS_Y], ofs[MC3XXX_AXIS_Z]);
//
//		if (s_bMPOL & 0x01)
//			ofs[0] = -ofs[0];
//		if (s_bMPOL & 0x02)
//			ofs[1] = -ofs[1];
//	return err;
//}

/*****************************************
 *** MC3XXX_ResetCalibration
 *****************************************/
static int MC3XXX_ResetCalibration(struct i2c_client *client)
{
	struct mc3xxx_i2c_data *obj = i2c_get_clientdata(client);
	u8 buf[MC3XXX_AXES_NUM] = {0x00, 0x00, 0x00};
	s16 tmp = 0;
	int err = 0;
	u8  bMsbFilter	   = 0x3F;
	s16 wSignBitMask	 = 0x2000;
	s16 wSignPaddingBits = 0xC000;

	return 0;
	buf[0] = 0x43;
	err = MC3XXX_i2c_write_block(client, 0x07, buf, 1);
	if (err)
		GSE_ERR("error 0x07: %d\n", err);

	err = MC3XXX_i2c_write_block(client, 0x21, offset_buf, 6);
	if (err)
		GSE_ERR("error: %d\n", err);

	buf[0] = 0x41;
	err = MC3XXX_i2c_write_block(client, 0x07, buf, 1);
	if (err)
		GSE_ERR("error: %d\n", err);

	mdelay(20);

	if (IS_MCFM12() || IS_MCFM3X() || IS_MENSA()) {
		bMsbFilter	   = 0x7F;
		wSignBitMask	 = 0x4000;
		wSignPaddingBits = 0x8000;
	}

	tmp = ((offset_buf[1] & bMsbFilter) << 8) + offset_buf[0];
	if (tmp & wSignBitMask)
		tmp |= wSignPaddingBits;
	offset_data[0] = tmp;

	tmp = ((offset_buf[3] & bMsbFilter) << 8) + offset_buf[2];
	if (tmp & wSignBitMask)
		tmp |= wSignPaddingBits;
	offset_data[1] = tmp;

	tmp = ((offset_buf[5] & bMsbFilter) << 8) + offset_buf[4];
	if (tmp & wSignBitMask)
		tmp |= wSignPaddingBits;
	offset_data[2] = tmp;

	memset(obj->cali_sw, 0x00, sizeof(obj->cali_sw));

	return err;
}

/*****************************************
 *** MC3XXX_ReadCalibration
 *****************************************/
static int MC3XXX_ReadCalibration(struct i2c_client *client, int dat[MC3XXX_AXES_NUM])
{
    struct mc3xxx_i2c_data *obj = i2c_get_clientdata(client);

	dat[obj->cvt.map[MC3XXX_AXIS_X]] = obj->cvt.sign[MC3XXX_AXIS_X]*obj->cali_sw[MC3XXX_AXIS_X];
    dat[obj->cvt.map[MC3XXX_AXIS_Y]] = obj->cvt.sign[MC3XXX_AXIS_Y]*obj->cali_sw[MC3XXX_AXIS_Y];
    dat[obj->cvt.map[MC3XXX_AXIS_Z]] = obj->cvt.sign[MC3XXX_AXIS_Z]*obj->cali_sw[MC3XXX_AXIS_Z];


    GSE_LOG("dat[x,y,z] %d %d %d \n",dat[MC3XXX_AXIS_X] ,dat[MC3XXX_AXIS_Y],dat[MC3XXX_AXIS_Z]);

    GSE_LOG("dat[mapxyz] %d %d %d \n",dat[obj->cvt.map[MC3XXX_AXIS_X]] ,dat[obj->cvt.map[MC3XXX_AXIS_Y]],dat[obj->cvt.map[MC3XXX_AXIS_Z]]);

    return 0;
}

/*****************************************
 *** MC3XXX_Convertxyz
  *****************************************/
static void  MC3XXX_Calixyz(struct SENSOR_DATA *sensor_data)
{
	struct SENSOR_DATA data;
	data.x = 0-sensor_data->x;
	data.y = 0-sensor_data->y;
	data.z = GRAVITY_EARTH_1000 - sensor_data->z;
	GSE_LOG("no convert data  %d %d %d \n",data.x,data.y,data.z);
	if(((data.x >= 6807) && (data.x <= 12807)))
	{
		sensor_data->x = GRAVITY_EARTH_1000 - data.x;
		sensor_data->y = 0 - data.y;
		sensor_data->z = 0 - data.z;
	}else if(((0-data.x) > 6807) && (0-data.x) <= 12807)
	{
		sensor_data->x = 0-(GRAVITY_EARTH_1000 + data.x);
		sensor_data->y = 0 - data.y;
		sensor_data->z = 0 - data.z;
	}else if(((data.y >= 6807) && (data.y <= 12807)))
	{
		sensor_data->x = 0 - data.x;
		sensor_data->y = GRAVITY_EARTH_1000 - data.y;
		sensor_data->z = 0 - data.z;
	}else if(((0-data.y) > 6807) && (0-data.y) <= 12807)
	{
		sensor_data->x = 0 - data.x;
		sensor_data->y = 0-(GRAVITY_EARTH_1000 + data.y);
		sensor_data->z = 0 - data.z;
	}else if(((data.x >= 6807) && (data.x <= 12807)))
	{
		sensor_data->x = 0 - data.x;
		sensor_data->y = 0 - data.y;
		sensor_data->z = GRAVITY_EARTH_1000 - data.z;
	}else if(((0-data.z) > 6807) && (0-data.z) <= 12807)
	{
		sensor_data->x = 0 - data.x;
		sensor_data->y = 0 - data.y;
		sensor_data->z = 0-(GRAVITY_EARTH_1000 + data.z);
	}
	GSE_LOG("convert data  %d %d %d \n",sensor_data->x,sensor_data->y,sensor_data->z);

}

/*****************************************
 *** MC3XXX_WriteCalibration
 *****************************************/
static int MC3XXX_WriteCalibration(struct i2c_client *client, int dat[MC3XXX_AXES_NUM])
{
	struct mc3xxx_i2c_data *obj = i2c_get_clientdata(client);
	int err = 0;
	int cali[MC3XXX_AXES_NUM] = {0};



	GSE_LOG("UPDATE dat: (%+3d %+3d %+3d)\n", dat[MC3XXX_AXIS_X], dat[MC3XXX_AXIS_Y], dat[MC3XXX_AXIS_Z]);

	cali[MC3XXX_AXIS_X] = dat[MC3XXX_AXIS_X];
    cali[MC3XXX_AXIS_Y] = dat[MC3XXX_AXIS_Y];
    cali[MC3XXX_AXIS_Z] = dat[MC3XXX_AXIS_Z];
	GSE_LOG("MC3XXX_WriteCalibration:cali %d %d %d \n",cali[MC3XXX_AXIS_X] ,cali[MC3XXX_AXIS_Y],cali[MC3XXX_AXIS_Z]);

    obj->cali_sw[MC3XXX_AXIS_X] = obj->cvt.sign[MC3XXX_AXIS_X]*(cali[obj->cvt.map[MC3XXX_AXIS_X]]);
    obj->cali_sw[MC3XXX_AXIS_Y] = obj->cvt.sign[MC3XXX_AXIS_Y]*(cali[obj->cvt.map[MC3XXX_AXIS_Y]]);
    obj->cali_sw[MC3XXX_AXIS_Z] = obj->cvt.sign[MC3XXX_AXIS_Z]*(cali[obj->cvt.map[MC3XXX_AXIS_Z]]);

	GSE_LOG("UPDATE dat:obj->cali_sw: (%3d %3d %3d)\n", obj->cali_sw[MC3XXX_AXIS_X], obj->cali_sw[MC3XXX_AXIS_Y], obj->cali_sw[MC3XXX_AXIS_Z]);
	GSE_LOG("MC3XXX_WriteCalibration:dat[map x,y,x] %d %d %d \n",dat[obj->cvt.map[MC3XXX_AXIS_X]] ,dat[obj->cvt.map[MC3XXX_AXIS_Y]],dat[obj->cvt.map[MC3XXX_AXIS_Z]]);

	return err;
}

/*****************************************
 *** MC3XXX_SetPowerMode
 *****************************************/
static int MC3XXX_SetPowerMode(struct i2c_client *client, bool enable)
{
	u8 databuf[2] = {0};
	int res = 0;
	u8 addr = MC3XXX_REG_MODE_FEATURE;
	struct mc3xxx_i2c_data *obj = i2c_get_clientdata(client);

	if (enable == mc3xxx_sensor_power)
		GSE_LOG("Sensor power status should not be set again!!!\n");

	if (MC3XXX_i2c_read_block(client, addr, databuf, 1)) {
		GSE_ERR("read power ctl register err!\n");
		return MC3XXX_RETCODE_ERROR_I2C;
	}

	GSE_LOG("set power read MC3XXX_REG_MODE_FEATURE =%x\n", databuf[0]);

	if (enable) {
		databuf[0] = 0xC1;
		res = MC3XXX_i2c_write_block(client, MC3XXX_REG_MODE_FEATURE, databuf, 1);
	} else {
		databuf[0] = 0xC3;
		res = MC3XXX_i2c_write_block(client, MC3XXX_REG_MODE_FEATURE, databuf, 1);
	}

	if (res < 0) {
		GSE_LOG("fwq set power mode failed!\n");
		return MC3XXX_RETCODE_ERROR_I2C;
	} else if (atomic_read(&obj->trace) & MCUBE_TRC_INFO)
		GSE_LOG("fwq set power mode ok %d!\n", databuf[1]);

	mc3xxx_sensor_power = enable;

	return MC3XXX_RETCODE_SUCCESS;
}



/*****************************************
 *** MC3XXX_SetSampleRate
 *****************************************/
static void MC3XXX_SetSampleRate(struct i2c_client *pt_i2c_client)
{
	unsigned char	_baDataBuf[2] = { 0 },_baData2Buf[2] = { 0 };

	GSE_LOG("[%s]\n", __func__);

	_baDataBuf[0] = MC3XXX_REG_SAMPLE_RATE;
	_baDataBuf[1] = 0x00;

	if (IS_MCFM12() || IS_MCFM3X()) {
		_baData2Buf[0] = 0x2A;
		MC3XXX_i2c_read_block(pt_i2c_client, 0x2A, _baData2Buf, 1);

		GSE_LOG("[%s] REG(0x2A) = 0x%02X\n", __func__, _baData2Buf[0]);

		_baData2Buf[0] = (_baData2Buf[0] & 0xC0);

		switch (_baData2Buf[0]) {
			case 0x00:
				_baDataBuf[0] = 0x00;
				break;
			case 0x40:
				_baDataBuf[0] = 0x08;
				break;
			case 0x80:
				_baDataBuf[0] = 0x09;
				break;
			case 0xC0:
				_baDataBuf[0] = 0x0A;
				break;
			default:
				GSE_ERR("[%s] no chance to get here... check code!\n", __func__);
				break;
		}
	}else if (IS_MENSA()){
		MC3XXX_i2c_read_block(pt_i2c_client, MC3XXX_REG_SAMPLE_RATE, _baDataBuf, 1);

		GSE_LOG("[%s] REG(0x08) = 0x%02X\n", __func__, _baDataBuf[0]);

		_baDataBuf[0] = ((_baDataBuf[0] & 0xF8) | 0x04);

	} else
		_baDataBuf[0] = 0x00;

	MC3XXX_i2c_write_block(pt_i2c_client, MC3XXX_REG_SAMPLE_RATE, _baDataBuf, 1);
}

/*****************************************
 *** MC3XXX_LPF
 *****************************************/
static void MC3XXX_LPF(struct i2c_client *pt_i2c_client)
{
	unsigned char	_baDataBuf[2] = { 0 };
	int res = 0;

	MC3XXX_i2c_read_block(pt_i2c_client, MC3XXX_REG_RANGE_CONTROL, _baDataBuf, 1);
	GSE_LOG("[%s] REG(0x08) = 0x%02X\n", __func__, _baDataBuf[0]);
	_baDataBuf[0] = ((_baDataBuf[0] & 0xF0) | 0x09);
	MC3XXX_i2c_write_block(pt_i2c_client, MC3XXX_REG_SAMPLE_RATE, _baDataBuf, 1);
	if (res < 0)
		GSE_ERR("MC3XXX_LPF failed.\n");
}
/*****************************************
 *** MC3XXX_ConfigRegRange
 *****************************************/
static void MC3XXX_ConfigRegRange(struct i2c_client *pt_i2c_client)
{
	unsigned char	_baDataBuf[2] = { 0 };
	int res = 0;

	/* _baDataBuf[0] = 0x3F; */
	/* Modify low pass filter bandwidth to 512hz, for solving sensor data don't change issue */
	//_baDataBuf[0] = 0x25;
	//_baDataBuf[0] = 0x21;//for mensa

	MC3XXX_i2c_read_block(pt_i2c_client, MC3XXX_REG_RANGE_CONTROL, _baDataBuf, 1);
	GSE_LOG("[%s] REG(0x08) = 0x%02X\n", __func__, _baDataBuf[0]);
	_baDataBuf[0] = ((_baDataBuf[0] & 0x8F) | 0x20);

	res = MC3XXX_i2c_write_block(pt_i2c_client, MC3XXX_REG_RANGE_CONTROL, _baDataBuf, 1);
	if (res < 0)
		GSE_ERR("MC3XXX_ConfigRegRange fail\n");

	GSE_LOG("[%s] set 0x%X\n", __func__, _baDataBuf[1]);
}

/*****************************************
 *** MC3XXX_SetGain
 *****************************************/
static void MC3XXX_SetGain(void)
{
	gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = 4096;


	GSE_LOG("[%s] gain: %d / %d / %d\n", __func__, gsensor_gain.x, gsensor_gain.y, gsensor_gain.z);
}

/*****************************************
 *** MC3XXX_Init
 *****************************************/
static int MC3XXX_Init(struct i2c_client *client, int reset_cali)
{
	unsigned char	_baDataBuf[2] = { 0 };

	GSE_LOG("[%s]\n", __func__);

	#ifdef _MC3XXX_SUPPORT_POWER_SAVING_SHUTDOWN_POWER_
	if (MC3XXX_RETCODE_SUCCESS != _mc3xxx_i2c_auto_probe(client))
		return MC3XXX_RETCODE_ERROR_I2C;

	/* GSE_LOG("[%s] confirmed i2c addr: 0x%X\n", __FUNCTION__, client->addr); */
	#endif

	_baDataBuf[0] = 0x43;
	MC3XXX_i2c_write_block(client, MC3XXX_REG_MODE_FEATURE, _baDataBuf, 1);

	MC3XXX_SetSampleRate(client);
	MC3XXX_LPF(client);
	MC3XXX_ConfigRegRange(client);
	MC3XXX_SetGain();

	_baDataBuf[0] = 0x04;
	MC3XXX_i2c_write_block(client, MC3XXX_REG_MOTION_CTRL, _baDataBuf, 1);

	_baDataBuf[0] = 0x00;
	MC3XXX_i2c_write_block(client, MC3XXX_REG_INTERRUPT_ENABLE, _baDataBuf, 1);

	_baDataBuf[0] = 0;
	MC3XXX_i2c_read_block(client, 0x2A, _baDataBuf, 1);
	s_bMPOL = (_baDataBuf[0] & 0x03);

	GSE_LOG("[%s] init ok.\n", __func__);

	return MC3XXX_RETCODE_SUCCESS;
}

/*****************************************
 *** MC3XXX_ReadChipInfo
 *****************************************/
static int MC3XXX_ReadChipInfo(struct i2c_client *client, char *buf, int bufsize)
{
	if ((NULL == buf) || (bufsize <= 30))
		return -1;

	if (NULL == client) {
		*buf = 0;
		return -2;
	}

	sprintf(buf, "MC3416 Chip");
	return 0;
}

/*****************************************
 *** MC3XXX_ReadSensorData
 *****************************************/
static int MC3XXX_ReadSensorData(struct i2c_client *pt_i2c_client, char *pbBuf, int nBufSize)
{
	int					   _naAccelData[MC3XXX_AXES_NUM] = { 0 };
	struct mc3xxx_i2c_data   *_pt_i2c_obj = ((struct mc3xxx_i2c_data *) i2c_get_clientdata(pt_i2c_client));

	if ((NULL == pt_i2c_client) || (NULL == pbBuf)) {
		GSE_ERR("ERR: Null Pointer\n");
		return MC3XXX_RETCODE_ERROR_NULL_POINTER;
	}

	if (false == mc3xxx_sensor_power) {
		if (MC3XXX_RETCODE_SUCCESS != MC3XXX_SetPowerMode(pt_i2c_client, true))
			GSE_ERR("ERR: fail to set power mode!\n");
	}



	if (MC3XXX_RETCODE_SUCCESS != MC3XXX_ReadData(pt_i2c_client, _pt_i2c_obj->data)) {
		GSE_ERR("ERR: fail to read data!\n");

		return MC3XXX_RETCODE_ERROR_I2C;
	}

	/* output format: mg */
	if (atomic_read(&_pt_i2c_obj->trace) & MCUBE_TRC_INFO)
		GSE_LOG("[%s] raw data: %d, %d, %d\n", __func__, _pt_i2c_obj->data[MC3XXX_AXIS_X],
			_pt_i2c_obj->data[MC3XXX_AXIS_Y], _pt_i2c_obj->data[MC3XXX_AXIS_Z]);

	_naAccelData[(_pt_i2c_obj->cvt.map[MC3XXX_AXIS_X])] = (_pt_i2c_obj->cvt.sign[MC3XXX_AXIS_X]
		* _pt_i2c_obj->data[MC3XXX_AXIS_X]);
	_naAccelData[(_pt_i2c_obj->cvt.map[MC3XXX_AXIS_Y])] = (_pt_i2c_obj->cvt.sign[MC3XXX_AXIS_Y]
		* _pt_i2c_obj->data[MC3XXX_AXIS_Y]);
	_naAccelData[(_pt_i2c_obj->cvt.map[MC3XXX_AXIS_Z])] = (_pt_i2c_obj->cvt.sign[MC3XXX_AXIS_Z]
		* _pt_i2c_obj->data[MC3XXX_AXIS_Z]);

	if (atomic_read(&_pt_i2c_obj->trace) & MCUBE_TRC_INFO)
		GSE_LOG("[%s] map data: %d, %d, %d!\n", __func__, _naAccelData[MC3XXX_AXIS_X],
			_naAccelData[MC3XXX_AXIS_Y], _naAccelData[MC3XXX_AXIS_Z]);

	_naAccelData[MC3XXX_AXIS_X] = (_naAccelData[MC3XXX_AXIS_X] * GRAVITY_EARTH_1000 / gsensor_gain.x);
	_naAccelData[MC3XXX_AXIS_Y] = (_naAccelData[MC3XXX_AXIS_Y] * GRAVITY_EARTH_1000 / gsensor_gain.y);
	_naAccelData[MC3XXX_AXIS_Z] = (_naAccelData[MC3XXX_AXIS_Z] * GRAVITY_EARTH_1000 / gsensor_gain.z);

	if (atomic_read(&_pt_i2c_obj->trace) & MCUBE_TRC_INFO)
		GSE_LOG("[%s] accel data: %d, %d, %d!\n", __func__, _naAccelData[MC3XXX_AXIS_X],
			_naAccelData[MC3XXX_AXIS_Y], _naAccelData[MC3XXX_AXIS_Z]);

	sprintf(pbBuf, "%04x %04x %04x",
		_naAccelData[MC3XXX_AXIS_X], _naAccelData[MC3XXX_AXIS_Y], _naAccelData[MC3XXX_AXIS_Z]);

	return MC3XXX_RETCODE_SUCCESS;
}


/*****************************************
 *** MC3XXX_ReadRawData
 *****************************************/
static int MC3XXX_ReadRawData(struct i2c_client *client, char *buf)
{
	int res = 0;
	s16 sensor_data[3] = {0};

	if (!buf || !client)
		return -EINVAL;

	if (mc3xxx_sensor_power == false) {
		res = MC3XXX_SetPowerMode(client, true);
		if (res)
			GSE_ERR("Power on mc3xxx error %d!\n", res);
	}

	res = MC3XXX_ReadData(client, sensor_data);
	if (res) {
		GSE_ERR("I2C error: ret value=%d", res);
		return -EIO;
	}
	sprintf(buf, "%04x %04x %04x", sensor_data[MC3XXX_AXIS_X],
	sensor_data[MC3XXX_AXIS_Y], sensor_data[MC3XXX_AXIS_Z]);

	return 0;
}

/*****************************************
 *** MC3XXX_JudgeTestResult
 *****************************************/
static int MC3XXX_JudgeTestResult(struct i2c_client *client)
{
	int	res				  = 0;
	int	self_result		  = 0;
	s16	acc[MC3XXX_AXES_NUM] = { 0 };

	res = MC3XXX_ReadData(client, acc);
	if (res) {
		GSE_ERR("I2C error: ret value=%d", res);
		return -EIO;
	}

	acc[MC3XXX_AXIS_X] = acc[MC3XXX_AXIS_X] * 1000 / gsensor_gain.x;
	acc[MC3XXX_AXIS_Y] = acc[MC3XXX_AXIS_Y] * 1000 / gsensor_gain.y;
	acc[MC3XXX_AXIS_Z] = acc[MC3XXX_AXIS_Z] * 1000 / gsensor_gain.z;

	self_result = ((acc[MC3XXX_AXIS_X] * acc[MC3XXX_AXIS_X])
		   + (acc[MC3XXX_AXIS_Y] * acc[MC3XXX_AXIS_Y])
		   + (acc[MC3XXX_AXIS_Z] * acc[MC3XXX_AXIS_Z]));

	if ((self_result > 475923) && (self_result < 2185360)) {
		GSE_ERR("MC3XXX_JudgeTestResult successful\n");
		return MC3XXX_RETCODE_SUCCESS;
	}

	GSE_ERR("MC3XXX_JudgeTestResult failt\n");
	return -EINVAL;
}

/*****************************************
 *** MC3XXX_SelfCheck
 *****************************************/
static void MC3XXX_SelfCheck(struct i2c_client *client, u8 *pUserBuf)
{
	u8	_bRData1 = 0;
	u8	_bRData2 = 0;
	u8	_bRData3 = 0;
	u8	_baDataBuf[2] = { 0 };

	MC3XXX_i2c_read_block(client, 0x20, &_bRData1, 1);
	MC3XXX_i2c_read_block(client, 0x3B, &_bRData2, 1);

	_baDataBuf[0] = 0x43;
	MC3XXX_i2c_write_block(client, 0x07, _baDataBuf, 1);

	mdelay(10);

	for ( ; ; ) {
		_baDataBuf[0] = 0x6D;
		MC3XXX_i2c_write_block(client, 0x1B, _baDataBuf, 1);

		_baDataBuf[0] = 0x43;
		MC3XXX_i2c_write_block(client, 0x1B, _baDataBuf, 1);

		_bRData3 = 0x00;
		MC3XXX_i2c_read_block(client, 0x04, &_bRData3, 1);

		if (_bRData3 & 0x40)
			break;
	}

	_baDataBuf[0] = (_bRData2 & 0xFE);
	MC3XXX_i2c_write_block(client, 0x3B, _baDataBuf, 1);

	_baDataBuf[0] = 0x03;
	MC3XXX_i2c_write_block(client, 0x20, _baDataBuf, 1);

	_baDataBuf[0] = 0x40;
	MC3XXX_i2c_write_block(client, 0x14, _baDataBuf, 1);

	mdelay(10);

	_baDataBuf[0] = pUserBuf[0];
	MC3XXX_i2c_write_block(client, 0x00, _baDataBuf, 1);

	_baDataBuf[0] = 0x41;
	MC3XXX_i2c_write_block(client, 0x07, _baDataBuf, 1);

	mdelay(10);

	_baDataBuf[0] = 0x43;
	MC3XXX_i2c_write_block(client, 0x07, _baDataBuf, 1);

	mdelay(10);

	//MC3XXX_Read_Reg_Map(client, pUserBuf);

	mdelay(10);

	_baDataBuf[0] = 0x00;
	MC3XXX_i2c_write_block(client, 0x14, _baDataBuf, 1);

	_baDataBuf[0] = _bRData1;
	MC3XXX_i2c_write_block(client, 0x20, _baDataBuf, 1);

	_baDataBuf[0] = _bRData2;
	MC3XXX_i2c_write_block(client, 0x3B, _baDataBuf, 1);

	mdelay(10);

	for ( ; ; ) {
		_baDataBuf[0] = 0x6D;
		MC3XXX_i2c_write_block(client, 0x1B, _baDataBuf, 1);

		_baDataBuf[0] = 0x43;
		MC3XXX_i2c_write_block(client, 0x1B, _baDataBuf, 1);

		_bRData3 = 0xFF;
		MC3XXX_i2c_read_block(client, 0x04, &_bRData3, 1);

		if (!(_bRData3 & 0x40))
			break;
	}

	mdelay(10);
}

/*****************************************
 *** MC3XXX_GetOpenStatus
 *****************************************/
#ifdef _MC3XXX_SUPPORT_PERIODIC_DOC_
static int	MC3XXX_GetOpenStatus(void)
{
	/* GSE_LOG("[%s] %d\n", __FUNCTION__, atomic_read(&s_t_mc3xxx_open_status)); */

	wait_event_interruptible(wq_mc3xxx_open_status, (atomic_read(&s_t_mc3xxx_open_status) != 0));

	/* GSE_LOG("[%s] pass wait_event_interruptible: %d\n", __FUNCTION__, atomic_read(&s_t_mc3xxx_open_status)); */

	return atomic_read(&s_t_mc3xxx_open_status);
}
#endif

/*****************************************
 *** show_chipinfo_value
 *****************************************/
static ssize_t show_chipinfo_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = mc3xxx_i2c_client;
	char strbuf[MC3XXX_BUF_SIZE] = {0};

	GSE_LOG("fwq show_chipinfo_value\n");
	if (NULL == client) {
		GSE_ERR("i2c client is null!!\n");
		return 0;
	}

	MC3XXX_ReadChipInfo(client, strbuf, MC3XXX_BUF_SIZE);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);
}

/*****************************************
 *** show_sensordata_value
 *****************************************/
static ssize_t show_sensordata_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = mc3xxx_i2c_client;
	char strbuf[MC3XXX_BUF_SIZE] = { 0 };

	if (NULL == client) {
		GSE_ERR("i2c client is null!!\n");
		return 0;
	}

	mc3xxx_mutex_lock();
	MC3XXX_ReadSensorData(client, strbuf, MC3XXX_BUF_SIZE);
	mc3xxx_mutex_unlock();
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);
}

/*****************************************
 *** show_selftest_value
 *****************************************/
static ssize_t show_selftest_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = mc3xxx_i2c_client;

	if (NULL == client) {
		GSE_ERR("i2c client is null!!\n");
		return 0;
	}

	return snprintf(buf, 8, "%s\n", selftestRes);
}

/*****************************************
 *** store_selftest_value
 *****************************************/
static ssize_t store_selftest_value(struct device_driver *ddri, const char *buf, size_t count)
{   /*write anything to this register will trigger the process*/
	struct i2c_client *client = mc3xxx_i2c_client;
	int num = 0;
	int ret = 0;

	ret = kstrtoint(buf, 10, &num);
	if (ret != 0) {
		GSE_ERR("parse number fail\n");
		return count;
	} else if (0 == num) {
		GSE_ERR("invalid data count\n");
		return count;
	}

	GSE_LOG("NORMAL:\n");
	mc3xxx_mutex_lock();
	MC3XXX_SetPowerMode(client, true);
	mc3xxx_mutex_unlock();
	GSE_LOG("SELFTEST:\n");

	if (!MC3XXX_JudgeTestResult(client)) {
		GSE_LOG("SELFTEST : PASS\n");
		strcpy(selftestRes, "y");
	} else {
		GSE_LOG("SELFTEST : FAIL\n");
		strcpy(selftestRes, "n");
	}

	return count;
}



/*****************************************
 *** show_trace_value
 *****************************************/
static ssize_t show_trace_value(struct device_driver *ddri, char *buf)
{
	ssize_t res = 0;
	struct mc3xxx_i2c_data *obj = mc3xxx_obj_i2c_data;

	GSE_LOG("fwq show_trace_value\n");

	if (obj == NULL) {
		GSE_ERR("i2c_data obj is null!!\n");
		return 0;
	}

	res = snprintf(buf, PAGE_SIZE, "0x%04X\n", atomic_read(&obj->trace));
	return res;
}

/*****************************************
 *** store_trace_value
 *****************************************/
static ssize_t store_trace_value(struct device_driver *ddri, const char *buf, size_t count)
{
	struct mc3xxx_i2c_data *obj = mc3xxx_obj_i2c_data;
	int trace = 0;

	GSE_LOG("fwq store_trace_value\n");

	if (obj == NULL) {
		GSE_ERR("i2c_data obj is null!!\n");
		return 0;
	}

	if (1 == sscanf(buf, "0x%x", &trace))
		atomic_set(&obj->trace, trace);
	else
		GSE_ERR("invalid content: '%s', length = %zu\n", buf, count);

	return count;
}

/*****************************************
 *** show_status_value
 *****************************************/
static ssize_t show_status_value(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	struct mc3xxx_i2c_data *obj = mc3xxx_obj_i2c_data;

	GSE_LOG("fwq show_status_value\n");

	if (obj == NULL) {
		GSE_ERR("i2c_data obj is null!!\n");
		return 0;
	}

	if (obj->hw)
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: %d %d (%d %d)\n",
			obj->hw->i2c_num, obj->hw->direction, obj->hw->power_id, obj->hw->power_vol);
	else
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: NULL\n");

	return len;
}

/*****************************************
 *** show_power_status
 *****************************************/
static ssize_t show_power_status(struct device_driver *ddri, char *buf)
{
	ssize_t res = 0;
	u8 uData = 0;
	struct mc3xxx_i2c_data *obj = mc3xxx_obj_i2c_data;

	if (obj == NULL) {
		GSE_ERR("i2c_data obj is null!!\n");
		return 0;
	}
	MC3XXX_i2c_read_block(obj->client, MC3XXX_REG_MODE_FEATURE, &uData, 1);

	res = snprintf(buf, PAGE_SIZE, "0x%04X\n", uData);
	return res;
}

/*****************************************
 *** show_version_value
 *****************************************/
static ssize_t show_version_value(struct device_driver *ddri, char *buf)
{
		return snprintf(buf, PAGE_SIZE, "%s\n", MC3XXX_DEV_DRIVER_VERSION);
}

/*****************************************
 *** show_chip_id
 *****************************************/
static ssize_t show_chip_id(struct device_driver *ddri, char *buf)
{
	struct mc3xxx_i2c_data   *_pt_i2c_data = mc3xxx_obj_i2c_data;

	return MC3XXX_Read_Chip_ID(_pt_i2c_data->client, buf);
}

/*****************************************
 *** show_regiter_map
 *****************************************/
static ssize_t show_regiter_map(struct device_driver *ddri, char *buf)
{
	u8		 _bIndex	   = 0;
	u8		 _baRegMap[MC3XXX_REGMAP_LENGTH] = { 0 };
	ssize_t	_tLength	  = 0;

	struct i2c_client *client = mc3xxx_i2c_client;

	if ((0xA5 == buf[0]) && (0x7B == buf[1]) && (0x40 == buf[2])) {
		mc3xxx_mutex_lock();
		MC3XXX_Read_Reg_Map(client, buf);
		mc3xxx_mutex_unlock();

		buf[0x21] = s_baOTP_OffsetData[0];
		buf[0x22] = s_baOTP_OffsetData[1];
		buf[0x23] = s_baOTP_OffsetData[2];
		buf[0x24] = s_baOTP_OffsetData[3];
		buf[0x25] = s_baOTP_OffsetData[4];
		buf[0x26] = s_baOTP_OffsetData[5];

		_tLength = 64;
	} else {
		mc3xxx_mutex_lock();
		MC3XXX_Read_Reg_Map(client, _baRegMap);
		mc3xxx_mutex_unlock();

	for (_bIndex = 0; _bIndex < MC3XXX_REGMAP_LENGTH; _bIndex++)
		_tLength += snprintf((buf + _tLength), (PAGE_SIZE - _tLength), "Reg[0x%02X]: 0x%02X\n",
			_bIndex, _baRegMap[_bIndex]);
	}

	return _tLength;
}

/*****************************************
 *** store_regiter_map
 *****************************************/
static ssize_t store_regiter_map(struct device_driver *ddri, const char *buf, size_t count)
{
	return count;
}

/*****************************************
 *** show_chip_orientation
 *****************************************/
static ssize_t show_chip_orientation(struct device_driver *ptDevDrv, char *pbBuf)
{
	ssize_t		  _tLength = 0;
	struct acc_hw   *_ptAccelHw = get_cust_acc();

	GSE_LOG("[%s] default direction: %d\n", __func__, _ptAccelHw->direction);

	_tLength = snprintf(pbBuf, PAGE_SIZE, "default direction = %d\n", _ptAccelHw->direction);

	return _tLength;
}

/*****************************************
 *** store_chip_orientation
 *****************************************/
static ssize_t store_chip_orientation(struct device_driver *ptDevDrv, const char *pbBuf, size_t tCount)
{
	int _nDirection = 0;
	int ret = 0;
	struct mc3xxx_i2c_data   *_pt_i2c_obj = mc3xxx_obj_i2c_data;

	if (NULL == _pt_i2c_obj)
		return 0;

	ret = kstrtoint(pbBuf, 10, &_nDirection);
	if (ret != 0) {
		if (hwmsen_get_convert(_nDirection, &_pt_i2c_obj->cvt))
			GSE_ERR("ERR: fail to set direction\n");
	}

	GSE_LOG("[%s] set direction: %d\n", __func__, _nDirection);

	return tCount;
}

/*****************************************
 *** show_accuracy_status
 *****************************************/
static ssize_t show_accuracy_status(struct device_driver *ddri, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", s_bAccuracyStatus);
}

/*****************************************
 *** store_accuracy_status
 *****************************************/
static ssize_t store_accuracy_status(struct device_driver *ddri, const char *buf, size_t count)
{
	int	_nAccuracyStatus = 0;
	int ret = 0;

	ret = kstrtoint(buf, 10, &_nAccuracyStatus);
	if (ret != 0) {
		GSE_ERR("incorrect argument\n");
		return count;
	}

	if (SENSOR_STATUS_ACCURACY_HIGH < _nAccuracyStatus) {
		GSE_ERR("illegal accuracy status\n");
		return count;
	}

	s_bAccuracyStatus = ((int8_t) _nAccuracyStatus);

	return count;
}

/*****************************************
 *** show_selfcheck_value
 *****************************************/
static ssize_t show_selfcheck_value(struct device_driver *ptDevDriver, char *pbBuf)
{
	struct i2c_client   *_pt_i2c_client = mc3xxx_i2c_client;

	/* GSE_LOG("[%s] 0x%02X\n", __FUNCTION__, pbBuf[0]); */

	mc3xxx_mutex_lock();
	MC3XXX_SelfCheck(_pt_i2c_client, pbBuf);
	MC3XXX_Init(_pt_i2c_client, 0);
	mc3xxx_mutex_unlock();

	return 64;
}

/*****************************************
 *** store_selfcheck_value
 *****************************************/
static ssize_t store_selfcheck_value(struct device_driver *ddri, const char *buf, size_t count)
{
	/* reserved */
	/* GSE_LOG("[%s] buf[0]: 0x%02X\n", __FUNCTION__, buf[0]); */

	return count;
}

/*****************************************
 *** show_chip_validate_value
 *****************************************/
static ssize_t show_chip_validate_value(struct device_driver *ptDevDriver, char *pbBuf)
{
	unsigned char	_bChipValidation = 0;

	_bChipValidation = MC3XXX_ValidateSensorIC(&s_bPCODE, &s_bHWID);

	return snprintf(pbBuf, PAGE_SIZE, "%d\n", _bChipValidation);
}

/*****************************************
 *** show_pdoc_enable_value
 *****************************************/
static ssize_t show_pdoc_enable_value(struct device_driver *ptDevDriver, char *pbBuf)
{
	#ifdef _MC3XXX_SUPPORT_PERIODIC_DOC_
	return snprintf(pbBuf, PAGE_SIZE, "%d\n", s_bIsPDOC_Enabled);
	#else
	unsigned char	_bIsPDOC_Enabled = false;

	return snprintf(pbBuf, PAGE_SIZE, "%d\n", _bIsPDOC_Enabled);
	#endif
}


/*****************************************
 *** store_chip_register
 *****************************************/
static ssize_t store_chip_register(struct device_driver *ptDevDrv, const char *pbBuf, size_t tCount)
{
	int	_nRegister	= 0;
	int	_nValue		= 0;
	unsigned char	_baDataBuf[2] = { 0 };

	struct i2c_client	*_pt_i2c_client = mc3xxx_i2c_client;
	//struct mc3xxx_i2c_data *obj = mc3xxx_obj_i2c_data;

	//if (NULL == _pt_i2c_client)
	//	return -EINVAL;

	sscanf(pbBuf, "%x %x", &_nRegister, &_nValue);

	GSE_LOG("[%s] _nRegister: 0x%02X, _nValue: 0x%02X\n", __func__, _nRegister, _nValue);

	_baDataBuf[0] = ((u8) _nRegister);
	_baDataBuf[1] = ((u8) _nValue);
	//MC3XXX_i2c_write_block(_pt_i2c_client, _baDataBuf[0], _baDataBuf[1], 1);
	i2c_master_send(_pt_i2c_client, _baDataBuf, 0x2);

	return (tCount);
}
#ifdef CONFIG_HUAWEI_DSM
static ssize_t mc3416_show_test_dsm(struct device_driver *ddri, char *buf)
{
	return snprintf(buf,100,
		"test data_err: echo 1 > dsm_excep\n"
		"test i2c_err: 	echo 2 > dsm_excep\n"
		"test xyz_all_zero_err: echo 3 > dsm_excep\n");
}

/*
*	test data or i2c error interface for device monitor
*/
static ssize_t mc3416_store_test_dsm(struct device_driver *ddri, const char *buf, size_t tCount)
{
	int mode;
	int ret=0;

	if(sscanf(buf, "%d", &mode)!=1)
	{
		return -EINVAL;
	}
	switch(mode){
		case 1:	/*test data error interface*/
			ret = gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ABNORMAL);
			break;
		case 2: /*test i2c error interface*/
			ret = gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_I2C_ERR);
			break;
		case 3:/*test xyz_all_zero error interface*/
		    ret = gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ALL_ZERO);
		    break;
		default:
			break;
	}

	return ret;
}
#endif
/*****************************************
 *** DRIVER ATTRIBUTE LIST TABLE
 *****************************************/
static DRIVER_ATTR(chipinfo   ,		   S_IRUGO, show_chipinfo_value,   NULL);
static DRIVER_ATTR(sensordata ,		   S_IRUGO, show_sensordata_value, NULL);
static DRIVER_ATTR(selftest   , S_IWUSR | S_IRUGO, show_selftest_value,   store_selftest_value);
static DRIVER_ATTR(trace	  , S_IWUSR | S_IRUGO, show_trace_value,	  store_trace_value);
static DRIVER_ATTR(status	 ,		   S_IRUGO, show_status_value,	 NULL);
static DRIVER_ATTR(power	  ,		   S_IRUGO, show_power_status,	 NULL);
static DRIVER_ATTR(version	,		   S_IRUGO, show_version_value,	NULL);
static DRIVER_ATTR(chipid	 ,		   S_IRUGO, show_chip_id,		  NULL);
static DRIVER_ATTR(regmap	 , S_IWUSR | S_IRUGO, show_regiter_map,	  store_regiter_map);
static DRIVER_ATTR(orientation, S_IWUSR | S_IRUGO, show_chip_orientation, store_chip_orientation);
static DRIVER_ATTR(accuracy   , S_IWUSR | S_IRUGO, show_accuracy_status , store_accuracy_status);
static DRIVER_ATTR(selfcheck  , S_IWUSR | S_IRUGO, show_selfcheck_value , store_selfcheck_value);
static DRIVER_ATTR(validate   ,		   S_IRUGO, show_chip_validate_value, NULL);
static DRIVER_ATTR(pdoc	   ,		   S_IRUGO, show_pdoc_enable_value  , NULL);
static DRIVER_ATTR(reg	,			S_IWUSR | S_IRUGO		, NULL					, store_chip_register);
#ifdef CONFIG_HUAWEI_DSM
static DRIVER_ATTR(dsm_excep, 0660, mc3416_show_test_dsm, mc3416_store_test_dsm);
#endif
static struct driver_attribute   *mc3xxx_attr_list[] = {
    &driver_attr_chipinfo,
    &driver_attr_sensordata,
    &driver_attr_selftest,
    &driver_attr_trace,
    &driver_attr_status,
    &driver_attr_power,
    &driver_attr_version,
    &driver_attr_chipid,
    &driver_attr_regmap,
    &driver_attr_orientation,
    &driver_attr_accuracy,
    &driver_attr_selfcheck,
    &driver_attr_validate,
    &driver_attr_pdoc,
    &driver_attr_reg,
    #ifdef CONFIG_HUAWEI_DSM
    &driver_attr_dsm_excep,
    #endif
};

/*****************************************
 *** mc3xxx_create_attr
 *****************************************/
static int mc3xxx_create_attr(struct device_driver *driver)
{
	int idx, err = 0;
	int num = (int)(sizeof(mc3xxx_attr_list)/sizeof(mc3xxx_attr_list[0]));

	if (driver == NULL)
		return -EINVAL;

	for (idx = 0; idx < num; idx++) {
		err = driver_create_file(driver, mc3xxx_attr_list[idx]);
		if (err) {
			GSE_ERR("driver_create_file (%s) = %d\n", mc3xxx_attr_list[idx]->attr.name, err);
			break;
		}
	}
	return err;
}

/*****************************************
 *** mc3xxx_delete_attr
 *****************************************/
static int mc3xxx_delete_attr(struct device_driver *driver)
{
	int idx , err = 0;
	int num = (int)(sizeof(mc3xxx_attr_list)/sizeof(mc3xxx_attr_list[0]));

	if (driver == NULL)
		return -EINVAL;

	for (idx = 0; idx < num; idx++)
		driver_remove_file(driver, mc3xxx_attr_list[idx]);

	return err;
}


/*****************************************
 *** mc3xxx_open
 *****************************************/
static int mc3xxx_open(struct inode *inode, struct file *file)
{
	file->private_data = mc3xxx_i2c_client;

	if (file->private_data == NULL) {
		GSE_ERR("null pointer!!\n");
		return -EINVAL;
	}
	return nonseekable_open(inode, file);
}

/*****************************************
 *** mc3xxx_release
 *****************************************/
static int mc3xxx_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

/*****************************************
 *** mc3xxx_ioctl
 *****************************************/
static long mc3xxx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = (struct i2c_client *)file->private_data;
	struct mc3xxx_i2c_data *obj = (struct mc3xxx_i2c_data *)i2c_get_clientdata(client);
	char strbuf[MC3XXX_BUF_SIZE] = {0};
	void __user *data = NULL;
	struct SENSOR_DATA sensor_data = {0};
	long err = 0;
	int cali[3] = {0};

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));


	if (err) {
		GSE_ERR("access error: %08X, (%2d, %2d)\n", cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}

	switch (cmd) {
	case GSENSOR_IOCTL_INIT:
		GSE_LOG("fwq GSENSOR_IOCTL_INIT\n");
		mc3xxx_mutex_lock();
		MC3XXX_Init(client, 0);
		mc3xxx_mutex_unlock();
		break;

	case GSENSOR_IOCTL_READ_CHIPINFO:
		GSE_LOG("fwq GSENSOR_IOCTL_READ_CHIPINFO\n");
		data = (void __user *) arg;
		if (data == NULL) {
			err = -EINVAL;
			break;
		}

		MC3XXX_ReadChipInfo(client, strbuf, MC3XXX_BUF_SIZE);
		if (copy_to_user(data, strbuf, strlen(strbuf)+1)) {
			err = -EFAULT;
			break;
		}
		break;

	case GSENSOR_IOCTL_READ_SENSORDATA:
		data = (void __user *) arg;
		if (data == NULL) {
			err = -EINVAL;
        #ifdef CONFIG_HUAWEI_DSM
        gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ABNORMAL);
        #endif
			break;
		}
		mc3xxx_mutex_lock();

		MC3XXX_ReadSensorData(client, strbuf, MC3XXX_BUF_SIZE);

		mc3xxx_mutex_unlock();
		if (copy_to_user(data, strbuf, strlen(strbuf)+1)) {
			err = -EFAULT;
			break;
		}
		break;

	case GSENSOR_IOCTL_READ_GAIN:
		GSE_LOG("fwq GSENSOR_IOCTL_READ_GAIN\n");
		data = (void __user *) arg;
		if (data == NULL) {
			err = -EINVAL;
			break;
		}

		if (copy_to_user(data, &gsensor_gain, sizeof(struct GSENSOR_VECTOR3D))) {
			err = -EFAULT;
			break;
		}
		break;

	case GSENSOR_IOCTL_READ_OFFSET:
		GSE_LOG("fwq GSENSOR_IOCTL_READ_OFFSET\n");
		data = (void __user *) arg;
		if (data == NULL) {
			err = -EINVAL;
			break;
		}

		if (copy_to_user(data, &gsensor_offset, sizeof(struct GSENSOR_VECTOR3D))) {
			err = -EFAULT;
			break;
		}
		break;

	case GSENSOR_IOCTL_READ_RAW_DATA:
		GSE_LOG("fwq GSENSOR_IOCTL_READ_RAW_DATA\n");
		data = (void __user *) arg;
		if (data == NULL) {
			err = -EINVAL;
        #ifdef CONFIG_HUAWEI_DSM
        gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ABNORMAL);
        #endif
			break;
		}
		mc3xxx_mutex_lock();
		MC3XXX_ReadRawData(client, strbuf);
		mc3xxx_mutex_unlock();
		if (copy_to_user(data, strbuf, strlen(strbuf)+1)) {
			err = -EFAULT;
        #ifdef CONFIG_HUAWEI_DSM
        gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ABNORMAL);
        #endif
			break;
		}
		break;

	case GSENSOR_IOCTL_SET_CALI:
		GSE_LOG("fwq GSENSOR_IOCTL_SET_CALI!!\n");
		data = (void __user *)arg;
		if (data == NULL) {
			err = -EINVAL;
			break;
		}
		if (copy_from_user(&sensor_data, data, sizeof(sensor_data))) {
			err = -EFAULT;
			break;
		}
		if (atomic_read(&obj->suspend)) {
			GSE_ERR("Perform calibration in suspend state!!\n");
			err = -EINVAL;
		} else {
            //obj->cali_sw1[MC3XXX_AXIS_X] += sensor_data.x;
            //obj->cali_sw1[MC3XXX_AXIS_Y] += sensor_data.y;
            //obj->cali_sw1[MC3XXX_AXIS_Z] += sensor_data.z;
		    //GSE_LOG("MC3XXX_WriteCalibration %d %d %d \n",sensor_data.x ,sensor_data.y,sensor_data.z);
		    MC3XXX_Calixyz(&sensor_data);
			GSE_LOG("Calixyz Calibration %d %d %d \n",sensor_data.x ,sensor_data.y,sensor_data.z);
            cali[MC3XXX_AXIS_X] = sensor_data.x * gsensor_gain.x / GRAVITY_EARTH_1000;
			cali[MC3XXX_AXIS_Y] = sensor_data.y * gsensor_gain.y / GRAVITY_EARTH_1000;
			cali[MC3XXX_AXIS_Z] = sensor_data.z * gsensor_gain.z / GRAVITY_EARTH_1000;

			mc3xxx_mutex_lock();
			err = MC3XXX_WriteCalibration(client, cali);
			mc3xxx_mutex_unlock();
		}
		break;

	case GSENSOR_IOCTL_CLR_CALI:
		GSE_LOG("fwq GSENSOR_IOCTL_CLR_CALI!!\n");
		mc3xxx_mutex_lock();
		err = MC3XXX_ResetCalibration(client);
		mc3xxx_mutex_unlock();
		break;

	case GSENSOR_IOCTL_GET_CALI:
		GSE_LOG("fwq mc3xxx GSENSOR_IOCTL_GET_CALI\n");
		data = (void __user *)arg;
		if (data == NULL) {
			err = -EINVAL;
			break;
		}
            if((err = MC3XXX_ReadCalibration(client, cali)))
            {
                break;
            }


			sensor_data.x = cali[MC3XXX_AXIS_X] * GRAVITY_EARTH_1000 / gsensor_gain.x;
			sensor_data.y = cali[MC3XXX_AXIS_Y] * GRAVITY_EARTH_1000 / gsensor_gain.y;
			sensor_data.z = cali[MC3XXX_AXIS_Z] * GRAVITY_EARTH_1000 / gsensor_gain.z;
			GSE_LOG("MC3XXX_ReadCalibration:sensor_data[x.y.z] %d %d %d \n",sensor_data.x,sensor_data.y,sensor_data.z);
            #ifdef CONFIG_HUAWEI_DSM
		   if(sensor_data.x == 0 && sensor_data.y == 0 && sensor_data.z == 0)
		   {
			    gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ALL_ZERO);
		   }
			#endif
		if (copy_to_user(data, &sensor_data, sizeof(sensor_data))) {
			err = -EFAULT;
			break;
		}
		break;



	default:
		GSE_ERR("unknown IOCTL: 0x%08x\n", cmd);
		err = -ENOIOCTLCMD;
		break;

	}

	return err;
}

#ifdef CONFIG_COMPAT
static long mc3xxx_compat_ioctl(struct file *file, unsigned int cmd,
	   unsigned long arg)
{
	long err = 0;

	void __user *arg32 = compat_ptr(arg);

	if (!file->f_op || !file->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case COMPAT_GSENSOR_IOCTL_READ_SENSORDATA:
		if (arg32 == NULL) {
			err = -EINVAL;
			break;
		}

		err = file->f_op->unlocked_ioctl(file, GSENSOR_IOCTL_READ_SENSORDATA, (unsigned long)arg32);
		if (err) {
			GSE_ERR("GSENSOR_IOCTL_READ_SENSORDATA unlocked_ioctl failed.");
			return err;
		}
		break;

	case COMPAT_GSENSOR_IOCTL_SET_CALI:
		if (arg32 == NULL) {
			err = -EINVAL;
			break;
		}

		err = file->f_op->unlocked_ioctl(file, GSENSOR_IOCTL_SET_CALI, (unsigned long)arg32);
		if (err) {
			GSE_ERR("GSENSOR_IOCTL_SET_CALI unlocked_ioctl failed.");
			return err;
		}
		break;

	case COMPAT_GSENSOR_IOCTL_GET_CALI:
		if (arg32 == NULL) {
			err = -EINVAL;
			break;
		}

		err = file->f_op->unlocked_ioctl(file, GSENSOR_IOCTL_GET_CALI, (unsigned long)arg32);
		if (err) {
			GSE_ERR("GSENSOR_IOCTL_GET_CALI unlocked_ioctl failed.");
			return err;
		}
	break;

	case COMPAT_GSENSOR_IOCTL_CLR_CALI:
		if (arg32 == NULL) {
			err = -EINVAL;
			break;
		}

		err = file->f_op->unlocked_ioctl(file, GSENSOR_IOCTL_CLR_CALI, (unsigned long)arg32);
		if (err) {
			GSE_ERR("GSENSOR_IOCTL_CLR_CALI unlocked_ioctl failed.");
			return err;
		}
		break;

	default:
		GSE_ERR("unknown IOCTL: 0x%08x\n", cmd);
		err = -ENOIOCTLCMD;

	}

	return err;
}
#endif
/*****************************************
 *** MC3XXX_reset
 *****************************************/
static void MC3XXX_reset(struct i2c_client *client)
{
	unsigned char	_baBuf[2] = { 0 };

	_baBuf[0] = 0x43;

	MC3XXX_i2c_write_block(client, MC3XXX_REG_MODE_FEATURE, _baBuf, 0x01);

	MC3XXX_i2c_read_block(client, MC3XXX_REG_DEVICE_STATUS, _baBuf, 0x01);

	if (0x00 == (_baBuf[0] & 0x40)) {
		_baBuf[0] = 0xE1;
		MC3XXX_i2c_write_block(client, 0x1B, _baBuf, 0x01);

		_baBuf[0] = 0x25;
		MC3XXX_i2c_write_block(client, 0x1B, _baBuf, 0x01);
	}

	_baBuf[0] = 0x43;
	MC3XXX_i2c_write_block(client, 0x07, _baBuf, 1);

	_baBuf[0] = 0x80;
	MC3XXX_i2c_write_block(client, 0x1C, _baBuf, 1);

	_baBuf[0] = 0x80;
	MC3XXX_i2c_write_block(client, MC3XXX_REG_MISC, _baBuf, 1);

	mdelay(5);

	_baBuf[0] = 0x00;
	MC3XXX_i2c_write_block(client, 0x1C, _baBuf, 1);

	_baBuf[0] = 0x00;
	MC3XXX_i2c_write_block(client, MC3XXX_REG_MISC, _baBuf, 1);

	mdelay(5);

	MC3XXX_i2c_read_block(client, 0x21, offset_buf, 6);

	MC3XXX_i2c_read_block(client, MC3XXX_REG_DEVICE_STATUS, _baBuf, 0x01);

	if (_baBuf[0] & 0x40) {
		_baBuf[0] = 0xE1;
		MC3XXX_i2c_write_block(client, 0x1B, _baBuf, 0x01);

		_baBuf[0] = 0x25;
		MC3XXX_i2c_write_block(client, 0x1B, _baBuf, 0x01);
	}

	_baBuf[0] = 0x41;

	MC3XXX_i2c_write_block(client, MC3XXX_REG_MODE_FEATURE, _baBuf, 0x01);
}

/*****************************************
 *** STRUCT:: mc3xxx_fops
 *****************************************/
static const struct file_operations	mc3xxx_fops = {
						   .owner		= THIS_MODULE,
						   .open		= mc3xxx_open,
						   .release		= mc3xxx_release,
						   .unlocked_ioctl	= mc3xxx_ioctl,
						#ifdef CONFIG_COMPAT
						   .compat_ioctl	= mc3xxx_compat_ioctl,
						#endif
						   };

/*****************************************
 *** STRUCT:: mc3xxx_device
 *****************************************/
static struct miscdevice mc3xxx_device = {
						.minor = MISC_DYNAMIC_MINOR,
						.name  = "gsensor",
						.fops  = &mc3xxx_fops,
						};

/*****************************************
 *** mc3xxx_suspend
 *****************************************/
static int mc3xxx_suspend(struct i2c_client *client, pm_message_t msg)
{
	struct mc3xxx_i2c_data *obj = i2c_get_clientdata(client);
	int err = 0;

	GSE_LOG("mc3xxx_suspend\n");

	if (msg.event == PM_EVENT_SUSPEND) {
		if (obj == NULL) {
			GSE_ERR("null pointer!!\n");
			return -EINVAL;
		}

		atomic_set(&obj->suspend, 1);
		mc3xxx_mutex_lock();
		err = MC3XXX_SetPowerMode(client, false);
		mc3xxx_mutex_unlock();
		if (err) {
			GSE_ERR("write power control fail!!\n");
			return err;
		}
		MC3XXX_power(obj->hw, 0);
	}
	return err;
}

/*****************************************
 *** mc3xxx_resume
 *****************************************/
static int mc3xxx_resume(struct i2c_client *client)
{
	struct mc3xxx_i2c_data *obj = i2c_get_clientdata(client);
	int err;

	GSE_LOG("mc3xxx_resume\n");
	if (obj == NULL) {
		GSE_ERR("null pointer!!\n");
		return -EINVAL;
	}

	MC3XXX_power(obj->hw, 1);
	mc3xxx_mutex_lock();
	err = MC3XXX_Init(client, 0);
	if (err) {
		mc3xxx_mutex_unlock();
		GSE_ERR("initialize client fail!!\n");
		return err;
	}

	err = MC3XXX_SetPowerMode(client, true);
	mc3xxx_mutex_unlock();
	if (err) {
		GSE_ERR("write power control fail!!\n");
		return err;
	}
	atomic_set(&obj->suspend, 0);
	return 0;
}


/*****************************************
 *** _mc3xxx_i2c_auto_probe
 *****************************************/
static int _mc3xxx_i2c_auto_probe(struct i2c_client *client)
{
	#define _MC3XXX_I2C_PROBE_ADDR_COUNT_	\
		(sizeof(mc3xxx_i2c_auto_probe_addr) / sizeof(mc3xxx_i2c_auto_probe_addr[0]))
	unsigned char	_baData1Buf[2] = { 0 };
	unsigned char	_baData2Buf[2] = { 0 };
	//unsigned char	_baData3Buf[2] = { 0 };		//Buffer for MENSA PCODE
	int _nCount = 0;
	int _naCheckCount[_MC3XXX_I2C_PROBE_ADDR_COUNT_] = { 0 };

	memset(_naCheckCount, 0, sizeof(_naCheckCount));
_I2C_AUTO_PROBE_RECHECK_:
	s_bPCODE  = 0x00;
	s_bPCODER = 0x00;
	s_bHWID   = 0x00;

	for (_nCount = 0; _nCount < _MC3XXX_I2C_PROBE_ADDR_COUNT_; _nCount++) {
		client->addr = mc3xxx_i2c_auto_probe_addr[_nCount];
		_baData1Buf[0] = 0;

		if (0 > MC3XXX_i2c_read_block(client, 0x3B, _baData1Buf, 1))	//Low bits 0x3B[7:6]
			continue;
		_naCheckCount[_nCount]++;

		if (0x00 == _baData1Buf[0]) {
			if (1 == _naCheckCount[_nCount]) {
				MC3XXX_reset(client);
				mdelay(3);
				goto _I2C_AUTO_PROBE_RECHECK_;
			} else
				continue;
		}

		_baData2Buf[0] = 0;
		MC3XXX_i2c_read_block(client, 0x18, _baData2Buf, 1);
		s_bPCODER = _baData1Buf[0];
		if (MC3XXX_RETCODE_SUCCESS == MC3XXX_ValidateSensorIC(&_baData1Buf[0], &_baData2Buf[0])) {
			s_bPCODE = _baData1Buf[0];
			s_bHWID  = _baData2Buf[0];
			return MC3XXX_RETCODE_SUCCESS;
		}
	}

	return MC3XXX_RETCODE_ERROR_I2C;
	#undef _MC3XXX_I2C_PROBE_ADDR_COUNT_
}
/* if use  this typ of enable , Gsensor should report inputEvent(x, y, z ,stats, div) to HAL */
static int mc3xxx_open_report_data(int open)
{
	/* should queuq work to report event if  is_report_input_direct=true */
	return 0;
}

/* if use  this typ of enable , Gsensor only enabled but not report inputEvent to HAL */
static int mc3xxx_enable_nodata(int en)
{
	int res = 0;
	int retry = 0;
	bool power = false;

	if (1 == en)
		power = true;
	if (0 == en)
		power = false;

	for (retry = 0; retry < 3; retry++) {
		res = MC3XXX_SetPowerMode(mc3xxx_obj_i2c_data->client, power);
		if (res == 0) {
			GSE_LOG("MC3XXX_SetPowerMode done\n");
			break;
		}
		GSE_LOG("MC3XXX_SetPowerMode fail\n");
	}

	if (res != 0) {
		GSE_LOG("MC3XXX_SetPowerMode fail!\n");
		return -1;
	}
	GSE_LOG("mc3xxx_enable_nodata OK!\n");
	return 0;
}

static int mc3xxx_set_delay(u64 ns)
{
	int value = 0;

	value = (int)ns/1000/1000;
	GSE_LOG("mc3xxx_set_delay (%d), chip only use 1024HZ\n", value);
	return 0;
}

static int mc3xxx_get_data(int *x , int *y, int *z, int *status)
{
	char buff[MC3XXX_BUF_SIZE];
	int ret;

	MC3XXX_ReadSensorData(mc3xxx_obj_i2c_data->client, buff, MC3XXX_BUF_SIZE);
	ret = sscanf(buff, "%x %x %x", x, y, z);
	*status = SENSOR_STATUS_ACCURACY_MEDIUM;

	return 0;
}


/*****************************************
 *** mc3xxx_i2c_probe
 *****************************************/
static int mc3xxx_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_client *new_client;
	struct mc3xxx_i2c_data *obj;
	struct acc_control_path ctl = {0};
	struct acc_data_path data = {0};
	int err = 1;
	GSE_LOG("mc3416_i2c_probe\n");
	if (MC3XXX_RETCODE_SUCCESS != _mc3xxx_i2c_auto_probe(client))
		goto exit;
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		err = -ENOMEM;
		goto exit;
	}

	obj->hw = hw;
	err = hwmsen_get_convert(obj->hw->direction, &obj->cvt);
	if (err) {
		GSE_ERR("invalid direction: %d\n", obj->hw->direction);
		goto exit_kfree;
	}

	mc3xxx_obj_i2c_data = obj;
	obj->client = client;
	new_client = obj->client;
	i2c_set_clientdata(new_client, obj);
	atomic_set(&obj->trace, 0);
	atomic_set(&obj->suspend, 0);
	new_client->timing = 400;
	mc3xxx_i2c_client = new_client;
	MC3XXX_reset(new_client);
	if (MC3XXX_RETCODE_SUCCESS != _mc3xxx_i2c_auto_probe(client))
		goto exit_init_failed;
	MC3XXX_i2c_read_block(client, 0x21, offset_buf, 6);
	err = MC3XXX_Init(new_client, 1);
	if (err)
		goto exit_init_failed;
	mc3xxx_mutex_init();
	err = misc_register(&mc3xxx_device);
	if (err) {
		GSE_ERR("mc3xxx_device register failed\n");
		goto exit_misc_device_register_failed;
	}
	ctl.is_use_common_factory = false;
	err = mc3xxx_create_attr(&(mc3xxx_init_info.platform_diver_addr->driver));
	if (err) {
		GSE_ERR("create attribute err = %d\n", err);
		goto exit_create_attr_failed;
	}
	MC3XXX_app_set_info();
	ctl.open_report_data = mc3xxx_open_report_data;
	ctl.enable_nodata = mc3xxx_enable_nodata;
	ctl.set_delay  = mc3xxx_set_delay;
	ctl.is_report_input_direct = false;
	ctl.is_support_batch = obj->hw->is_batch_supported;
	err = acc_register_control_path(&ctl);
	if (err) {
		GSE_ERR("register acc control path err\n");
		goto exit_kfree;
	}
	data.get_data = mc3xxx_get_data;
	data.vender_div = 1000;
	err = acc_register_data_path(&data);
	if (err) {
		GSE_ERR("register acc data path err= %d\n", err);
		goto exit_kfree;
	}
	GSE_LOG("%s: OK\n", __func__);
	s_nInitFlag = MC3XXX_INIT_SUCC;
	return 0;

exit_create_attr_failed:
	misc_deregister(&mc3xxx_device);
exit_misc_device_register_failed:
exit_init_failed:
exit_kfree:
	kfree(obj);
	obj = NULL;
exit:
	GSE_ERR("%s: err = %d\n", __func__, err);
	s_nInitFlag = MC3XXX_INIT_FAIL;

	return err;
}

/*****************************************
 *** mc3xxx_i2c_remove
 *****************************************/
static int mc3xxx_i2c_remove(struct i2c_client *client)
{
	int err = 0;

	err = mc3xxx_delete_attr(&(mc3xxx_init_info.platform_diver_addr->driver));
	if (err)
		GSE_ERR("mc3xxx_delete_attr fail: %d\n", err);

	err = misc_deregister(&mc3xxx_device);
	if (err)
		GSE_ERR("misc_deregister fail: %d\n", err);

	mc3xxx_i2c_client = NULL;
	i2c_unregister_device(client);
	kfree(i2c_get_clientdata(client));

	return 0;
}


/*****************************************
 *** mc3xxx_remove
 *****************************************/
static int mc3xxx_remove(void)
{
	MC3XXX_power(hw, 0);
	i2c_del_driver(&mc3xxx_i2c_driver);

	return 0;
}

/*****************************************
 *** mc3xxx_local_init
 *****************************************/
static int  mc3xxx_local_init(void)
{
	GSE_LOG("mc3416_local_init\n");
	MC3XXX_power(hw, 1);

	if (i2c_add_driver(&mc3xxx_i2c_driver)) {
		GSE_ERR("add driver error\n");
		return -1;
	}

	if (MC3XXX_INIT_FAIL == s_nInitFlag)
		return -1;
	return 0;
}

/*****************************************
 *** mc3xxx_init
 *****************************************/
static int __init mc3xxx_init(void)
{
	const char *name = "mediatek,mc3416";
	GSE_LOG("mc3416_init\n");
	hw = get_accel_dts_func(name, hw);
	if (!hw)
		GSE_ERR("get dts info fail\n");

	acc_driver_add(&mc3xxx_init_info);
	#ifdef MC3XXX_SUPPORT_MOTION
		step_c_driver_add(&mc3xxx_motion_init_info); //motion
	#endif
	return 0;
}

/*****************************************
 *** mc3xxx_exit
 *****************************************/
static void __exit mc3xxx_exit(void)
{
	GSE_LOG("mc3xxx_exit\n");
}

/*----------------------------------------------------------------------------*/
module_init(mc3xxx_init);
module_exit(mc3xxx_exit);
/*----------------------------------------------------------------------------*/
MODULE_DESCRIPTION("mc3XXX G-Sensor Driver");
MODULE_AUTHOR("mCube-inc");
MODULE_LICENSE("GPL");
MODULE_VERSION(MC3XXX_DEV_DRIVER_VERSION);
