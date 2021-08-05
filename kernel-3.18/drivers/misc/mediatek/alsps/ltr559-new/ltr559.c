/*
 * 
 * Author: MingHsien Hsieh <minghsien.hsieh@mediatek.com>
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
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include <mt-plat/mt_gpio.h>
#include "linux/mutex.h"

#include "cust_alsps.h"
#include "ltr559.h"
#include "alsps.h"
#ifdef CONFIG_HY_DRV_ASSIST
#include <linux/hy-assist.h>
#endif
#include <misc/app_info.h>
#ifdef CONFIG_HUAWEI_HW_I2C_DCT
#include <linux/hw_dev_dec.h>
#endif

#ifdef CONFIG_HUAWEI_DSM
#include "dsm_sensor.h"
#endif


/******************************************************************************
 * configuration
*******************************************************************************/
/*----------------------------------------------------------------------------*/

#define LTR559_DEV_NAME   "ltr559"

#define GN_MTK_BSP_PS_DYNAMIC_CALI

/*----------------------------------------------------------------------------*/
#define APS_TAG                  "[ltr559] "
#define APS_FUN(f)               printk(KERN_INFO APS_TAG"%s\n", __FUNCTION__)

#define APS_ERR(fmt, args...)	pr_err(APS_TAG fmt, ##args)
#define APS_LOG(fmt, args...)	pr_err(APS_TAG fmt, ##args)
#define APS_DBG(fmt, args...)	pr_err(APS_TAG fmt, ##args)


/*----------------------------------------------------------------------------*/

#define MAX_ELM 25
static unsigned int record[MAX_ELM];
static int rct=0,full=0;
static long ratio_sum=0;
static int als_raw_data = 0;
#define LTR559_PRECISE_VALUE 600
#define LTR559_DEFAULT_VALUE 100
static int ratio_value = 1;
BOOL ltr559_is_calibrate = FALSE;
static int als_alsval_ch0;
static int als_alsval_ch1;
static int als_ratio_a = 1;
extern char *lcd_name_for_als;
//static void ltr559_als_cal_ration(int raw_data);
static void start_to_calibrate(void);
static int custom_ps_thd_val_low;
static int custom_ps_thd_val_high;
static  DEFINE_MUTEX (read_lock);


static struct i2c_client *ltr559_i2c_client = NULL;
#ifdef CONFIG_HUAWEI_DSM
extern struct dsm_client *dsm_sensorhub_dclient;
#endif
/*----------------------------------------------------------------------------*/
static const struct i2c_device_id ltr559_i2c_id[] = {{LTR559_DEV_NAME,0},{}};
static unsigned long long int_top_time;
struct alsps_hw alsps_cust;
static struct alsps_hw *hw = &alsps_cust;
struct platform_device *alspsPltFmDev;


/*----------------------------------------------------------------------------*/
static int ltr559_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id); 
static int ltr559_i2c_remove(struct i2c_client *client);
static int ltr559_i2c_detect(struct i2c_client *client, struct i2c_board_info *info);
/*----------------------------------------------------------------------------*/
static int ltr559_i2c_suspend(struct i2c_client *client, pm_message_t msg);
static int ltr559_i2c_resume(struct i2c_client *client);
static int ltr559_ps_enable(struct i2c_client *client, int enable);

static int ps_get_data(int *value, int *status);

static int ps_gainrange;
static int als_gainrange;

static int final_prox_val;
static int final_lux_val;

static int last_min_value = 2047;

static int alsps_sensor = 0;
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
static int ltr559_als_read(struct i2c_client *client, u16* data);
static int ltr559_ps_read(struct i2c_client *client, u16* data);


/*----------------------------------------------------------------------------*/


typedef enum {
    CMC_BIT_ALS    = 1,
    CMC_BIT_PS     = 2,
} CMC_BIT;

/*----------------------------------------------------------------------------*/
struct ltr559_i2c_addr {    /*define a series of i2c slave address*/
    u8  write_addr;  
    u8  ps_thd;     /*PS INT threshold*/
};

/*----------------------------------------------------------------------------*/

struct ltr559_priv {
    struct alsps_hw  *hw;
    struct i2c_client *client;
    struct work_struct  eint_work;
    struct mutex lock;
	/*i2c address group*/
    struct ltr559_i2c_addr  addr;

     /*misc*/
    u16		    als_modulus;
    atomic_t    i2c_retry;
    atomic_t    als_debounce;   /*debounce time after enabling als*/
    atomic_t    als_deb_on;     /*indicates if the debounce is on*/
    atomic_t    als_deb_end;    /*the jiffies representing the end of debounce*/
    atomic_t    ps_mask;        /*mask ps: always return far away*/
    atomic_t    ps_debounce;    /*debounce time after enabling ps*/
    atomic_t    ps_deb_on;      /*indicates if the debounce is on*/
    atomic_t    ps_deb_end;     /*the jiffies representing the end of debounce*/
    atomic_t    ps_suspend;
    atomic_t    als_suspend;
	atomic_t  init_done;
	struct device_node *irq_node;
	int		irq;

    /*data*/
    u16         als;
    u16          ps;
    u8          _align;
    u16         als_level_num;
    u16         als_value_num;
    u32         als_level[C_CUST_ALS_LEVEL-1];
    u32         als_value[C_CUST_ALS_LEVEL];
	u16			ps_cali;

    atomic_t    als_cmd_val;    /*the cmd value can't be read, stored in ram*/
    atomic_t    ps_cmd_val;     /*the cmd value can't be read, stored in ram*/
    atomic_t    ps_thd_val;     /*the cmd value can't be read, stored in ram*/
	atomic_t    ps_thd_val_high;     /*the cmd value can't be read, stored in ram*/
	atomic_t    ps_thd_val_low;     /*the cmd value can't be read, stored in ram*/
    ulong       enable;         /*enable mask*/
    ulong       pending_intr;   /*pending interrupt*/

    /*early suspend*/
#if defined(CONFIG_HAS_EARLYSUSPEND)
    struct early_suspend    early_drv;
#endif     
};

 struct PS_CALI_DATA_STRUCT
{
    int close;
    int far_away;
    int valid;
} ;

static struct PS_CALI_DATA_STRUCT ps_cali={0,0,0};
static int intr_flag_value = 0;


static struct ltr559_priv *ltr559_obj = NULL;


static struct i2c_client *ltr559_i2c_client;

static DEFINE_MUTEX(ltr559_mutex);


static int ltr559_local_init(void);
static int ltr559_remove(void);
static int ltr559_dynamic_calibrate(void);

static int ltr559_init_flag =  -1;
static struct alsps_init_info ltr559_init_info = {
		.name = "ltr559",
		.init = ltr559_local_init,
		.uninit = ltr559_remove,

};


#ifdef CONFIG_OF
static const struct of_device_id alsps_of_match[] = {
	{.compatible = "mediatek,alsps"},
	{},
};
#endif

/*----------------------------------------------------------------------------*/
static struct i2c_driver ltr559_i2c_driver = {	
	.probe      = ltr559_i2c_probe,
	.remove     = ltr559_i2c_remove,
	.detect     = ltr559_i2c_detect,
	.suspend    = ltr559_i2c_suspend,
	.resume     = ltr559_i2c_resume,
	.id_table   = ltr559_i2c_id,
	.driver = {
		.name           = LTR559_DEV_NAME,
		#ifdef CONFIG_OF
		.of_match_table = alsps_of_match,
		#endif
	},
};

#ifdef CONFIG_HUAWEI_DSM
static int als_ps_sensor_report_dsm_err(int type)
{
    int size = 0;
    int total_size = 0;
    if (FACTORY_BOOT == get_boot_mode()||(dsm_sensorhub_dclient == NULL))
    return 0;
    /* try to get permission to use the buffer */
    if(dsm_client_ocuppy(dsm_sensorhub_dclient))
    {
            /* buffer is busy */
            APS_ERR("%s: buffer is busy!", __func__);
            return -EBUSY;
    }
    switch(type)
    {
        case DSM_SHB_ERR_LIGHT_I2C_ERR:
            /* report i2c infomation */
            size = dsm_client_record(dsm_sensorhub_dclient,"als_ps_sensor I2C error:%d\n",type);
            break;
        case DSM_SHB_ERR_LIGHT_IRQ_ERR:
            size = dsm_client_record(dsm_sensorhub_dclient,"als_ps_sensor irq error:%d\n",type);
            break;
        case DSM_SHB_ERR_LIGHT_ENABLE_ERR:
            size = dsm_client_record(dsm_sensorhub_dclient,"als_ps_sensor enable error:%d\n",type);
            break;
        case DSM_SHB_ERR_LIGHT_THRESHOLD_ERR:
            size = dsm_client_record(dsm_sensorhub_dclient,"als_ps_sensor sensor threshold error:%d\n",type);
			break;
        default:
            APS_ERR("%s:unsupported dsm report type.\n",__func__);
            break;
        }
    total_size += size;
    dsm_client_notify(dsm_sensorhub_dclient, type);
    return total_size;
}
#endif


/* 
 * #########
 * ## I2C ##
 * #########
 */

// I2C Read
static int ltr559_i2c_read_reg(u8 regnum)
{
	u8 buffer[1]={0};
	u8 reg_value[1]={0};
	int res = 0;
	mutex_lock(&read_lock);
	
	buffer[0]= regnum;
	res = i2c_master_send(ltr559_obj->client, buffer, 0x1);
	if(res <= 0)	{
	   
	   APS_ERR("read reg send res = %d\n",res);
       #ifdef CONFIG_HUAWEI_DSM
       als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_I2C_ERR);
       #endif
	   mutex_unlock(&read_lock);
		return res;
	}
	res = i2c_master_recv(ltr559_obj->client, reg_value, 0x1);
	if(res <= 0)
	{
		APS_ERR("read reg recv res = %d\n",res);
		
        #ifdef CONFIG_HUAWEI_DSM
        als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_I2C_ERR);
        #endif
		mutex_unlock(&read_lock);
		return res;
	}
	mutex_unlock(&read_lock);
	return reg_value[0];
}

// I2C Write
static int ltr559_i2c_write_reg(u8 regnum, u8 value)
{
	u8 databuf[2];    
	int res = 0;
   
	databuf[0] = regnum;   
	databuf[1] = value;
	res = i2c_master_send(ltr559_obj->client, databuf, 0x2);

	if (res < 0)
		{
            #ifdef CONFIG_HUAWEI_DSM
            als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_I2C_ERR);
            #endif
			APS_ERR("wirte reg send res = %d\n",res);
		   	return res;
		}
		
	else
		return 0;
}


/*----------------------------------------------------------------------------*/
static ssize_t ltr559_show_als(struct device_driver *ddri, char *buf)
{
	int res;
	if(!ltr559_obj)
	{
	    APS_ERR("ltr559_obj is null!!\n");
		return 0;
	}
	res = ltr559_als_read(ltr559_obj->client, &ltr559_obj->als);
	return snprintf(buf, PAGE_SIZE, "%d\n", ltr559_obj->als);
	
}
/*----------------------------------------------------------------------------*/
static ssize_t ltr559_show_ps(struct device_driver *ddri, char *buf)
{
	int  res;
	if(!ltr559_obj)
	{
		APS_ERR("ltr559_obj is null!!\n");
		return 0;
	}
	res = ltr559_ps_read(ltr559_obj->client, &ltr559_obj->ps);
    return snprintf(buf, PAGE_SIZE, "0x%04X\n", ltr559_obj->ps);     
}
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
static ssize_t ltr559_show_status(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	
	if(!ltr559_obj)
	{
		APS_ERR("ltr559_obj is null!!\n");
		return 0;
	}
	
	if(ltr559_obj->hw)
	{
	
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: %d, (%d %d)\n", 
			ltr559_obj->hw->i2c_num, ltr559_obj->hw->power_id, ltr559_obj->hw->power_vol);
		
	}
	else
	{
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: NULL\n");
	}


	len += snprintf(buf+len, PAGE_SIZE-len, "MISC: %d %d\n", atomic_read(&ltr559_obj->als_suspend), atomic_read(&ltr559_obj->ps_suspend));

	return len;
}

/*----------------------------------------------------------------------------*/
static ssize_t ltr559_store_status(struct device_driver *ddri, const char *buf, size_t count)
{
	int status1,ret;
	if(!ltr559_obj)
	{
		APS_ERR("ltr559_obj is null!!\n");
		return 0;
	}
	
	if(1 == sscanf(buf, "%d ", &status1))
	{ 
	    //ret=ltr559_ps_enable(ps_gainrange);
		ret=ltr559_ps_enable(ltr559_obj->client, status1);
		APS_DBG("iret= %d, ps_gainrange = %d\n", ret, ps_gainrange);
	}
	else
	{
		APS_DBG("invalid content: %s, length = %zu\n", buf, count);
	}
	return count;    
}


