/*
* Copyright(C)2016 MediaTek Inc.
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
#include "mc3451.h"
#include "step_counter.h"

#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>

#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/namei.h>
#include <linux/mount.h>

#include <linux/timer.h>
#include <linux/timex.h>
#include <linux/rtc.h>
/*dingleilei*/
#include <misc/app_info.h>
#ifdef CONFIG_HUAWEI_HW_I2C_DCT
#include <linux/hw_dev_dec.h>
#endif
#ifdef CONFIG_HUAWEI_DSM
#include "dsm_sensor.h"
#endif
/*dingleilei*/
/*****************************************************************************
 *** CONFIGURATION
 *****************************************************************************/
#define MC3451_SUPPORT_STEP_COUNT
//#define GSENSOR_INTERRUPT_FILT
//#define GSENSOR_PEDOMETER20_CONFIG
#define DEBUG_SWITCH        1
//#define GSENSOR_PEDOMETER_DEBUG

/*****************************************************************************
 *** CONSTANT / DEFINITION
 *****************************************************************************/
#define GRAVITY_EARTH_1000           9807   /* about (9.80665f)*1000 */

#define PD_DATA_PATH              "/sdcard/pedo_data"
#define UPDATE_FILE_PATH_1        "/system/etc/firmware/MC3451_V0A_FW.bin"
#define UPDATE_FILE_PATH_2        "/system/etc/firmware/MC3451_V0B_FW.bin"


/**************************
 *** INFORMATION
 **************************/
#define MC3451_DEV_NAME                        "MC3451"
//#define MC3451_DEV_NAME_ACC                        "MC3451_ACC"
#define MC3451_STEP_C                           "MC3451_STEP_C"
#define MC3451_DEV_DRIVER_VERSION              "1.0.0"

/**************************
 *** COMMON
 **************************/
#define MC3451_AXIS_X           0
#define MC3451_AXIS_Y           1
#define MC3451_AXIS_Z           2
#define MC3451_AXES_NUM         3
#define MC3451_DATA_LEN         6

#define MC3451_INIT_SUCC        (0)
#define MC3451_INIT_FAIL        (-1)

#define MC3451_REGMAP_LENGTH    (19)
#define C_MAX_FIR_LENGTH        (32)

#if DEBUG_SWITCH
#define GSE_TAG                  "[Gsensor] "
#define GSE_FUN()                pr_err(GSE_TAG"%s\n", __func__)
#define GSE_ERR(fmt, args...)    pr_err(GSE_TAG"%s %d : "fmt, __func__, __LINE__, ##args)
#define GSE_LOG(fmt, args...)    pr_err(GSE_TAG fmt, ##args)

#else
#define GSE_TAG
#define GSE_ERR(fmt, args...)   do {} while (0)
#define GSE_LOG(fmt, args...)   do {} while (0)
#endif

#define IS_PEDO10()    ((0x000A == s_bPCODE) || (0x0009 == s_bPCODE))
#define IS_PEDO20()    (0x0000B == s_bPCODE)

#define OTP_VERSION1    0X000A
#define OTP_VERSION2    0X000B
#define OTP_VERSION3    0X0C00
//#define SRAM_VERSION    0X8001
#define SRAM_VERSION    0X8000
#ifdef GSENSOR_PEDOMETER20_CONFIG
#define ANYMOTION_STATIONARY_COUNT_ADDR		16
#define STEP_SENSITIVITY_ADDR				17
#define RUNNING_THRES_ADDR					18
#define NORMAL_WALKING_THRES_ADDR			22
#define SLOW_WALKING_THRES_ADDR				26

#define USER_ANYMOTION_STATIONARY_COUNT 	0x0	// 0~255
#define USER_STEP_SENSITIVITY           	0x0 // 0~255
#define USER_RUNNING_THRES              	0x0 // 0~2^32-1
#define USER_NORMAL_WALKING_THRES       	0x0 // 0~2^32-1
#define USER_SLOW_WALKING_THRES         	0x0 // 0~2^32-1
#endif
/*****************************************************************************
 *** DATA TYPE / STRUCTURE DEFINITION / ENUM
 *****************************************************************************/
struct scale_factor {
    u8  whole;
    u8  fraction;
};

struct data_resolution {
    struct scale_factor scalefactor;
    int                 sensitivity;
};

struct data_filter {
    s16 raw[C_MAX_FIR_LENGTH][MC3451_AXES_NUM];
    int sum[MC3451_AXES_NUM];
    int num;
    int idx;
};

struct mc3451_i2c_data {
    /* ================================================ */
    struct i2c_client         *client;
    struct acc_hw             *hw;
    struct hwmsen_convert      cvt;
    #if defined(GSENSOR_INTERRUPT_FILT)
    struct input_dev *input;
    struct work_struct  irq_work;
    int irq;
    #endif
    /* ================================================ */
    struct data_resolution       *reso;
    atomic_t                    trace;
    atomic_t                    suspend;
    atomic_t                    selftest;
    atomic_t                    filter;
    s16                          cali_sw[MC3451_AXES_NUM + 1];

    /* ================================================ */
    s16                          offset[MC3451_AXES_NUM + 1];
    s16                          data[MC3451_AXES_NUM + 1];
};

/*****************************************************************************
 *** STATIC FUNCTION
 *****************************************************************************/
static int mc3451_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int mc3451_i2c_remove(struct i2c_client *client);
static int mc3451_suspend(struct i2c_client *client, pm_message_t msg);
static int mc3451_resume(struct i2c_client *client);
static int mc3451_local_init(void);
static int mc3451_remove(void);

static int MC3451_SetPowerMode(struct i2c_client *client, bool enable);

static int mc3451_step_c_local_init(void);
static int mc3451_step_c_local_uninit(void);

/*****************************************************************************
 *** STATIC VARIBLE & CONTROL BLOCK DECLARATION
 *****************************************************************************/
static bool mc3451_sensor_power;
static unsigned char global_Register    = 0x00;

unsigned int gsensor_int_gpio_number    = 0;
static unsigned char    s_bPCODE        = 0x00;
#ifdef GSENSOR_PEDOMETER_DEBUG
static bool  pedometer_debug_flag       = false;
static struct timer_list ms_timer;
//static struct hrtimer hr_timer;
static struct work_struct work;
static struct file *pd_file = NULL;
static mm_segment_t old_fs;
u8 *buffer = NULL;
#endif

static DEFINE_MUTEX(mc3451_i2c_mutex);

//static struct task_struct *probe_thread;
//DECLARE_WAIT_QUEUE_HEAD(init_waiter);

#ifdef CONFIG_HUAWEI_DSM
extern struct dsm_client *dsm_sensorhub_dclient;
#endif


static struct GSENSOR_VECTOR3D gsensor_gain = { 1024,1024,1024},gsensor_offset;
static struct i2c_client         *mc3451_i2c_client = NULL;
static struct mc3451_i2c_data     *mc3451_obj_i2c_data = NULL;

/* Maintain  cust info here */
struct acc_hw accel_cust_mc3451;
static struct acc_hw *hw = &accel_cust_mc3451;

static struct step_c_init_info  mc3451_step_c_init_info = {
    .name   = MC3451_STEP_C,
    .init   = mc3451_step_c_local_init,
    .uninit = mc3451_step_c_local_uninit,
};

static struct acc_init_info  mc3451_init_info = {
    .name   = MC3451_DEV_NAME,
    .init   = mc3451_local_init,
    .uninit = mc3451_remove,
    };

#ifdef CONFIG_OF
static const struct of_device_id accel_of_match[] = {
    {.compatible = "mediatek,gsensor"},
    {},
};
#endif
static const struct i2c_device_id mc3451_i2c_id[] = { {MC3451_DEV_NAME, 0}, {} };

static struct i2c_driver    mc3451_i2c_driver = {
                                .driver = {
                                .name = MC3451_DEV_NAME,
                                #ifdef CONFIG_OF
                                .of_match_table = accel_of_match,
                                #endif
                                },
                                .probe   = mc3451_i2c_probe,
                                .remove  = mc3451_i2c_remove,
                                .suspend = mc3451_suspend,
                                .resume  = mc3451_resume,
                                .id_table = mc3451_i2c_id,
                            };

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

#if defined(GSENSOR_INTERRUPT_FILT)
static int of_get_mc3451_platform_data(struct device *dev)
{
    //int ret, num;

    if (dev->of_node) {
        const struct of_device_id *match;

        match = of_match_device(of_match_ptr(accel_of_match), dev);
        if (!match) {
            GSE_ERR("Error: No device match found\n");
            return -ENODEV;
        }
    }
    gsensor_int_gpio_number = of_get_named_gpio(dev->of_node, "int-gpio", 0);
    /*ret = of_property_read_u32(dev->of_node, "rst-gpio", &num);
    if (!ret)
        tpd_rst_gpio_number = num;
    ret = of_property_read_u32(dev->of_node, "int-gpio", &num);
    if (!ret)
        tpd_int_gpio_number = num;
  */
    GSE_ERR("g_vproc_vsel_gpio_number %d\n", gsensor_int_gpio_number);
    return 0;
}
#endif

/*****************************************************************************
 *** FUNCTION
 *****************************************************************************/

