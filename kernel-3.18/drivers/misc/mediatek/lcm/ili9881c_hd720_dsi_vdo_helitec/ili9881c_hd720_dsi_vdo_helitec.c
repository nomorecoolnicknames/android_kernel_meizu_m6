/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define LOG_TAG "LCM"

#ifndef BUILD_LK
#include <linux/string.h>
#include <linux/kernel.h>
#endif

#include "lcm_drv.h"


#ifdef BUILD_LK
#include <platform/upmu_common.h>
#include <platform/mt_gpio.h>
#include <platform/mt_i2c.h>
#include <platform/mt_pmic.h>
#include <string.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#else
/*#include <mach/mt_pm_ldo.h>*/
#ifdef CONFIG_MTK_LEGACY
#include <mach/mt_gpio.h>
#endif
#endif
#ifdef CONFIG_MTK_LEGACY
#include <cust_gpio_usage.h>
#endif
#ifndef CONFIG_FPGA_EARLY_PORTING
#if defined(CONFIG_MTK_LEGACY)
#include <cust_i2c.h>
#endif
#endif

#ifdef CONFIG_LCDKIT_DRIVER
#include "lcdkit_fb_util.h"
#endif

#ifdef BUILD_LK
#define LCM_LOGI(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#define LCM_LOGI(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

#define LCM_ID_NT35695 (0xf5)

static const unsigned int BL_MIN_LEVEL = 20;
static LCM_UTIL_FUNCS lcm_util;


#define SET_RESET_PIN(v)	(lcm_util.set_reset_pin((v)))
#define MDELAY(n)		(lcm_util.mdelay(n))
#define UDELAY(n)		(lcm_util.udelay(n))

#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd) lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
		lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd) \
	  lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size) \
		lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

#define set_gpio_lcd_enp(cmd) \
		lcm_util.set_gpio_lcd_enp_bias(cmd)
#ifndef BUILD_LK
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
/* #include <linux/jiffies.h> */
/* #include <linux/delay.h> */
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>


#ifndef CONFIG_FPGA_EARLY_PORTING

#define TPS_I2C_BUSNUM  I2C_I2C_LCD_BIAS_CHANNEL	/* for I2C channel 0 */
#define I2C_ID_NAME "tps65132"
#define TPS_ADDR 0x3E

#if defined(CONFIG_MTK_LEGACY)
static struct i2c_board_info tps65132_board_info __initdata = { I2C_BOARD_INFO(I2C_ID_NAME, TPS_ADDR) };
#endif
#if !defined(CONFIG_MTK_LEGACY)
static const struct of_device_id lcm_of_match[] = {
		{.compatible = "mediatek,I2C_LCD_BIAS"},
		{},
};
#endif

/*static struct i2c_client *tps65132_i2c_client;*/
static struct i2c_client *tps65132_i2c_client;

/*****************************************************************************
 * Function Prototype
 *****************************************************************************/
static int tps65132_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int tps65132_remove(struct i2c_client *client);
/*****************************************************************************
 * Data Structure
 *****************************************************************************/

struct tps65132_dev {
	struct i2c_client *client;

};

static const struct i2c_device_id tps65132_id[] = {
	{I2C_ID_NAME, 0},
	{}
};

/* #if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)) */
/* static struct i2c_client_address_data addr_data = { .forces = forces,}; */
/* #endif */
static struct i2c_driver tps65132_iic_driver = {
	.id_table = tps65132_id,
	.probe = tps65132_probe,
	.remove = tps65132_remove,
	/* .detect			   = mt6605_detect, */
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "tps65132",
#if !defined(CONFIG_MTK_LEGACY)
			.of_match_table = lcm_of_match,
#endif
		   },
};

static int tps65132_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	LCM_LOGI("tps65132_iic_probe\n");
	LCM_LOGI("TPS: info==>name=%s addr=0x%x\n", client->name, client->addr);
	tps65132_i2c_client = client;
	return 0;
}