/*----------------------------------------------------------------------------*/
static ssize_t ltr559_show_reg(struct device_driver *ddri, char *buf)
{
	int i,len=0;
	int reg[]={0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,
		0x8d,0x8e,0x8f,0x90,0x91,0x92,0x93,0x94,0x95,0x97,0x98,0x99,0x9a,0x9e};
	for(i=0;i<27;i++)
		{
		len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%04X value: 0x%04X\n", reg[i],ltr559_i2c_read_reg(reg[i]));	

	    }
	return len;
}
/*----------------------------------------------------------------------------*/
static ssize_t ltr559_store_reg(struct device_driver *ddri, const char *buf, size_t count)
{
	int ret,value;
	u8 reg;
	if(!ltr559_obj)
	{
		APS_ERR("ltr559_obj is null!!\n");
		return 0;
	}
	
	if(2 == sscanf(buf, "%hhx %x ", &reg, &value))
	{ 
		APS_DBG("before write reg: %x, reg_value = %x  write value=%x\n", reg,ltr559_i2c_read_reg(reg),value);
	    ret=ltr559_i2c_write_reg(reg,value);
		APS_DBG("after write reg: %x, reg_value = %x\n", reg,ltr559_i2c_read_reg(reg));
	}
	else
	{
		APS_DBG("invalid content: '%s', length = %zu\n", buf, count);
	}
	return count;    
}

#ifdef CONFIG_HUAWEI_DSM
static ssize_t ltr559_show_test_dsm(struct device_driver *ddri, char *buf)
{
    return snprintf(buf,100,
        "test irq_err: echo 1 > dsm_excep\n"
        "test i2c_err:  echo 2 > dsm_excep\n"
        "test enable_err: echo 3 > dsm_excep\n"
        "test threshold_err: echo 4 > dsm_excep\n");
}
/*
*   test data or i2c error interface for device monitor
*/
static ssize_t ltr559_store_test_dsm(struct device_driver *ddri, const char *buf, size_t tCount)
{
    int mode;
    int ret=0;

    if(sscanf(buf, "%d", &mode)!=1)
    {
        return -EINVAL;
    }
    switch(mode){
         case 1: /*test irq error interface*/
            ret = als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_IRQ_ERR);
            break;
        case 2: /*test i2c error interface*/
            ret = als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_I2C_ERR);
            break;
        case 3:/*test enable error interface*/
            ret = als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_ENABLE_ERR);
            break;
        case 4:/*test threshold error interface*/
            ret = als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_THRESHOLD_ERR);
            break;
        default:
            break;
    }

    return ret;
}
#endif
static ssize_t ltr559_show_light_value(struct device_driver *ddri, char *buf)
{
    ssize_t len = 0;

    len += snprintf(buf+len,PAGE_SIZE-len,"\n%s\n","------start to show als value-----");
    len += snprintf(buf+len, PAGE_SIZE-len,"raw_data_value=%d,precise_value=%d,defalut_value=%d,ration_value=%d,is_calibrate=%s,"\
	"calibrat_value=%d,ration_a=%d,lcd_name_for_als = %s]\n",als_raw_data,LTR559_PRECISE_VALUE,LTR559_DEFAULT_VALUE,ratio_value,(ltr559_is_calibrate == FALSE)? "No":"Yes" ,final_lux_val,als_ratio_a,lcd_name_for_als);
    len += snprintf(buf+len,PAGE_SIZE-len,"\n%s\n","------end to show als value-------");

    return len;
}
static ssize_t ltr559_show_calibrate_ration(struct device_driver *ddri, char *buf)
{
    ssize_t len = 0;

    len += snprintf(buf+len,PAGE_SIZE-len,"ration = %d\n",ratio_value);

    return len;
}

static ssize_t ltr559_store_calibrate_ration(struct device_driver *ddri, const char *buf, size_t count)
{
    int value;

    if(!ltr559_obj)
    {
        APS_ERR("ltr559_obj is null!!\n");
        return 0;
    }

    if(1 == sscanf(buf,"%d",&value))
    {
        APS_DBG("before store calibrate_ration the  ration is %d\n",ratio_value);
        ratio_value = value;
        APS_DBG("after store calibrate_ration the  ration is %d\n",ratio_value);
    }
    else
    {
        APS_DBG("invalid content: %s, length = %zu\n", buf, count);
    }

    return count;
}

static ssize_t ltr559_show_calibrate_cmd(struct device_driver *ddri, char *buf)
{
    ssize_t len = 0;

    if(FALSE  ==  ltr559_is_calibrate)
    {
        len += snprintf(buf+len,PAGE_SIZE-len,"%s\n","------Fail-----");
    }
    else if(TRUE  ==  ltr559_is_calibrate)
    {
        len += snprintf(buf+len,PAGE_SIZE-len,"%s\n","------Success-----");
    }
    else
    {
    }
    return len;
}
static ssize_t ltr559_store_calibrate_cmd(struct device_driver *ddri, const char *buf, size_t count)
{
    int value;

    if(!ltr559_obj)
    {
        APS_ERR("ltr559_obj is null!!\n");
        return 0;
    }
    if(1 == sscanf(buf,"%d",&value))
    {
        APS_DBG("the cmd value is %d\n",value);
        switch(value)
        {
            case 0:
            {
                ltr559_is_calibrate = FALSE;
                APS_DBG("ltr559_is_calibrate cmd is %d\n",ltr559_is_calibrate);
                break;
            }
            case 1:
            {
                ltr559_is_calibrate = FALSE;
                if((als_ratio_a <	460 )&&(als_ratio_a >= 300) )
                 {
                    start_to_calibrate();
                }
                APS_DBG("ltr559_is_calibrate cmd is true and the final result is %d\n",ltr559_is_calibrate);
                break;
            }
            default:
                APS_DBG("unknown cmd\n");
        }
    }
    else
    {
        APS_DBG("invalid content: %s, length = %zu\n", buf, count);
    }
    return count;
}
static ssize_t ltr559_show_chipinfo(struct device_driver *ddri, char *buf)
{
    ssize_t len = 0;

    len += snprintf(buf+len,PAGE_SIZE-len,"%s\n","ltr559");
    return len;

}
/*----------------------------------------------------------------------------*/
static DRIVER_ATTR(als,     0664, ltr559_show_als,   NULL);
static DRIVER_ATTR(ps,      0664, ltr559_show_ps,    NULL);
static DRIVER_ATTR(status,  0664, ltr559_show_status,  ltr559_store_status);
static DRIVER_ATTR(reg,     0664, ltr559_show_reg,   ltr559_store_reg);
#ifdef CONFIG_HUAWEI_DSM
static DRIVER_ATTR(dsm_excep, 0660, ltr559_show_test_dsm, ltr559_store_test_dsm);
#endif
/*chip info*/
static DRIVER_ATTR(chipinfo,     0664, ltr559_show_chipinfo,   NULL);
/*display various value*/
static DRIVER_ATTR(als_cal_show,     0664, ltr559_show_light_value,   NULL);
/*start cail*/
static DRIVER_ATTR(als_cal_start,     0664, ltr559_show_calibrate_cmd, ltr559_store_calibrate_cmd);
/*show and store ration value*/
static DRIVER_ATTR(als_cal_value,     0664, ltr559_show_calibrate_ration,ltr559_store_calibrate_ration);
/*----------------------------------------------------------------------------*/
static struct driver_attribute *ltr559_attr_list[] = {
    &driver_attr_als,
    &driver_attr_ps,    
    &driver_attr_status,
    &driver_attr_reg,
    #ifdef CONFIG_HUAWEI_DSM
    &driver_attr_dsm_excep,
    #endif
    &driver_attr_chipinfo,
    &driver_attr_als_cal_show,
    &driver_attr_als_cal_start,
    &driver_attr_als_cal_value
};
/*----------------------------------------------------------------------------*/
static int ltr559_create_attr(struct device_driver *driver) 
{
	int idx, err = 0;
	int num = (int)(sizeof(ltr559_attr_list)/sizeof(ltr559_attr_list[0]));

	if (driver == NULL)
	{
		return -EINVAL;
	}

	for(idx = 0; idx < num; idx++)
	{
		err = driver_create_file(driver, ltr559_attr_list[idx]);
		if(err)
		{            
			APS_ERR("driver_create_file (%s) = %d\n", ltr559_attr_list[idx]->attr.name, err);
			break;
		}
	}    
	return err;
}
/*----------------------------------------------------------------------------*/
	static int ltr559_delete_attr(struct device_driver *driver)
	{
	int idx ,err = 0;
	int num = (int)(sizeof(ltr559_attr_list)/sizeof(ltr559_attr_list[0]));

	if (!driver)
	return -EINVAL;

	for (idx = 0; idx < num; idx++) 
	{
		driver_remove_file(driver, ltr559_attr_list[idx]);
	}
	
	return err;
}

/*----------------------------------------------------------------------------*/

#ifdef CONFIG_HY_DRV_ASSIST
static ssize_t ltr559_ic_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n","LTR559");
}
static ssize_t ltr559_vendor_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n","DYNA IMAGE");
}
static ssize_t ltr559_exist_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", alsps_sensor);
}
static ssize_t ltr559_ps_data_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
	int ret,status,psData;
	ret = ps_get_data(&psData,&status);
    return sprintf(buf, "%d\n", psData);
}
#endif

/* 
 * ###############
 * ## PS CONFIG ##
 * ###############

 */

static int ltr559_ps_set_thres(void)
{
	int res;
	u8 databuf[2];

	struct i2c_client *client = ltr559_obj->client;
	struct ltr559_priv *obj = ltr559_obj;	
	//APS_FUN();

	//BUILD_BUG_ON_ZERO(0>1);

	//APS_DBG("ps_cali.valid: %d\n", ps_cali.valid);
	if(1 == ps_cali.valid)
	{
		databuf[0] = LTR559_PS_THRES_LOW_0; 
		databuf[1] = (u8)(ps_cali.far_away & 0x00FF);
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			goto EXIT_ERR;
			return ltr559_ERR_I2C;
		}
		databuf[0] = LTR559_PS_THRES_LOW_1; 
		databuf[1] = (u8)((ps_cali.far_away & 0xFF00) >> 8);
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			goto EXIT_ERR;
			return ltr559_ERR_I2C;
		}
		databuf[0] = LTR559_PS_THRES_UP_0;	
		databuf[1] = (u8)(ps_cali.close & 0x00FF);
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			goto EXIT_ERR;
			return ltr559_ERR_I2C;
		}
		databuf[0] = LTR559_PS_THRES_UP_1;	
		databuf[1] = (u8)((ps_cali.close & 0xFF00) >> 8);
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			goto EXIT_ERR;
			return ltr559_ERR_I2C;
		}
	}
	else
	{
		databuf[0] = LTR559_PS_THRES_LOW_0; 
		databuf[1] = (u8)((atomic_read(&obj->ps_thd_val_low)) & 0x00FF);
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			goto EXIT_ERR;
			return ltr559_ERR_I2C;
		}
		databuf[0] = LTR559_PS_THRES_LOW_1; 
		databuf[1] = (u8)((atomic_read(&obj->ps_thd_val_low )>> 8) & 0x00FF);
		
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			goto EXIT_ERR;
			return ltr559_ERR_I2C;
		}
		databuf[0] = LTR559_PS_THRES_UP_0;	
		databuf[1] = (u8)((atomic_read(&obj->ps_thd_val_high)) & 0x00FF);
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			goto EXIT_ERR;
			return ltr559_ERR_I2C;
		}
		databuf[0] = LTR559_PS_THRES_UP_1;	
		databuf[1] = (u8)((atomic_read(&obj->ps_thd_val_high) >> 8) & 0x00FF);
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			goto EXIT_ERR;
			return ltr559_ERR_I2C;
		}
	
	}
	APS_DBG("ps low: %d high: %d\n", atomic_read(&obj->ps_thd_val_low), atomic_read(&obj->ps_thd_val_high));

	res = 0;
	return res;
	
	EXIT_ERR:
	APS_ERR("set thres: %d\n", res);
	return res;

}
struct delayed_work pscali_work;
static void ltr559_pscali_delay_work(struct work_struct *work)
{
	if (ltr559_dynamic_calibrate() < 0)
		APS_ERR("PS: ltr559_dynamic_calibrate err \n");
}
//static int ltr559_ps_enable(int gainrange)
static int ltr559_ps_enable(struct i2c_client *client, int enable)
{
	//struct ltr559_priv *obj = ltr559_obj;
	u8 regdata;	
	int err;
	u8 databuf[2];	
	int res;
	struct ltr559_priv *obj = ltr559_obj;   
	
	//int setgain;
    	APS_LOG("ltr559_ps_enable() ...start!\n");
	err = ltr559_i2c_write_reg(LTR559_PS_MEAS_RATE, 0x00);//0x0F
	if(err<0)
	{
			APS_LOG("set ps measusrment rate error\n");
	}


	if(0 == obj->hw->polling_mode_ps)
	{	
		//APS_LOG("eint enable+++\n");
		atomic_set(&obj->ps_thd_val_high,  2047);
		atomic_set(&obj->ps_thd_val_low, 0);
		ltr559_ps_set_thres();
		
		databuf[0] = LTR559_INTERRUPT;	
		databuf[1] = 0x01;
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			APS_LOG("set interrupt mode error\n");
			#ifdef CONFIG_HUAWEI_DSM
			als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_IRQ_ERR	);
			#endif
		}

		databuf[0] = LTR559_INTERRUPT_PERSIST;	
		databuf[1] = 0x01;
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			APS_LOG("set interrupt persist error\n");
			#ifdef CONFIG_HUAWEI_DSM
			als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_IRQ_ERR	);
			#endif
		}

	}

	regdata = ltr559_i2c_read_reg(LTR559_PS_CONTR);

	if (enable == 1) {
		//APS_LOG("PS: enable ps only \n");
		err = ltr559_i2c_write_reg(LTR559_PS_CONTR, 0x03);
	} else {
		//APS_LOG("PS: disable ps only \n");
		err = ltr559_i2c_write_reg(LTR559_PS_CONTR, 0x00);
	}

	if(err<0)
	{
		APS_ERR("PS: enable ps err: %d en: %d \n", err, enable);
		return err;
	}
	//msleep(WAKEUP_DELAY);
	//APS_LOG("sensor wakeup done\n");
	regdata = ltr559_i2c_read_reg(LTR559_PS_CONTR);
	//APS_LOG("LTR559_PS_CONTR = %x\n",regdata);