/**************I2C operate API*****************************/
static int mc3xxx_i2c_read_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
    u8 beg = addr;
    int err;
    struct i2c_msg msgs[2] = {{0}, {0} };

    mutex_lock(&mc3451_i2c_mutex);

    msgs[0].addr = client->addr;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = &beg;

    msgs[1].addr = client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = len;
    msgs[1].buf = data;

    if (!client) {
        mutex_unlock(&mc3451_i2c_mutex);
        return -EINVAL;
    } else if (len > C_I2C_FIFO_SIZE) {
        GSE_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
        mutex_unlock(&mc3451_i2c_mutex);
        return -EINVAL;
    }
    err = i2c_transfer(client->adapter, msgs, sizeof(msgs)/sizeof(msgs[0]));
    if (err != 2) {
        GSE_ERR("i2c_transfer error: (%d %p %d) %d\n", addr, data, len, err);
    #ifdef CONFIG_HUAWEI_DSM
            gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_I2C_ERR);
        #endif

        err = -EIO;
    } else
        err = 0;

    mutex_unlock(&mc3451_i2c_mutex);
    return err;

}
static int mc3xxx_i2c_write_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{   /*because address also occupies one byte, the maximum length for write is 7 bytes*/
    int err, idx, num;
    char buf[C_I2C_FIFO_SIZE];

    err = 0;
    mutex_lock(&mc3451_i2c_mutex);
    if (!client) {
        mutex_unlock(&mc3451_i2c_mutex);
        return -EINVAL;
    } else if (len >= C_I2C_FIFO_SIZE) {
        GSE_ERR(" length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
        mutex_unlock(&mc3451_i2c_mutex);
        return -EINVAL;
    }

    num = 0;
    buf[num++] = addr;
    for (idx = 0; idx < len; idx++)
        buf[num++] = data[idx];

    err = i2c_master_send(client, buf, num);
    if (err < 0) {
        GSE_ERR("send command error!!\n");
        #ifdef CONFIG_HUAWEI_DSM
            gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_I2C_ERR);
         #endif

        mutex_unlock(&mc3451_i2c_mutex);
        return -EFAULT;
    }
    err = 0;

    mutex_unlock(&mc3451_i2c_mutex);
    return err;
}

static int mc3451_i2c_read_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
    int i,err;
    int err_num=0;
    for(i=0; i<5; i++)
    {
        err=mc3xxx_i2c_read_block(client, addr, data, len);
        //mdelay(10);
        udelay(200);
        if(err){
            err_num++;
            if(err_num>3){
                #ifdef CONFIG_HUAWEI_DSM
                            gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_I2C_ERR);
                        #endif
            }
            continue;
        }
        else
            return MC3451_RETCODE_SUCCESS;
    }
    return MC3451_RETCODE_ERROR_I2C;

}
static int mc3451_i2c_write_block(struct i2c_client *client, u8 addr, u8 *data, u8 len)
{
    int i,err;
    int err_num=0;
    for(i=0; i<5; i++)
    {
        err=mc3xxx_i2c_write_block(client, addr, data, len);
        if(err){
            err_num++;
            if(err_num>3){
                #ifdef CONFIG_HUAWEI_DSM
                            gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_I2C_ERR);
                        #endif
            }
            continue;
        }
        else
            return MC3451_RETCODE_SUCCESS;
    }
    return MC3451_RETCODE_ERROR_I2C;
}

/*******************Step Counter*******************************/
static int MC3451_GetPedo(struct i2c_client *client, uint32_t *Value, int *status)
{
    unsigned char DataBuf[5] = {0,0,0,0,0};
    int err=0;
    GSE_LOG("mc3451 get pedo......\n");
    err = mc3451_i2c_read_block(client, MC3451_REG_PEDOMETER, DataBuf, 5);
    if(err == 0)
        GSE_LOG("pedometer %d %d %d %d %d.\n", DataBuf[0], DataBuf[1], DataBuf[2], DataBuf[3], DataBuf[4]);
    else{
        GSE_LOG("pedometer Nack.\n");
        return MC3451_RETCODE_ERROR_I2C;
    }

    *status = DataBuf[0];
    *Value = (DataBuf[4]<<24) |(DataBuf[3]<<16) |(DataBuf[2]<<8) |DataBuf[1];

    return MC3451_RETCODE_SUCCESS;
}

static int MC3451_EnablePedo(struct i2c_client *client, bool enable)
{
    unsigned char  DataBuf[2] = {0};
    int err = 0;

    if (enable) {
        GSE_LOG("enable Pedometer\n");
        DataBuf[0] = 0x01;
        err = mc3451_i2c_write_block(client, MC3451_REG_PEDOMETER_CONTROL, DataBuf, 1);
    } else {
        GSE_LOG("disable Pedometer\n");
        DataBuf[0] = 0x00;
        err = mc3451_i2c_write_block(client, MC3451_REG_PEDOMETER_CONTROL, DataBuf, 1);
    }

    if (err < 0) {
        GSE_LOG("MC3451 set pedo mode failed!\n");
        return MC3451_RETCODE_ERROR_I2C;
    } else
        GSE_LOG("mc3451 set pedo mode ok %d!\n", DataBuf[0]);

    return MC3451_RETCODE_SUCCESS;
}

static int MC3451_ConfigPedo(struct i2c_client *client)
{
/*
    unsigned char  DataBuf[2] = {0};
    int err = 0;

    DataBuf[0] = 0x32;
    err = mc3451_i2c_write_block(client, MOTION_THM_COMMAND, DataBuf, 1);
    DataBuf[0] = 0x04;
    err = mc3451_i2c_write_block(client, MOTION_DUM_COMMAND, DataBuf, 1);
    DataBuf[0] = 0x64;
    err = mc3451_i2c_write_block(client, MOTION_THS_COMMAND, DataBuf, 1);
    DataBuf[0] = 0x03;
    err = mc3451_i2c_write_block(client, MOTION_DUS_COMMAND, DataBuf, 1);
    DataBuf[0] = 0x04;
    err = mc3451_i2c_write_block(client, STEP_WAITING_COMMAND, DataBuf, 1);
    DataBuf[0] = 0x1D;
    err = mc3451_i2c_write_block(client, THRETH_DYN_COMMAND, DataBuf, 1);
    DataBuf[0] = 0x0A;
    err = mc3451_i2c_write_block(client, MINCOUNT_CON_COMMAND, DataBuf, 1);
    DataBuf[0] = 0x32;
    err = mc3451_i2c_write_block(client, TIMEOUT_CON_COMMAND, DataBuf, 1);

    if (err < 0) {
        GSE_LOG("MC3451 set pedo parameter failed!\n");
        return MC3451_RETCODE_ERROR_I2C;
    } else
        GSE_LOG("MC3451 set pedo parameter ok %d!\n", DataBuf[1]);
*/
    return MC3451_RETCODE_SUCCESS;
}

static int mc3451_step_c_open_report_data(int open)
{
    return 0;
}
static int mc3451_step_c_enable_nodata(int en)
{
    return 0;
}
static int mc3451_step_c_enable_step_detect(int en)
{
    return mc3451_step_c_enable_nodata(en);
}

static int mc3451_step_c_set_delay(u64 delay)
{
        return 0;
}
static int mc3451_step_d_set_delay(u64 delay)
{
        return 0;
}
static int mc3451_step_c_get_data(uint32_t *value, int *status)
{
    int err = 0;
    unsigned int StepCounter = 0;
    err = MC3451_GetPedo(mc3451_obj_i2c_data->client, &StepCounter, status);
    if(err)
        return MC3451_RETCODE_ERROR_I2C;

    *value = (uint32_t)StepCounter;
    *status = SENSOR_STATUS_ACCURACY_MEDIUM;

    GSE_LOG("%s:StepCounter=%d,*value = %u.\n", __func__,StepCounter,*value);

    return MC3451_RETCODE_SUCCESS;
}
static int mc3451_step_c_get_data_step_d(uint32_t *value, int *status)
{
    return 0;
}
static int mc3451_step_c_get_data_significant(uint32_t *value, int *status)
{
    return 0;
}
static int mc3451_step_c_enable_significant(int en)
{
    return 0;
}
static int mc3451_step_c_local_init(void)
{
    int res = -1;
    struct step_c_control_path step_ctl={0};
    struct step_c_data_path step_data={0};

    GSE_LOG("%s start \n", __FUNCTION__);

    step_ctl.open_report_data= mc3451_step_c_open_report_data;
    step_ctl.enable_nodata = mc3451_step_c_enable_nodata;
    step_ctl.enable_step_detect  = mc3451_step_c_enable_step_detect;
    step_ctl.step_c_set_delay = mc3451_step_c_set_delay;
    step_ctl.step_d_set_delay = mc3451_step_d_set_delay;
    step_ctl.is_report_input_direct = false;
    step_ctl.is_support_batch = false;
    step_ctl.enable_significant = mc3451_step_c_enable_significant;

    res = step_c_register_control_path(&step_ctl);
    if(res)
    {
         GSE_ERR("register step counter control path err\n");
        goto mc3451_step_c_local_init_failed;
    }

    step_data.get_data = mc3451_step_c_get_data;
    step_data.get_data_step_d = mc3451_step_c_get_data_step_d;
    step_data.get_data_significant = mc3451_step_c_get_data_significant;

    step_data.vender_div = 1;
    res = step_c_register_data_path(&step_data);
    if(res)
    {
        GSE_ERR("register step counter data path err= %d\n", res);
        goto mc3451_step_c_local_init_failed;
    }

    return 0;

mc3451_step_c_local_init_failed:

    GSE_ERR("%s init failed!\n", __FUNCTION__);
    return res;

}

static int mc3451_step_c_local_uninit(void)
{
    return 0;
}

/*****************************************
 *** MC3451_power
 *****************************************/
static void MC3451_power(struct acc_hw *hw, unsigned int on)
{

}

/*****************************************
 *** MC3451_ValidateSensorIC
 *****************************************/