static int tps65132_remove(struct i2c_client *client)
{
	LCM_LOGI("tps65132_remove\n");
	tps65132_i2c_client = NULL;
	i2c_unregister_device(client);
	return 0;
}

/*static int tps65132_write_bytes(unsigned char addr, unsigned char value)*/
#if defined(CONFIG_ARCH_MT6797)
static int tps65132_write_bytes(unsigned char addr, unsigned char value)
{
	int ret = 0;
	struct i2c_client *client = tps65132_i2c_client;
	char write_data[2] = { 0 };
	write_data[0] = addr;
	write_data[1] = value;
	ret = i2c_master_send(client, write_data, 2);
	if (ret < 0)
		LCM_LOGI("tps65132 write data fail !!\n");
	return ret;
}
#endif

static int __init tps65132_iic_init(void)
{
	LCM_LOGI("tps65132_iic_init\n");
#if defined(CONFIG_MTK_LEGACY)
	i2c_register_board_info(TPS_I2C_BUSNUM, &tps65132_board_info, 1);
#endif
	LCM_LOGI("tps65132_iic_init2\n");
	if(i2c_add_driver(&tps65132_iic_driver) != 0){
	LCM_LOGI("tps65132_iic_init failed\n");
	}
	LCM_LOGI("tps65132_iic_init success\n");
	return 0;
}

static void __exit tps65132_iic_exit(void)
{
	LCM_LOGI("tps65132_iic_exit\n");
	i2c_del_driver(&tps65132_iic_driver);
}


module_init(tps65132_iic_init);
module_exit(tps65132_iic_exit);

MODULE_AUTHOR("Mike Liu");
MODULE_DESCRIPTION("MTK TPS65132 I2C Driver");
MODULE_LICENSE("GPL");
#endif
#endif

/* static unsigned char lcd_id_pins_value = 0xFF; */
static const unsigned char LCD_MODULE_ID = 0x01;
#define LCM_DSI_CMD_MODE		0
#define FRAME_WIDTH 			(720)
#define FRAME_HEIGHT			(1280)

#define LCM_PHYSICAL_WIDTH		(64800)
#define LCM_PHYSICAL_HEIGHT		(115200)

#define LCT_LCM_ATA_TEST

#define set_gpio_lcd_enp(cmd) lcm_util.set_gpio_lcd_enp_bias(cmd)
#define set_gpio_lcd_enn(cmd) lcm_util.set_gpio_lcd_enn_bias(cmd)


#define REGFLAG_DELAY		0xFFFC
#define REGFLAG_UDELAY	0xFFFB
#define REGFLAG_END_OF_TABLE	0xFFFD
#define REGFLAG_RESET_LOW	0xFFFE
#define REGFLAG_RESET_HIGH	0xFFFF

//static LCM_DSI_MODE_SWITCH_CMD lcm_switch_mode_cmd;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif


struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};

static struct LCM_setting_table lcm_suspend_setting[] = {

	{0x28, 0, {} },
	{REGFLAG_DELAY, 20, {} },
	{0x10, 0, {} },
	{REGFLAG_DELAY, 120, {} },
};

static struct LCM_setting_table init_setting[] = {

	{0xFF, 3, {0x98,0x81,0x03}},

//GIP_1

	{0x01, 1, {0x08}},

	{0x02, 1, {0x00}},

	{0x03, 1, {0x73}},

	{0x04, 1, {0x73}},

	{0x05, 1, {0x14}},

	{0x06, 1, {0x06}},

	{0x07, 1, {0x02}},

	{0x08, 1, {0x05}},

	{0x09, 1, {0x14}},

	{0x0A, 1, {0x14}},

	{0x0B, 1, {0x00}},

	{0x0C, 1, {0x14}},

	{0x0D, 1, {0x14}},

	{0x0E, 1, {0x00}},

	{0x0F, 1, {0x0C}},

	{0x10, 1, {0x0C}},

	{0x11, 1, {0x0C}},

	{0x12, 1, {0x0C}},