#ifdef GN_MTK_BSP_PS_DYNAMIC_CALI
	if (enable == 1 && (regdata & 0x02)) {
	schedule_delayed_work(&pscali_work,msecs_to_jiffies(100));
	} else {
	cancel_delayed_work_sync(&pscali_work);
	}
#endif
	//ltr559_ps_set_thres();
	return 0;
	
}


static int ltr559_ps_read(struct i2c_client *client, u16 *data)
{
	int psval_lo, psval_hi, psdata;
    psdata=-1;
	psval_lo = ltr559_i2c_read_reg(LTR559_PS_DATA_0);
	//APS_DBG("ps_rawdata_psval_lo = %d\n", psval_lo);
	if (psval_lo < 0){
	    
	    APS_DBG("psval_lo error\n");
		psdata = psval_lo;
		goto out;
	}
	psval_hi = ltr559_i2c_read_reg(LTR559_PS_DATA_1);
    //APS_DBG("ps_rawdata_psval_hi = %d\n", psval_hi);
	if (psval_hi < 0){
	    APS_DBG("psval_hi error\n");
		psdata = psval_hi;
		goto out;
	}
	
	psdata = ((psval_hi & 7)* 256) + psval_lo;
    //psdata = ((psval_hi&0x7)<<8) + psval_lo;
    APS_DBG("ps_rawdata = %d\n", psdata);
	*data = psdata;
	return 0;
    
	out:
	final_prox_val = psdata;
    APS_DBG("ps read error: ps_rawdata = %d\n", psdata);
	return psdata;
}

/* 
 * ################
 * ## ALS CONFIG ##
 * ################
 */


/*---------------------------add by hongguang for dynamic calibrate-------------------------------------------------*/
#ifdef GN_MTK_BSP_PS_DYNAMIC_CALI
static int ltr559_dynamic_calibrate(void)
{
	//int ret = 0;
	int i = 0;
	int j = 0;
	int data = 0;
	int noise = 0;
	//int len = 0;
	int err = 0;
	int max = 0;
	//int idx_table = 0;
	unsigned long data_total = 0;
	struct ltr559_priv *obj = ltr559_obj;

	//APS_FUN(f);
	if (!obj) goto err;

	//msleep(10);


	ltr559_i2c_write_reg(LTR559_PS_MEAS_RATE, 0x0F);

	for (i = 0; i < 3; i++) {
		if (max++ > 3) {
			goto err;
		}
		msleep(11);
		err=ltr559_ps_read(obj->client, &obj->ps);
		if(err){
			APS_ERR("ltr559_dynamic_calibrate read ps fail!!!\n");
		}
		data = obj->ps;
		
		if(data == 0){
			j++;
		}	
		data_total += data;
	}
	noise = data_total/(3 - j);
	//isadjust = 1;
	if((noise < last_min_value+300)){
	    last_min_value = noise;
		if(noise < 100){
			atomic_set(&obj->ps_thd_val_high,  noise+125);//125
			atomic_set(&obj->ps_thd_val_low, noise+60); //60
		}else if(noise < 200){
			atomic_set(&obj->ps_thd_val_high,  noise+135);//135
			atomic_set(&obj->ps_thd_val_low, noise+70);//70
		}else if(noise < 400){
			atomic_set(&obj->ps_thd_val_high,  noise+150);
			atomic_set(&obj->ps_thd_val_low, noise+80);
		}else if(noise < 800){
			atomic_set(&obj->ps_thd_val_high,  noise+180);
			atomic_set(&obj->ps_thd_val_low, noise+100);
	    }else if(noise < 1000){
			atomic_set(&obj->ps_thd_val_high,  noise+200);
			atomic_set(&obj->ps_thd_val_low, noise+150);
		}else if(noise < 1650){
			atomic_set(&obj->ps_thd_val_high,  noise+400);
			atomic_set(&obj->ps_thd_val_low, noise+150);
		}
		else{
			atomic_set(&obj->ps_thd_val_high,  1750);
			atomic_set(&obj->ps_thd_val_low, 1700);
			}
	custom_ps_thd_val_low = atomic_read(&obj->ps_thd_val_low);
	custom_ps_thd_val_high = atomic_read(&obj->ps_thd_val_high);
	}
	else
	{
		atomic_set(&obj->ps_thd_val_low,custom_ps_thd_val_low);
		atomic_set(&obj->ps_thd_val_high,custom_ps_thd_val_high);
	}
	APS_DBG(" calibrate:noise=%d, thdlow= %d , thdhigh = %d\n", noise,  atomic_read(&obj->ps_thd_val_low),  atomic_read(&obj->ps_thd_val_high));

	ltr559_i2c_write_reg(LTR559_PS_MEAS_RATE, 0x00);
	ltr559_ps_set_thres();
	return 0;
err:
	APS_ERR("ltr559_dynamic_calibrate fail!!!\n");
	ltr559_i2c_write_reg(LTR559_PS_MEAS_RATE, 0x00);
	return -1;
}
#endif

/*---------------------------------add by hongguang for dynamic calibrate-------------------------------------------*/





static int ltr559_als_enable(struct i2c_client *client, int enable)
{
	//struct ltr559_priv *obj = i2c_get_clientdata(client);	
	int err=0;	
	u8 regdata=0;	
	//if (enable == obj->als_enable)		
	//	return 0;

	regdata = ltr559_i2c_read_reg(LTR559_ALS_CONTR);

	if (enable == 1) {
		APS_LOG("ALS(1): enable als only \n");
		err = ltr559_i2c_write_reg(LTR559_ALS_CONTR, 0x0D);
	} else {
		APS_LOG("ALS(1): disable als only \n");
		err = ltr559_i2c_write_reg(LTR559_ALS_CONTR, 0x00);
	}

	regdata = ltr559_i2c_read_reg(LTR559_ALS_CONTR);
	APS_LOG("ALS(1):  als_contr = = %d \n", regdata);
	if(err<0)
	{
		APS_ERR("ALS: enable als err: %d en: %d \n", err, enable);
		return err;
	}

	//obj->als_enable = enable;
	
	mdelay(WAKEUP_DELAY);

	return 0;
	
        
}
#if 0
/*----------------modified by steven for avglux function-----------------------*/		
static int get_avg_lux(unsigned int lux)
{
	int lux_a;
	int i = 0;
	int max_val_1,min_val_1,max_index_1,min_index_1;
	int max_val_2,min_val_2,max_index_2,min_index_2;

	if(rct >= MAX_ELM)
		full=1;
	
	max_val_1 = lux;
	min_val_1 = lux;
	max_val_2 = lux;
	min_val_2 = lux;

	max_index_1 = 0;
	min_index_1 = 0;
	max_index_2 = 0;
	min_index_2 = 0;

	if(full){
		rct %= MAX_ELM;
		lux_sum -= record[rct];
	}
	lux_sum += lux;
	record[rct]=lux;
	rct++;
	
	for(i = 0;i < MAX_ELM;i++)
	{
		if(record[i] > max_val_1){
			max_val_1 = record[i];
			max_index_1 = i;			
		}		
	}

	for(i = 0;i < MAX_ELM;i++)
	{
		if(i != max_index_1){
			if(record[i] > max_val_2){
				max_val_2 = record[i];
				max_index_2 = i;			
			}	
		}
	}

	
	for(i = 0;i < MAX_ELM;i++)
	{
		if(record[i] < min_val_1){
			min_val_1 = record[i];
			min_index_1 = i;			
		}		
	}

	for(i = 0;i < MAX_ELM;i++)
	{
		if(i != min_index_1){
			if(record[i] < min_val_2){
				min_val_2 = record[i];
				min_index_2 = i;			
			}
		}		
	}

	
	if(full){
	lux_a = (lux_sum - max_val_1 - max_val_2 - min_val_1 - min_val_2) / (MAX_ELM - 4);
	}else{
	lux_a = lux_sum /rct;
	}
	return lux_a;
}
#endif



static int get_ratio(unsigned int ratio_c)
{
	int ratio_avg;
	int i = 0;
	int inc_a = 0;
	int max_val_1,min_val_1,max_index_1,min_index_1;
	int max_val_2,min_val_2,max_index_2,min_index_2;
	
	max_val_1 = ratio_c;
	min_val_1 = ratio_c;
	max_val_2 = ratio_c;
	min_val_2 = ratio_c;

	max_index_1 = 0;
	min_index_1 = 0;
	max_index_2 = 0;
	min_index_2 = 0;

	if(rct >= MAX_ELM)
		full=1;
	

	if(full){
		rct %= MAX_ELM;
		ratio_sum -= record[rct];
	}
	ratio_sum += ratio_c;
	record[rct]=ratio_c;
	rct++;
	
	if(full){
	ratio_avg = (ratio_sum ) / (MAX_ELM);
	}else{
	ratio_avg = ratio_sum /rct;
	}
	
	
	for(i = 0;i < MAX_ELM;i++)
	{
		if(record[i] > max_val_1){
			max_val_1 = record[i];
			max_index_1 = i;			
		}		
	}

	for(i = 0;i < MAX_ELM;i++)
	{
		if(i != max_index_1){
			if(record[i] > max_val_2){
				max_val_2 = record[i];
				max_index_2 = i;			
			}	
		}
	}

	
	for(i = 0;i < MAX_ELM;i++)
	{
		if(record[i] < min_val_1){
			min_val_1 = record[i];
			min_index_1 = i;			
		}		
	}

	for(i = 0;i < MAX_ELM;i++)
	{
		if(i != min_index_1){
			if(record[i] < min_val_2){
				min_val_2 = record[i];
				min_index_2 = i;			
			}
		}		
	}
	//APS_DBG("min_index ratio_avg = %d \n",ratio_avg);
	//APS_DBG("min_index_2 = %d, max_index_2=%d \n",min_index_2,max_index_2);
	//APS_DBG("record[min_index_2] = %d,record[max_index_2]=%d \n",record[min_index_2],record[max_index_2]);

	if(ratio_avg > 780 && ratio_avg <800 ){
		if(record[min_index_2]<790 && record[max_index_2]>790){ 
  		  inc_a = 1;
		}
	}
	
	return inc_a;
}


#define ALS_LEVEL_NUM  12
#define ALS_VALUE_NUM  13
int als_level[] = {0,10,50,100,200,500,1000,2000,5000,10000,20000,30000};
int als_value[] = {0,10,50,100,200,500,1000,2000,5000,10000,20000,30000,30000};
int ratio_a = 100;