static int MC3451_ValidateSensorIC(struct i2c_client *client)
{
    unsigned char   DataBuf[2] = { 0 };
    int err = 0,retry = 3, ChipVersion =0;
    int app_err = 0;

    while(retry-- > 0){
        err = mc3451_i2c_read_block(client, MC3451_REG_VERSION, DataBuf, 2);
        if (err)
        {
            GSE_LOG("i2c read MC3451_REG_VERSION error Ecode =%d\n", err);
            return MC3451_RETCODE_ERROR_I2C;
        }
        ChipVersion = (int)((DataBuf[0] << 8)|(DataBuf[1]));
        if(!((ChipVersion == OTP_VERSION1) || (ChipVersion == OTP_VERSION2) || ((ChipVersion & 0x0C00) == OTP_VERSION3)))
        {
            GSE_LOG("OS MODE,Version=0x%04x\n", ChipVersion);
            DataBuf[0] = 0x5A;
            err = mc3451_i2c_write_block(client, MC3451_REG_RTB, DataBuf, 1);
            if(err)
                return MC3451_RETCODE_ERROR_I2C;
            GSE_LOG("Return to Bootload\n");
            mdelay(100);
            continue;
        }else
        {
            s_bPCODE = ChipVersion;
            GSE_LOG("OTP MODE Version: 0x%04X,s_bPCODE = 0x%04X\n", ChipVersion,s_bPCODE);
            app_err = app_info_set("G-Sensor", "MC3451");
            if(app_err < 0){
                    GSE_LOG("bma421 failed to add app_info\n");
            }
            #ifdef CONFIG_HUAWEI_HW_I2C_DCT
                set_hw_dev_flag(DEV_I2C_G_SENSOR);
            #endif
            return MC3451_RETCODE_SUCCESS;
        }
    }
    return MC3451_RETCODE_ERROR_GET_DATA;
}

/*****************************************
 *** MC3451_ReadRegMap
 *****************************************/
static int MC3451_ReadRegMap(struct i2c_client *p_i2c_client, u8 *pbUserBuf)
{
    u8     _baData[MC3451_REGMAP_LENGTH] = { 0 };
    int    _nIndex = 0;

    GSE_LOG("[%s]\n", __func__);

    if (NULL == p_i2c_client)
        return (-EINVAL);

    for (_nIndex = 0; _nIndex < MC3451_REGMAP_LENGTH; _nIndex++) {
        mc3451_i2c_read_block(p_i2c_client, _nIndex, &_baData[_nIndex], 1);

        if (NULL != pbUserBuf)
            pbUserBuf[_nIndex] = _baData[_nIndex];

        GSE_LOG("[Gsensor] REG[0x%02X] = 0x%02X\n", _nIndex, _baData[_nIndex]);
    }

    return 0;
}
/*****************************************
 *** MC3451_ReadData
 *****************************************/
static int    MC3451_ReadData(struct i2c_client *pt_i2c_client, s16 waData[MC3451_AXES_NUM])
{
    u8    _baData[MC3451_DATA_LEN] = { 0 };

    if (mc3451_i2c_read_block(pt_i2c_client, MC3451_REG_READ_XYZ, _baData,MC3451_DATA_LEN))
    {
        GSE_ERR("ERR: fail to read data via I2C!\n");

        return MC3451_RETCODE_ERROR_I2C;
    }

    waData[MC3451_AXIS_X] = ((signed short) ((_baData[1]<<8) | _baData[0]));
    waData[MC3451_AXIS_Y] = ((signed short) ((_baData[3]<<8) | _baData[2]));
    waData[MC3451_AXIS_Z] = ((signed short) ((_baData[5]<<8) | _baData[4]));

    //GSE_LOG("[%s][high] X: %d, Y: %d, Z: %d\n",__func__, waData[MC3451_AXIS_X], waData[MC3451_AXIS_Y], waData[MC3451_AXIS_Z]);

    return MC3451_RETCODE_SUCCESS;
}

/*****************************************
 *** MC3451_ReadSensorData
 *****************************************/
static int MC3451_ReadSensorData(struct i2c_client *pt_i2c_client, char *pbBuf, int nBufSize)
{
    int   AccelData[MC3451_AXES_NUM] = { 0 };
    struct mc3451_i2c_data   *_pt_i2c_obj = ((struct mc3451_i2c_data *) i2c_get_clientdata(pt_i2c_client));

    if (false == mc3451_sensor_power) {
        if (MC3451_RETCODE_SUCCESS != MC3451_SetPowerMode(pt_i2c_client, true))
            GSE_ERR("ERR: fail to set power mode!\n");
    }

    if (MC3451_RETCODE_SUCCESS != MC3451_ReadData(pt_i2c_client, _pt_i2c_obj->data)) {
        GSE_ERR("ERR: fail to read data!\n");

        return MC3451_RETCODE_ERROR_I2C;
    }

    //add for sw cali
    _pt_i2c_obj->data[MC3451_AXIS_X] += _pt_i2c_obj->cali_sw[MC3451_AXIS_X];
    _pt_i2c_obj->data[MC3451_AXIS_Y] += _pt_i2c_obj->cali_sw[MC3451_AXIS_Y];
    _pt_i2c_obj->data[MC3451_AXIS_Z] += _pt_i2c_obj->cali_sw[MC3451_AXIS_Z];

    /* output format: mg */
    //GSE_LOG("[%s] raw data: %d, %d, %d\n", __func__, _pt_i2c_obj->data[MC3451_AXIS_X],
    //_pt_i2c_obj->data[MC3451_AXIS_Y], _pt_i2c_obj->data[MC3451_AXIS_Z]);

    AccelData[(_pt_i2c_obj->cvt.map[MC3451_AXIS_X])] = (_pt_i2c_obj->cvt.sign[MC3451_AXIS_X]
        * _pt_i2c_obj->data[MC3451_AXIS_X]);
    AccelData[(_pt_i2c_obj->cvt.map[MC3451_AXIS_Y])] = (_pt_i2c_obj->cvt.sign[MC3451_AXIS_Y]
        * _pt_i2c_obj->data[MC3451_AXIS_Y]);
    AccelData[(_pt_i2c_obj->cvt.map[MC3451_AXIS_Z])] = (_pt_i2c_obj->cvt.sign[MC3451_AXIS_Z]
        * _pt_i2c_obj->data[MC3451_AXIS_Z]);

    GSE_LOG("[%s] map raw data: %d, %d, %d\n", __func__, AccelData[MC3451_AXIS_X],
            AccelData[MC3451_AXIS_Y], AccelData[MC3451_AXIS_Z]);

    AccelData[MC3451_AXIS_X] = (AccelData[MC3451_AXIS_X] * GRAVITY_EARTH_1000 / gsensor_gain.x);
    AccelData[MC3451_AXIS_Y] = (AccelData[MC3451_AXIS_Y] * GRAVITY_EARTH_1000 / gsensor_gain.y);
    AccelData[MC3451_AXIS_Z] = (AccelData[MC3451_AXIS_Z] * GRAVITY_EARTH_1000 / gsensor_gain.z);

    sprintf(pbBuf, "%04x %04x %04x",
        AccelData[MC3451_AXIS_X], AccelData[MC3451_AXIS_Y], AccelData[MC3451_AXIS_Z]);

    return MC3451_RETCODE_SUCCESS;
}

/*****************************************
 *** MC3451_ReadChipInfo
 *****************************************/
static int MC3451_ReadChipInfo(struct i2c_client *client, char *buf, int bufsize)
{
    sprintf(buf, "MC3451 Chip");
    return 0;
}

/*****************************************
 *** MC3451_ReadRawData
 *****************************************/
static int MC3451_ReadRawData(struct i2c_client *client, char *buf)
{
    int res = 0;
    s16 sensor_data[3]={0};

    if((res = MC3451_ReadData(client, sensor_data)))
    {
        GSE_ERR("I2C error: ret value=%d", res);
        return EIO;
    }
    else
    {
        sprintf(buf, "%04x %04x %04x", sensor_data[MC3451_AXIS_X],sensor_data[MC3451_AXIS_Y], sensor_data[MC3451_AXIS_Z]);
    }

    return 0;
}

/*****************************************
 *** MC3451_ResetCalibration
 *****************************************/
static int MC3451_ResetCalibration(struct i2c_client *client)
{
    struct mc3451_i2c_data *obj = i2c_get_clientdata(client);

    memset(obj->cali_sw, 0x00, sizeof(obj->cali_sw));
    memset(obj->offset, 0x00, sizeof(obj->offset));

    return 0;
}

/*****************************************
 *** MC3451_ReadCalibration
 *****************************************/
static int MC3451_ReadCalibration(struct i2c_client *client, int dat[MC3451_AXES_NUM])
{
    struct mc3451_i2c_data *obj = i2c_get_clientdata(client);

    dat[obj->cvt.map[MC3451_AXIS_X]] = obj->cvt.sign[MC3451_AXIS_X]*obj->cali_sw[MC3451_AXIS_X];
    dat[obj->cvt.map[MC3451_AXIS_Y]] = obj->cvt.sign[MC3451_AXIS_Y]*obj->cali_sw[MC3451_AXIS_Y];
    dat[obj->cvt.map[MC3451_AXIS_Z]] = obj->cvt.sign[MC3451_AXIS_Z]*obj->cali_sw[MC3451_AXIS_Z]; 

    GSE_LOG("MC3451_ReadCalibration %d %d %d \n",dat[obj->cvt.map[MC3451_AXIS_X]] ,dat[obj->cvt.map[MC3451_AXIS_Y]],dat[obj->cvt.map[MC3451_AXIS_Z]]);

    return 0;
}

