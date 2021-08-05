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

#include <linux/types.h>
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif
#include <mach/mt_charging.h>
#include <mt-plat/charging.h>
#include <mt-plat/battery_common.h>
#include "sy6923d.h"
#include <linux/timer.h>
static void monitor_reg_status(unsigned long data);
/**********************************************************
  *
  *   [I2C Slave Setting]
  *
  *********************************************************/

#ifdef CONFIG_OF
#else

#define sy6923d_SLAVE_ADDR_WRITE   0xD4
#define sy6923d_SLAVE_ADDR_Read    0xD5

#ifdef I2C_SWITHING_CHARGER_CHANNEL
#define sy6923d_BUSNUM I2C_SWITHING_CHARGER_CHANNEL
#else
#define sy6923d_BUSNUM 0
#endif

#endif
static struct timer_list monitor_timer;
static struct delayed_work dump_reg_delaywork;
#ifdef CONFIG_HUAWEI_HW_I2C_DCT
#include <linux/hw_dev_dec.h>
#endif
#ifdef CONFIG_HUAWEI_DSM
kal_bool g_read_write = KAL_TRUE;
#endif
static struct i2c_client *new_client;
static const struct i2c_device_id sy6923d_i2c_id[] = { {"sy6923d", 0}, {} };

kal_bool chargin_hw_init_done = false;
static int sy6923d_driver_probe(struct i2c_client *client, const struct i2c_device_id *id);
char host_status;


/**********************************************************
  *
  *   [Global Variable]
  *
  *********************************************************/
unsigned char sy6923d_reg[sy6923d_REG_NUM] = { 0 };

static DEFINE_MUTEX(sy6923d_i2c_access);
static DEFINE_MUTEX(sy6923d_access_mutex);

int g_sy6923d_hw_exist = 0;

/**********************************************************
  *
  *   [I2C Function For Read/Write sy6923d]
  *
  *********************************************************/
#ifdef CONFIG_MTK_I2C_EXTENSION
unsigned int sy6923d_read_byte(unsigned char cmd, unsigned char *returnData)
{
	char cmd_buf[1] = { 0x00 };
	char readData = 0;
	int ret = 0;

	mutex_lock(&sy6923d_i2c_access);

	/* new_client->addr = ((new_client->addr) & I2C_MASK_FLAG) | I2C_WR_FLAG; */
	new_client->ext_flag =
	    ((new_client->ext_flag) & I2C_MASK_FLAG) | I2C_WR_FLAG | I2C_DIRECTION_FLAG;

	cmd_buf[0] = cmd;
	ret = i2c_master_send(new_client, &cmd_buf[0], (1 << 8 | 1));
	if (ret < 0) {
		/* new_client->addr = new_client->addr & I2C_MASK_FLAG; */
		new_client->ext_flag = 0;
		mutex_unlock(&sy6923d_i2c_access);

		return 0;
	}

	readData = cmd_buf[0];
	*returnData = readData;

	/* new_client->addr = new_client->addr & I2C_MASK_FLAG; */
	new_client->ext_flag = 0;
	mutex_unlock(&sy6923d_i2c_access);

	return 1;
}

unsigned int sy6923d_write_byte(unsigned char cmd, unsigned char writeData)
{
	char write_data[2] = { 0 };
	int ret = 0;

	mutex_lock(&sy6923d_i2c_access);

	write_data[0] = cmd;
	write_data[1] = writeData;

	new_client->ext_flag = ((new_client->ext_flag) & I2C_MASK_FLAG) | I2C_DIRECTION_FLAG;

	ret = i2c_master_send(new_client, write_data, 2);
	if (ret < 0) {
		new_client->ext_flag = 0;
		mutex_unlock(&sy6923d_i2c_access);
		return 0;
	}

	new_client->ext_flag = 0;
	mutex_unlock(&sy6923d_i2c_access);
	return 1;
}
#else
unsigned int sy6923d_read_byte(unsigned char cmd, unsigned char *returnData)
{
	unsigned char xfers = 2;
	int ret, retries = 1;

	mutex_lock(&sy6923d_i2c_access);

	do {
		struct i2c_msg msgs[2] = {
			{
				.addr = new_client->addr,
				.flags = 0,
				.len = 1,
				.buf = &cmd,
			},
			{

				.addr = new_client->addr,
				.flags = I2C_M_RD,
				.len = 1,
				.buf = returnData,
			}
		};

		/*
		 * Avoid sending the segment addr to not upset non-compliant
		 * DDC monitors.
		 */
		ret = i2c_transfer(new_client->adapter, msgs, xfers);

		if (ret == -ENXIO) {
			battery_log(BAT_LOG_CRTI, "skipping non-existent adapter %s\n", new_client->adapter->name);
			break;
		}
	} while (ret != xfers && --retries);

	mutex_unlock(&sy6923d_i2c_access);

#ifdef CONFIG_HUAWEI_DSM
	if(ret == xfers)
		g_read_write = KAL_TRUE;
	else
	g_read_write = KAL_FALSE;
#endif
	return ret == xfers ? 1 : -1;
}