static int ltr559_als_read(struct i2c_client *client, u16* data)
{
	int alsval_ch0_lo, alsval_ch0_hi, alsval_ch0;
	int alsval_ch1_lo, alsval_ch1_hi, alsval_ch1;
	long int  luxdata_int;
	int ratio;
	int als;
	int idx;
	int level_high;
	int level_low;
	int level_diff;
	int value_high;
	int value_low;
	int value_diff;
	int value;

	alsval_ch1_lo = ltr559_i2c_read_reg(LTR559_ALS_DATA_CH1_0);
	alsval_ch1_hi = ltr559_i2c_read_reg(LTR559_ALS_DATA_CH1_1);
	alsval_ch1 = (alsval_ch1_hi * 256) + alsval_ch1_lo;

	alsval_ch0_lo = ltr559_i2c_read_reg(LTR559_ALS_DATA_CH0_0);
	alsval_ch0_hi = ltr559_i2c_read_reg(LTR559_ALS_DATA_CH0_1);
	alsval_ch0 = (alsval_ch0_hi * 256) + alsval_ch0_lo;
	//APS_DBG("alsval_ch0_lo = %d,alsval_ch0_hi=%d,alsval_ch0=%d\n",alsval_ch0_lo,alsval_ch0_hi,alsval_ch0);
	//APS_DBG("alsval_ch1_lo = %d,alsval_ch1_hi=%d,alsval_ch1=%d\n",alsval_ch1_lo,alsval_ch1_hi,alsval_ch1);
    if((alsval_ch1==0)||(alsval_ch0==0))
    {
        luxdata_int = 0;
		ratio = 1000;
    }else{
		ratio = (alsval_ch1*1000) /(alsval_ch0+alsval_ch1);
    }
	//APS_DBG("ratio = %d  gainrange = %d\n",ratio,als_gainrange);
	if(ratio > ratio_a+10 || ratio < ratio_a -10)
	{ratio_a = ratio;}
	if (get_ratio(ratio) == 2 )
	{ratio_a = 800;}

	als_ratio_a = ratio_a;
	als_alsval_ch0 = alsval_ch0;
	als_alsval_ch1 = alsval_ch1;
	#if 0
		luxdata_int = (((5926 * alsval_ch0)+(1185 * alsval_ch1)))/21657;
		if(luxdata_int < 100 && ludata_int > 25 && ratio_a >760)
			ratio_a = 850;
	#endif
	//APS_DBG("als_ratio_a = %d\n",als_ratio_a);
	/*CWF*/
	if(!strcmp("ili9881c_hd720_dsi_vdo_helitec",lcd_name_for_als))
	{
		//APS_DBG("config lcd is ili9881c_hd720_dsi_vdo_helitec actually lcd is %s\n",lcd_name_for_als);
        if (ratio_a < 600){
			luxdata_int = ((((17743 * alsval_ch0)+(11059 * alsval_ch1)))/5190)*138/143;
		//luxdata_int = ((((17743 * alsval_ch0)+(11059 * alsval_ch1)))/5190)*50/143;
		//als_level[7] = 1700;
		    if(1 == ratio_value)
				luxdata_int = luxdata_int*ratio_value;
			else
				luxdata_int = luxdata_int*ratio_value/LTR559_DEFAULT_VALUE;
		}
		else if ((ratio_a < 660) && (ratio_a >= 600)){
			luxdata_int = (((42785 * alsval_ch0)-(19548 * alsval_ch1))/6000)*120/100;
		//luxdata_int = ((42785 * alsval_ch0)-(19548 * alsval_ch1))/6000);
		}
		/*D*/
		else if((ratio_a <  785 )&&(ratio_a >= 660))
		{
			luxdata_int = ((((((5926 * alsval_ch0)+(1185 * alsval_ch1)))/8360)*190/314)*135/200)*148/100;
		//luxdata_int = ((((5926 * alsval_ch0)+(1185 * alsval_ch1)))/8360)*190/314;
		    if(1 == ratio_value)
				luxdata_int = luxdata_int*ratio_value;
			/*xia*/
			else if(ratio_value >= 105)
				luxdata_int = luxdata_int*ratio_value/LTR559_DEFAULT_VALUE*23/25;
			/*zhong*/
			else if(ratio_value > 95&&ratio_value < 105)
				luxdata_int = luxdata_int*ratio_value/LTR559_DEFAULT_VALUE;
			/*shang hei*/
			else if(ratio_value <=95 && ratio_value > 85)
				luxdata_int = luxdata_int*LTR559_DEFAULT_VALUE/ratio_value;
			/*shang bai*/
			else if(ratio_value <=85 && ratio_value > 1)
				luxdata_int = luxdata_int*LTR559_DEFAULT_VALUE/ratio_value*3/5;
		}
		/*A*/
		else if ((ratio_a < 900) && (ratio_a >= 785)) {
			luxdata_int = ((((((5926 * alsval_ch0)+(1185 * alsval_ch1)))/27997)*115/100)*110/200)*155/100;
			//luxdata_int = ((((5926 * alsval_ch0)+(1185 * alsval_ch1)))/27997)*115/100;
			if(1 == ratio_value)
				luxdata_int = luxdata_int*ratio_value;
			/*xia bai*/
			else if(ratio_value >= 125)
				luxdata_int = (luxdata_int*ratio_value/LTR559_DEFAULT_VALUE)*5/6;
			/*xia hei*/
			else if(ratio_value >= 110 && ratio_value < 125)
				luxdata_int = luxdata_int*ratio_value/LTR559_DEFAULT_VALUE;
			/*zhong*/
            else if(ratio_value > 95&&ratio_value < 110)
				luxdata_int = luxdata_int*ratio_value/LTR559_DEFAULT_VALUE;
			/*shang bai*/
			else if(ratio_value <= 95 && ratio_value > 83)
				luxdata_int = luxdata_int*LTR559_DEFAULT_VALUE/ratio_value;
			/*shang hei*/
			else if(ratio_value <=83 && ratio_value > 1)
				luxdata_int = luxdata_int*LTR559_DEFAULT_VALUE/ratio_value*50/78;

		}
	}
	else if(!strcmp("hx8394f_hd720_dsi_vdo_truly",lcd_name_for_als))
	{
        //APS_DBG("config lcd is hx8394f_hd720_dsi_vdo_truly actually lcd is %s\n",lcd_name_for_als);

		if (ratio_a < 600){
			luxdata_int = ((((17743 * alsval_ch0)+(11059 * alsval_ch1)))/5190)*62/100;//38->62
			//als_level[7] = 1700;
			if(1 == ratio_value)
				luxdata_int = luxdata_int*ratio_value;
			else
				luxdata_int = luxdata_int*ratio_value/LTR559_DEFAULT_VALUE;
		}
		else if ((ratio_a < 660) && (ratio_a >= 600)){
			luxdata_int = ((42785 * alsval_ch0)-(19548 * alsval_ch1))/6000;
		}
		/*D*/
		else if((ratio_a <  780 )&&(ratio_a >= 660))
		{
			luxdata_int = ((((5926 * alsval_ch0)+(1185 * alsval_ch1)))/8360)*19/31;
			if(1 == ratio_value)
				luxdata_int = luxdata_int*ratio_value;
			/*xia*/
			else if(ratio_value >= 110)
				luxdata_int = luxdata_int*ratio_value/LTR559_DEFAULT_VALUE*23/25;
			/*zhong*/
			else if(ratio_value > 98&&ratio_value < 110)
				luxdata_int = luxdata_int*ratio_value/LTR559_DEFAULT_VALUE;
			/*shang bai*/
			else if(ratio_value <=98 && ratio_value > 88)
				luxdata_int = luxdata_int*LTR559_DEFAULT_VALUE/ratio_value;
			/*shang hei*/
			else if(ratio_value <=88 && ratio_value > 1)
				luxdata_int = luxdata_int*LTR559_DEFAULT_VALUE/ratio_value*3/4;
		}
		/*A*/
		else if ((ratio_a < 950) && (ratio_a >= 780)) {
			luxdata_int = ((((5926 * alsval_ch0)+(1185 * alsval_ch1)))/27997)*98/100;//115->95
			if(ratio_a <= 798 && ratio_value >= 117 && ratio_value <= 136)
			{
			    luxdata_int = ((((5926 * alsval_ch0)+(1185 * alsval_ch1)))/8360)*19/31;
			}
			if(1 == ratio_value)
				luxdata_int = luxdata_int*ratio_value;
			/*xia bai*/
			else if(ratio_value >= 125)
				luxdata_int = (luxdata_int*ratio_value/LTR559_DEFAULT_VALUE)*5/6;
			/*xia hei*/
			else if(ratio_value >= 110 && ratio_value < 125)
				luxdata_int = luxdata_int*ratio_value/LTR559_DEFAULT_VALUE;
			/*zhong*/
            else if(ratio_value > 98&&ratio_value < 110)
				luxdata_int = luxdata_int*ratio_value/LTR559_DEFAULT_VALUE;
			/*shang bai*/
			else if(ratio_value <= 98 && ratio_value > 88)
				luxdata_int = luxdata_int*LTR559_DEFAULT_VALUE/ratio_value;
			/*shang hei*/
			else if(ratio_value <=88 && ratio_value > 1)
				luxdata_int = luxdata_int*LTR559_DEFAULT_VALUE/ratio_value*5/6;

		}
	}
	else {
	luxdata_int = 0;
	}
	APS_DBG(" %s:luxdata_int = %ld,ratio_value = %d,ratio= %d,ratio_a = %d, alsval_ch0 = %d,alsval_ch1 = %d,gainrange = %d\n", __func__,luxdata_int,ratio_value,ratio,ratio_a,alsval_ch0,alsval_ch1,als_gainrange);
	als = luxdata_int;

#if 1
	for (idx = 0; idx < ALS_LEVEL_NUM; idx++) {
	         if (als < als_level[idx])
	                   break;
	}

	if (idx >= ALS_VALUE_NUM) {
	         APS_ERR("ambient light sensor exceed range not eorroor\n");
	         idx = ALS_VALUE_NUM - 1;
	}
	if(idx>=ALS_LEVEL_NUM)
		idx=ALS_LEVEL_NUM-1;
	level_high = als_level[idx];
	level_low = (idx > 0) ? als_level[idx - 1] : 0;
	level_diff = level_high - level_low;
	value_high = als_value[idx];
	value_low = (idx > 0) ? als_value[idx - 1] : 0;
	value_diff = value_high - value_low;
	value = 0;

	if ((level_low >= level_high) || (value_low >= value_high))
	         value = value_low;
	else
	         value =(level_diff * value_low + (als - level_low) * value_diff +((level_diff + 1) >> 1)) / level_diff;
	//APS_DBG("ALS: %d [%d, %d] => %d [%d, %d]\n", als, level_low, level_high, value, value_low, value_high);
    als_raw_data = als;
	//APS_DBG("als_als_raw_data is %d\n",als_raw_data);
	//APS_DBG("als_als_raw_data is %d\n",value);
	//if(1 == ratio_value)
		//luxdata_int = value*ratio_value;
	//else
		//luxdata_int = value*ratio_value/LTR559_DEFAULT_VALUE;
	//luxdata_int = value;
    luxdata_int = value;
#endif

#if 0
	if(luxdata_int >= 5 && luxdata_int <= 16){
		luxdata_int = 10;
	}
#endif
	#if 0
	/*----------------modified by steven for avglux function-----------------------*/		
	luxdata_int = get_avg_lux(luxdata_int);
	#endif
	
	*data = luxdata_int;
	final_lux_val = luxdata_int;
	return 0;

	
}
void start_to_calibrate(void)
{
	ltr559_is_calibrate = TRUE;

	if(!strcmp("ili9881c_hd720_dsi_vdo_helitec",lcd_name_for_als))
	{
		APS_DBG("%s,config lcd ili9881c_hd720_dsi_vdo_helitec\n",__func__);
		ratio_value = LTR559_DEFAULT_VALUE*LTR559_PRECISE_VALUE/(((((17743 * als_alsval_ch0)+(11059 * als_alsval_ch1)))/5190)*103/143);
	}
	else if(!strcmp("hx8394f_hd720_dsi_vdo_truly",lcd_name_for_als))
	{
		APS_DBG("%s,config lcd hx8394f_hd720_dsi_vdo_truly\n",__func__);
		ratio_value = LTR559_DEFAULT_VALUE*LTR559_PRECISE_VALUE/(((((17743 * als_alsval_ch0)+(11059 * als_alsval_ch1)))/5190)*46/100);
	}
	APS_DBG("ALS_PRECISE_VALUE = %d,ALS_RATION = %d\n",LTR559_PRECISE_VALUE,ratio_value);
}
/*----------------------------------------------------------------------------*/
int ltr559_get_addr(struct alsps_hw *hw, struct ltr559_i2c_addr *addr)
{
	/***
	if(!hw || !addr)
	{
		return -EFAULT;
	}
	addr->write_addr= hw->i2c_addr[0];
	***/
	return 0;
}


/*-----------------------------------------------------------------------------*/
void ltr559_eint_func(void)
{
	struct ltr559_priv *obj = ltr559_obj;
	//APS_FUN();

	if(!obj)
	{
		return;
	}
	int_top_time = sched_clock();
	schedule_work(&obj->eint_work);
	//schedule_delayed_work(&obj->eint_work);
}

#if defined(CONFIG_OF)
static irqreturn_t ltr559_eint_handler(int irq, void *desc)
{
	ltr559_eint_func();
	disable_irq_nosync(ltr559_obj->irq);

	return IRQ_HANDLED;
}
#endif