	{0x13, 1, {0x14}},

	{0x14, 1, {0x0C}},

	{0x15, 1, {0x00}},

	{0x16, 1, {0x00}},

	{0x17, 1, {0x00}},

	{0x18, 1, {0x00}},

	{0x19, 1, {0x00}},

	{0x1A, 1, {0x00}},

	{0x1B, 1, {0x00}},

	{0x1C, 1, {0x00}},

	{0x1D, 1, {0x00}},

	{0x1E, 1, {0xC8}},

	{0x1F, 1, {0x80}},

	{0x20, 1, {0x02}},

	{0x21, 1, {0x00}},

	{0x22, 1, {0x02}},

	{0x23, 1, {0x00}},

	{0x24, 1, {0x00}},

	{0x25, 1, {0x00}},

	{0x26, 1, {0x00}},

	{0x27, 1, {0x00}},

	{0x28, 1, {0xFB}},

	{0x29, 1, {0x43}},

	{0x2A, 1, {0x00}},

	{0x2B, 1, {0x00}},

	{0x2C, 1, {0x07}},

	{0x2D, 1, {0x07}},

	{0x2E, 1, {0xFF}},

	{0x2F, 1, {0xFF}},

	{0x30, 1, {0x11}},

	{0x31, 1, {0x00}},

	{0x32, 1, {0x00}},

	{0x33, 1, {0x00}},

	{0x34, 1, {0x84}},

	{0x35, 1, {0x80}},

	{0x36, 1, {0x07}},

	{0x37, 1, {0x00}},

	{0x38, 1, {0x00}},

	{0x39, 1, {0x00}},

	{0x3A, 1, {0x00}},

	{0x3B, 1, {0x00}},

	{0x3C, 1, {0x00}},

	{0x3D, 1, {0x00}},

	{0x3E, 1, {0x00}},

	{0x3F, 1, {0x00}},

	{0x40, 1, {0x00}},

	{0x41, 1, {0x88}},

	{0x42, 1, {0x00}},

	{0x43, 1, {0x80}},

	{0x44, 1, {0x08}},

//GIP_2

	{0x50, 1, {0x01}},

	{0x51, 1, {0x23}},

	{0x52, 1, {0x45}},

	{0x53, 1, {0x67}},

	{0x54, 1, {0x89}},

	{0x55, 1, {0xAB}},

	{0x56, 1, {0x01}},

	{0x57, 1, {0x23}},

	{0x58, 1, {0x45}},

	{0x59, 1, {0x67}},

	{0x5A, 1, {0x89}},

	{0x5B, 1, {0xAB}},

	{0x5C, 1, {0xCD}},

	{0x5D, 1, {0xEF}},
	
//GIP_3

	{0x5E, 1, {0x10}},

	{0x5F, 1, {0x02}},

	{0x60, 1, {0x08}},

	{0x61, 1, {0x09}},

	{0x62, 1, {0x10}},

	{0x63, 1, {0x12}},

	{0x64, 1, {0x11}},

	{0x65, 1, {0x13}},

	{0x66, 1, {0x0C}},

	{0x67, 1, {0x02}},

	{0x68, 1, {0x02}},

	{0x69, 1, {0x02}},

	{0x6A, 1, {0x02}},

	{0x6B, 1, {0x02}},

	{0x6C, 1, {0x0E}},

	{0x6D, 1, {0x0D}},

	{0x6E, 1, {0x0F}},

	{0x6F, 1, {0x02}},

	{0x70, 1, {0x02}},

	{0x71, 1, {0x06}},

	{0x72, 1, {0x07}},

	{0x73, 1, {0x02}},

	{0x74, 1, {0x02}},

	{0x75, 1, {0x02}},

	{0x76, 1, {0x07}},

	{0x77, 1, {0x06}},

	{0x78, 1, {0x11}},

	{0x79, 1, {0x13}},

	{0x7A, 1, {0x10}},

	{0x7B, 1, {0x12}},

	{0x7C, 1, {0x0F}},