unsigned int sy6923d_write_byte(unsigned char cmd, unsigned char writeData)
{
	unsigned char xfers = 1;
	int ret, retries = 1;
	unsigned char buf[8];

	mutex_lock(&sy6923d_i2c_access);

	buf[0] = cmd;
	memcpy(&buf[1], &writeData, 1);

	do {
		struct i2c_msg msgs[1] = {
			{
				.addr = new_client->addr,
				.flags = 0,
				.len = 1 + 1,
				.buf = buf,
			},
		};

		/*
		 * Avoid sending the segment addr to not upset non-compliant
		 * DDC monitors.
		 */
		ret = i2c_transfer(new_client->adapter, msgs, xfers);

		if (ret == -ENXIO) {
			battery_log(BAT_LOG_CRTI, "skipping non-existent adapter %s\n", new_client->adapter->name);
			break;
		}
	} while (ret != xfers && --retries);

	mutex_unlock(&sy6923d_i2c_access);

#ifdef CONFIG_HUAWEI_DSM
	if(ret == xfers)
		g_read_write = KAL_TRUE;
	else
	g_read_write = KAL_FALSE;
#endif
	return ret == xfers ? 1 : -1;
}
#endif

/**********************************************************
  *
  *   [Read / Write Function] 
  *
  *********************************************************/
unsigned int sy6923d_read_interface (unsigned char RegNum, unsigned char *val, unsigned char MASK, unsigned char SHIFT)
{
    unsigned char sy6923d_reg = 0;
    int ret = 0;

   battery_log(BAT_LOG_FULL,"--------------------------------------------------\n");

    ret = sy6923d_read_byte(RegNum, &sy6923d_reg);

	battery_log(BAT_LOG_FULL,"[sy6923d_read_interface] Reg[%x]=0x%x\n", RegNum, sy6923d_reg);
	
    sy6923d_reg &= (MASK << SHIFT);
    *val = (sy6923d_reg >> SHIFT);
	
	battery_log(BAT_LOG_FULL,"[sy6923d_read_interface] val=0x%x\n", *val);
	
    return ret;
}

unsigned int sy6923d_config_interface (unsigned char RegNum, unsigned char val, unsigned char MASK, unsigned char SHIFT)
{
    unsigned char sy6923d_reg = 0;
    int ret = 0;

    battery_log(BAT_LOG_FULL,"--------------------------------------------------\n");

    ret = sy6923d_read_byte(RegNum, &sy6923d_reg);
	if(ret == 0)
		battery_log(BAT_LOG_FULL,"[sy6923d_config_interface] ret=0\n");
    battery_log(BAT_LOG_FULL,"[sy6923d_config_interface] Reg[%x]=0x%x\n", RegNum, sy6923d_reg);
    
    sy6923d_reg &= ~(MASK << SHIFT);
    sy6923d_reg |= (val << SHIFT);

	if(RegNum == sy6923d_CON4 && val == 1 && MASK ==CON4_RESET_MASK && SHIFT == CON4_RESET_SHIFT)
	{
		// RESET bit
		printk ("[caozhg] set reset1.\n");
	}
	else if(RegNum == sy6923d_CON4)
	{
		sy6923d_reg &= ~0x80;	//RESET bit read returs 1, so clear it
		printk ("[caozhg] set reset2.\n");
	}
	 if((RegNum == 1)&&(otg_online == 1))
       {
               if((sy6923d_reg & 0x01) != 1)
			sy6923d_reg = sy6923d_reg |0x01;
	}

    ret = sy6923d_write_byte(RegNum, sy6923d_reg);
    battery_log(BAT_LOG_FULL,"[sy6923d_config_interface] write Reg[%x]=0x%x\n", RegNum, sy6923d_reg);

    // Check
    //sy6923d_read_byte(RegNum, &sy6923d_reg);
    //printk("[sy6923d_config_interface] Check Reg[%x]=0x%x\n", RegNum, sy6923d_reg);

    return ret;
}