/*****************************************
 *** MC3451_WriteCalibration
 *****************************************/
static int MC3451_WriteCalibration(struct i2c_client *client, int dat[MC3451_AXES_NUM])
{
    struct mc3451_i2c_data *obj = i2c_get_clientdata(client);
    int err = 0;
    int cali[MC3451_AXES_NUM] = {0};
    GSE_LOG("UPDATE dat: (%+3d %+3d %+3d)\n", dat[MC3451_AXIS_X], dat[MC3451_AXIS_Y], dat[MC3451_AXIS_Z]);

    cali[MC3451_AXIS_X] = dat[MC3451_AXIS_X];
    cali[MC3451_AXIS_Y] = dat[MC3451_AXIS_Y];
    cali[MC3451_AXIS_Z] = dat[MC3451_AXIS_Z];
    obj->cali_sw[MC3451_AXIS_X] = obj->cvt.sign[MC3451_AXIS_X]*(cali[obj->cvt.map[MC3451_AXIS_X]]);
    obj->cali_sw[MC3451_AXIS_Y] = obj->cvt.sign[MC3451_AXIS_Y]*(cali[obj->cvt.map[MC3451_AXIS_Y]]);
    obj->cali_sw[MC3451_AXIS_Z] = obj->cvt.sign[MC3451_AXIS_Z]*(cali[obj->cvt.map[MC3451_AXIS_Z]]);

    GSE_LOG("MC3451_WriteCalibration %d %d %d \n",dat[obj->cvt.map[MC3451_AXIS_X]] ,dat[obj->cvt.map[MC3451_AXIS_Y]],dat[obj->cvt.map[MC3451_AXIS_Z]]);

    return err;
}

/*****************************************
 * MC3451_SetPowerMode
 * write 0x01(val)-->0x05(reg) chip sleep
 * Read any regs twices the chip wakeup
 *****************************************/
static int MC3451_SetPowerMode(struct i2c_client *client, bool enable)
{
/*
    u8 databuf[2] = {0};
    int res=0,i=1;

    if (enable == mc3451_sensor_power){
        GSE_LOG("Sensor power status should not be set again!!!\n");
        return MC3451_RETCODE_SUCCESS;
    }

    if (enable) {
        do{
            res = mc3451_i2c_read_block(client, MC3451_REG_SLEEP, databuf, 1);
            GSE_LOG("mc3451 wakeup the chip:%d!\n",i);
        }while(i--);
    } else {
        databuf[0] = 0x01;
        res = mc3451_i2c_write_block(client, MC3451_REG_SLEEP, databuf, 1);
        GSE_LOG("mc3451 sleep the chip!\n");
    }

    if (res < 0) {
        GSE_LOG("mc3451 set power mode failed!\n");
        return MC3451_RETCODE_ERROR_I2C;
    } else{
        GSE_LOG("mc3451  set power mode %d ok!\n", databuf[1]);
    }
*/
    mc3451_sensor_power = enable;

    return MC3451_RETCODE_SUCCESS;
}
/*
int mc3451_check_fs_mounted(char *path_name)
{
    struct path root_path;
    struct path path;
    int err = 0;

    err = kern_path("/", LOOKUP_FOLLOW, &root_path);
    if (err)
        return MC3451_RETCODE_ERROR_STATUS;

    err = kern_path(path_name, LOOKUP_FOLLOW, &path);
    if (err) {
        return MC3451_RETCODE_ERROR_STATUS;
    }

    if (path.mnt->mnt_sb == root_path.mnt->mnt_sb){
        path_put(&root_path);
        path_put(&path);
        return MC3451_RETCODE_ERROR_STATUS;
    }

    path_put(&root_path);
    path_put(&path);
    return MC3451_RETCODE_SUCCESS;
}
*/

int fw_check_sum(struct i2c_client *client,char *dl_code,int dl_length)
{
     unsigned char check_sum[2] ={0};
    int i = 0;

    for(i = 0; i < dl_length; i++)
    {
        check_sum[0] += dl_code[i];
    }

    mc3451_i2c_read_block(client, MC3451_REG_RTB, &check_sum[1], 1);

    if(check_sum[0] != check_sum[1])
    {
        GSE_LOG("Check Sum err:code sum=0x%x,err:ram sum=0x%x\n", check_sum[0], check_sum[1]);
        return MC3451_RETCODE_ERROR_GET_DATA;
    }

    GSE_LOG("Check Sum succes:code sum=0x%x,err:ram sum=0x%x\n", check_sum[0], check_sum[1]);
    return MC3451_RETCODE_SUCCESS;
}

int MC3451_UpdateFirmware(struct i2c_client *client)
{
    mm_segment_t old_fs;
    struct file *fw_file = NULL;
    u32 fw_length;
    u8 *buffer = NULL;
    int err=0,i=0;
    /*
	int retry = 10; //ait 10s(max) if fs is not ready
    while(retry-- > 0){
        //check if rootfs is ready
        if (mc3451_check_fs_mounted("/system")) {
            GSE_ERR("filesystem is not ready retry =%d!",retry);
            msleep(1000);
            continue;
        }
    }
	*/
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    if(IS_PEDO10()){
        GSE_ERR(" the chip is pedometer 1.0 open bin file:%s",UPDATE_FILE_PATH_1);
        fw_file = filp_open(UPDATE_FILE_PATH_1, O_RDONLY, 0);
        if (IS_ERR(fw_file)) {
            GSE_ERR("open bin file:%s fail error code:%ld,Update use head file!",UPDATE_FILE_PATH_1,PTR_ERR(fw_file));
            set_fs(old_fs);

            for(i = 0;i < sizeof(pedo10_sram_hex)/2;i++)
            {
                err = mc3451_i2c_write_block(client, MC3451_REG_SRAM_ADDR,  pedo10_sram_hex+i*2, 2);
                if(err)
                {
                    GSE_LOG("mc3451 i2c write firmware failed.\n");
                    return MC3451_RETCODE_ERROR_I2C;
                }
            }
            err = fw_check_sum(client,pedo10_sram_hex,sizeof(pedo10_sram_hex));
            if(err)
                return MC3451_RETCODE_ERROR_GET_DATA;
            else
                return MC3451_RETCODE_SUCCESS;
        }
     }else if(IS_PEDO20()){
        GSE_ERR(" the chip is pedometer 2.0 open bin file:%s",UPDATE_FILE_PATH_2);
        fw_file = filp_open(UPDATE_FILE_PATH_2, O_RDONLY, 0);
        if (IS_ERR(fw_file)) {
            GSE_ERR("open bin file:%s fail error code:%ld,Update use head file!",UPDATE_FILE_PATH_2,PTR_ERR(fw_file));
            set_fs(old_fs);

            for(i = 0;i < sizeof(pedo20_sram_hex)/2;i++)
            {
                err = mc3451_i2c_write_block(client, MC3451_REG_SRAM_ADDR,  pedo20_sram_hex+i*2, 2);
                if(err)
                {
                    GSE_LOG("mc3451 i2c write firmware failed.\n");
                    return MC3451_RETCODE_ERROR_I2C;
                }
            }
            err = fw_check_sum(client,pedo20_sram_hex,sizeof(pedo20_sram_hex));
            if(err)
                return MC3451_RETCODE_ERROR_GET_DATA;
            else
                return MC3451_RETCODE_SUCCESS;
        }
     }

    fw_file->f_op->llseek(fw_file, 0, SEEK_SET);
    fw_length = fw_file->f_op->llseek(fw_file, 0, SEEK_END);
    fw_file->f_op->llseek(fw_file, 0, SEEK_SET);
    buffer = kzalloc(fw_length, GFP_KERNEL);
    fw_file->f_op->read(fw_file, (char *)buffer, fw_length, &fw_file->f_pos);

    GSE_LOG("Update use bin file,the fw_length =%d!",fw_length);

    for(i = 0;i < fw_length/2;i++)
    {
        err = mc3451_i2c_write_block(client, MC3451_REG_SRAM_ADDR, buffer+i*2, 2);
        if(err)
        {
            GSE_LOG("mc3451 i2c write firmware failed.\n");
            filp_close(fw_file, NULL);
            set_fs(old_fs);
            kfree(buffer);
            return MC3451_RETCODE_ERROR_I2C;
        }
    }

    err = fw_check_sum(client,buffer,fw_length);
    if(err)
        return MC3451_RETCODE_ERROR_GET_DATA;
    else
        return MC3451_RETCODE_SUCCESS;
    filp_close(fw_file, NULL);
    set_fs(old_fs);
    kfree(buffer);
    return err;
}

/*****************************************
 *** MC3451_Init
 *****************************************/