/*----------------------------------------------------------------------------*/
/*for interrup work mode support -- by liaoxl.lenovo 12.08.2011*/
int ltr559_setup_eint(struct i2c_client *client)
{


	int ret;
	struct pinctrl *pinctrl;
	//struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_cfg;

	u32 ints[2] = {0, 0};

	APS_FUN();

	alspsPltFmDev = get_alsps_platformdev();
	/* gpio setting */
	pinctrl = devm_pinctrl_get(&alspsPltFmDev->dev);
	if (IS_ERR(pinctrl)) {
		ret = PTR_ERR(pinctrl);
		APS_ERR("Cannot find alsps pinctrl!\n");
	}
	/*pins_default = pinctrl_lookup_state(pinctrl, "pin_default");
	if (IS_ERR(pins_default)) {
		ret = PTR_ERR(pins_default);
		APS_ERR("Cannot find alsps pinctrl default!\n");

	} */

	pins_cfg = pinctrl_lookup_state(pinctrl, "pin_cfg");
	if (IS_ERR(pins_cfg)) {
		ret = PTR_ERR(pins_cfg);
		APS_ERR("Cannot find alsps pinctrl pin_cfg!\n");

	}
/* eint request */
	if (ltr559_obj->irq_node) {
		ret=of_property_read_u32_array(ltr559_obj->irq_node, "debounce", ints, ARRAY_SIZE(ints));
		if(ret){
			APS_ERR("of_property_read_u32_array failed \n");
			}
		ret=gpio_request(ints[0], "p-sensor");
		if(ret){
			APS_ERR("gpio_request failed \n");
			}
		gpio_set_debounce(ints[0], ints[1]);
		pinctrl_select_state(pinctrl, pins_cfg);
		APS_LOG("ints[0] = %d, ints[1] = %d!!\n", ints[0], ints[1]);

		ltr559_obj->irq = irq_of_parse_and_map(ltr559_obj->irq_node, 0);
		APS_LOG("ltr559_obj->irq = %d\n", ltr559_obj->irq);
		if (!ltr559_obj->irq) {
			APS_ERR("irq_of_parse_and_map fail!!\n");
			return -EINVAL;
		}
//#if __WORDSIZE==64
//		APS_ERR("irq to gpio = %d \n",irq_to_gpio(ltr559_obj->irq));
//#endif
		if (request_irq(ltr559_obj->irq, ltr559_eint_handler, IRQF_TRIGGER_NONE , "ALS-eint", NULL)) { //IRQF_TRIGGER_FALLING
			APS_ERR("IRQ LINE NOT AVAILABLE!!\n");
			#ifdef CONFIG_HUAWEI_DSM
			als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_IRQ_ERR);
			#endif
			return -EINVAL;
		}else{
		        APS_ERR("ltr558  IRQ success!!!\n");
                }
		//enable_irq(ltr559_obj->irq);
	} else {
		APS_ERR("null irq node!!\n");
		return -EINVAL;
	}
	
    return 0;
}


/*----------------------------------------------------------------------------*/
static void ltr559_power(struct alsps_hw *hw, unsigned int on) 
{

}

/*----------------------------------------------------------------------------*/
/*for interrup work mode support -- by liaoxl.lenovo 12.08.2011*/
static int ltr559_check_and_clear_intr(struct i2c_client *client) 
{
// ***

	int res,intp,intl;
	u8 buffer[2];	
	u8 temp;
		//if (mt_get_gpio_in(GPIO_ALS_EINT_PIN) == 1) /*skip if no interrupt*/	
		//	  return 0;
	
		APS_FUN();
		buffer[0] = LTR559_ALS_PS_STATUS;
		res = i2c_master_send(client, buffer, 0x1);
		if(res <= 0)
		{
			goto EXIT_ERR;
		}
		res = i2c_master_recv(client, buffer, 0x1);
		if(res <= 0)
		{
			goto EXIT_ERR;
		}
		temp = buffer[0];
		res = 1;
		intp = 0;
		intl = 0;
		if(0 != (buffer[0] & 0x02))
		{
			res = 0;
			intp = 1;
		}
		if(0 != (buffer[0] & 0x08))
		{
			res = 0;
			intl = 1;		
		}
	
		if(0 == res)
		{
			if((1 == intp) && (0 == intl))
			{
				APS_LOG("PS interrupt\n");
				buffer[1] = buffer[0] & 0xfD;
				
			}
			else if((0 == intp) && (1 == intl))
			{
				APS_LOG("ALS interrupt\n");
				buffer[1] = buffer[0] & 0xf7;
			}
			else
			{
				APS_LOG("Check ALS/PS interrup error\n");
				buffer[1] = buffer[0] & 0xf5;
			}
			//buffer[0] = LTR559_ALS_PS_STATUS	;
			//res = i2c_master_send(client, buffer, 0x2);
			//if(res <= 0)
			//{
			//	goto EXIT_ERR;
			//}
			//else
			//{
			//	res = 0;
			//}
		}
	
		return res;
	
	EXIT_ERR:
		APS_ERR("ltr559_check_and_clear_intr fail\n");
		return 1;

}
/*----------------------------------------------------------------------------*/


static int ltr559_check_intr(struct i2c_client *client) 
{
	

	int res,intp,intl;
	u8 buffer[2];

	//if (mt_get_gpio_in(GPIO_ALS_EINT_PIN) == 1) /*skip if no interrupt*/  
	//    return 0;
    //APS_FUN();

	buffer[0] = LTR559_ALS_PS_STATUS;
	res = i2c_master_send(client, buffer, 0x1);
	if(res <= 0)
	{
		goto EXIT_ERR;
	}
	res = i2c_master_recv(client, buffer, 0x1);
	if(res <= 0)
	{
		goto EXIT_ERR;
	}
	APS_LOG("status = %x\n", buffer[0]);
	res = 1;
	intp = 0;
	intl = 0;
	if(0 != (buffer[0] & 0x02))
	{
	
		res = 0;
		intp = 1;
	}
	if(0 != (buffer[0] & 0x08))
	{
		res = 0;
		intl = 1;		
	}

		if(0 == res)
		{
			if((1 == intp) && (0 == intl))
			{
				APS_LOG("PS interrupt\n");
				buffer[1] = buffer[0] & 0xfD;
				
			}
			else if((0 == intp) && (1 == intl))
			{
				APS_LOG("ALS interrupt\n");
				buffer[1] = buffer[0] & 0xf7;
			}
			else
			{
				APS_LOG("Check ALS/PS interrup error\n");
				buffer[1] = buffer[0] & 0xf5;
			}
			//buffer[0] = LTR559_ALS_PS_STATUS	;
			//res = i2c_master_send(client, buffer, 0x2);
			//if(res <= 0)
			//{
			//	goto EXIT_ERR;
			//}
			//else
			//{
			//	res = 0;
			//}
		}

	return res;

EXIT_ERR:
	APS_ERR("ltr559_check_intr fail\n");
	return 1;
}

static int ltr559_clear_intr(struct i2c_client *client) 
{
	int res;
	u8 buffer[2];

	APS_FUN();
	
	buffer[0] = LTR559_ALS_PS_STATUS;
	res = i2c_master_send(client, buffer, 0x1);
	if(res <= 0)
	{
		goto EXIT_ERR;
	}
	res = i2c_master_recv(client, buffer, 0x1);
	if(res <= 0)
	{
		goto EXIT_ERR;
	}
	APS_DBG("buffer[0] = %d \n",buffer[0]);
	buffer[1] = buffer[0] & 0x01;
	buffer[0] = LTR559_ALS_PS_STATUS	;

	res = i2c_master_send(client, buffer, 0x2);
	if(res <= 0)
	{
		goto EXIT_ERR;
	}
	else
	{
		res = 0;
	}

	return res;

EXIT_ERR:
	APS_ERR("ltr559_check_intr fail\n");
	return 1;
}





static int ltr559_devinit(void)
{
	int res;
	int init_ps_gain;
	int init_als_gain;
    u8 databuf[2];
	int chipid=0;
	int err=0;
	struct i2c_client *client = ltr559_obj->client;

	struct ltr559_priv *obj = ltr559_obj;   
	//mdelay(PON_DELAY);
	init_ps_gain = MODE_PS_Gain16;
	client->timing = 400;
	//init_ps_gain = 0;
	chipid=ltr559_i2c_read_reg(LTR559_MANUFACTURER_ID);
	if(0x05==chipid){
		err = app_info_set("ALSPS-Sensor", "LTR559");
		if(err < 0){
			APS_LOG("ltr559 failed to add app_info\n");
			}
#ifdef CONFIG_HUAWEI_HW_I2C_DCT
    set_hw_dev_flag(DEV_I2C_ALSP_SENSOR);
#endif
		}
	APS_LOG("LTR559_PS setgain = %d!\n",init_ps_gain);
	res = ltr559_i2c_write_reg(LTR559_PS_CONTR, init_ps_gain); 
	if(res<0)
	{
	    APS_LOG("ltr559 set ps gain error\n");
	    return res;
	}
	
	mdelay(WAKEUP_DELAY);
    
	res = ltr559_i2c_write_reg(LTR559_PS_N_PULSES, 6);
	if(res<0)
	{
		APS_LOG("ltr559 set ps pulse error\n");
		return res;
	} 

	// Enable ALS to Full Range at startup
	als_gainrange = ALS_RANGE_64K;

	init_als_gain = als_gainrange;

	switch (init_als_gain)
	{
		case ALS_RANGE_64K:
			res = ltr559_i2c_write_reg(LTR559_ALS_CONTR, MODE_ALS_Range1);
			break;

		case ALS_RANGE_32K:
			res = ltr559_i2c_write_reg(LTR559_ALS_CONTR, MODE_ALS_Range2);
			break;

		case ALS_RANGE_16K:
			res = ltr559_i2c_write_reg(LTR559_ALS_CONTR, MODE_ALS_Range3);
			break;
			
		case ALS_RANGE_8K:
			res = ltr559_i2c_write_reg(LTR559_ALS_CONTR, MODE_ALS_Range4);
			break;
			
		case ALS_RANGE_1300:
			res = ltr559_i2c_write_reg(LTR559_ALS_CONTR, MODE_ALS_Range5);
			break;

		case ALS_RANGE_600:
			res = ltr559_i2c_write_reg(LTR559_ALS_CONTR, MODE_ALS_Range6);
			break;
			
		default:
			res = ltr559_i2c_write_reg(LTR559_ALS_CONTR, MODE_ALS_Range1);			
			APS_ERR("proxmy sensor gainrange %d!\n", init_als_gain);
			break;
	}

	res = ltr559_i2c_write_reg(LTR559_ALS_CONTR, MODE_ALS_Range4);
	if(res<0)
	{
	    APS_LOG("ltr559 set als gain error1\n");
	    return res;
	}

	res = ltr559_i2c_write_reg(LTR559_ALS_MEAS_RATE, 0x01); // 100ms meas rate for ALS
	if(res<0)
	{
		APS_LOG("ltr559 LTR559_ALS_MEAS_RATE  error\n");
		return res;
	}

	/*for interrup work mode support */
	if(0 == obj->hw->polling_mode_ps)
	{	
		APS_LOG("eint enable");
		ltr559_ps_set_thres();
		
		databuf[0] = LTR559_INTERRUPT;	
		databuf[1] = 0x01;
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			goto EXIT_ERR;
			return ltr559_ERR_I2C;
		}

		databuf[0] = LTR559_INTERRUPT_PERSIST;	
		databuf[1] = 0x00;
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			goto EXIT_ERR;
			return ltr559_ERR_I2C;
		}

	}

	if((res = ltr559_setup_eint(client))!=0)
	{
		APS_ERR("setup eint: %d\n", res);
		return res;
	}
	
	if((res = ltr559_check_and_clear_intr(client)))
	{
		APS_ERR("check/clear intr: %d\n", res);
		//    return res;
	}

	res = 0;

	EXIT_ERR:
	APS_ERR("init dev: %d\n", res);
	return res;

}
/*----------------------------------------------------------------------------*/

/*
static int ltr559_get_als_value(struct ltr559_priv *obj, u16 als)
{
	int idx;
	int invalid = 0;
	//APS_DBG("als  = %d\n",als); 
	for(idx = 0; idx < obj->als_level_num; idx++)
	{
		if(als < obj->hw->als_level[idx])
		{
			break;
		}
	}
	
	if(idx >= obj->als_value_num)
	{
		APS_ERR("exceed range\n"); 
		idx = obj->als_value_num - 1;
	}
	
	if(1 == atomic_read(&obj->als_deb_on))
	{
		unsigned long endt = atomic_read(&obj->als_deb_end);
		if(time_after(jiffies, endt))
		{
			atomic_set(&obj->als_deb_on, 0);
		}
		
		if(1 == atomic_read(&obj->als_deb_on))
		{
			invalid = 1;
		}
	}

	if(!invalid)
	{
		//APS_DBG("ALS: %05d => %05d\n", als, obj->hw->als_value[idx]);	
		return obj->hw->als_value[idx];
	}
	else
	{
		//APS_ERR("ALS: %05d => %05d (-1)\n", als, obj->hw->als_value[idx]);    
		return -1;
	}
}
*/
/*----------------------------------------------------------------------------*/
static int ltr559_get_ps_value(struct ltr559_priv *obj, u16 ps)
{
	int val, invalid = 0;

	static int val_temp = 1;
	if((ps > atomic_read(&obj->ps_thd_val_high)))
	{
		val = 0;  /*close*/
		val_temp = 0;
		intr_flag_value = 1;
	}
			//else if((ps < atomic_read(&obj->ps_thd_val_low))&&(temp_ps[0]  < atomic_read(&obj->ps_thd_val_low)))
	else if((ps < atomic_read(&obj->ps_thd_val_low)))
	{
		val = 1;  /*far away*/
		val_temp = 1;
		intr_flag_value = 0;
	}
	else
		val = val_temp;	
			
	
	if(atomic_read(&obj->ps_suspend))
	{
		invalid = 1;
	}
	else if(1 == atomic_read(&obj->ps_deb_on))
	{
		unsigned long endt = atomic_read(&obj->ps_deb_end);
		if(time_after(jiffies, endt))
		{
			atomic_set(&obj->ps_deb_on, 0);
		}
		
		if (1 == atomic_read(&obj->ps_deb_on))
		{
			invalid = 1;
		}
	}
	else if (obj->als > 50000)
	{
		//invalid = 1;
		APS_DBG("ligh too high will result to failt proximiy\n");
		return 1;  /*far away*/
	}

	if(!invalid)
	{
		APS_DBG("PS:  %05d => %05d\n", ps, val);
		return val;
	}	
	else
	{
		return -1;
	}	
}