//write one register directly
unsigned int sy6923d_config_interface_liao (unsigned char RegNum, unsigned char val)
{   
    int ret = 0;
    
    ret = sy6923d_write_byte(RegNum, val);

    return ret;
}
/**********************************************************
  *
  *   [Internal Function] 
  *
  *********************************************************/
//CON0----------------------------------------------------

void sy6923d_set_tmr_rst(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON4), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON4_TMR_RST_MASK),
                                    (unsigned char)(CON4_TMR_RST_SHIFT)
                                    );
}

unsigned int sy6923d_get_otg_status(void)
{
    unsigned int ret=0;
    unsigned char val=0;

    ret=sy6923d_read_interface(     (unsigned char)(sy6923d_CON0), 
                                    (&val),
                                    (unsigned char)(CON0_OTG_MASK),
                                    (unsigned char)(CON0_OTG_SHIFT)
                                    );
    return val;
}

void sy6923d_set_en_stat(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON0), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON0_EN_STAT_MASK),
                                    (unsigned char)(CON0_EN_STAT_SHIFT)
                                    );
}

unsigned int sy6923d_get_chip_status(void)
{
    unsigned int ret=0;
    unsigned char val=0;

    ret=sy6923d_read_interface(     (unsigned char)(sy6923d_CON0), 
                                    (&val),
                                    (unsigned char)(CON0_STAT_MASK),
                                    (unsigned char)(CON0_STAT_SHIFT)
                                    );
    return val;
}

unsigned int sy6923d_get_boost_status(void)
{
    unsigned int ret=0;
    unsigned char val=0;

    ret=sy6923d_read_interface(     (unsigned char)(sy6923d_CON0), 
                                    (&val),
                                    (unsigned char)(CON0_BOOST_MASK),
                                    (unsigned char)(CON0_BOOST_SHIFT)
                                    );
    return val;
}

unsigned int sy6923d_get_fault_status(void)
{
    unsigned int ret=0;
    unsigned char val=0;

    ret=sy6923d_read_interface(     (unsigned char)(sy6923d_CON0), 
                                    (&val),
                                    (unsigned char)(CON0_FAULT_MASK),
                                    (unsigned char)(CON0_FAULT_SHIFT)
                                    );
    return val;
}

//CON1----------------------------------------------------

void sy6923d_set_input_charging_current(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_LIN_LIMIT_MASK),
                                    (unsigned char)(CON1_LIN_LIMIT_SHIFT)
                                    );
}

void sy6923d_set_v_low(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_LOW_V_MASK),
                                    (unsigned char)(CON1_LOW_V_SHIFT)
                                    );
}

void sy6923d_set_te(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_TE_MASK),
                                    (unsigned char)(CON1_TE_SHIFT)
                                    );
}

void sy6923d_set_ce(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_CE_MASK),
                                    (unsigned char)(CON1_CE_SHIFT)
                                    );
}

void sy6923d_set_hz_mode(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_HZ_MODE_MASK),
                                    (unsigned char)(CON1_HZ_MODE_SHIFT)
                                    );
}

void sy6923d_set_opa_mode(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_OPA_MODE_MASK),
                                    (unsigned char)(CON1_OPA_MODE_SHIFT)
                                    );
}

//CON2----------------------------------------------------

void sy6923d_set_oreg(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON2), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON2_OREG_MASK),
                                    (unsigned char)(CON2_OREG_SHIFT)
                                    );
}

void sy6923d_set_otg_pl(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON2), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON2_OTG_PL_MASK),
                                    (unsigned char)(CON2_OTG_PL_SHIFT)
                                    );
}

void sy6923d_set_otg_en(unsigned int val)
{
    unsigned int ret=0;
    if(val == 1)
 {
     setup_timer(&monitor_timer,monitor_reg_status,0);
     add_timer(&monitor_timer);
  }else
      del_timer_sync(&monitor_timer);
    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON2), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON2_OTG_EN_MASK),
                                    (unsigned char)(CON2_OTG_EN_SHIFT)
                                    );
}

//CON3----------------------------------------------------