	{0x7D, 1, {0x02}},

	{0x7E, 1, {0x02}},

	{0x7F, 1, {0x02}},

	{0x80, 1, {0x02}},

	{0x81, 1, {0x02}},

	{0x82, 1, {0x0D}},

	{0x83, 1, {0x0E}},

	{0x84, 1, {0x0C}},

	{0x85, 1, {0x02}},

	{0x86, 1, {0x02}},

	{0x87, 1, {0x09}},

	{0x88, 1, {0x08}},

	{0x89, 1, {0x02}},

	{0x8A, 1, {0x02}},

//CMD_PAGE 4

	{0xFF, 3, {0x98,0x81,0x04}},

	{0x6C, 1, {0x15}},

	{0x6E, 1, {0x28}},

	{0x6F, 1, {0x35}},

	{0x3A, 1, {0x24}},

	{0x8D, 1, {0x1F}},

	{0x87, 1, {0xBA}},

	{0x26, 1, {0x76}},

	{0xB2, 1, {0xD1}},

	{0xB5, 1, {0x07}},

	{0x69, 1, {0x57}},

	{0x33, 1, {0x44}},

	{0x88, 1, {0x0B}},

	{0x35, 1, {0x1F}},

	{0xB5, 1, {0x07}},
	{0x0B, 1, {0x80}},

	{0x0E, 1, {0x17}},
//CMD_PAGE 1

	{0xFF, 3, {0x98,0x81,0x01}},

	{0x22, 1, {0x0A}},

	{0x31, 1, {0x00}},

	{0x53, 1, {0x37}},

//	{0x55, 1, {0x88}},

	{0x50, 1, {0x5A}},

	{0x51, 1, {0x55}},

	{0x60, 1, {0x14}},

	{0x61, 1, {0x00}},

	{0x62, 1, {0x19}},

	{0x63, 1, {0x10}},



	{0xFF, 3, {0x98, 0x81, 0x01}},
	{0xA0, 1, {0x1C}},
	{0xA1, 1, {0x28}},

	{0xA2, 1, {0x34}},
	{0xA3, 1, {0x0D}},

	{0xA4, 1, {0x10}},

	{0xA5, 1, {0x25}},

	{0xA6, 1, {0x19}},

	{0xA7, 1, {0x1C}},

	{0xA8, 1, {0x8C}},

	{0xA9, 1, {0x1B}},

	{0xAA, 1, {0x27}},

	{0xAB, 1, {0x79}},

	{0xAC, 1, {0x1A}},

	{0xAD, 1, {0x17}},

	{0xAE, 1, {0x4C}},

	{0xAF, 1, {0x22}},

	{0xB0, 1, {0x27}},

	{0xB1, 1, {0x5A}},

	{0xB2, 1, {0x68}},

	{0xB3, 1, {0x39}},

	{0xC0, 1, {0x1B}},
	{0xC1, 1, {0x28}},

	{0xC2, 1, {0x34}},
	{0xC3, 1, {0x0D}},

	{0xC4, 1, {0x10}},

	{0xC5, 1, {0x25}},

	{0xC6, 1, {0x19}},

	{0xC7, 1, {0x1C}},

	{0xC8, 1, {0x8C}},

	{0xC9, 1, {0x1B}},

	{0xCA, 1, {0x28}},

	{0xCB, 1, {0x79}},

	{0xCC, 1, {0x1A}},

	{0xCD, 1, {0x18}},

	{0xCE, 1, {0x4D}},

	{0xCF, 1, {0x23}},

	{0xD0, 1, {0x28}},

	{0xD1, 1, {0x5A}},

	{0xD2, 1, {0x68}},

	{0xD3, 1, {0x39}},