static int MC3451_Init(struct i2c_client *client, int reset_cali)
{
    unsigned char DataBuf[2] = { 0 };
    int err=0,sensor_data = 0;

    GSE_LOG("[%s]\n", __func__);

    /*check code runing location*/
    mc3451_i2c_read_block(client, MC3451_REG_VERSION, DataBuf, 2);
    sensor_data = (int)((DataBuf[0] << 8) | (DataBuf[1]));
    GSE_LOG("The Code ID: 0x%04X \n", sensor_data);
	if((sensor_data & 0x8000) == SRAM_VERSION)//OS MODE
    {
	    GSE_LOG("The Code  ID: 0x%04X running SRAM jump to OTP for firmware download.\n", sensor_data);
        DataBuf[0] = 0x5A;
        err = mc3451_i2c_write_block(client, MC3451_REG_RTB, DataBuf, 1);
        if(err)
            return MC3451_RETCODE_ERROR_I2C;
        GSE_LOG("Return to Bootload\n");
        mdelay(100);
    }

    /*download fw to sram*/
    DataBuf[0] = 0x00;
    DataBuf[1] = 0x00;
    err = mc3451_i2c_write_block(client, MC3451_REG_DOWNLOAD_ADDR, DataBuf, 2);
    if(err)
        return MC3451_RETCODE_ERROR_I2C;

    /*updata firmware*/
    err= MC3451_UpdateFirmware(client);
    if(err)
        return MC3451_RETCODE_ERROR_I2C;

    /*Jump to OS*/
    DataBuf[0] = 0x55;
    DataBuf[1] = 0xAA;
    err = mc3451_i2c_write_block(client, MC3451_REG_PEDOMETER_CONTROL, DataBuf, 2);
    if(err)
        return MC3451_RETCODE_ERROR_I2C;
    mdelay(100);

    /*check code runing location*/
    mc3451_i2c_read_block(client, MC3451_REG_VERSION, DataBuf, 2);
    sensor_data = (int)((DataBuf[0] << 8) | (DataBuf[1]));
    if((sensor_data & 0x8000) != SRAM_VERSION)//OS MODE
    {
        GSE_LOG("The Code ID: 0x%04X,Jump to OS failed.\n",sensor_data);
        return MC3451_RETCODE_ERROR_I2C;
    }else{
        GSE_LOG("The Code ID: 0x%04X,Runing SRAM\n",sensor_data);
    }

    /*enable Pedometer*/
    err = MC3451_EnablePedo(client, true);
    if(MC3451_RETCODE_SUCCESS != err)
    {
        GSE_LOG("mc3451_EnablePedo failed at open action!\n");
        return err;
    }

    /* config Pedometer parameter*/
    if (IS_PEDO10())
    {
        err = MC3451_ConfigPedo(client);
        if(MC3451_RETCODE_SUCCESS != err)
        {
            GSE_LOG("mc3451 pedo config parameter failed!\n");
            return err;
        }
    }

    return MC3451_RETCODE_SUCCESS;
}

/*****************************************
 *** show_sensordata_value
 *****************************************/
static ssize_t show_sensordata_value(struct device_driver *ddri, char *buf)
{
    struct i2c_client *client = mc3451_i2c_client;
    char strbuf[MC3451_BUF_SIZE] = { 0 };

    if (NULL == client) {
        GSE_ERR("i2c client is null!!\n");
        return 0;
    }

    MC3451_ReadSensorData(client, strbuf, MC3451_BUF_SIZE);

    return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);
}

/*****************************************
 *** show_stepcounter_value
 *****************************************/
static ssize_t show_stepcounter_value(struct device_driver *ddri, char *buf)
{
    struct i2c_client *client = mc3451_i2c_client;
    char strbuf[MC3451_BUF_SIZE] = { 0 };
    unsigned int StepCounter = 0,stepbuf[2] = {0};
    int status,err;

    if (NULL == client) {
        GSE_ERR("i2c client is null!!\n");
        return 0;
    }
    err = MC3451_GetPedo(client, &StepCounter, &status);
        if(err)
            return MC3451_RETCODE_ERROR_I2C;

    stepbuf[0] =(uint32_t) status;
    stepbuf[1] = StepCounter;
    sprintf(strbuf, "%u %u",StepCounter,status);

    return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);
}

/*****************************************
 *** store_stepcounter_debug
 *****************************************/
static ssize_t store_stepcounter_debug(struct device_driver *ddri, const char *pdBuf, size_t tCount)
{
#ifdef  GSENSOR_PEDOMETER_DEBUG
	mm_segment_t oldfs;
	struct timex  txc;  
	struct rtc_time tm;
    unsigned int _nValue=0;
    char file_name[64] = { 0 };

    sscanf(pdBuf, "%d", &_nValue);
    GSE_LOG("[%s] _nValue:%d\n", __func__,_nValue);
    //return (tCount);
    if(_nValue == 1){
        oldfs = get_fs();
        set_fs(KERNEL_DS);
        do_gettimeofday(&(txc.time));
        rtc_time_to_tm(txc.time.tv_sec,&tm);
        sprintf(file_name,"%s_%d_%d_%d_%d_%d_%d.txt",PD_DATA_PATH,tm.tm_year+1900,tm.tm_mon,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);
        printk("(%s)\n",file_name);

        pd_file =  filp_open(file_name, O_RDWR | O_CREAT, 0664);
        if (IS_ERR(pd_file)){
            GSE_ERR("open PD_DATA_PATH:%s fail error code:%ld.",PD_DATA_PATH,PTR_ERR(pd_file));
            return (tCount);
        }else{
            GSE_LOG("enable pedometer debug the log store:%s",file_name);
            pedometer_debug_flag = true;
        }
        //pd_buffer = kzalloc(fw_length, GFP_KERNEL);
    }else if(_nValue == 0){
        GSE_ERR("disable pedometer debug the log store:%s",PD_DATA_PATH);
        pedometer_debug_flag = false;
        msleep(300);
        filp_close(pd_file,NULL);
        set_fs(old_fs);
    }
#endif
    return (tCount);
}
/*****************************************
 *** show_chip_orientation
 *****************************************/