unsigned int sy6923d_get_vender_code(void)
{
    unsigned int ret=0;
    unsigned char val=0;

    ret=sy6923d_read_interface(     (unsigned char)(sy6923d_CON3), 
                                    (&val),
                                    (unsigned char)(CON3_VENDER_CODE_MASK),
                                    (unsigned char)(CON3_VENDER_CODE_SHIFT)
                                    );
    return val;
}

unsigned int sy6923d_get_pn(void)
{
    unsigned int ret=0;
    unsigned char val=0;

    ret=sy6923d_read_interface(     (unsigned char)(sy6923d_CON3), 
                                    (&val),
                                    (unsigned char)(CON3_PIN_MASK),
                                    (unsigned char)(CON3_PIN_SHIFT)
                                    );
    return val;
}

unsigned int sy6923d_get_revision(void)
{
    unsigned int ret=0;
    unsigned char val=0;

    ret=sy6923d_read_interface(     (unsigned char)(sy6923d_CON3), 
                                    (&val),
                                    (unsigned char)(CON3_REVISION_MASK),
                                    (unsigned char)(CON3_REVISION_SHIFT)
                                    );
    return val;
}

//CON4----------------------------------------------------

void sy6923d_set_reset(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON4), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON4_RESET_MASK),
                                    (unsigned char)(CON4_RESET_SHIFT)
                                    );
}

void sy6923d_set_iocharge(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON4), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON4_I_CHR_MASK),
                                    (unsigned char)(CON4_I_CHR_SHIFT)
                                    );
}

unsigned int sy6923d_get_iocharge(void)
{
    unsigned int ret=0; 
    unsigned char val=0;   

    ret=sy6923d_read_interface(   (unsigned char)(sy6923d_CON4), 
                                    (&val),
                                    (unsigned char)(CON4_I_CHR_MASK),
                                    (unsigned char)(CON4_I_CHR_SHIFT)
                                    );
    return val;
}

void sy6923d_set_iterm(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON4), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON4_I_TERM_MASK),
                                    (unsigned char)(CON4_I_TERM_SHIFT)
                                    );
}

//CON5----------------------------------------------------
/*
void sy6923d_set_dis_vreg(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON5), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON5_DIS_VREG_MASK),
                                    (unsigned char)(CON5_DIS_VREG_SHIFT)
                                    );
}
*/
void sy6923d_set_io_level(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON5), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON5_IO_LEVEL_MASK),
                                    (unsigned char)(CON5_IO_LEVEL_SHIFT)
                                    );
}

unsigned int sy6923d_get_sp_status(void)
{
    unsigned int ret=0;
    unsigned char val=0;

    ret=sy6923d_read_interface(     (unsigned char)(sy6923d_CON5), 
                                    (&val),
                                    (unsigned char)(CON5_SP_STATUS_MASK),
                                    (unsigned char)(CON5_SP_STATUS_SHIFT)
                                    );
    return val;
}

unsigned int sy6923d_get_en_level(void)
{
    unsigned int ret=0;
    unsigned char val=0;

    ret=sy6923d_read_interface(     (unsigned char)(sy6923d_CON5), 
                                    (&val),
                                    (unsigned char)(CON5_EN_LEVEL_MASK),
                                    (unsigned char)(CON5_EN_LEVEL_SHIFT)
                                    );
    return val;
}

void sy6923d_set_vsp(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON5), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON5_VSP_MASK),
                                    (unsigned char)(CON5_VSP_SHIFT)
                                    );
}
//CON6----------------------------------------------------

void sy6923d_set_i_safe(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON6), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON6_ISAFE_MASK),
                                    (unsigned char)(CON6_ISAFE_SHIFT)
                                    );
}

void sy6923d_set_v_safe(unsigned int val)
{
    unsigned int ret=0;    

    ret=sy6923d_config_interface(   (unsigned char)(sy6923d_CON6), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON6_VSAFE_MASK),
                                    (unsigned char)(CON6_VSAFE_SHIFT)
                                    );
}





/**********************************************************
  *
  *   [Internal Function]
  *
  *********************************************************/