	{0xFF, 3, {0x98,0x81,0x02}},
	{0x03,1,{0x4D}},
	{0x04,1,{0x45}},
	{0x05,1,{0x44}},
	{0x06,1,{0x20}},
	{0x07,1,{0x00}},	//pwm 30k
//CMD_PAGE 0
	{0xFF, 3, {0x98,0x81,0x04}},
	{0x4C,1,{0x02}},
	{0x52,1,{0x37}},
	{0x53,1,{0x37}},
	{0x54,1,{0x46}},
	{0xFF, 3, {0x98,0x81,0x00}},

	{0x11, 0, {}},

	{REGFLAG_DELAY, 60, {}},
	{0x29, 0, {}},
	{REGFLAG_DELAY, 20, {}},
	{0x51, 2, {0x00,0x00}},
	{0x53, 1, {0x2c}},
	{0x55, 1, {0x01}},
	#ifdef CONFIG_LCT_BACKLIGHT_DIMMING
	{0x68, 2, {0x01,0x02}},// 07 02
	#else
	{0x68, 2, {0x03,0x01}},// 07 02
	#endif
	{REGFLAG_DELAY, 5, {}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table bl_level[] = {
	{0x51,2,{0x00,0x00}},
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static void push_table(void *cmdq, struct LCM_setting_table *table,
	unsigned int count, unsigned char force_update)
{
	unsigned int i;
	unsigned cmd;

	for (i = 0; i < count; i++) {
		cmd = table[i].cmd;

		switch (cmd) {

		case REGFLAG_DELAY:
			if (table[i].count <= 10)
				MDELAY(table[i].count);
			else
				MDELAY(table[i].count);
			break;

		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			break;

		case REGFLAG_END_OF_TABLE:
			break;

		default:
			dsi_set_cmdq_V22(cmdq, cmd, table[i].count, table[i].para_list, force_update);
		}
	}
}


static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}


static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, sizeof(LCM_PARAMS));

	params->type = LCM_TYPE_DSI;

	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	params->physical_width = LCM_PHYSICAL_WIDTH/1000;
	params->physical_height = LCM_PHYSICAL_HEIGHT/1000;
	params->physical_width_um = LCM_PHYSICAL_WIDTH;
	params->physical_height_um = LCM_PHYSICAL_HEIGHT;

#if (LCM_DSI_CMD_MODE)
	params->dsi.mode = CMD_MODE;
	params->dsi.switch_mode = SYNC_PULSE_VDO_MODE;
	lcm_dsi_mode = CMD_MODE;
#else
	params->dsi.mode = SYNC_PULSE_VDO_MODE;
	//params->dsi.switch_mode = CMD_MODE;
	//lcm_dsi_mode = SYNC_PULSE_VDO_MODE;
#endif
	LCM_LOGI("lcm_get_params lcm_dsi_mode %d\n", lcm_dsi_mode);
	params->dsi.switch_mode_enable = 0;

	/* DSI */
	/* Command mode setting */
	params->dsi.LANE_NUM = LCM_FOUR_LANE;
	/* The following defined the fomat for data coming from LCD engine. */
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

	/* Highly depends on LCD driver capability. */
	params->dsi.packet_size = 256;
	/* video mode timing */

	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active = 8;
	params->dsi.vertical_backporch = 24;
	params->dsi.vertical_frontporch = 24;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 80;
	params->dsi.horizontal_backporch = 120;//52
	params->dsi.horizontal_frontporch = 120;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.ssc_disable = 0;
#ifndef CONFIG_FPGA_EARLY_PORTING
#if (LCM_DSI_CMD_MODE)
	params->dsi.PLL_CLOCK = 420;	/* this value must be in MTK suggested table */
#else
	params->dsi.PLL_CLOCK = 257;	/* this value must be in MTK suggested table */
#endif
	//params->dsi.PLL_CK_CMD = 420;
	//params->dsi.PLL_CK_VDO = 440;
#else
	params->dsi.pll_div1 = 0;
	params->dsi.pll_div2 = 0;
	params->dsi.fbk_div = 0x1;
#endif
	//params->dsi.CLK_HS_POST = 36;
	params->dsi.clk_lp_per_line_enable = 0;

	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 1;