/*----------------------------------------------------------------------------*/
/* if use  this typ of enable , Gsensor should report inputEvent(x, y, z ,stats, div) to HAL */
static int als_open_report_data(int open)
{
	/* should queuq work to report event if  is_report_input_direct=true */
	return 0;
}

/* if use  this typ of enable , Gsensor only enabled but not report inputEvent to HAL */

static int als_enable_nodata(int en)
{
	int res = 0;

	APS_LOG("ltr559_obj als enable value = %d\n", en);


	mutex_lock(&ltr559_mutex);
	if (en)
		set_bit(CMC_BIT_ALS, &ltr559_obj->enable);
	else
		clear_bit(CMC_BIT_ALS, &ltr559_obj->enable);
	mutex_unlock(&ltr559_mutex);
	if (!ltr559_obj) {
		APS_ERR("ltr559_obj is null!!\n");
		return -1;
	}
	res = ltr559_als_enable(ltr559_obj->client, en);
	if (res) {
		APS_ERR("als_enable_nodata is failed!!\n");
		return -1;
	}
	return 0;
}

static int als_set_delay(u64 ns)
{
	return 0;
}

static int als_get_data(int *value, int *status)
{
	int err = 0;

	struct ltr559_priv *obj = NULL;


	if (!ltr559_obj) {
		APS_ERR("ltr559_obj is null!!\n");
		return -1;
	}
	obj = ltr559_obj;
	err = ltr559_als_read(obj->client, &obj->als);
	if (err)
		err = -1;
	else {
// *value = ltr559_get_als_value(obj, obj->als);
        //APS_ERR("als_get_data als: %d\n", obj->als);
		*value = obj->als;
		if (*value < 0)
			err = -1;
		*status = SENSOR_STATUS_ACCURACY_MEDIUM;
	}

	return err;
}

/* if use  this typ of enable , Gsensor should report inputEvent(x, y, z ,stats, div) to HAL */
static int ps_open_report_data(int open)
{
	/* should queuq work to report event if  is_report_input_direct=true */
	return 0;
}

/* if use  this typ of enable , Gsensor only enabled but not report inputEvent to HAL */

static int ps_enable_nodata(int en)
{
	int res = 0;


	APS_LOG("ltr559_obj als enable value = %d\n", en);


	mutex_lock(&ltr559_mutex);
	if (en)
		set_bit(CMC_BIT_PS, &ltr559_obj->enable);

	else
		clear_bit(CMC_BIT_PS, &ltr559_obj->enable);

	mutex_unlock(&ltr559_mutex);
	if (!ltr559_obj) {
		APS_ERR("ltr559_obj is null!!\n");
		return -1;
	}
	res = ltr559_ps_enable(ltr559_obj->client, en);

	if (res) {
		APS_ERR("als_enable_nodata is failed!!\n");
		return -1;
	}
	return 0;

}

static int ps_set_delay(u64 ns)
{
	return 0;
}

static int ps_get_data(int *value, int *status)
{
	int err = 0;



	if (!ltr559_obj) {
		APS_ERR("ltr559_obj is null!!\n");
		return -1;
	}

	err = ltr559_ps_read(ltr559_obj->client, &ltr559_obj->ps);
	if (err)
		err = -1;
	else {
		*value = ltr559_get_ps_value(ltr559_obj, ltr559_obj->ps);
		if (*value < 0)
			err = -1;
		*status = SENSOR_STATUS_ACCURACY_MEDIUM;
	}

	return err;
}


/*----------------------------------------------------------------------------*/
/*for interrup work mode support */
static void ltr559_eint_work(struct work_struct *work)
{
    struct ltr559_priv *obj = (struct ltr559_priv *)container_of(work, struct ltr559_priv, eint_work);
	int err;
	//hwm_sensor_data sensor_data;
	//u8 buffer[1];
	//u8 reg_value[1];
	u8 databuf[2];
	int res = 0;
	int value = 1;
	//APS_FUN();
	err = ltr559_check_intr(obj->client);
	if(err < 0)
	{
		APS_ERR("ltr559_eint_work check intrs: %d\n", err);
	}
	else
	{
	    //get raw data
		err=ltr559_ps_read(obj->client, &obj->ps);
		if(err < 0)
		{
			goto fun_out;
		}
		APS_DBG("ltr559_eint_work rawdata ps=%d als_ch0=%d!\n",obj->ps,obj->als);
		value = ltr559_get_ps_value(obj, obj->ps);
		//sensor_data.values[0] = ltr559_get_ps_value(obj, obj->ps);
		//sensor_data.value_divide = 1;
		//sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;
		/*singal interrupt function add*/
		//intr_flag_value = value;
		APS_DBG("intr_flag_value=%d\n",intr_flag_value);
		if(intr_flag_value){
			APS_DBG(" interrupt value ps will < ps_threshold_low");
			databuf[0] = LTR559_PS_THRES_LOW_0;
			databuf[1] = (u8)((atomic_read(&obj->ps_thd_val_low)) & 0x00FF);
			res = i2c_master_send(obj->client, databuf, 0x2);
			if(res <= 0){
				goto fun_out;
			}
			databuf[0] = LTR559_PS_THRES_LOW_1;
			databuf[1] = (u8)(((atomic_read(&obj->ps_thd_val_low)) & 0xFF00) >> 8);
			res = i2c_master_send(obj->client, databuf, 0x2);
			if(res <= 0){
				goto fun_out;
			}
			databuf[0] = LTR559_PS_THRES_UP_0;
			databuf[1] = (u8)(0x00FF);
			res = i2c_master_send(obj->client, databuf, 0x2);
			if(res <= 0){
				goto fun_out;
			}
			databuf[0] = LTR559_PS_THRES_UP_1;
			databuf[1] = (u8)((0xFF00) >> 8);
			res = i2c_master_send(obj->client, databuf, 0x2);
			if(res <= 0){
				goto fun_out;
			}
		}
		else{
            #ifdef GN_MTK_BSP_PS_DYNAMIC_CALI
            if(last_min_value> 20 && obj->ps < (last_min_value - 50))
			{
		        if(obj->ps < 100)
				{
					atomic_set(&obj->ps_thd_val_high,  obj->ps+125);//70
					atomic_set(&obj->ps_thd_val_low, obj->ps+60); //50
		        }else if(obj->ps < 200){
					atomic_set(&obj->ps_thd_val_high,  obj->ps+135);
					atomic_set(&obj->ps_thd_val_low, obj->ps+70);
		        }else if(obj->ps < 400){
					atomic_set(&obj->ps_thd_val_high,  obj->ps+150);
					atomic_set(&obj->ps_thd_val_low, obj->ps+80);
		        }else if(obj->ps < 800){
					atomic_set(&obj->ps_thd_val_high,  obj->ps+180);
					atomic_set(&obj->ps_thd_val_low, obj->ps+100);
		        }else if(obj->ps < 1200){
					atomic_set(&obj->ps_thd_val_high,  obj->ps+200);
					atomic_set(&obj->ps_thd_val_low, obj->ps+150);
				}else if(obj->ps < 1650){
					atomic_set(&obj->ps_thd_val_high,  obj->ps+400);
					atomic_set(&obj->ps_thd_val_low, obj->ps+150);
				}else{
					atomic_set(&obj->ps_thd_val_high,  1750);
					atomic_set(&obj->ps_thd_val_low, 1700);
			}
			}
			#endif
			APS_DBG(" interrupt value ps will > ps_threshold_high");
			databuf[0] = LTR559_PS_THRES_LOW_0;
			databuf[1] = (u8)(0 & 0x00FF);
			res = i2c_master_send(obj->client, databuf, 0x2);
			if(res <= 0)
			{
				goto fun_out;
			}
			databuf[0] = LTR559_PS_THRES_LOW_1;
			databuf[1] = (u8)((0 & 0xFF00) >> 8);
			res = i2c_master_send(obj->client, databuf, 0x2);
			if(res <= 0)
			{
				goto fun_out;
			}
			databuf[0] = LTR559_PS_THRES_UP_0;
			databuf[1] = (u8)((atomic_read(&obj->ps_thd_val_high)) & 0x00FF);
			res = i2c_master_send(obj->client, databuf, 0x2);
			if(res <= 0)
			{
				goto fun_out;
			}
			databuf[0] = LTR559_PS_THRES_UP_1;
			databuf[1] = (u8)(((atomic_read(&obj->ps_thd_val_high)) & 0xFF00) >> 8);
			res = i2c_master_send(obj->client, databuf, 0x2);
			if(res <= 0)
			{
				goto fun_out;
			}
		}
		//let up layer to know
		//if((err = hwmsen_get_interrupt_data(ID_PROXIMITY, &sensor_data)))
		//{
		  //APS_ERR("call hwmsen_get_interrupt_data fail = %d\n", err);
		//}
		res=ps_report_interrupt_data(value);
		if (res != 0)
			APS_ERR("ltr559_eint_work err: %d\n", res);
	}
	fun_out:
		ltr559_clear_intr(obj->client);
		//mt65xx_eint_unmask(CUST_EINT_ALS_NUM);
		enable_irq(ltr559_obj->irq);
}



/****************************************************************************** 
 * Function Configuration
******************************************************************************/
static int ltr559_open(struct inode *inode, struct file *file)
{
	file->private_data = ltr559_i2c_client;

	if (!file->private_data)
	{
		APS_ERR("null pointer!!\n");
		return -EINVAL;
	}
	
	return nonseekable_open(inode, file);
}
/*----------------------------------------------------------------------------*/
static int ltr559_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}
/*----------------------------------------------------------------------------*/
static long ltr559_unlocked_ioctl(struct file *file, unsigned int cmd,
       unsigned long arg)       
{
	struct i2c_client *client = (struct i2c_client*)file->private_data;
	struct ltr559_priv *obj = i2c_get_clientdata(client);  
	int err = 0;
	void __user *ptr = (void __user*) arg;
	int dat;
	uint32_t enable;
	int ps_result;
	int ps_cali;
	int threshold[2];
	APS_DBG("cmd= %d\n", cmd); 
	switch (cmd)
	{
		case ALSPS_SET_PS_MODE:
			if(copy_from_user(&enable, ptr, sizeof(enable)))
			{
				err = -EFAULT;
				goto err_out;
			}
			//if(enable)
			//{
			    err = ltr559_ps_enable(obj->client, enable);
				if(err < 0)
				{
					APS_ERR("enable ps fail: %d en: %d\n", err, enable);
					#ifdef CONFIG_HUAWEI_DSM
					als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_ENABLE_ERR);
					#endif
					goto err_out;
				}
				set_bit(CMC_BIT_PS, &obj->enable);
			//}
			//else
			//{
			//    err = ltr559_ps_disable();
			//	if(err < 0)
			//	{
			//		APS_ERR("disable ps fail: %d\n", err); 
			//		goto err_out;
			//	}
			//	
			//	clear_bit(CMC_BIT_PS, &obj->enable);
			//}
			break;

		case ALSPS_GET_PS_MODE:
			enable = test_bit(CMC_BIT_PS, &obj->enable) ? (1) : (0);
			if(copy_to_user(ptr, &enable, sizeof(enable)))
			{
				err = -EFAULT;
				goto err_out;
			}
			break;

		case ALSPS_GET_PS_DATA:
			APS_DBG("ALSPS_GET_PS_DATA\n"); 
		    err = ltr559_ps_read(obj->client, &obj->ps);
			if(err < 0)
			{
				goto err_out;
			}
			
			dat = ltr559_get_ps_value(obj, obj->ps);
			if(copy_to_user(ptr, &dat, sizeof(dat)))
			{
				err = -EFAULT;
				goto err_out;
			}  
			break;

		case ALSPS_GET_PS_RAW_DATA:    
			err = ltr559_ps_read(obj->client, &obj->ps);
			if(err < 0)
			{
				goto err_out;
			}
			dat = obj->ps;
			if(copy_to_user(ptr, &dat, sizeof(dat)))
			{
				err = -EFAULT;
				goto err_out;
			}  
			break;

		case ALSPS_SET_ALS_MODE:
			if(copy_from_user(&enable, ptr, sizeof(enable)))
			{
				err = -EFAULT;
				goto err_out;
			}
			//if(enable)
			//{
			    err = ltr559_als_enable(obj->client, enable);
				if(err < 0)
				{
					APS_ERR("enable als fail: %d en: %d\n", err, enable);
					#ifdef CONFIG_HUAWEI_DSM
					als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_ENABLE_ERR);
					#endif
					goto err_out;
				}
				set_bit(CMC_BIT_ALS, &obj->enable);
			//}
			//else
			//{
			//    err = ltr559_als_disable();
			//	if(err < 0)
			//	{
			//		APS_ERR("disable als fail: %d\n", err); 
			//		goto err_out;
			//	}
			//	clear_bit(CMC_BIT_ALS, &obj->enable);
			//}
			break;

		case ALSPS_GET_ALS_MODE:
			enable = test_bit(CMC_BIT_ALS, &obj->enable) ? (1) : (0);
			if(copy_to_user(ptr, &enable, sizeof(enable)))
			{
				err = -EFAULT;
				goto err_out;
			}
			break;

		case ALSPS_GET_ALS_DATA: 
		    err = ltr559_als_read(obj->client, &obj->als);
			if(err < 0)
			{
				goto err_out;
			}

			//dat = ltr559_get_als_value(obj, obj->als);
			dat = obj->als;

			//APS_ERR("ltr559_unlocked_ioctl als: %d\n", obj->als);
			if(copy_to_user(ptr, &dat, sizeof(dat)))
			{
				err = -EFAULT;
				goto err_out;
			}              
			break;

		case ALSPS_GET_ALS_RAW_DATA:    
			err = ltr559_als_read(obj->client, &obj->als);
			if(err < 0)
			{
				goto err_out;
			}

			dat = obj->als;
			if(copy_to_user(ptr, &dat, sizeof(dat)))
			{
				err = -EFAULT;
				goto err_out;
			}              
			break;
			