void sy6923d_hw_component_detect(void) //caozhg-need
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = sy6923d_read_interface(0x03, &val, 0xFF, 0x0);

	if (val == 0)
		g_sy6923d_hw_exist = 0;
	else {
#ifdef CONFIG_HUAWEI_HW_I2C_DCT
		set_hw_dev_flag(DEV_I2C_CHARGER);
#endif
		g_sy6923d_hw_exist = 1;
	}

	pr_debug("[sy6923d_hw_component_detect] exist=%d, Reg[0x03]=0x%x\n",
		 g_sy6923d_hw_exist, val);
}

int is_sy6923d_exist(void)
{
	pr_debug("[is_sy6923d_exist] g_sy6923d_hw_exist=%d\n", g_sy6923d_hw_exist);

	return g_sy6923d_hw_exist;
}
static void monitor_reg_status(unsigned long data)
{
mod_timer(&monitor_timer,jiffies+HZ);
schedule_delayed_work(&dump_reg_delaywork, msecs_to_jiffies(50));
}
void sy6923d_dump_register(void)
{
    int i=0;
    printk("[sy6923d] ");
	for (i=0;i<sy6923d_REG_NUM;i++)
    {
        sy6923d_read_byte(i, &sy6923d_reg[i]);
        printk("[0x%X]=0x%X ", i, sy6923d_reg[i]);        
    }
    printk("\n");
}

void sy6923d_hw_init(void)
{
	/*battery_log(BAT_LOG_CRTI, "[sy6923d_hw_init] After HW init\n");*/
	sy6923d_dump_register();
}
static ssize_t show_host_enable(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", host_status);
}

static ssize_t store_host_enable(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t size) {
	int ret;
	if(strlen(buf) <= sizeof(char)){
		ret = sscanf(buf, "%s", &host_status);
	}
	else{
		printk("buf size is invalid \n");
		return 0;
	}
	return size;
}
static DEVICE_ATTR(host_enable, 0660, show_host_enable, store_host_enable);


static int sy6923d_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	ret = gpio_request(LCT_CHR_EN_PIN,NULL);
	if(ret){
		battery_log (BAT_LOG_CRTI, "LCT_CHR_EN_PIN request failed");
	}
	ret = gpio_direction_output(LCT_CHR_EN_PIN,GPIO_DIR_OUT);
	if(ret){
		battery_log (BAT_LOG_CRTI, "LCT_CHR_EN_PIN set direction failed");
	}

	battery_log(BAT_LOG_CRTI, "[sy6923d_driver_probe]\n");

	new_client = client;
	new_client->timing=400;
	/* --------------------- */
	sy6923d_hw_component_detect();
	sy6923d_dump_register();
	/* sy6923d_hw_init(); //move to charging_hw_xxx.c */
	chargin_hw_init_done = true;

	/* Hook chr_control_interface with battery's interface */
	battery_charging_control = chr_control_interface;
	return 0;
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/
unsigned char g_reg_value_sy6923d = 0;
static ssize_t show_sy6923d_access(struct device *dev, struct device_attribute *attr, char *buf)
{
	battery_log(BAT_LOG_CRTI, "[show_sy6923d_access] 0x%x\n", g_reg_value_sy6923d);
	return sprintf(buf, "%u\n", g_reg_value_sy6923d);
}

static ssize_t store_sy6923d_access(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret = 0;
	char *pvalue = NULL, *addr, *val;
	unsigned int reg_value = 0;
	unsigned int reg_address = 0;

	battery_log(BAT_LOG_CRTI, "[store_sy6923d_access]\n");

	if (buf != NULL && size != 0) {
		battery_log(BAT_LOG_CRTI, "[store_sy6923d_access] buf is %s and size is %zu\n", buf,
			    size);

		pvalue = (char *)buf;
		if (size > 3) {
			addr = strsep(&pvalue, " ");
			if(addr == NULL)
				return size;
			ret = kstrtou32(addr, 16, (unsigned int *)&reg_address);
		} else
			ret = kstrtou32(pvalue, 16, (unsigned int *)&reg_address);

		if (size > 3) {
			val = strsep(&pvalue, " ");
			if(val == NULL)
				return size;
			ret = kstrtou32(val, 16, (unsigned int *)&reg_value);
			battery_log(BAT_LOG_CRTI,
				    "[store_sy6923d_access] write sy6923d reg 0x%x with value 0x%x !\n",
				    (unsigned int) reg_address, reg_value);
			ret = sy6923d_config_interface(reg_address, reg_value, 0xFF, 0x0);
		} else {
			ret = sy6923d_read_interface(reg_address, &g_reg_value_sy6923d, 0xFF, 0x0);
			battery_log(BAT_LOG_CRTI,
				    "[store_sy6923d_access] read sy6923d reg 0x%x with value 0x%x !\n",
				    (unsigned int) reg_address, g_reg_value_sy6923d);
			battery_log(BAT_LOG_CRTI,
				    "[store_sy6923d_access] Please use \"cat sy6923d_access\" to get value\r\n");
		}
	}
	return size;
}