	params->dsi.lcm_esd_check_table[0].cmd = 0x0a;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9c;
	params->dsi.lcm_esd_check_table[1].cmd = 0x0b;
	params->dsi.lcm_esd_check_table[1].count = 1;
	params->dsi.lcm_esd_check_table[1].para_list[0] = 0x00;
	params->dsi.lcm_esd_check_table[2].cmd = 0x0d;
	params->dsi.lcm_esd_check_table[2].count = 1;
	params->dsi.lcm_esd_check_table[2].para_list[0] = 0x00;
	params->dsi.lcm_esd_check_table[3].cmd = 0x54;
	params->dsi.lcm_esd_check_table[3].count = 1;
	params->dsi.lcm_esd_check_table[3].para_list[0] = 0x54;
	params->dsi.lcm_esd_check_table[3].para_list[1] = 0x24;
	params->dsi.lcm_esd_check_table[3].para_list[2] = 0x2C;

	params->dsi.HS_TRAIL = 5;

}

#ifdef BUILD_LK
#ifndef CONFIG_FPGA_EARLY_PORTING
#define TPS65132_SLAVE_ADDR_WRITE  0x7C
static struct mt_i2c_t TPS65132_i2c;

static int TPS65132_write_byte(kal_uint8 addr, kal_uint8 value)
{
	kal_uint32 ret_code = I2C_OK;
	kal_uint8 write_data[2];
	kal_uint16 len;

	write_data[0] = addr;
	write_data[1] = value;

	TPS65132_i2c.id = I2C_I2C_LCD_BIAS_CHANNEL;	/* I2C2; */
	/* Since i2c will left shift 1 bit, we need to set FAN5405 I2C address to >>1 */
	TPS65132_i2c.addr = (TPS65132_SLAVE_ADDR_WRITE >> 1);
	TPS65132_i2c.mode = ST_MODE;
	TPS65132_i2c.speed = 100;
	len = 2;

	ret_code = i2c_write(&TPS65132_i2c, write_data, len);
	/* printf("%s: i2c_write: ret_code: %d\n", __func__, ret_code); */

	return ret_code;
}

#else


#endif
#endif