/*----------------------------------for factory mode test add by hongguang---------------------------------------*/
		case ALSPS_GET_PS_TEST_RESULT:
			err = ltr559_ps_read(obj->client, &obj->ps);
			if (err)
				goto err_out;
		
			if (obj->ps > atomic_read(&obj->ps_thd_val_high))
				ps_result = 0;
			else
				ps_result = 1;
		
			if (copy_to_user(ptr, &ps_result, sizeof(ps_result))) {
				err = -EFAULT;
				goto err_out;
			}
			break;
		
		case ALSPS_IOCTL_CLR_CALI:
			if (copy_from_user(&dat, ptr, sizeof(dat))) {
				err = -EFAULT;
				goto err_out;
			}
			if (dat == 0)
				obj->ps_cali = 0;
			break;
		
		case ALSPS_IOCTL_GET_CALI:
			ps_cali = obj->ps_cali;
			if (copy_to_user(ptr, &ps_cali, sizeof(ps_cali))) {
				err = -EFAULT;
				goto err_out;
			}
			break;
		
		case ALSPS_IOCTL_SET_CALI:
			if (copy_from_user(&ps_cali, ptr, sizeof(ps_cali))) {
				err = -EFAULT;
				goto err_out;
			}
		
			obj->ps_cali = ps_cali;
			break;
		
		case ALSPS_SET_PS_THRESHOLD:
			if (copy_from_user(threshold, ptr, sizeof(threshold))) {
				err = -EFAULT;
				#ifdef CONFIG_HUAWEI_DSM
				als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_THRESHOLD_ERR);
				#endif
				goto err_out;
			}
			APS_ERR("%s set threshold high: 0x%x, low: 0x%x\n", __func__, threshold[0],	threshold[1]);
			atomic_set(&obj->ps_thd_val_high, (threshold[0] + obj->ps_cali));
			atomic_set(&obj->ps_thd_val_low, (threshold[1] + obj->ps_cali));	/* need to confirm */
		
			//set_psensor_threshold(obj->client);
		
			break;
		
		case ALSPS_GET_PS_THRESHOLD_HIGH:
			threshold[0] = atomic_read(&obj->ps_thd_val_high) - obj->ps_cali;
			APS_ERR("%s get threshold high: 0x%x\n", __func__, threshold[0]);
			if (copy_to_user(ptr, &threshold[0], sizeof(threshold[0]))) {
				err = -EFAULT;
				#ifdef CONFIG_HUAWEI_DSM
				als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_THRESHOLD_ERR);
				#endif
				goto err_out;
			}
			break;
		
		case ALSPS_GET_PS_THRESHOLD_LOW:
			threshold[0] = atomic_read(&obj->ps_thd_val_low) - obj->ps_cali;
			APS_ERR("%s get threshold low: 0x%x\n", __func__, threshold[0]);
			if (copy_to_user(ptr, &threshold[0], sizeof(threshold[0]))) {
				err = -EFAULT;
				#ifdef CONFIG_HUAWEI_DSM
				als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_THRESHOLD_ERR);
				#endif
				goto err_out;
			}
			break;
/*-------------------------------for factory mode test add by hongguang-----------------------------------------------------------*/

		default:
			APS_ERR("%s not supported = 0x%04x", __FUNCTION__, cmd);
			err = -ENOIOCTLCMD;
			break;
	}

	err_out:
	return err;    
}

/*----------------------------------------------------------------------------*/
static struct file_operations ltr559_fops = {
	//.owner = THIS_MODULE,
	.open = ltr559_open,
	.release = ltr559_release,
	.unlocked_ioctl = ltr559_unlocked_ioctl,
};
/*----------------------------------------------------------------------------*/
static struct miscdevice ltr559_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "als_ps",
	.fops = &ltr559_fops,
};

static int ltr559_i2c_suspend(struct i2c_client *client, pm_message_t msg) 
{
	struct ltr559_priv *obj = i2c_get_clientdata(client);    
	int err;
	APS_FUN();    

	if(msg.event == PM_EVENT_SUSPEND)
	{   
		if(!obj)
		{
			APS_ERR("null pointer!!\n");
			return -EINVAL;
		}
		
		atomic_set(&obj->als_suspend, 1);
		err = ltr559_als_enable(obj->client, 0);
		if(err < 0)
		{
			APS_ERR("disable als: %d\n", err);
		    #ifdef CONFIG_HUAWEI_DSM
		    als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_ENABLE_ERR);
		    #endif
			return err;
		}
		#if 0
		atomic_set(&obj->ps_suspend, 1);
		if(0==test_bit(CMC_BIT_PS,  &obj->enable))
		{
			err = ltr559_ps_enable(obj->client, 0);
			if(err < 0)
			{
				APS_ERR("disable ps:  %d\n", err);
				return err;
			}
		
			ltr559_power(obj->hw, 0);
		}
		#endif
	}
	return 0;
}
/*----------------------------------------------------------------------------*/
static int ltr559_i2c_resume(struct i2c_client *client)
{
	struct ltr559_priv *obj = i2c_get_clientdata(client);        
	int err;
	APS_FUN();

	if(!obj)
	{
		APS_ERR("null pointer!!\n");
		return -EINVAL;
	}

	ltr559_power(obj->hw, 1);
/*	err = ltr559_devinit();
	if(err < 0)
	{
		APS_ERR("initialize client fail!!\n");
		return err;        
	}*/
	atomic_set(&obj->als_suspend, 0);
	if(test_bit(CMC_BIT_ALS, &obj->enable))
	{
	    err = ltr559_als_enable(obj->client, 1);
	    if (err < 0)
		{
			APS_ERR("enable als fail: %d\n", err);        
		#ifdef CONFIG_HUAWEI_DSM
		als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_ENABLE_ERR);
		#endif
		}
	}
	# if 0
	atomic_set(&obj->ps_suspend, 0);
	if(0==test_bit(CMC_BIT_PS,  &obj->enable))
	{
		err = ltr559_ps_enable(obj->client, 1);
	    if (err < 0)
		{
			APS_ERR("enable ps fail: %d\n", err);                
		}
	}
	#endif
	return 0;
}
#if defined(CONFIG_HAS_EARLYSUSPEND)
static void ltr559_early_suspend(struct early_suspend *h) 
{   /*early_suspend is only applied for ALS*/
	struct ltr559_priv *obj = container_of(h, struct ltr559_priv, early_drv);   
	int err;
	APS_FUN();    

	if(!obj)
	{
		APS_ERR("null pointer!!\n");
		return;
	}
	
	atomic_set(&obj->als_suspend, 1); 
	err = ltr559_als_enable(obj->client, 0);
	if(err < 0)
	{
		APS_ERR("disable als fail: %d\n", err); 
		#ifdef CONFIG_HUAWEI_DSM
		als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_ENABLE_ERR);
		#endif
	}
}

static void ltr559_late_resume(struct early_suspend *h)
{   /*early_suspend is only applied for ALS*/
	struct ltr559_priv *obj = container_of(h, struct ltr559_priv, early_drv);         
	int err;
	APS_FUN();

	if(!obj)
	{
		APS_ERR("null pointer!!\n");
		return;
	}

	atomic_set(&obj->als_suspend, 0);
	if(test_bit(CMC_BIT_ALS, &obj->enable))
	{
	    err = ltr559_als_enable(obj->client, 1);
		if(err < 0)
		{
			APS_ERR("enable als fail: %d\n", err);        
			#ifdef CONFIG_HUAWEI_DSM
			als_ps_sensor_report_dsm_err(DSM_SHB_ERR_LIGHT_ENABLE_ERR);
			#endif
		}
	}
}
#endif
#if 0
int ltr559_ps_operate(void* self, uint32_t command, void* buff_in, int size_in,
		void* buff_out, int size_out, int* actualout)
{
	int err = 0;
	int value;
	hwm_sensor_data* sensor_data;
	struct ltr559_priv *obj = (struct ltr559_priv *)self;
	
	switch (command)
	{
		case SENSOR_DELAY:
			if((buff_in == NULL) || (size_in < sizeof(int)))
			{
				APS_ERR("Set delay parameter error!\n");
				err = -EINVAL;
			}
			// Do nothing
			break;

		case SENSOR_ENABLE:
			if((buff_in == NULL) || (size_in < sizeof(int)))
			{
				APS_ERR("Enable sensor parameter error!\n");
				err = -EINVAL;
			}
			else
			{				
				value = *(int *)buff_in;
				if(value)
				{
				    err = ltr559_ps_enable(obj->client, 1);
					if(err < 0)
					{
						APS_ERR("enable ps fail: %d\n", err); 
						return -1;
					}
					set_bit(CMC_BIT_PS, &obj->enable);
				}
				else
				{
				    err = ltr559_ps_disable(obj->client, 0);
					if(err < 0)
					{
						APS_ERR("disable ps fail: %d\n", err); 
						return -1;
					}
					clear_bit(CMC_BIT_PS, &obj->enable);
				}
			}
			break;

		case SENSOR_GET_DATA:
			if((buff_out == NULL) || (size_out< sizeof(hwm_sensor_data)))
			{
				APS_ERR("get sensor data parameter error!\n");
				err = -EINVAL;
			}
			else
			{
				APS_ERR("get sensor ps data !\n");
				sensor_data = (hwm_sensor_data *)buff_out;
				err = ltr559_ps_read(obj->client, &obj->ps);
    			if(err < 0)
    			{
    				err = -1;
    				break;
    			}
				sensor_data->values[0] = ltr559_get_ps_value(obj, obj->ps);
				sensor_data->value_divide = 1;
				sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;			
			}
			break;
		default:
			APS_ERR("proxmy sensor operate function no this parameter %d!\n", command);
			err = -1;
			break;
	}
	
	return err;
}

int ltr559_als_operate(void* self, uint32_t command, void* buff_in, int size_in,
		void* buff_out, int size_out, int* actualout)
{
	int err = 0;
	int value;
	hwm_sensor_data* sensor_data;
	struct ltr559_priv *obj = (struct ltr559_priv *)self;

	switch (command)
	{
		case SENSOR_DELAY:
			if((buff_in == NULL) || (size_in < sizeof(int)))
			{
				APS_ERR("Set delay parameter error!\n");
				err = -EINVAL;
			}
			// Do nothing
			break;

		case SENSOR_ENABLE:
			if((buff_in == NULL) || (size_in < sizeof(int)))
			{
				APS_ERR("Enable sensor parameter error!\n");
				err = -EINVAL;
			}
			else
			{
				value = *(int *)buff_in;				
				if(value)
				{
				    err = ltr559_als_enable(obj->client, 1);
					if(err < 0)
					{
						APS_ERR("enable als fail: %d\n", err); 
						return -1;
					}
					set_bit(CMC_BIT_ALS, &obj->enable);
				}
				else
				{
				    err = ltr559_als_disable(obj->client, 0);
					if(err < 0)
					{
						APS_ERR("disable als fail: %d\n", err); 
						return -1;
					}
					clear_bit(CMC_BIT_ALS, &obj->enable);
				}
				
			}
			break;

		case SENSOR_GET_DATA:
			if((buff_out == NULL) || (size_out< sizeof(hwm_sensor_data)))
			{
				APS_ERR("get sensor data parameter error!\n");
				err = -EINVAL;
			}
			else
			{
				APS_ERR("get sensor als data !\n");
				sensor_data = (hwm_sensor_data *)buff_out;
				obj->als = ltr559_als_read(obj->client, &obj->als);
                #if defined(MTK_AAL_SUPPORT)
				sensor_data->values[0] = obj->als;
				#else
				sensor_data->values[0] = ltr559_get_als_value(obj, obj->als);
				#endif
				sensor_data->value_divide = 1;
				sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
			}
			break;
		default:
			APS_ERR("light sensor operate function no this parameter %d!\n", command);
			err = -1;
			break;
	}
	
	return err;
}
#endif