static ssize_t show_chip_orientation(struct device_driver *ptDevDrv, char *pbBuf)
{
    ssize_t       _tLength = 0;
    struct acc_hw   *_ptAccelHw = hw;

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
    struct mc3451_i2c_data   *_pt_i2c_obj = mc3451_obj_i2c_data;

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
 *** show_cali_value
 *****************************************/
static ssize_t show_cali_value(struct device_driver *ddri, char *buf)
{
    int len = 0;
    struct mc3451_i2c_data   *_pt_i2c_obj = mc3451_obj_i2c_data;

    GSE_LOG("fwq show_cali_value \n");

    len += snprintf(buf+len, PAGE_SIZE-len, "[SW] (%+3d, %+3d, %+3d) : (0x%02X, 0x%02X, 0x%02X)\n",
        _pt_i2c_obj->cali_sw[MC3451_AXIS_X], _pt_i2c_obj->cali_sw[MC3451_AXIS_Y], _pt_i2c_obj->cali_sw[MC3451_AXIS_Z],
        _pt_i2c_obj->cali_sw[MC3451_AXIS_X], _pt_i2c_obj->cali_sw[MC3451_AXIS_Y], _pt_i2c_obj->cali_sw[MC3451_AXIS_Z]);

    return len;
}

/*****************************************
 *** store_cali_value
 *****************************************/
static ssize_t store_cali_value(struct device_driver *ddri, const char *buf, size_t count)
{
    struct mc3451_i2c_data   *_pt_i2c_obj = mc3451_obj_i2c_data;
    int x=0,y=0,z=0;

    if(3 == sscanf(buf, "0x%02X 0x%02X 0x%02X", &x, &y, &z))
    {
        _pt_i2c_obj->cali_sw[MC3451_AXIS_X] = x;
        _pt_i2c_obj->cali_sw[MC3451_AXIS_X] = y;
        _pt_i2c_obj->cali_sw[MC3451_AXIS_X] = z;
    }

    return count;
}

/*****************************************
 *** show_chip_regiter
 *****************************************/
static ssize_t show_chip_register(struct device_driver *ddri, char *buf)
{
    unsigned int    _tLength    = 0;
    unsigned char   DataBuf[2] = { 0 };
    //short sensor_data[2];
    int err;

    struct i2c_client *client = mc3451_i2c_client;

    err = mc3451_i2c_read_block(client,global_Register,DataBuf, 2);
    //sensor_data[0] = (short)(DataBuf[0] << 8)|(short)(DataBuf[1]);
    if(err == 0)
	    GSE_LOG("mc3451 read Register: 0x%02X, MSB_Value: 0x%02X,LSB_Value: 0x%02X\n", global_Register,DataBuf[0],DataBuf[1]);
    else{
        GSE_LOG("read reg err\n");
        return MC3451_RETCODE_ERROR_I2C;
    }
	_tLength = snprintf(buf, PAGE_SIZE, "_nRegister: 0x%02X, MSB_Value: 0x%02X,LSB_Value: 0x%02X\n", global_Register,DataBuf[0],DataBuf[1]);
    return (_tLength);
}

/*****************************************
 *** store_chip_register
 *****************************************/
static ssize_t store_chip_register(struct device_driver *ddri, const char *pbBuf, size_t tCount)
{
    unsigned int  _nRegister=0,_nValue1=0,_nValue2=0;
    unsigned char   DataBuf[2] = { 0 };
    int err;

    struct i2c_client *client = mc3451_i2c_client;

    sscanf(pbBuf, "%x %x %x", &_nRegister, &_nValue1,&_nValue2);
    DataBuf[0] = _nRegister;
    DataBuf[1] = _nValue1;
    DataBuf[2] = _nValue2;
    GSE_ERR("[%s] _nRegister: 0x%02X, _nValue1: 0x%02X,_nValue1: 0x%02X\n", __func__, _nRegister, _nValue1,_nValue2);
    if(_nValue1 == 0xff){
        global_Register = _nRegister;
        return (tCount);
    }
    if (DataBuf[2] == 0)
    {
        err = mc3451_i2c_write_block(client, DataBuf[0], &DataBuf[1], 1);
    }else{
        err = mc3451_i2c_write_block(client, DataBuf[0], &DataBuf[1], 2);
    }
    if(err == 0)
        GSE_LOG("write reg ok\n");
    else{
        GSE_LOG("write reg err\n");
    }

    return (tCount);
}

/*****************************************
 *** show_regiter_map
 *****************************************/
static ssize_t show_regiter_map(struct device_driver *ddri, char *buf)
{
    u8       _bIndex       = 0;
    u8       _baRegMap[MC3451_REGMAP_LENGTH] = { 0 };
    ssize_t _tLength      = 0;

    struct i2c_client *client = mc3451_i2c_client;

    if ((0xA5 == buf[0]) && (0x7B == buf[1])) {

        MC3451_ReadRegMap(client, buf);
        _tLength = 64;
    } else {
        MC3451_ReadRegMap(client, _baRegMap);

        for (_bIndex = 0; _bIndex < MC3451_REGMAP_LENGTH; _bIndex++)
            _tLength += snprintf((buf + _tLength), (PAGE_SIZE - _tLength), "Reg[0x%02X]: 0x%02X\n",
                    _bIndex, _baRegMap[_bIndex]);
    }

    return _tLength;
}
/*****************************************
 *** show_code_version
 *****************************************/
static ssize_t show_code_version(struct device_driver *ddri, char *buf)
{
	unsigned int    _tLength	= 0;
	unsigned char   DataBuf[2] = { 0 };
	short sensor_data[2];
	int err;

	struct i2c_client *client = mc3451_i2c_client;

	err = mc3451_i2c_read_block(client,MC3451_REG_VERSION,DataBuf, 2);
	sensor_data[0] = (short)(DataBuf[0] << 8)|(short)(DataBuf[1]);
	if(err == 0)
	    GSE_LOG("mc3451 read 0x01 sensor_data=0x%x",sensor_data[0]);
	else{
		GSE_LOG("read reg err\n");
		return MC3451_RETCODE_ERROR_I2C;
	}

	_tLength = snprintf(buf, PAGE_SIZE, "The Code Version:_nRegister: 0x%02X, _nValue: 0x%04X\n", MC3451_REG_VERSION,sensor_data[0]);

	return (_tLength);
}

#ifdef CONFIG_HUAWEI_DSM
static ssize_t mc3451_show_test_dsm(struct device_driver *ddri, char *buf)
{
    return snprintf(buf,100,
        "test data_err: echo 1 > dsm_excep\n"
        "test i2c_err:  echo 2 > dsm_excep\n"
        "test xyz_all_zero_err: echo 3 > dsm_excep\n");
}

/*
 *  test data or i2c error interface for device monitor
*/
static ssize_t mc3451_store_test_dsm(struct device_driver *ddri, const char *buf, size_t tCount)
{
    int mode;
    int ret=0;

    if(sscanf(buf, "%d", &mode)!=1)
    {
        return -EINVAL;
    }
    switch(mode){
        case 1: /*test data error interface*/
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
static DRIVER_ATTR(sensordata ,       S_IRUGO | S_IRUGO, show_sensordata_value,  NULL);
static DRIVER_ATTR(stepcounter,       S_IWUSR | S_IRUGO, show_stepcounter_value, store_stepcounter_debug);
static DRIVER_ATTR(orientation,       S_IWUSR | S_IRUGO, show_chip_orientation,  store_chip_orientation);
static DRIVER_ATTR(cali,       S_IWUSR | S_IRUGO, show_cali_value,      store_cali_value);
static DRIVER_ATTR(reg,       S_IWUSR | S_IRUGO, show_chip_register,     store_chip_register);
static DRIVER_ATTR(regmap,       S_IWUSR | S_IRUGO, show_regiter_map,       NULL);
static DRIVER_ATTR(codeversion,   S_IWUSR | S_IRUGO, show_code_version,      NULL);
#ifdef CONFIG_HUAWEI_DSM
static DRIVER_ATTR(dsm_excep, 0660, mc3451_show_test_dsm,mc3451_store_test_dsm);
#endif


static struct driver_attribute   *mc3451_attr_list[] = {
                            &driver_attr_sensordata,
                            &driver_attr_stepcounter,
                            &driver_attr_orientation,
                            &driver_attr_cali,
                            &driver_attr_reg,
                            &driver_attr_regmap,
                            &driver_attr_codeversion,
                            #ifdef CONFIG_HUAWEI_DSM
                            &driver_attr_dsm_excep,
                            #endif
                            };

/*****************************************
 *** mc3451_create_attr
 *****************************************/
static int mc3451_create_attr(struct device_driver *driver)
{
    int idx, err = 0;
    int num = (int)(sizeof(mc3451_attr_list)/sizeof(mc3451_attr_list[0]));

    if (driver == NULL)
        return -EINVAL;

    for (idx = 0; idx < num; idx++) {
        err = driver_create_file(driver, mc3451_attr_list[idx]);
        if (err) {
            GSE_ERR("driver_create_file (%s) = %d\n", mc3451_attr_list[idx]->attr.name, err);
            break;
        }
    }
    return err;
}

/*****************************************
 *** mc3451_delete_attr
 *****************************************/
static int mc3451_delete_attr(struct device_driver *driver)
{
    int idx , err = 0;
    int num = (int)(sizeof(mc3451_attr_list)/sizeof(mc3451_attr_list[0]));

    if (driver == NULL)
        return -EINVAL;

    for (idx = 0; idx < num; idx++)
        driver_remove_file(driver, mc3451_attr_list[idx]);

    return err;
}

/*****************************************
 *** mc3451_open
 *****************************************/
static int mc3451_open(struct inode *inode, struct file *file)
{
    file->private_data = mc3451_i2c_client;

    if (file->private_data == NULL) {
        GSE_ERR("null pointer!!\n");
        return -EINVAL;
    }
    return nonseekable_open(inode, file);
}

/*****************************************
 *** mc3451_release
 *****************************************/
static int mc3451_release(struct inode *inode, struct file *file)
{
    file->private_data = NULL;
    return 0;
}

/*****************************************
 *** mc3451_ioctl
 *****************************************/
static long mc3451_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct i2c_client *client = (struct i2c_client*)file->private_data;
    struct mc3451_i2c_data *obj = (struct mc3451_i2c_data*)i2c_get_clientdata(client);
    char strbuf[MC3451_BUF_SIZE] = {0};
    //unsigned int StepCounter = 0,stepbuf[2] = {0};
    //int status;
    void __user *data = NULL;
    struct SENSOR_DATA sensor_data = {0};
    long err = 0;
    int cali[3] = {0};

    if(_IOC_DIR(cmd) & _IOC_READ)
    {
        err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    }
    else if(_IOC_DIR(cmd) & _IOC_WRITE)
    {
        err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    }

    if(err)
    {
        GSE_LOG("access error: %08X, (%2d, %2d)\n", cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd));
        return -EFAULT;
    }

    switch(cmd)
    {
        case GSENSOR_IOCTL_INIT:
            GSE_LOG("fwq GSENSOR_IOCTL_INIT\n");
            MC3451_Init(client, 0);
            break;
        case GSENSOR_IOCTL_READ_CHIPINFO:
            GSE_LOG("fwq GSENSOR_IOCTL_READ_CHIPINFO\n");
            data = (void __user *) arg;
            if(data == NULL)
            {
                err = -EINVAL;
                break;
            }

            MC3451_ReadChipInfo(client, strbuf, MC3451_BUF_SIZE);
            if(copy_to_user(data, strbuf, strlen(strbuf)+1))
            {
                err = -EFAULT;
                break;
            }
            break;

        case GSENSOR_IOCTL_READ_SENSORDATA:
            data = (void __user *) arg;
            if(data == NULL)
            {
                err = -EINVAL;
                #ifdef CONFIG_HUAWEI_DSM
                gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ABNORMAL);
                #endif
                break;
            }
            MC3451_ReadSensorData(client, strbuf, MC3451_BUF_SIZE);
            if(copy_to_user(data, strbuf, strlen(strbuf)+1))
            {
                err = -EFAULT;
                #ifdef CONFIG_HUAWEI_DSM
                gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ABNORMAL);
                #endif
                break;
            }
            break;

        case GSENSOR_IOCTL_READ_GAIN:
            GSE_LOG("fwq GSENSOR_IOCTL_READ_GAIN\n");
            data = (void __user *) arg;
            if(data == NULL)
            {
                err = -EINVAL;
                break;
            }

            if(copy_to_user(data, &gsensor_gain, sizeof(struct GSENSOR_VECTOR3D)))
            {
                err = -EFAULT;
                break;
            }
            break;

        case GSENSOR_IOCTL_READ_OFFSET:
            GSE_LOG("fwq GSENSOR_IOCTL_READ_OFFSET\n");
            data = (void __user *) arg;
            if(data == NULL)
            {
                err = -EINVAL;
                break;
            }

            if(copy_to_user(data, &gsensor_offset, sizeof(struct GSENSOR_VECTOR3D)))
            {
                err = -EFAULT;
                break;
            }
            break;

        case GSENSOR_IOCTL_READ_RAW_DATA:
            GSE_LOG("fwq GSENSOR_IOCTL_READ_RAW_DATA\n");
            data = (void __user *) arg;
            if(data == NULL)
            {
                err = -EINVAL;
                break;
            }
            MC3451_ReadRawData(client, strbuf);
            if(copy_to_user(data, strbuf, strlen(strbuf)+1))
            {
                err = -EFAULT;
                break;
            }
            break;

        case GSENSOR_IOCTL_SET_CALI:
            GSE_LOG("fwq GSENSOR_IOCTL_SET_CALI!!\n");
            data = (void __user*)arg;
            if(data == NULL)
            {
                err = -EINVAL;
                break;
            }
            if(copy_from_user(&sensor_data, data, sizeof(sensor_data)))
            {
                err = -EFAULT;
                break;
            }
            if(atomic_read(&obj->suspend))
            {
                GSE_ERR("Perform calibration in suspend state!!\n");
                err = -EINVAL;
            }
            else
            {
                GSE_LOG("GSENSOR_IOCTL_SET_CALI!!sensor_data .x =%d,sensor_data .z =%d,sensor_data .z =%d \n",sensor_data.x,sensor_data.y,sensor_data.z);
                cali[MC3451_AXIS_X] = sensor_data.x * gsensor_gain.x / GRAVITY_EARTH_1000;
                cali[MC3451_AXIS_Y] = sensor_data.y * gsensor_gain.y / GRAVITY_EARTH_1000;
                cali[MC3451_AXIS_Z] = sensor_data.z * gsensor_gain.z / GRAVITY_EARTH_1000;

                err = MC3451_WriteCalibration(client, cali);
            }
            break;

        case GSENSOR_IOCTL_CLR_CALI:
            GSE_LOG("fwq GSENSOR_IOCTL_CLR_CALI!!\n");
            err = MC3451_ResetCalibration(client);
            break;

        case GSENSOR_IOCTL_GET_CALI:
            GSE_LOG("fwq mc3xxx GSENSOR_IOCTL_GET_CALI\n");
            data = (void __user*)arg;
            if(data == NULL)
            {
                err = -EINVAL;
                break;
            }
            if((err = MC3451_ReadCalibration(client, cali)))
            {
                break;
            }

            sensor_data.x = cali[MC3451_AXIS_X] * GRAVITY_EARTH_1000 / gsensor_gain.x;
            sensor_data.y = cali[MC3451_AXIS_Y] * GRAVITY_EARTH_1000 / gsensor_gain.y;
            sensor_data.z = cali[MC3451_AXIS_Z] * GRAVITY_EARTH_1000 / gsensor_gain.z;

            #ifdef CONFIG_HUAWEI_DSM
            if(sensor_data.x == 0 && sensor_data.y == 0 && sensor_data.z == 0)
            {
                gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ALL_ZERO);
            }
            #endif

            if(copy_to_user(data, &sensor_data, sizeof(sensor_data)))
            {
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
/*****************************************
 *** mc3451_compat_ioctl
 *****************************************/
static long   mc3451_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    long            err = 0;
    void __user    *arg32 = compat_ptr(arg);

    if (!file->f_op || !file->f_op->unlocked_ioctl)
    {
         GSE_LOG("[%s] file->f_op or file->f_op->unlocked_ioctl is NULL\n", __func__);
         return -ENOTTY;
    }
    switch(cmd)
    {

             GSE_LOG("fwq COMPAT_GSENSOR_IOCTL_READ_SENSORDATA\n");
             if (NULL == arg32)
             {
                 err = -EINVAL;
                 break;
             }

             err = file->f_op->unlocked_ioctl(file, GSENSOR_IOCTL_READ_SENSORDATA, (unsigned long)arg32);

             if (err < 0)
                 GSE_ERR("[%s] COMPAT_GSENSOR_IOCTL_READ_SENSORDATA is failed\n", __func__);

            break;
    case COMPAT_GSENSOR_IOCTL_SET_CALI:
        GSE_LOG("COMPAT_GSENSOR_IOCTL_SET_CALI");
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
        GSE_LOG("COMPAT_GSENSOR_IOCTL_GET_CALI");
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
        GSE_LOG("COMPAT_GSENSOR_IOCTL_CLR_CALI");
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
#endif // END of #ifdef CONFIG_COMPAT

/*****************************************
 *** STRUCT:: mc3451_fops
 *****************************************/
static const struct file_operations    mc3451_fops = {
                           .owner        = THIS_MODULE,
                           .open        = mc3451_open,
                           .release        = mc3451_release,
                           .unlocked_ioctl    = mc3451_ioctl,
                            #ifdef CONFIG_COMPAT
                            .compat_ioctl    = mc3451_compat_ioctl,
                            #endif
                           };

/*****************************************
 *** STRUCT:: mc3451_device
 *****************************************/
static struct miscdevice mc3451_device = {
                        .minor = MISC_DYNAMIC_MINOR,
                        .name  = "gsensor",
                        .fops  = &mc3451_fops,
                        };


/*****************************************
 *** mc3451_suspend
 *****************************************/
static int mc3451_suspend(struct i2c_client *client, pm_message_t msg)
{
    struct mc3451_i2c_data *obj = i2c_get_clientdata(client);
    int err = 0;

    GSE_LOG("mc3451_suspend\n");

    if (msg.event == PM_EVENT_SUSPEND) {
        if (obj == NULL) {
            GSE_ERR("null pointer!!\n");
            return -EINVAL;
        }

        atomic_set(&obj->suspend, 1);

        err = MC3451_SetPowerMode(client, false);
        if (err) {
            GSE_ERR("write power control fail!!\n");
            return err;
        }

        MC3451_power(obj->hw, 0);
    }
    return err;
}

/*****************************************
 *** mc3451_resume
 *****************************************/
static int mc3451_resume(struct i2c_client *client)
{
    struct mc3451_i2c_data *obj = i2c_get_clientdata(client);
    int err;
    GSE_FUN();

    if(obj == NULL)
    {
        GSE_ERR("null pointer!!\n");
        return -EINVAL;
    }

    MC3451_power(obj->hw, 1);

    err = MC3451_SetPowerMode(client, true);

    if (err)
    {
        GSE_ERR("write power control fail!!\n");
        return err;
    }

    atomic_set(&obj->suspend, 0);

    return 0;
}

/* if use  this typ of enable , Gsensor should report inputEvent(x, y, z ,stats, div) to HAL */
static int mc3451_accel_open_report_data(int open)
{
    /* should queuq work to report event if  is_report_input_direct=true */
    return 0;
}

/* if use  this typ of enable , Gsensor only enabled but not report inputEvent to HAL */
static int mc3451_accel_enable_nodata(int en)
{
    int res = 0,retry = 0;
    bool power = false;

    if (1 == en)
        power = true;
    if (0 == en)
        power = false;

    GSE_LOG("[%s] nEnable: %d\n", __FUNCTION__, en);

    for (retry = 0; retry < 3; retry++) {
        res = MC3451_SetPowerMode(mc3451_obj_i2c_data->client, power);
        if (res == 0) {
            GSE_LOG("MC3451_SetPowerMode done\n");
            break;
        }
        GSE_LOG("MC3451_SetPowerMode fail\n");
    }

    if (res != 0) {
        GSE_LOG("MC3451_SetPowerMode fail!\n");
        return -1;
    }
    GSE_LOG("mc3451_accel_enable_nodata OK!\n");
    return 0;
}

static int mc3451_accel_set_delay(u64 ns)
{
    int value = 0;

    value = (int)ns/1000/1000;
    GSE_LOG("mc3451_accel_set_delay %d(ns), chip only use 1024HZ\n", value);
    return 0;
}
static int gsensor_zero_count = 0;
static int mc3451_accel_get_data(int *x , int *y, int *z, int *status)
{
    char buff[MC3451_BUF_SIZE];
    int ret;

    MC3451_ReadSensorData(mc3451_obj_i2c_data->client, buff, MC3451_BUF_SIZE);
    ret = sscanf(buff, "%x %x %x", x, y, z);
    *status = SENSOR_STATUS_ACCURACY_MEDIUM;

    //GSE_LOG("[%s] X: %d, Y: %d, Z:%d\n", __FUNCTION__, *pnX, *pnY, *pnZ);
    if ((0 == *x) && (0 == *y) && (0 == *z))
        {
        gsensor_zero_count++;
        GSE_LOG( "mc3451_accel_get_data,  gsensor_zero_count = %d; \n",gsensor_zero_count);
        if(gsensor_zero_count == 3)
            {
                gsensor_report_dsm_err(DSM_SHB_ERR_GSENSOR_DATA_ALL_ZERO);
                gsensor_zero_count = 0;
            }
        }
        else
        {
            gsensor_zero_count = 0;
        }

    return 0;
}

#ifdef GSENSOR_INTERRUPT_FILT
static int mc3451_filt_pos(struct mc3451_i2c_data *mc3451){

    unsigned char   DataBuf[2] = { 0 };

    if (mc3451_i2c_read_block(mc3451->client, 0X03, DataBuf, 1) != 0)
        return MC3451_RETCODE_ERROR_I2C;

    input_report_abs(mc3451->input,KEY_POWER,1);
    input_sync(mc3451->input);
    input_report_abs(mc3451->input,KEY_POWER,0);
    input_sync(mc3451->input);

    return 0;
}

static irqreturn_t mc3451_irq_thread(int irq, void *dev_id)
{
    struct mc3451_i2c_data *mc3451 = dev_id;
    mc3451_filt_pos(mc3451);
    return IRQ_HANDLED;
}
#endif

static int mc3451_fw_init(void *client)
{
    int err = -1;
	err = MC3451_Init(client, 1);
    if (MC3451_RETCODE_SUCCESS == err)
    {
        GSE_LOG("[MC3451 firmware init success");
        return MC3451_RETCODE_SUCCESS;
    }
    else
    {
        GSE_LOG("[MC3451 firmware init failed");
        return MC3451_RETCODE_ERROR_STATUS;
    }
}

#ifdef GSENSOR_PEDOMETER_DEBUG
    //enum hrtimer_restart Wmcu_hrtimer_callback( struct hrtimer *timer )
    static void work_mstimer_handler(struct work_struct *data)
    {
        u8    Rdata[MC3451_DATA_LEN] = { 0 };
        s16 waData[MC3451_AXES_NUM] = {0};
        u8 Pd_Buffer[128] = {0};
        int err=0;
        u64 ts_nsec;
        unsigned long rem_nsec;

        struct i2c_client *client = mc3451_i2c_client;

        ts_nsec = local_clock();
        rem_nsec = do_div(ts_nsec,1000000000);

        if(pedometer_debug_flag == true)
        {
            err = mc3451_i2c_read_block(client, 0x09, Rdata,MC3451_DATA_LEN);
            if(err == 0)
            {
                waData[MC3451_AXIS_X] = ((signed short) ((Rdata[1]<<8) | Rdata[0]));
                waData[MC3451_AXIS_Y] = ((signed short) ((Rdata[3]<<8) | Rdata[2]));
                waData[MC3451_AXIS_Z] = ((signed short) ((Rdata[5]<<8) | Rdata[4]));
                printk("Pedometer raw X = %d  Y = %d  Z = %d\n", waData[MC3451_AXIS_X] , waData[MC3451_AXIS_Y] , waData[MC3451_AXIS_Z]);
            }
            sprintf(Pd_Buffer, "\n[Pedometer_rawdata][%5lu.%06lu] X: %6d, Y: %6d, Z: %6d.",(unsigned long)ts_nsec,rem_nsec/1000, waData[MC3451_AXIS_X] , waData[MC3451_AXIS_Y] , waData[MC3451_AXIS_Z]);
            pd_file->f_op->write(pd_file, Pd_Buffer, strlen(Pd_Buffer), &pd_file->f_pos);
        }
        //GSE_LOG("[%s][60ms] X: %d, Y: %d, Z: %d\n",__func__, waData[MC3451_AXIS_X], waData[MC3451_AXIS_Y], waData[MC3451_AXIS_Z]);
        mod_timer(&ms_timer, jiffies + msecs_to_jiffies(60));
        //return HRTIMER_NORESTART;
    }

    static void Wmcu_mstimer_callback(unsigned long data)
    {
        schedule_work(&work);
    }
#endif


/*****************************************
 *** mc3451_i2c_probe
 *****************************************/

static int mc3451_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct i2c_client *new_client;
    struct mc3451_i2c_data *obj;
    #ifdef GSENSOR_INTERRUPT_FILT
    struct input_dev    *input_dev;
    #endif
    struct acc_control_path ctl = {0};
    struct acc_data_path data = {0};
    struct task_struct *mc3451_fw_thread;
    int err = -1;

    GSE_LOG("%s\n",__func__);

    err = MC3451_ValidateSensorIC(client);
    if(err)
    {
        GSE_ERR("ERR: fail to probe mCube sensor!\n");
        goto exit;
    }

    obj = kzalloc(sizeof(*obj), GFP_KERNEL);
    if (!obj) {
        err = -ENOMEM;
        goto exit;
    }
    #ifdef GSENSOR_INTERRUPT_FILT
    of_get_mc3451_platform_data(&client->dev);
    /* configure the gpio pins */
    err = gpio_request_one(gsensor_int_gpio_number, GPIOF_IN,
                 "gsensor_int");
    if (err < 0) {
        GSE_ERR("Unable to request gpio int_pin\n");
        goto gpio_request_failed;
    }

    err = request_threaded_irq(client->irq, NULL, mc3451_irq_thread,
            IRQF_TRIGGER_RISING | IRQF_ONESHOT,
            "mc3451_irq", obj);
    if (err < 0) {
        dev_err(&client->dev,
        "irq %d busy? error %d\n", client->irq, err);
        goto gpio_request_failed;
    }

    /* allocate an input device */
    input_dev = input_allocate_device();
    if (!input_dev)
    {
        dev_err(&client->dev, "failed to allocate input device\n");
        err = -ENOMEM;
        goto exit;
    }

    input_dev->name = "mc3451_filt";
    input_dev->id.bustype = BUS_I2C;
    input_dev->dev.parent = &client->dev;

    //Set the input device to support the type of event
    input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_SYN);
    input_set_capability(input_dev, EV_KEY, KEY_POWER);
    obj->input = input_dev;

    err = input_register_device(input_dev);
    if (err)
        goto exit_irq_failed;
    #endif

    obj->hw = hw;
    err = hwmsen_get_convert(obj->hw->direction, &obj->cvt);
    if (err) {
        GSE_ERR("invalid direction: %d\n", obj->hw->direction);
        goto exit_kfree;
    }

    mc3451_obj_i2c_data = obj;
    obj->client = client;
    new_client = obj->client;
    i2c_set_clientdata(new_client, obj);
    atomic_set(&obj->trace, 0);
    atomic_set(&obj->suspend, 0);
    new_client->timing = 400;
    mc3451_i2c_client = new_client;

    err = misc_register(&mc3451_device);
    if (err) {
        GSE_ERR("mc3451_device register failed\n");
        goto exit_misc_device_register_failed;
    }
    ctl.is_use_common_factory = false;
    err = mc3451_create_attr(&(mc3451_init_info.platform_diver_addr->driver));
    if (err) {
        GSE_ERR("create attribute err = %d\n", err);
        goto exit_create_attr_failed;
    }

    ctl.open_report_data = mc3451_accel_open_report_data;
    ctl.enable_nodata = mc3451_accel_enable_nodata;
    ctl.set_delay  = mc3451_accel_set_delay;
    ctl.is_report_input_direct = false;
    ctl.is_support_batch = obj->hw->is_batch_supported;
    err = acc_register_control_path(&ctl);
    if (err) {
        GSE_ERR("register acc control path err\n");
        goto exit_kfree;
    }
    data.get_data = mc3451_accel_get_data;
    data.vender_div = 1000;
    err = acc_register_data_path(&data);
    if (err) {
        GSE_ERR("register acc data path err= %d\n", err);
        goto exit_kfree;
    }

    #ifdef MC3451_SUPPORT_STEP_COUNT
        step_c_driver_add(&mc3451_step_c_init_info); //step counter
    #endif

    mc3451_fw_thread = kthread_run(mc3451_fw_init, (void *)client, "mc3451_fw_init");
    if (IS_ERR(mc3451_fw_thread)) {
        err = PTR_ERR(mc3451_fw_thread);
        GSE_ERR("failed to create mc3451_fw_thread: %d\n", err);
        return err;
    }

    #ifdef GSENSOR_PEDOMETER_DEBUG
    //old_fs = get_fs();
    //set_fs(KERNEL_DS);
    //fd_file =  filp_open(DATA_PATH , O_RDWR | O_CREAT, 0644);
    //set_fs(old_fs);
    //filp_close(fd_file,NULL);
    //hrtimer_init( &hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
    //hr_timer.function = &Wmcu_hrtimer_callback;
    //hrtimer_start( &hr_timer, ktime_set(0, 600*1000*1000), HRTIMER_MODE_REL );
    INIT_WORK(&work, work_mstimer_handler);
    setup_timer( &ms_timer, Wmcu_mstimer_callback, 0 );
    mod_timer( &ms_timer, jiffies + msecs_to_jiffies(60) );
    #endif

    GSE_LOG("%s: OK\n", __func__);
    return 0;

exit_create_attr_failed:
exit_misc_device_register_failed:
    misc_deregister(&mc3451_device);
//exit_init_failed:
#ifdef GSENSOR_INTERRUPT_FILT
exit_irq_failed:
gpio_request_failed:
    gpio_free(gsensor_int_gpio_number);
#endif
exit_kfree:
    kfree(obj);
exit:
    GSE_ERR("%s: err = %d\n", __func__, err);
    return err;

}