static void lcm_init(void)
{
	unsigned char cmd = 0x0;
	unsigned char data = 0xFF;
	int ret = 0;
	cmd = 0x00;
	data = 0x0E;

	SET_RESET_PIN(0);
	set_gpio_lcd_enp(1);
	MDELAY(5);
	set_gpio_lcd_enn(1);
	MDELAY(5);
#ifdef BUILD_LK
	ret = TPS65132_write_byte(cmd, data);
#else
#if defined(CONFIG_ARCH_MT6797)
	ret = tps65132_write_bytes(cmd, data);
#endif
#endif

	if (ret < 0)
		LCM_LOGI("ili9881c----tps6132----cmd=%0x--i2c write error----\n", cmd);
	else
		LCM_LOGI("ili9881c----tps6132----cmd=%0x--i2c write success----\n", cmd);

	cmd = 0x01;
	data = 0x0E;

#ifdef BUILD_LK
	ret = TPS65132_write_byte(cmd, data);
#else
#if defined(CONFIG_ARCH_MT6797)
	ret = tps65132_write_bytes(cmd, data);
#endif
#endif

	if (ret < 0)
		LCM_LOGI("ili9881c----tps6132----cmd=%0x--i2c write error----\n", cmd);
	else
		LCM_LOGI("ili9881c----tps6132----cmd=%0x--i2c write success----\n", cmd);

	SET_RESET_PIN(1);
	MDELAY(1);
	SET_RESET_PIN(0);
	MDELAY(1);

	SET_RESET_PIN(1);
	MDELAY(10);
	
	push_table(NULL, init_setting, sizeof(init_setting) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_suspend(void)
{
	push_table(NULL, lcm_suspend_setting, sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table), 1);
	SET_RESET_PIN(0);
	MDELAY(10);
	set_gpio_lcd_enn(0);
	set_gpio_lcd_enp(0);

	/* SET_RESET_PIN(0); */
}

#ifdef CONFIG_LCT_CABC_MODE_SUPPORT

#define  CABC_MODE_SETTING_DIS 0
#define  CABC_MODE_SETTING_UI 1
#define  CABC_MODE_SETTING_STILL 2
#define  CABC_MODE_SETTING_MV  3

static unsigned char cabc_mode = 0x01;
#define  INIT_SETTING_CABC_INDEX 204

static struct LCM_setting_table lcm_setting_dis[] = {
	{0x55,1,{0x00}},
	{REGFLAG_DELAY, 5, {}},
	{0x53,1,{0x2c}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_setting_ui[] = {
	{0x55,1,{0x01}},
	{REGFLAG_DELAY, 5, {}},
	{0x53,1,{0x2c}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_setting_still[] = {
	{0x55,1,{0x02}},
	{REGFLAG_DELAY, 5, {}},
	{0x53,1,{0x2c}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_setting_mv[] = {
	{0x55,1,{0x03}},
	{REGFLAG_DELAY, 5, {}},
	{0x53,1,{0x2c}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static void push_table_v22(void *handle, struct LCM_setting_table *table, unsigned int count,
			   unsigned char force_update)
{
	unsigned int i;
	for (i = 0; i < count; i++) {

		unsigned cmd;
		void *cmdq = handle;
		cmd = table[i].cmd;

		switch (cmd) {

		case REGFLAG_DELAY:
			MDELAY(table[i].count);
			break;

		case REGFLAG_END_OF_TABLE:
			break;

		default:
			dsi_set_cmdq_V22(cmdq, cmd, table[i].count, table[i].para_list, force_update);
		}
	}

}

static void lcm_cabc_cmdq(void *handle, unsigned int mode)
{
	cabc_mode = mode;
	switch(mode){
		case CABC_MODE_SETTING_DIS:
			push_table_v22(handle,lcm_setting_dis, sizeof(lcm_setting_dis) /sizeof(struct LCM_setting_table), 1);
				break;
		case CABC_MODE_SETTING_UI:
			push_table_v22(handle,lcm_setting_ui, sizeof(lcm_setting_ui) /sizeof(struct LCM_setting_table), 1);
				 break;
		case CABC_MODE_SETTING_STILL:
			 push_table_v22(handle,lcm_setting_still, sizeof(lcm_setting_still) /sizeof(struct LCM_setting_table), 1);
			break;
		case CABC_MODE_SETTING_MV:
			 push_table_v22(handle,lcm_setting_mv, sizeof(lcm_setting_mv) /sizeof(struct LCM_setting_table), 1);
				 break;
		default:
			 push_table_v22(handle,lcm_setting_ui, sizeof(lcm_setting_ui) /sizeof(struct LCM_setting_table), 1);
	}
}
#endif


static void lcm_resume(void)
{
	init_setting[INIT_SETTING_CABC_INDEX].para_list[0] = cabc_mode;
	lcm_init();
}

#if 0
static unsigned int lcm_compare_id(void)
{
	unsigned int id = 0;
	unsigned char buffer[2];
	unsigned int array[16];

	SET_RESET_PIN(1);
	SET_RESET_PIN(0);
	MDELAY(1);

	SET_RESET_PIN(1);
	MDELAY(20);

	array[0] = 0x00023700;	/* read id return two byte,version and id */
	dsi_set_cmdq(array, 1, 1);

	read_reg_v2(0xF4, buffer, 2);
	id = buffer[0];		/* we only need ID */

	LCM_LOGI("%s,nt35695 debug: nt35695 id = 0x%08x\n", __func__, id);

	if (id == LCM_ID_NT35695)
		return 1;
	else
		return 0;

}
#endif


static void lcm_setbacklight_cmdq(void *handle, unsigned int level)
{
	LCM_LOGI("%s,ili9881c_helitec backlight: level = %d\n", __func__, level);

	bl_level[0].para_list[0] = (level >> 4);
	bl_level[0].para_list[1] = (level << 4);
	push_table(handle, bl_level, sizeof(bl_level) / sizeof(struct LCM_setting_table), 1);
}

#ifdef LCT_LCM_ATA_TEST
#define LCT_LCM_ATA_DEBUG
static unsigned int lcm_ata_check(unsigned char *buffer)
{
	int   array[4];
	char  read_buffer[3];
	char  id0,id1,id=0;
	/* page 1 */
	array[0] = 0x00043902;
	array[1] = 0x018198ff;
	dsi_set_cmdq(array, 2, 1);

	array[0] = 0x00013700;// read id return two byte,version and id
	dsi_set_cmdq(array, 1, 1);
	read_reg_v2(0x00, read_buffer, 1);
	id0  =	read_buffer[0];

	array[0] = 0x00013700;// read id return two byte,version and id
	dsi_set_cmdq(array, 1, 1);
	read_reg_v2(0x01, read_buffer, 1);
	id1  =	read_buffer[0];
	id = (id0 << 8) | id1;

	#ifdef LCT_LCM_ATA_DEBUG
	printk("ili9881-c %s, id0 = 0x%x , id1 = 0x%x, id = 0x%08x\n", __func__, id0, id1, id);
	#endif

	if ((id0 == 0x98)&&(id1 == 0x81)) //cpt
		return 1;
	else
		return 0;
}
#endif

#ifdef CONFIG_LCDKIT_DRIVER
static void  set_lcdkit_info(void)
{
	lcdkit_info.panel_infos.cabc_support = 1;
	lcdkit_info.panel_infos.cabc_mode = 0;
	lcdkit_info.panel_infos.inversion_support = 1;
	lcdkit_info.panel_infos.inversion_mode = 0;
	lcdkit_info.panel_infos.check_reg_support = 1;
	lcdkit_info.panel_infos.scan_support = 1;
	lcdkit_info.panel_infos.scan_mode = 0;
	lcdkit_info.panel_infos.esd_support = 1;
	lcdkit_info.panel_infos.mipi_detect_support = 1;
	lcdkit_info.panel_infos.lp2hs_mipi_check_support = 1;

	lcdkit_info.panel_infos.bias_power_ctrl_mode |= POWER_CTRL_BY_GPIO;
	lcdkit_info.panel_infos.gpio_lcd_vsn = 90;
	lcdkit_info.panel_infos.gpio_lcd_vsp = 17;
	lcdkit_info.panel_infos.enable_PT_test = 0;
	lcdkit_info.panel_infos.PT_test_support = 1;
	lcdkit_info.panel_infos.model_name = "HELITEC_ILI9881c 5.2' VIDEO AUO 1280x720";
	lcdkit_info.panel_infos.bl_level_max = 255;
	lcdkit_info.panel_infos.bl_level_min = 4;
	lcdkit_info.panel_infos.bl_max_nit = 0;
	lcdkit_info.panel_infos.panel_status_cmds = 0x0a;
	lcdkit_info.panel_infos.inversion_mode_cmds = 0x31;
	lcdkit_info.panel_infos.scan_mode_cmds = 0x36;
	lcdkit_info.panel_infos.scan_mode_normal_data = 0x00;
	lcdkit_info.panel_infos.scan_mode_reverse_data = 0x07;
}
#endif

LCM_DRIVER ili9881c_hd720_dsi_vdo_helitec_lcm_drv = {
	.name = "ili9881c_hd720_dsi_vdo_helitec",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
#ifdef  LCT_LCM_ATA_TEST
	.ata_check = lcm_ata_check,
#endif
	//.compare_id = lcm_compare_id,	
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
#ifdef CONFIG_LCDKIT_DRIVER
	.set_lcm_panel_support = set_lcdkit_info,
#endif

#ifdef CONFIG_LCT_CABC_MODE_SUPPORT
	.set_cabc_cmdq = lcm_cabc_cmdq,
#endif

};