/*----------------------------------------------------------------------------*/
static int ltr559_i2c_detect(struct i2c_client *client, struct i2c_board_info *info) 
{    
	strcpy(info->type, LTR559_DEV_NAME);
	return 0;
}

/*----------------------------------------------------------------------------*/
static int ltr559_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ltr559_priv *obj;
	//struct hwmsen_object obj_ps, obj_als;
	struct als_control_path als_ctl = {0};
	struct als_data_path als_data = {0};
	struct ps_control_path ps_ctl = {0};
	struct ps_data_path ps_data = {0};
	int err = 0;

	APS_LOG("ltr559_i2c_probe\n");

	if(!(obj = kzalloc(sizeof(*obj), GFP_KERNEL)))
	{
		err = -ENOMEM;
		goto exit;
	}
	memset(obj, 0, sizeof(*obj));
	ltr559_obj = obj;

	obj->hw = hw;
	//ltr559_get_addr(obj->hw, &obj->addr);

	INIT_WORK(&obj->eint_work, ltr559_eint_work);
	INIT_DELAYED_WORK(&pscali_work, ltr559_pscali_delay_work);
	obj->client = client;
	i2c_set_clientdata(client, obj);	
	atomic_set(&obj->als_debounce, 300);
	atomic_set(&obj->als_deb_on, 0);
	atomic_set(&obj->als_deb_end, 0);
	atomic_set(&obj->ps_debounce, 300);
	atomic_set(&obj->ps_deb_on, 0);
	atomic_set(&obj->ps_deb_end, 0);
	atomic_set(&obj->ps_mask, 0);
	atomic_set(&obj->als_suspend, 0);
	atomic_set(&obj->ps_thd_val_high,  obj->hw->ps_threshold_high);
	atomic_set(&obj->ps_thd_val_low,  obj->hw->ps_threshold_low);
	//atomic_set(&obj->als_cmd_val, 0xDF);
	//atomic_set(&obj->ps_cmd_val,  0xC1);
	atomic_set(&obj->ps_thd_val,  obj->hw->ps_threshold);

	
	obj->irq_node = of_find_compatible_node(NULL, NULL, "mediatek, als-eint");

	obj->enable = 0;
	obj->pending_intr = 0;
	obj->als_level_num = sizeof(obj->hw->als_level)/sizeof(obj->hw->als_level[0]);
	obj->als_value_num = sizeof(obj->hw->als_value)/sizeof(obj->hw->als_value[0]);   
	obj->als_modulus = (400*100)/(16*150);//(1/Gain)*(400/Tine), this value is fix after init ATIME and CONTROL register value
										//(400)/16*2.72 here is amplify *100
	BUG_ON(sizeof(obj->als_level) != sizeof(obj->hw->als_level));
	memcpy(obj->als_level, obj->hw->als_level, sizeof(obj->als_level));
	BUG_ON(sizeof(obj->als_value) != sizeof(obj->hw->als_value));
	memcpy(obj->als_value, obj->hw->als_value, sizeof(obj->als_value));
	atomic_set(&obj->i2c_retry, 3);
	set_bit(CMC_BIT_ALS, &obj->enable);
	set_bit(CMC_BIT_PS, &obj->enable);

	APS_LOG("ltr559_devinit() start...!\n");
	ltr559_i2c_client = client;
	err = ltr559_devinit();
	if(err)
	{
		goto exit_init_failed;
	}
	APS_LOG("ltr559_devinit() ...OK!\n");

	//printk("@@@@@@ manufacturer value:%x\n",ltr559_i2c_read_reg(0x87));

	err  = misc_register(&ltr559_device);
	if(err)
	{
		APS_ERR("ltr559_device register failed\n");
		goto exit_misc_device_register_failed;
	}

	
	/* Register sysfs attribute */
	err = ltr559_create_attr(&(ltr559_init_info.platform_diver_addr->driver));
	if(err)
	{
		printk(KERN_ERR "create attribute err = %d\n", err);
		goto exit_create_attr_failed;
	}

	#if 0
	obj_ps.self = ltr559_obj;
	/*for interrup work mode support -- by liaoxl.lenovo 12.08.2011*/
	if(1 == obj->hw->polling_mode_ps)
	{
		obj_ps.polling = 1;
	}
	else
	{
		obj_ps.polling = 0;
	}
	obj_ps.sensor_operate = ltr559_ps_operate;
	if(err = hwmsen_attach(ID_PROXIMITY, &obj_ps))
	{
		APS_ERR("attach fail = %d\n", err);
		goto exit_create_attr_failed;
	}
	
	obj_als.self = ltr559_obj;
	obj_als.polling = 1;
	obj_als.sensor_operate = ltr559_als_operate;
	if(err = hwmsen_attach(ID_LIGHT, &obj_als))
	{
		APS_ERR("attach fail = %d\n", err);
		goto exit_create_attr_failed;
	}
	#endif

	als_ctl.open_report_data = als_open_report_data;
	als_ctl.enable_nodata = als_enable_nodata;
	als_ctl.set_delay  = als_set_delay;
	als_ctl.is_report_input_direct = false;
	als_ctl.is_support_batch = false;


	err = als_register_control_path(&als_ctl);
	if (err) {
		APS_ERR("register fail = %d\n", err);
		goto exit_sensor_obj_attach_fail;
	}

	als_data.get_data = als_get_data;
	als_data.vender_div = 100;
	err = als_register_data_path(&als_data);
	if (err) {
		APS_ERR("tregister fail = %d\n", err);
		goto exit_sensor_obj_attach_fail;
	}

	ps_ctl.open_report_data = ps_open_report_data;
	ps_ctl.enable_nodata = ps_enable_nodata;
	ps_ctl.set_delay  = ps_set_delay;
	ps_ctl.is_report_input_direct = true;	//false0309
	ps_ctl.is_support_batch = false;
	err = ps_register_control_path(&ps_ctl);
	if (err) {
		APS_ERR("register fail = %d\n", err);
		goto exit_sensor_obj_attach_fail;
	}

	ps_data.get_data = ps_get_data;
	ps_data.vender_div = 100;
	err = ps_register_data_path(&ps_data);
	if (err) {
		APS_ERR("tregister fail = %d\n", err);
		goto exit_sensor_obj_attach_fail;
	}

	err = batch_register_support_info(ID_LIGHT, als_ctl.is_support_batch, 1, 0);
	if (err)
		APS_ERR("register light batch support err = %d\n", err);

	err = batch_register_support_info(ID_PROXIMITY, ps_ctl.is_support_batch, 1, 0);
	if (err)
		APS_ERR("register proximity batch support err = %d\n", err);

#if defined(CONFIG_HAS_EARLYSUSPEND)
	obj->early_drv.level	= EARLY_SUSPEND_LEVEL_DISABLE_FB - 1,
	obj->early_drv.suspend	= ltr559_early_suspend,
	obj->early_drv.resume	= ltr559_late_resume,	 
	register_early_suspend(&obj->early_drv);
#endif
	
	#ifdef CONFIG_HY_DRV_ASSIST
	alsps_assist_register_attr("ic",&ltr559_ic_show,NULL);
	alsps_assist_register_attr("vendor",&ltr559_vendor_show,NULL);
	alsps_assist_register_attr("exist",&ltr559_exist_show,NULL);
	alsps_assist_register_attr("ps_data",&ltr559_ps_data_show,NULL);
	#endif

	alsps_sensor = 1;
	ltr559_init_flag = 0;
	APS_LOG("%s: OK\n", __func__);
	return 0;
	
exit_create_attr_failed:
exit_sensor_obj_attach_fail:
exit_misc_device_register_failed:
		misc_deregister(&ltr559_device);
exit_init_failed:
		kfree(obj);
exit:
	ltr559_i2c_client = NULL;
	APS_ERR("%s: err = %d\n", __func__, err);
	ltr559_init_flag =  -1;
	return err;

#if 0
#if defined(CONFIG_HAS_EARLYSUSPEND)
	obj->early_drv.level    = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1,
	obj->early_drv.suspend  = ltr559_early_suspend,
	obj->early_drv.resume   = ltr559_late_resume,    
	register_early_suspend(&obj->early_drv);
#endif

	APS_LOG("%s: OK\n", __func__);
	return 0;

	exit_create_attr_failed:
	misc_deregister(&ltr559_device);
	exit_misc_device_register_failed:
	exit_init_failed:
	//i2c_detach_client(client);
	exit_kfree:
	kfree(obj);
	exit:
	ltr559_i2c_client = NULL;           
//	MT6516_EINTIRQMask(CUST_EINT_ALS_NUM);  /*mask interrupt if fail*/
	APS_ERR("%s: err = %d\n", __func__, err);
	return err;
#endif
}

/*----------------------------------------------------------------------------*/

static int ltr559_i2c_remove(struct i2c_client *client)
{
	int err;
	err = ltr559_delete_attr(&ltr559_i2c_driver.driver);
	if(err)
	{
		APS_ERR("ltr559_delete_attr fail: %d\n", err);
	} 

	err = misc_deregister(&ltr559_device);
	if(err)
	{
		APS_ERR("misc_deregister fail: %d\n", err);    
	}
	
	ltr559_i2c_client = NULL;
	i2c_unregister_device(client);
	kfree(i2c_get_clientdata(client));

	return 0;
}
/*----------------------------------------------------------------------------*/
#if 0
static int ltr559_probe(struct platform_device *pdev) 
{
	struct alsps_hw *hw = get_cust_alsps_hw();

	ltr559_power(hw, 1);
	//ltr559_force[0] = hw->i2c_num;
	//ltr559_force[1] = hw->i2c_addr[0];
	//APS_DBG("I2C = %d, addr =0x%x\n",ltr559_force[0],ltr559_force[1]);
	if(i2c_add_driver(&ltr559_i2c_driver))
	{
		APS_ERR("add driver error\n");
		return -1;
	} 
	return 0;
}
#endif
/*----------------------------------------------------------------------------*/
static int ltr559_remove(void)
{
	//struct alsps_hw *hw = get_cust_alsps_hw();
	APS_FUN();    
	ltr559_power(hw, 0);    
	i2c_del_driver(&ltr559_i2c_driver);
	return 0;
}
/*----------------------------------------------------------------------------*/
#if 0
static struct platform_driver ltr559_alsps_driver = {
	.probe      = ltr559_probe,
	.remove     = ltr559_remove,    
	.driver     = {
		.name  = "als_ps",
		//.owner = THIS_MODULE,
	}
};
#endif

//#ifdef CONFIG_OF
//static const struct of_device_id alsps_of_match[] = {
//	{ .compatible = "mediatek,als_ps", },
//	{},
//};
//#endif

//static struct platform_driver ltr559_alsps_driver =
//{
//	.probe      = ltr559_probe,
//	.remove     = ltr559_remove,    
//	.driver     = 
//	{
//		.name = "als_ps",
//       #ifdef CONFIG_OF
//		.of_match_table = alsps_of_match,
//		#endif
//	}
//};

static int  ltr559_local_init(void)
{
	/* printk("fwq loccal init+++\n"); */

	ltr559_power(hw, 1);
	if (i2c_add_driver(&ltr559_i2c_driver)) {
		APS_ERR("add driver error\n");
		return -1;
	}
	if (-1 == ltr559_init_flag)
		return -1;

	return 0;
}

/*----------------------------------------------------------------------------*/
static int __init ltr559_init(void)
{
	const char *name = "mediatek,ltr559";

	APS_FUN();

	//i2c_register_board_info(0, &i2c_ltr559, 1);
	//if(platform_driver_register(&ltr559_alsps_driver))
	//{
	//	APS_ERR("failed to register driver");
	//	return -ENODEV;
	//}
	
	hw =   get_alsps_dts_func(name, hw);
	if (!hw)
		APS_ERR("get dts info fail\n");
	alsps_driver_add(&ltr559_init_info);
	return 0;
}
/*----------------------------------------------------------------------------*/
static void __exit ltr559_exit(void)
{
	APS_FUN();
	//platform_driver_unregister(&ltr559_alsps_driver);
}
/*----------------------------------------------------------------------------*/
module_init(ltr559_init);
module_exit(ltr559_exit);
/*----------------------------------------------------------------------------*/
MODULE_AUTHOR("XX Xx");
MODULE_DESCRIPTION("LTR-559ALS Driver");
MODULE_LICENSE("GPL");