/*****************************************
 *** mc3451_i2c_remove
 *****************************************/
static int mc3451_i2c_remove(struct i2c_client *client)
{
    int err = 0;

    if((err = mc3451_delete_attr(&mc3451_init_info.platform_diver_addr->driver)))
    {
        GSE_ERR("mc3451_delete_attr fail: %d\n", err);
    }

    if((err = misc_deregister(&mc3451_device)))
    {
        GSE_ERR("misc_deregister fail: %d\n", err);
    }
#ifdef GSENSOR_PEDOMETER_DEBUG
    del_timer( &ms_timer );
    //hrtimer_cancel( &hr_timer );
#endif
    mc3451_i2c_client = NULL;
    i2c_unregister_device(client);
    kfree(i2c_get_clientdata(client));

    return 0;
}

/*****************************************
 *** mc3451_remove
 *****************************************/
static int mc3451_remove(void)
{
    GSE_FUN();

    MC3451_power(hw, 0);
    i2c_del_driver(&mc3451_i2c_driver);

    return 0;
}

/*****************************************
 *** mc3451_local_init
 *****************************************/
static int  mc3451_local_init(void)
{
    GSE_LOG("mc3451_local_init\n");
    MC3451_power(hw, 1);

    if (i2c_add_driver(&mc3451_i2c_driver)) {
        GSE_ERR("add driver error\n");
        return -1;
    }

    return 0;
}

/*****************************************
 *** mc3451_init
 *****************************************/
static int __init mc3451_init(void)
{
    const char *name = "mediatek,mc3451";
    hw = get_accel_dts_func(name, hw);
    if (!hw)
    GSE_ERR("get dts info fail\n");

    acc_driver_add(&mc3451_init_info);

    return 0;
}

/*****************************************
 *** mc3451_exit
 *****************************************/
static void __exit mc3451_exit(void)
{
    GSE_FUN();
}

/*----------------------------------------------------------------------------*/
module_init(mc3451_init);
module_exit(mc3451_exit);
/*----------------------------------------------------------------------------*/
MODULE_DESCRIPTION("mc3451 G-Sensor and Step Counter Driver");
MODULE_AUTHOR("mCube-inc");
MODULE_LICENSE("GPL");
MODULE_VERSION(MC3451_DEV_DRIVER_VERSION);