static DEVICE_ATTR(sy6923d_access, 0664, show_sy6923d_access, store_sy6923d_access);	/* 664 */

static int sy6923d_user_space_probe(struct platform_device *dev)
{
	int ret_device_file = 0;

	battery_log(BAT_LOG_CRTI, "******** sy6923d_user_space_probe!! ********\n");

	ret_device_file = device_create_file(&(dev->dev), &dev_attr_sy6923d_access);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_host_enable);

	return 0;
}

struct platform_device sy6923d_user_space_device = {
	.name = "sy6923d-user",
	.id = -1,
};

static struct platform_driver sy6923d_user_space_driver = {
	.probe = sy6923d_user_space_probe,
	.driver = {
		   .name = "sy6923d-user",
		   },
};

#ifdef CONFIG_OF
static const struct of_device_id sy6923d_of_match[] = {
	{.compatible = "mediatek,SWITHING_CHARGER"},
	{},
};
#else
static struct i2c_board_info i2c_sy6923d __initdata = {
	I2C_BOARD_INFO("sy6923d", (sy6923d_SLAVE_ADDR_WRITE >> 1))
};
#endif

static struct i2c_driver sy6923d_driver = {
	.driver = {
		   .name = "sy6923d",
#ifdef CONFIG_OF
		   .of_match_table = sy6923d_of_match,
#endif
		   },
	.probe = sy6923d_driver_probe,
	.id_table = sy6923d_i2c_id,
};
void monitor_work_func(struct work_struct *work)
{
    static int status = 0;//0 normal 1 error
    int ret = 0;
    unsigned char reg = 0;
    sy6923d_dump_register();
    ret = sy6923d_reg[0] & 0x07;//test reg[0] bit 0-2,000 mean's VBUS normal
    if(ret== 0){
        printk("sy6923d vbus normal\n");
        if(status == 1)
        {
        printk("sy6923d reset reg 1\n");
        sy6923d_read_byte(1, &reg);
        reg = reg |0x01;//keep on reg[1] bit 0 is 1
        sy6923d_write_byte(1, reg);
        status=0;
        }
        }
    else{
        printk("sy6923d vbus error\n");
        status=1;
        }
}
static int __init sy6923d_init(void)
{
	int ret = 0;

	/* i2c registeration using DTS instead of boardinfo*/
#ifdef CONFIG_OF
	battery_log(BAT_LOG_CRTI, "[sy6923d_init] init start with i2c DTS");
#else
	battery_log(BAT_LOG_CRTI, "[sy6923d_init] init start. ch=%d\n", sy6923d_BUSNUM);
	i2c_register_board_info(sy6923d_BUSNUM, &i2c_sy6923d, 1);
#endif
	INIT_DELAYED_WORK(&dump_reg_delaywork, monitor_work_func);
	if (i2c_add_driver(&sy6923d_driver) != 0) {
		battery_log(BAT_LOG_CRTI,
			    "[sy6923d_init] failed to register sy6923d i2c driver.\n");
	} else {
		battery_log(BAT_LOG_CRTI,
			    "[sy6923d_init] Success to register sy6923d i2c driver.\n");
	}

	/* sy6923d user space access interface */
	ret = platform_device_register(&sy6923d_user_space_device);
	if (ret) {
		battery_log(BAT_LOG_CRTI, "****[sy6923d_init] Unable to device register(%d)\n",
			    ret);
		return ret;
	}
	ret = platform_driver_register(&sy6923d_user_space_driver);
	if (ret) {
		battery_log(BAT_LOG_CRTI, "****[sy6923d_init] Unable to register driver (%d)\n",
			    ret);
		return ret;
	}

	return 0;
}

static void __exit sy6923d_exit(void)
{
	i2c_del_driver(&sy6923d_driver);
    cancel_delayed_work(&dump_reg_delaywork);
}
module_init(sy6923d_init);
module_exit(sy6923d_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C sy6923d Driver");
MODULE_AUTHOR("will cai <will.cai@mediatek.com>");
