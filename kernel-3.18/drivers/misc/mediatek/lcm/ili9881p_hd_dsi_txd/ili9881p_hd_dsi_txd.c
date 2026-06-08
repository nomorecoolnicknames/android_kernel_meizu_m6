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

#ifdef BUILD_LK
#define LCM_LOGI(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#define LCM_LOGI(fmt, args...)  pr_info("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

#define LCM_ID (0x98)

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
#define set_gpio_lcd_enn(cmd) \
		lcm_util.set_gpio_lcd_enn_bias(cmd)
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
#include <linux/jiffies.h>
#include <linux/string.h>


#ifndef CONFIG_FPGA_EARLY_PORTING
#define I2C_I2C_LCD_BIAS_CHANNEL 0
#define TPS_I2C_BUSNUM  I2C_I2C_LCD_BIAS_CHANNEL	/* for I2C channel 0 */
#define I2C_ID_NAME "tps65132"
#define TPS_ADDR 0x3E

#if defined(CONFIG_MTK_LEGACY)
static struct i2c_board_info tps65132_board_info __initdata = { I2C_BOARD_INFO(I2C_ID_NAME, TPS_ADDR) };
#endif
#if !defined(CONFIG_MTK_LEGACY)
static const struct of_device_id lcm_of_match[] = {
		{.compatible = "mediatek,i2c_lcd_bias"},
		{.compatible = "mediatek,I2C_LCD_BIAS"},
		{},
};
#endif

/*static struct i2c_client *tps65132_i2c_client;*/
struct i2c_client *tps65132_i2c_client;

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
	/* .detect               = mt6605_detect, */
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "ili9881p_hd_dsi_txd",
#if !defined(CONFIG_MTK_LEGACY)
			.of_match_table = lcm_of_match,
#endif
		   },
};

static int tps65132_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	LCM_LOGI("tps65132_iic_probe\n");
	LCM_LOGI("TPS: info==>name=%s addr=0x%x\n", client->name, client->addr);
	if (tps65132_i2c_client && tps65132_i2c_client != client)
		LCM_LOGI("M6 LCM tps65132 probe replacing active client=%p adapter=%d with client=%p adapter=%d\n",
			tps65132_i2c_client,
			tps65132_i2c_client->adapter ?
				tps65132_i2c_client->adapter->nr : -1,
			client, client->adapter ? client->adapter->nr : -1);
	LCM_LOGI("M6 LCM tps65132 probe client=%p adapter=%d name=%s addr=0x%x\n",
		client, client->adapter ? client->adapter->nr : -1,
		client->name, client->addr);
	tps65132_i2c_client = client;
	return 0;
}

static int tps65132_remove(struct i2c_client *client)
{
	LCM_LOGI("M6 LCM tps65132 remove client=%p adapter=%d name=%s active=%p\n",
		client, client->adapter ? client->adapter->nr : -1,
		client->name, tps65132_i2c_client);
	if (tps65132_i2c_client == client)
		tps65132_i2c_client = NULL;
	return 0;
}

/*static int tps65132_write_bytes(unsigned char addr, unsigned char value)*/
#if !defined(CONFIG_ARCH_MT6797)
int tps65132_write_bytes(unsigned char addr, unsigned char value)
{
	int ret = 0;
	struct i2c_client *client = tps65132_i2c_client;
	char write_data[2] = { 0 };

	if (!client) {
		LCM_LOGI("tps65132 i2c client is not ready !!\n");
		LCM_LOGI("M6 LCM tps65132 write blocked addr=0x%02x value=0x%02x client=NULL\n",
			addr, value);
		return -1;
	}

	write_data[0] = addr;
	write_data[1] = value;
	ret = i2c_master_send(client, write_data, 2);
	if (ret < 0)
		LCM_LOGI("tps65132 write data fail !!\n");
	LCM_LOGI("M6 LCM tps65132 write addr=0x%02x value=0x%02x ret=%d client=0x%x/%s adapter=%d\n",
		addr, value, ret, client->addr, client->name,
		client->adapter ? client->adapter->nr : -1);
	return ret;
}

static int tps65132_read_byte(unsigned char addr)
{
	int ret = 0;
	struct i2c_client *client = tps65132_i2c_client;

	if (!client) {
		LCM_LOGI("M6 LCM tps65132 read blocked addr=0x%02x client=NULL\n",
			addr);
		return -1;
	}

	ret = i2c_smbus_read_byte_data(client, addr);
	LCM_LOGI("M6 LCM tps65132 read addr=0x%02x ret=%d client=0x%x/%s adapter=%d\n",
		addr, ret, client->addr, client->name,
		client->adapter ? client->adapter->nr : -1);
	return ret;
}
#endif

static int __init tps65132_iic_init(void)
{
	int ret;

	LCM_LOGI("tps65132_iic_init\n");
#if defined(CONFIG_MTK_LEGACY)
	i2c_register_board_info(TPS_I2C_BUSNUM, &tps65132_board_info, 1);
#endif
	LCM_LOGI("tps65132_iic_init2\n");
	ret = i2c_add_driver(&tps65132_iic_driver);
	LCM_LOGI("M6 LCM tps65132_iic_init add_driver ret=%d\n", ret);
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
#define LCM_DSI_CMD_MODE									0
#define FRAME_WIDTH										(720)
#define FRAME_HEIGHT									(1280)

#define LCM_PHYSICAL_WIDTH									(68000)
#define LCM_PHYSICAL_HEIGHT									(121000)


#ifndef CONFIG_FPGA_EARLY_PORTING
#define GPIO_65132_EN GPIO_LCD_BIAS_ENP_PIN
#endif

#define REGFLAG_DELAY		0xFFFC
#define REGFLAG_UDELAY	0xFFFB
#define REGFLAG_END_OF_TABLE	0xFFFD
#define REGFLAG_RESET_LOW	0xFFFE
#define REGFLAG_RESET_HIGH	0xFFFF

static LCM_DSI_MODE_SWITCH_CMD lcm_switch_mode_cmd;

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

#if 0
static struct LCM_setting_table init_setting_source_port[] = {
	{ 0xFF, 0x03, {0x98, 0x81, 0x03} },
	/* GIP_1 */
	{ 0x01, 0x01, {0x00} },
	{ 0x02, 0x01, {0x00} },
	{ 0x03, 0x01, {0x53} },
	{ 0x04, 0x01, {0x13} },
	{ 0x05, 0x01, {0x00} },
	{ 0x06, 0x01, {0x03} },
	{ 0x07, 0x01, {0x02} },
	{ 0x08, 0x01, {0x00} },
	{ 0x09, 0x01, {0x28} },
	{ 0x0a, 0x01, {0x28} },
	{ 0x0b, 0x01, {0x00} },
	{ 0x0c, 0x01, {0x01} },
	{ 0x0d, 0x01, {0x00} },
	{ 0x0e, 0x01, {0x00} },
	{ 0x0f, 0x01, {0x28} },
	{ 0x10, 0x01, {0x28} },
	{ 0x11, 0x01, {0x00} },
	{ 0x12, 0x01, {0x00} },
	{ 0x13, 0x01, {0x00} },
	{ 0x14, 0x01, {0x00} },
	{ 0x15, 0x01, {0x00} },
	{ 0x16, 0x01, {0x00} },
	{ 0x17, 0x01, {0x00} },
	{ 0x18, 0x01, {0x00} },
	{ 0x19, 0x01, {0x00} },
	{ 0x1a, 0x01, {0x00} },
	{ 0x1b, 0x01, {0x00} },
	{ 0x1c, 0x01, {0x00} },
	{ 0x1d, 0x01, {0x00} },
	{ 0x1e, 0x01, {0x44} },
	{ 0x1f, 0x01, {0x80} },
	{ 0x20, 0x01, {0x01} },
	{ 0x21, 0x01, {0x02} },
	{ 0x22, 0x01, {0x00} },
	{ 0x23, 0x01, {0x00} },
	{ 0x24, 0x01, {0x00} },
	{ 0x25, 0x01, {0x00} },
	{ 0x26, 0x01, {0x00} },
	{ 0x27, 0x01, {0x00} },
	{ 0x28, 0x01, {0x33} },
	{ 0x29, 0x01, {0x03} },
	{ 0x2a, 0x01, {0x00} },
	{ 0x2b, 0x01, {0x00} },
	{ 0x2c, 0x01, {0x00} },
	{ 0x2d, 0x01, {0x00} },
	{ 0x2e, 0x01, {0x00} },
	{ 0x2f, 0x01, {0x00} },
	{ 0x30, 0x01, {0x00} },
	{ 0x31, 0x01, {0x00} },
	{ 0x32, 0x01, {0x00} },
	{ 0x33, 0x01, {0x00} },
	{ 0x34, 0x01, {0x04} },
	{ 0x35, 0x01, {0x00} },
	{ 0x36, 0x01, {0x05} }, /* Build Station: TXD panel scan direction from ILI9881C 720p refs */
	{ 0x37, 0x01, {0x00} },
	{ 0x38, 0x01, {0x3c} },
	{ 0x39, 0x01, {0x00} },
	{ 0x3a, 0x01, {0x40} },
	{ 0x3b, 0x01, {0x00} },
	{ 0x3c, 0x01, {0x00} },
	{ 0x3d, 0x01, {0x00} },
	{ 0x3e, 0x01, {0x00} },
	{ 0x3f, 0x01, {0x00} },
	{ 0x40, 0x01, {0x00} },
	{ 0x41, 0x01, {0x00} },
	{ 0x42, 0x01, {0x00} },
	{ 0x43, 0x01, {0x00} },
	{ 0x44, 0x01, {0x00} },
	{REGFLAG_UDELAY, 100, {} },
	/* GIP_2 */
	{ 0x50, 0x01, {0x01} },
	{ 0x51, 0x01, {0x23} },
	{ 0x52, 0x01, {0x45} },
	{ 0x53, 0x01, {0x67} },
	{ 0x54, 0x01, {0x89} },
	{ 0x55, 0x01, {0xab} },
	{ 0x56, 0x01, {0x01} },
	{ 0x57, 0x01, {0x23} },
	{ 0x58, 0x01, {0x45} },
	{ 0x59, 0x01, {0x67} },
	{ 0x5a, 0x01, {0x89} },
	{ 0x5b, 0x01, {0xab} },
	{ 0x5c, 0x01, {0xcd} },
	{ 0x5d, 0x01, {0xef} },
	{REGFLAG_UDELAY, 100, {} },
	/* GIP_3 */
	{ 0x5e, 0x01, {0x11} },
	{ 0x5f, 0x01, {0x01} },
	{ 0x60, 0x01, {0x00} },
	{ 0x61, 0x01, {0x15} },
	{ 0x62, 0x01, {0x14} },
	{ 0x63, 0x01, {0x0c} },
	{ 0x64, 0x01, {0x0d} },
	{ 0x65, 0x01, {0x0e} },
	{ 0x66, 0x01, {0x0f} },
	{ 0x67, 0x01, {0x06} },
	{ 0x68, 0x01, {0x02} },
	{ 0x69, 0x01, {0x02} },
	{ 0x6a, 0x01, {0x02} },
	{ 0x6b, 0x01, {0x02} },
	{ 0x6c, 0x01, {0x02} },
	{ 0x6d, 0x01, {0x02} },
	{ 0x6e, 0x01, {0x08} },
	{ 0x6f, 0x01, {0x02} },
	{ 0x70, 0x01, {0x02} },
	{ 0x71, 0x01, {0x02} },
	{ 0x72, 0x01, {0x02} },
	{ 0x73, 0x01, {0x02} },
	{ 0x74, 0x01, {0x02} },
	{ 0x75, 0x01, {0x01} },
	{ 0x76, 0x01, {0x00} },
	{ 0x77, 0x01, {0x15} },
	{ 0x78, 0x01, {0x14} },
	{ 0x79, 0x01, {0x0C} },
	{ 0x7a, 0x01, {0x0D} },
	{ 0x7b, 0x01, {0x0e} },
	{ 0x7c, 0x01, {0x0f} },
	{ 0x7d, 0x01, {0x08} },
	{ 0x7e, 0x01, {0x02} },
	{ 0x7f, 0x01, {0x02} },
	{ 0x80, 0x01, {0x02} },
	{ 0x81, 0x01, {0x02} },
	{ 0x82, 0x01, {0x02} },
	{ 0x83, 0x01, {0x02} },
	{ 0x84, 0x01, {0x06} },
	{ 0x85, 0x01, {0x02} },
	{ 0x86, 0x01, {0x02} },
	{ 0x87, 0x01, {0x02} },
	{ 0x88, 0x01, {0x02} },
	{ 0x89, 0x01, {0x02} },
	{ 0x8A, 0x01, {0x02} },
	{REGFLAG_UDELAY, 100, {} },
	/* CMD_Page 4 */
	{ 0xFF, 0x03, {0x98, 0x81, 0x04} },
	{ 0x6C, 0x01, {0x15} },
	{ 0x6E, 0x01, {0x2b} },
	{ 0x6F, 0x01, {0x35} },
	{ 0x35, 0x01, {0x1f} },
	{ 0x3A, 0x01, {0x24} },
	{ 0x8D, 0x01, {0x14} },
	{ 0x87, 0x01, {0xBA} },
	{ 0x26, 0x01, {0x76} },
	{ 0xB2, 0x01, {0xD1} },
	{ 0xB5, 0x01, {0x06} },
	{ 0x33, 0x01, {0x14} },
	{ 0x7a, 0x01, {0x10} },
	{REGFLAG_UDELAY, 100, {} },
	/* CMD_Page 1 */
	{ 0xFF, 0x03, {0x98, 0x81, 0x01} },
	{ 0x22, 0x01, {0x0A} },
	{ 0x31, 0x01, {0x00} },
	{ 0x53, 0x01, {0x83} },
	{ 0x55, 0x01, {0x9C} },
	{ 0x50, 0x01, {0xC7} },
	{ 0x51, 0x01, {0xC4} },
	{ 0x60, 0x01, {0x1A} },
	{ 0x62, 0x01, {0x00} },
	{ 0x63, 0x01, {0x00} },
	{ 0x2E, 0x01, {0xF0} },
	/* ============Gamma START============= */
	/* Pos Register */
	{ 0xA0, 0x01, {0x02} },
	{ 0xA1, 0x01, {0x22} },
	{ 0xA2, 0x01, {0x31} },
	{ 0xA3, 0x01, {0x15} },
	{ 0xA4, 0x01, {0x19} },
	{ 0xA5, 0x01, {0x2B} },
	{ 0xA6, 0x01, {0x1F} },
	{ 0xA7, 0x01, {0x20} },
	{ 0xA8, 0x01, {0x92} },
	{ 0xA9, 0x01, {0x1C} },
	{ 0xAA, 0x01, {0x28} },
	{ 0xAB, 0x01, {0x7C} },
	{ 0xAC, 0x01, {0x1B} },
	{ 0xAD, 0x01, {0x1B} },
	{ 0xAE, 0x01, {0x4F} },
	{ 0xAF, 0x01, {0x23} },
	{ 0xB0, 0x01, {0x29} },
	{ 0xB1, 0x01, {0x56} },
	{ 0xB2, 0x01, {0x5F} },
	{ 0xB3, 0x01, {0x3F} },
	/* Neg Register */
	{ 0xC0, 0x01, {0x02} },
	{ 0xC1, 0x01, {0x22} },
	{ 0xC2, 0x01, {0x31} },
	{ 0xC3, 0x01, {0x15} },
	{ 0xC4, 0x01, {0x19} },
	{ 0xC5, 0x01, {0x2B} },
	{ 0xC6, 0x01, {0x1F} },
	{ 0xC7, 0x01, {0x20} },
	{ 0xC8, 0x01, {0x92} },
	{ 0xC9, 0x01, {0x1C} },
	{ 0xCA, 0x01, {0x28} },
	{ 0xCB, 0x01, {0x7C} },
	{ 0xCC, 0x01, {0x1B} },
	{ 0xCD, 0x01, {0x1B} },
	{ 0xCE, 0x01, {0x4F} },
	{ 0xCF, 0x01, {0x23} },
	{ 0xD0, 0x01, {0x29} },
	{ 0xD1, 0x01, {0x56} },
	{ 0xD2, 0x01, {0x5F} },
	{ 0xD3, 0x01, {0x3F} },
	/* ============ Gamma END=========== */
	/* CMD_Page 0 */
	{ 0xFF, 0x03, {0x98, 0x81, 0x00} },
	/* Build Station: keep the last known-visible TXD MADCTL during bring-up. */
	{ 0x36, 0x01, {0x48} },
	{ 0x3A, 0x01, {0x77} },
	{ 0x35, 0x01, {0x00} },
	{ 0x11, 0x01, {0x00} },
	{REGFLAG_DELAY, 120, {} },
	{ 0x29, 0x01, {0x00} },
	/*
	 * Meizu M6 TXD panel needs extra time after Display ON before the
	 * video engine starts scanning, otherwise RDMA0 EOF can be missed.
	 */
	{REGFLAG_DELAY, 120, {} }
};
#endif

/* Stock Flyme LK/kernel ili9881p_hd_dsi_txd init table. */
static struct LCM_setting_table init_setting[] = {
	{ 0xFF, 0x03, {0x98, 0x81, 0x01} },
	{ 0x44, 0x01, {0x31} },
	{ 0xFF, 0x03, {0x98, 0x81, 0x05} },
	{ 0xB2, 0x01, {0x70} },
	{ 0x26, 0x01, {0x02} },
	{ 0x3D, 0x01, {0xA1} },
	{ 0x1B, 0x01, {0x01} },
	{ 0x52, 0x01, {0x5F} },
	{ 0x04, 0x01, {0x2A} },
	{ 0x06, 0x01, {0x2A} },
	{ 0x30, 0x01, {0xF7} },
	{ 0x29, 0x01, {0x00} },
	{ 0x2A, 0x01, {0x14} },
	{ 0x38, 0x01, {0xA8} },
	{ 0x52, 0x01, {0x5F} },
	{ 0x54, 0x01, {0x28} },
	{ 0x55, 0x01, {0x25} },
	{ 0x1A, 0x01, {0x50} },
	{ 0xFF, 0x03, {0x98, 0x81, 0x06} },
	{ 0x01, 0x01, {0x03} },
	{ 0x2B, 0x01, {0x0A} },
	{ 0xFF, 0x03, {0x98, 0x81, 0x02} },
	{ 0x01, 0x01, {0x50} },
	{ 0x15, 0x01, {0x10} },
	{ 0x42, 0x01, {0x2F} },
	{ 0x57, 0x01, {0x00} },
	{ 0x58, 0x01, {0x14} },
	{ 0x59, 0x01, {0x22} },
	{ 0x5A, 0x01, {0x13} },
	{ 0x5B, 0x01, {0x16} },
	{ 0x5C, 0x01, {0x29} },
	{ 0x5D, 0x01, {0x1D} },
	{ 0x5E, 0x01, {0x1F} },
	{ 0x5F, 0x01, {0x85} },
	{ 0x60, 0x01, {0x1E} },
	{ 0x61, 0x01, {0x2A} },
	{ 0x62, 0x01, {0x72} },
	{ 0x63, 0x01, {0x19} },
	{ 0x64, 0x01, {0x17} },
	{ 0x65, 0x01, {0x4A} },
	{ 0x66, 0x01, {0x1F} },
	{ 0x67, 0x01, {0x27} },
	{ 0x68, 0x01, {0x4D} },
	{ 0x69, 0x01, {0x5C} },
	{ 0x6A, 0x01, {0x30} },
	{ 0x6B, 0x01, {0x00} },
	{ 0x6C, 0x01, {0x14} },
	{ 0x6D, 0x01, {0x22} },
	{ 0x6E, 0x01, {0x13} },
	{ 0x6F, 0x01, {0x16} },
	{ 0x70, 0x01, {0x29} },
	{ 0x71, 0x01, {0x1D} },
	{ 0x72, 0x01, {0x1F} },
	{ 0x73, 0x01, {0x85} },
	{ 0x74, 0x01, {0x1E} },
	{ 0x75, 0x01, {0x2A} },
	{ 0x76, 0x01, {0x72} },
	{ 0x77, 0x01, {0x19} },
	{ 0x78, 0x01, {0x17} },
	{ 0x79, 0x01, {0x4A} },
	{ 0x7A, 0x01, {0x1F} },
	{ 0x7B, 0x01, {0x27} },
	{ 0x7C, 0x01, {0x4D} },
	{ 0x7D, 0x01, {0x5C} },
	{ 0x7E, 0x01, {0x30} },
	{ 0xFF, 0x03, {0x98, 0x81, 0x00} },
	{ 0x35, 0x01, {0x00} },
	{ 0x11, 0x01, {0x00} },
	{ REGFLAG_DELAY, 120, {} },
	{ 0x29, 0x01, {0x00} },
	{ REGFLAG_DELAY, 20, {} },
	{ REGFLAG_END_OF_TABLE, 0x00, {} },
};

#if 0
static struct LCM_setting_table lcm_set_window[] = {
	{0x2A, 4, {0x00, 0x00, (FRAME_WIDTH >> 8), (FRAME_WIDTH & 0xFF)} },
	{0x2B, 4, {0x00, 0x00, (FRAME_HEIGHT >> 8), (FRAME_HEIGHT & 0xFF)} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};
#endif
#if 0
static struct LCM_setting_table lcm_sleep_out_setting[] = {
	/* Sleep Out */
	{0x11, 1, {0x00} },
	{REGFLAG_DELAY, 120, {} },

	/* Display ON */
	{0x29, 1, {0x00} },
	{REGFLAG_DELAY, 20, {} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static struct LCM_setting_table lcm_deep_sleep_mode_in_setting[] = {
	/* Display off sequence */
	{0x28, 1, {0x00} },
	{REGFLAG_DELAY, 20, {} },

	/* Sleep Mode On */
	{0x10, 1, {0x00} },
	{REGFLAG_DELAY, 120, {} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};
#endif
static struct LCM_setting_table bl_level[] = {
	{0x51, 1, {0xFF} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static unsigned int lcm_m6_init_count;
static unsigned int lcm_m6_backlight_log_this_call;
static unsigned int lcm_m6_backlight_log_count;
static unsigned int lcm_m6_backlight_last_level = 0xffffffff;

static const char *lcm_m6_table_name(struct LCM_setting_table *table)
{
	if (table == init_setting)
		return "init";
	if (table == lcm_suspend_setting)
		return "suspend";
	if (table == bl_level)
		return "backlight";
	return "custom";
}

static void lcm_m6_log_table_cmd(const char *tag, unsigned int index,
	struct LCM_setting_table *entry, unsigned char force_update)
{
	unsigned int cmd = entry->cmd;

	if (cmd == REGFLAG_DELAY || cmd == REGFLAG_UDELAY ||
	    cmd == REGFLAG_END_OF_TABLE) {
		LCM_LOGI("M6 LCM table[%s] idx=%u flag=0x%04x count=%u force=%u\n",
			tag, index, cmd, entry->count, force_update);
		return;
	}

	LCM_LOGI("M6 LCM table[%s] idx=%u cmd=0x%02x count=%u p=%02x %02x %02x %02x force=%u\n",
		tag, index, cmd, entry->count,
		entry->para_list[0], entry->para_list[1],
		entry->para_list[2], entry->para_list[3],
		force_update);
}

#ifndef BUILD_LK
static void lcm_m6_select_page_for_trace(unsigned char page)
{
	unsigned char page_cmd[3] = { 0x98, 0x81, page };

	dsi_set_cmdq_V2(0xFF, sizeof(page_cmd), page_cmd, 1);
	MDELAY(2);
}

static void lcm_m6_trace_page5_2a(const char *tag, unsigned int index,
	const char *phase, unsigned char current_page)
{
	unsigned char read_buf[4];
	unsigned int read_count;

	if (current_page != 5)
		lcm_m6_select_page_for_trace(5);

	memset(read_buf, 0xA5, sizeof(read_buf));
	read_count = read_reg_v2(0x2A, read_buf, 1);
	LCM_LOGI("M6 LCM page5_2a_trace[%s] idx=%u phase=%s current_page=%u read=%02x %02x %02x %02x read_count=%u\n",
		tag, index, phase, current_page, read_buf[0], read_buf[1],
		read_buf[2], read_buf[3], read_count);

	if (current_page != 5)
		lcm_m6_select_page_for_trace(current_page);
}
#endif

static void push_table(void *cmdq, struct LCM_setting_table *table,
	unsigned int count, unsigned char force_update)
{
	unsigned int i;
	unsigned cmd;
	unsigned int log_table = 1;
	const char *tag = lcm_m6_table_name(table);
#ifndef BUILD_LK
	unsigned char m6_current_page = 0xFF;
	unsigned int m6_trace_init = (table == init_setting);
#endif

	if (table == bl_level)
		log_table = lcm_m6_backlight_log_this_call;

	if (log_table)
		LCM_LOGI("M6 LCM push_table start tag=%s count=%u force=%u cmdq=%p\n",
			tag, count, force_update, cmdq);
	for (i = 0; i < count; i++) {
		unsigned long start_jiffies = jiffies;

		cmd = table[i].cmd;
		if (log_table)
			lcm_m6_log_table_cmd(tag, i, &table[i], force_update);

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
#ifndef BUILD_LK
		if (m6_trace_init && cmd == 0xFF && table[i].count == 3 &&
		    table[i].para_list[0] == 0x98 &&
		    table[i].para_list[1] == 0x81)
			m6_current_page = table[i].para_list[2];
		if (m6_trace_init) {
			if (i == 12)
				lcm_m6_trace_page5_2a(tag, i,
					"after-page5-2a-write", m6_current_page);
			else if (i == 17)
				lcm_m6_trace_page5_2a(tag, i,
					"after-page5-cluster", m6_current_page);
			else if (i == 65)
				lcm_m6_trace_page5_2a(tag, i,
					"after-page0-select", m6_current_page);
			else if (i == 69)
				lcm_m6_trace_page5_2a(tag, i,
					"after-display-on", m6_current_page);
		}
#endif
		if (log_table)
			LCM_LOGI("M6 LCM table[%s] idx=%u done cmd=0x%04x elapsed_ms=%u\n",
				tag, i, cmd, jiffies_to_msecs(jiffies - start_jiffies));
	}
	if (log_table)
		LCM_LOGI("M6 LCM push_table end tag=%s count=%u force=%u cmdq=%p\n",
			tag, count, force_update, cmdq);
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
	params->dsi.mode = BURST_VDO_MODE;
	params->dsi.switch_mode = CMD_MODE;
	lcm_dsi_mode = BURST_VDO_MODE;
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

	params->dsi.vertical_sync_active = 20;
	params->dsi.vertical_backporch = 24;
	params->dsi.vertical_frontporch = 64;
	params->dsi.vertical_frontporch_for_low_power = 540;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 20;
	params->dsi.horizontal_backporch = 80;
	params->dsi.horizontal_frontporch = 100;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.ssc_disable = 1;
#ifndef CONFIG_FPGA_EARLY_PORTING
#if (LCM_DSI_CMD_MODE)
	params->dsi.PLL_CLOCK = 230;	/* this value must be in MTK suggested table */
#else
	params->dsi.PLL_CLOCK = 230;	/* this value must be in MTK suggested table */
#endif
	params->dsi.PLL_CK_CMD = 230;
	params->dsi.PLL_CK_VDO = 230;
#else
	params->dsi.pll_div1 = 0;
	params->dsi.pll_div2 = 0;
	params->dsi.fbk_div = 0x1;
#endif
	params->dsi.clk_lp_per_line_enable = 0;
	/*
	 * Disable runtime ESD polling for bring-up.  On meizu_M6 bootdiag the
	 * ili9881p check path repeatedly times out and forces panel recovery,
	 * which is followed by CMDQ/GED fence stalls during Android boot.
	 */
	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 1;
	params->dsi.lcm_esd_check_table[0].cmd = 0x0A;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9C;
	LCM_LOGI("M6 LCM params mode=%d lanes=%u size=%ux%u v=%u/%u/%u/%u h=%u/%u/%u pll=%u esd=%u/%u esd0=0x%02x/0x%02x\n",
		params->dsi.mode, params->dsi.LANE_NUM,
		params->width, params->height,
		params->dsi.vertical_sync_active,
		params->dsi.vertical_backporch,
		params->dsi.vertical_frontporch,
		params->dsi.vertical_active_line,
		params->dsi.horizontal_sync_active,
		params->dsi.horizontal_backporch,
		params->dsi.horizontal_frontporch,
		params->dsi.PLL_CLOCK,
		params->dsi.esd_check_enable,
		params->dsi.customization_esd_check_enable,
		params->dsi.lcm_esd_check_table[0].cmd,
		params->dsi.lcm_esd_check_table[0].para_list[0]);

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
	params->corner_pattern_width = 32;
	params->corner_pattern_height = 32;
#endif
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

/* extern int mt8193_i2c_write(u16 addr, u32 data); */
/* extern int mt8193_i2c_read(u16 addr, u32 *data); */

/* #define TPS65132_write_byte(add, data)  mt8193_i2c_write(add, data) */
/* #define TPS65132_read_byte(add)  mt8193_i2c_read(add) */

#endif
#endif


static void lcm_init_power(void)
{
	LCM_LOGI("M6 LCM init_power\n");
}

static void lcm_suspend_power(void)
{
	LCM_LOGI("M6 LCM suspend_power\n");
}

static void lcm_resume_power(void)
{
	LCM_LOGI("M6 LCM resume_power\n");
}

static void lcm_init(void)
{
	unsigned char cmd = 0x0;
	unsigned char data = 0xFF;
	unsigned int seq;
#ifndef CONFIG_FPGA_EARLY_PORTING
	int ret = 0;
#endif

	seq = ++lcm_m6_init_count;
	LCM_LOGI("M6 LCM init start seq=%u mode=%d init_count=%u table_count=%u\n",
		seq, lcm_dsi_mode, lcm_m6_init_count,
		(unsigned int)(sizeof(init_setting) / sizeof(struct LCM_setting_table)));
#if !defined(CONFIG_FPGA_EARLY_PORTING) && !defined(BUILD_LK)
	LCM_LOGI("M6 LCM init seq=%u tps_client=%p\n", seq, tps65132_i2c_client);
#endif

	cmd = 0x00;
	data = 0x0F;

	LCM_LOGI("M6 LCM init seq=%u reset=0 pre-bias\n", seq);
	SET_RESET_PIN(0);

#ifndef CONFIG_FPGA_EARLY_PORTING
#ifdef CONFIG_MTK_LEGACY
	mt_set_gpio_mode(GPIO_65132_EN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_65132_EN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_65132_EN, GPIO_OUT_ONE);
#else
	LCM_LOGI("M6 LCM init seq=%u bias_enp=1\n", seq);
	set_gpio_lcd_enp(1);
	MDELAY(5);
	LCM_LOGI("M6 LCM init seq=%u bias_enn=1\n", seq);
	set_gpio_lcd_enn(1);
#endif
	MDELAY(5);
#ifdef BUILD_LK
	ret = TPS65132_write_byte(cmd, data);
#else
#if !defined(CONFIG_ARCH_MT6797)
	ret = tps65132_write_bytes(cmd, data);
#endif
#endif

	if (ret < 0)
		LCM_LOGI("ili9881p_hd_dsi_txd----tps6132----cmd=%0x--i2c write error----\n", cmd);
	else
		LCM_LOGI("ili9881p_hd_dsi_txd----tps6132----cmd=%0x--i2c write success----\n", cmd);
	LCM_LOGI("M6 LCM init seq=%u tps reg0 ret=%d value=0x%02x\n",
		seq, ret, data);

	cmd = 0x01;
	data = 0x0F;

#ifdef BUILD_LK
	ret = TPS65132_write_byte(cmd, data);
#else
#if !defined(CONFIG_ARCH_MT6797)
	ret = tps65132_write_bytes(cmd, data);
#endif
#endif

	if (ret < 0)
		LCM_LOGI("ili9881p_hd_dsi_txd----tps6132----cmd=%0x--i2c write error----\n", cmd);
	else
		LCM_LOGI("ili9881p_hd_dsi_txd----tps6132----cmd=%0x--i2c write success----\n", cmd);
	LCM_LOGI("M6 LCM init seq=%u tps reg1 ret=%d value=0x%02x\n",
		seq, ret, data);
#if !defined(BUILD_LK) && !defined(CONFIG_ARCH_MT6797)
	tps65132_read_byte(0x00);
	tps65132_read_byte(0x01);
#endif

#endif
	LCM_LOGI("M6 LCM init seq=%u reset=1 delay=1ms\n", seq);
	SET_RESET_PIN(1);
	MDELAY(1);
	LCM_LOGI("M6 LCM init seq=%u reset=0 delay=2ms\n", seq);
	SET_RESET_PIN(0);
	MDELAY(2);

	LCM_LOGI("M6 LCM init seq=%u reset=1 delay=6ms\n", seq);
	SET_RESET_PIN(1);
	MDELAY(6);
	if (lcm_dsi_mode == CMD_MODE) {
		LCM_LOGI("ili9881p_hd_dsi_txd----not support ----lcm mode\n");
		LCM_LOGI("M6 LCM init seq=%u skip init table cmd mode=%d\n",
			seq, lcm_dsi_mode);
	} else {
		LCM_LOGI("M6 LCM init seq=%u push init table start mode=%d\n",
			seq, lcm_dsi_mode);
		push_table(NULL, init_setting, sizeof(init_setting) / sizeof(struct LCM_setting_table), 1);
		LCM_LOGI("M6 LCM init seq=%u push init table end mode=%d\n",
			seq, lcm_dsi_mode);
		LCM_LOGI("ili9881p_hd_dsi_txd----tps6132----lcm mode = vdo mode :%d----\n", lcm_dsi_mode);
	}
	LCM_LOGI("M6 LCM init end seq=%u mode=%d\n", seq, lcm_dsi_mode);
}

static void lcm_suspend(void)
{
	LCM_LOGI("M6 LCM suspend start mode=%d\n", lcm_dsi_mode);
	push_table(NULL, lcm_suspend_setting, sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table), 1);
	MDELAY(10);
#ifndef CONFIG_FPGA_EARLY_PORTING
#ifdef CONFIG_MTK_LEGACY
	mt_set_gpio_mode(GPIO_65132_EN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_65132_EN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_65132_EN, GPIO_OUT_ZERO);
#else
	LCM_LOGI("M6 LCM suspend bias_enn=0\n");
	set_gpio_lcd_enn(0);
	LCM_LOGI("M6 LCM suspend bias_enp=0\n");
	set_gpio_lcd_enp(0);
#endif
#endif
	/*SET_RESET_PIN(0);*/
	LCM_LOGI("M6 LCM suspend end mode=%d\n", lcm_dsi_mode);
}

static void lcm_resume(void)
{
	LCM_LOGI("M6 LCM resume start mode=%d\n", lcm_dsi_mode);
	lcm_init();
	LCM_LOGI("M6 LCM resume end mode=%d\n", lcm_dsi_mode);
}

static void lcm_update(unsigned int x, unsigned int y, unsigned int width, unsigned int height)
{
	unsigned int x0 = x;
	unsigned int y0 = y;
	unsigned int x1 = x0 + width - 1;
	unsigned int y1 = y0 + height - 1;

	unsigned char x0_MSB = ((x0 >> 8) & 0xFF);
	unsigned char x0_LSB = (x0 & 0xFF);
	unsigned char x1_MSB = ((x1 >> 8) & 0xFF);
	unsigned char x1_LSB = (x1 & 0xFF);
	unsigned char y0_MSB = ((y0 >> 8) & 0xFF);
	unsigned char y0_LSB = (y0 & 0xFF);
	unsigned char y1_MSB = ((y1 >> 8) & 0xFF);
	unsigned char y1_LSB = (y1 & 0xFF);

	unsigned int data_array[16];

	data_array[0] = 0x00053902;
	data_array[1] = (x1_MSB << 24) | (x0_LSB << 16) | (x0_MSB << 8) | 0x2a;
	data_array[2] = (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x00053902;
	data_array[1] = (y1_MSB << 24) | (y0_LSB << 16) | (y0_MSB << 8) | 0x2b;
	data_array[2] = (y1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x002c3909;
	dsi_set_cmdq(data_array, 1, 0);
}

static unsigned int lcm_compare_id(void)
{
	unsigned int id = 0;
	unsigned char buffer[2];
	unsigned int array[16];
	struct LCM_setting_table switch_table_page6[] = {
		{ 0xFF, 0x03, {0x98, 0x81, 0x06} }
	};
	struct LCM_setting_table switch_table_page0[] = {
		{ 0xFF, 0x03, {0x98, 0x81, 0x00} }
	};

	SET_RESET_PIN(1);
	MDELAY(10);
	SET_RESET_PIN(0);
	MDELAY(10);

	SET_RESET_PIN(1);
	MDELAY(50);

	push_table(NULL, switch_table_page6, sizeof(switch_table_page6) / sizeof(struct LCM_setting_table), 1);

	array[0] = 0x00013700;
	dsi_set_cmdq(array, 1, 1);

	read_reg_v2(0xF2, buffer, 1);
	id = buffer[0];

	LCM_LOGI("%s,ili9881p_hd_dsi_txd_f2_id=0x%08x\n", __func__, id);
	push_table(NULL, switch_table_page0, sizeof(switch_table_page0) / sizeof(struct LCM_setting_table), 1);

	if (id == 0x10)
		return 1;
	else
		return 0;

}


/* return TRUE: need recovery */
/* return FALSE: No need recovery */
static unsigned int lcm_esd_check(void)
{
	/*
	 * Keep the panel out of the ESD recovery loop while validating this
	 * port.  The original 0x53 read can report false failures/timeouts on
	 * this target and reset the panel under SurfaceFlinger/BootAnimation.
	 */
	return FALSE;
}

#ifndef BUILD_LK
struct m6_lcm_dcs_diag_read {
	unsigned char cmd;
	unsigned char len;
	const char *name;
};

static void lcm_m6_diag_read_dcs_registers(void)
{
	static const struct m6_lcm_dcs_diag_read reads[] = {
		{ 0x04, 3, "display_id" },
		{ 0x09, 4, "display_status" },
		{ 0x0A, 1, "power_mode" },
		{ 0x0B, 1, "madctl" },
		{ 0x0C, 1, "pixel_format" },
		{ 0x0D, 1, "image_mode" },
		{ 0x51, 1, "brightness" },
		{ 0x53, 1, "ctrl_display" },
		{ 0x55, 1, "cabc" },
		{ 0x2A, 4, "column_addr" },
		{ 0x2B, 4, "page_addr" },
		{ 0xDA, 1, "id1" },
		{ 0xDB, 1, "id2" },
		{ 0xDC, 1, "id3" },
	};
	static unsigned int diag_count;
	unsigned char read_buf[4];
	unsigned int read_count;
	unsigned int i;

	if (diag_count >= 8)
		return;

	diag_count++;
	for (i = 0; i < ARRAY_SIZE(reads); i++) {
		memset(read_buf, 0xA5, sizeof(read_buf));
		read_count = read_reg_v2(reads[i].cmd, read_buf, reads[i].len);
		LCM_LOGI("M6 LCM ATA dcs[%u] name=%s cmd=0x%02x len=%u read=%02x %02x %02x %02x read_count=%u\n",
			diag_count, reads[i].name, reads[i].cmd, reads[i].len,
			read_buf[0], read_buf[1], read_buf[2], read_buf[3],
			read_count);
	}
}

struct m6_lcm_stock_page_diag_read {
	unsigned char page;
	unsigned char cmd;
	unsigned char len;
	const char *name;
};

static void lcm_m6_diag_select_stock_page(unsigned char page)
{
	unsigned char page_cmd[3] = { 0x98, 0x81, page };

	dsi_set_cmdq_V2(0xFF, sizeof(page_cmd), page_cmd, 1);
	MDELAY(2);
	LCM_LOGI("M6 LCM stock_pages select page=%u\n", page);
}

void lcm_m6_diag_read_stock_pages(void)
{
	static const struct m6_lcm_stock_page_diag_read reads[] = {
		{ 0, 0x35, 1, "te_on" },
		{ 0, 0x36, 1, "madctl" },
		{ 0, 0x3A, 1, "pixel_format" },
		{ 0, 0x51, 1, "brightness" },
		{ 0, 0x53, 1, "ctrl_display" },
		{ 0, 0x55, 1, "cabc" },
		{ 1, 0x44, 1, "page1_vcom_gip" },
		{ 5, 0xB2, 1, "page5_b2" },
		{ 5, 0x26, 1, "page5_26" },
		{ 5, 0x3D, 1, "page5_3d" },
		{ 5, 0x1B, 1, "page5_1b" },
		{ 5, 0x52, 1, "page5_52" },
		{ 5, 0x04, 1, "page5_04" },
		{ 5, 0x06, 1, "page5_06" },
		{ 5, 0x30, 1, "page5_30" },
		{ 5, 0x29, 1, "page5_29" },
		{ 5, 0x2A, 1, "page5_2a" },
		{ 5, 0x38, 1, "page5_38" },
		{ 5, 0x54, 1, "page5_54" },
		{ 5, 0x55, 1, "page5_55" },
		{ 5, 0x1A, 1, "page5_1a" },
		{ 6, 0x01, 1, "page6_01" },
		{ 6, 0x2B, 1, "page6_2b" },
		{ 6, 0xF2, 1, "page6_f2_compare_id" },
		{ 2, 0x01, 1, "page2_01" },
		{ 2, 0x15, 1, "page2_15" },
		{ 2, 0x42, 1, "page2_42" },
		{ 2, 0x57, 1, "page2_gamma_57" },
		{ 2, 0x58, 1, "page2_gamma_58" },
		{ 2, 0x59, 1, "page2_gamma_59" },
		{ 2, 0x5A, 1, "page2_gamma_5a" },
		{ 2, 0x5B, 1, "page2_gamma_5b" },
		{ 2, 0x5C, 1, "page2_gamma_5c" },
		{ 2, 0x5D, 1, "page2_gamma_5d" },
		{ 2, 0x5E, 1, "page2_gamma_5e" },
		{ 2, 0x5F, 1, "page2_gamma_5f" },
		{ 2, 0x60, 1, "page2_gamma_60" },
		{ 2, 0x61, 1, "page2_gamma_61" },
		{ 2, 0x62, 1, "page2_gamma_62" },
		{ 2, 0x63, 1, "page2_gamma_63" },
		{ 2, 0x64, 1, "page2_gamma_64" },
		{ 2, 0x65, 1, "page2_gamma_65" },
		{ 2, 0x66, 1, "page2_gamma_66" },
		{ 2, 0x67, 1, "page2_gamma_67" },
		{ 2, 0x68, 1, "page2_gamma_68" },
		{ 2, 0x69, 1, "page2_gamma_69" },
		{ 2, 0x6A, 1, "page2_gamma_6a" },
		{ 2, 0x6B, 1, "page2_gamma_6b" },
		{ 2, 0x6C, 1, "page2_gamma_6c" },
		{ 2, 0x6D, 1, "page2_gamma_6d" },
		{ 2, 0x6E, 1, "page2_gamma_6e" },
		{ 2, 0x6F, 1, "page2_gamma_6f" },
		{ 2, 0x70, 1, "page2_gamma_70" },
		{ 2, 0x71, 1, "page2_gamma_71" },
		{ 2, 0x72, 1, "page2_gamma_72" },
		{ 2, 0x73, 1, "page2_gamma_73" },
		{ 2, 0x74, 1, "page2_gamma_74" },
		{ 2, 0x75, 1, "page2_gamma_75" },
		{ 2, 0x76, 1, "page2_gamma_76" },
		{ 2, 0x77, 1, "page2_gamma_77" },
		{ 2, 0x78, 1, "page2_gamma_78" },
		{ 2, 0x79, 1, "page2_gamma_79" },
		{ 2, 0x7A, 1, "page2_gamma_7a" },
		{ 2, 0x7B, 1, "page2_gamma_7b" },
		{ 2, 0x7C, 1, "page2_gamma_7c" },
		{ 2, 0x7D, 1, "page2_gamma_7d" },
		{ 2, 0x7E, 1, "page2_gamma_7e" },
	};
	static unsigned int diag_count;
	unsigned char current_page = 0xFF;
	unsigned char read_buf[4];
	unsigned int read_count;
	unsigned int i;

	if (diag_count >= 4)
		return;

	diag_count++;
	LCM_LOGI("M6 LCM stock_pages[%u] begin reads=%u\n",
		diag_count, (unsigned int)ARRAY_SIZE(reads));
	for (i = 0; i < ARRAY_SIZE(reads); i++) {
		if (current_page != reads[i].page) {
			lcm_m6_diag_select_stock_page(reads[i].page);
			current_page = reads[i].page;
		}
		memset(read_buf, 0xA5, sizeof(read_buf));
		read_count = read_reg_v2(reads[i].cmd, read_buf, reads[i].len);
		LCM_LOGI("M6 LCM stock_pages[%u] page=%u name=%s cmd=0x%02x len=%u read=%02x %02x %02x %02x read_count=%u\n",
			diag_count, reads[i].page, reads[i].name, reads[i].cmd,
			reads[i].len, read_buf[0], read_buf[1], read_buf[2],
			read_buf[3], read_count);
	}
	lcm_m6_diag_select_stock_page(0);
	LCM_LOGI("M6 LCM stock_pages[%u] end reset_page=0\n", diag_count);
}
#endif

static unsigned int lcm_ata_check(unsigned char *buffer)
{
#ifndef BUILD_LK
	unsigned int ret = 0;
	unsigned int x0 = FRAME_WIDTH / 4;
	unsigned int x1 = FRAME_WIDTH * 3 / 4;

	unsigned char x0_MSB = ((x0 >> 8) & 0xFF);
	unsigned char x0_LSB = (x0 & 0xFF);
	unsigned char x1_MSB = ((x1 >> 8) & 0xFF);
	unsigned char x1_LSB = (x1 & 0xFF);

	unsigned int data_array[3];
	unsigned char read_buf[4];
	unsigned int read_count;

	LCM_LOGI("ATA check size = 0x%x,0x%x,0x%x,0x%x\n", x0_MSB, x0_LSB, x1_MSB, x1_LSB);
	data_array[0] = 0x0005390A;	/* HS packet */
	data_array[1] = (x1_MSB << 24) | (x0_LSB << 16) | (x0_MSB << 8) | 0x2a;
	data_array[2] = (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x00043700;	/* read id return two byte,version and id */
	dsi_set_cmdq(data_array, 1, 1);

	memset(read_buf, 0xA5, sizeof(read_buf));
	read_count = read_reg_v2(0x2A, read_buf, 4);

	if ((read_buf[0] == x0_MSB) && (read_buf[1] == x0_LSB)
	    && (read_buf[2] == x1_MSB) && (read_buf[3] == x1_LSB))
		ret = 1;
	else
		ret = 0;
	LCM_LOGI("M6 LCM ATA expected=%02x %02x %02x %02x read=%02x %02x %02x %02x read_count=%u ret=%u\n",
		x0_MSB, x0_LSB, x1_MSB, x1_LSB,
		read_buf[0], read_buf[1], read_buf[2], read_buf[3],
		read_count, ret);
	lcm_m6_diag_read_dcs_registers();

	x0 = 0;
	x1 = FRAME_WIDTH - 1;

	x0_MSB = ((x0 >> 8) & 0xFF);
	x0_LSB = (x0 & 0xFF);
	x1_MSB = ((x1 >> 8) & 0xFF);
	x1_LSB = (x1 & 0xFF);

	data_array[0] = 0x0005390A;	/* HS packet */
	data_array[1] = (x1_MSB << 24) | (x0_LSB << 16) | (x0_MSB << 8) | 0x2a;
	data_array[2] = (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);
	return ret;
#else
	return 0;
#endif
}

static void lcm_setbacklight_cmdq(void *handle, unsigned int level)
{
	unsigned int delta;
	unsigned int log_this = 0;

	if (lcm_m6_backlight_last_level == 0xffffffff)
		delta = 0xffffffff;
	else if (level > lcm_m6_backlight_last_level)
		delta = level - lcm_m6_backlight_last_level;
	else
		delta = lcm_m6_backlight_last_level - level;

	bl_level[0].para_list[0] = level;

	if (lcm_m6_backlight_log_count < 8 || level == 0 ||
	    level == BL_MIN_LEVEL || level == 255 || delta >= 32)
		log_this = 1;

	lcm_m6_backlight_log_this_call = log_this;
	if (log_this) {
		lcm_m6_backlight_log_count++;
		LCM_LOGI("%s,ili9881p_hd_dsi_txd backlight: level = %d\n",
			__func__, level);
		LCM_LOGI("M6 LCM backlight handle=%p request=%u dcs51=0x%02x min=%u count=%u delta=%u\n",
			handle, level, bl_level[0].para_list[0], BL_MIN_LEVEL,
			lcm_m6_backlight_log_count, delta);
		LCM_LOGI("M6 DISPLAY truth[backlight-write][panel]: handle=%p request=%u dcs51=0x%02x min=%u count=%u delta=%u mode=%d\n",
			handle, level, bl_level[0].para_list[0], BL_MIN_LEVEL,
			lcm_m6_backlight_log_count, delta, lcm_dsi_mode);
	}
	lcm_m6_backlight_last_level = level;

	push_table(handle, bl_level, sizeof(bl_level) / sizeof(struct LCM_setting_table), 1);
	lcm_m6_backlight_log_this_call = 0;
}

static void *lcm_switch_mode(int mode)
{
#ifndef BUILD_LK
/* customization: 1. V2C config 2 values, C2V config 1 value; 2. config mode control register */
	if (mode == 0) {	/* V2C */
		lcm_switch_mode_cmd.mode = CMD_MODE;
		lcm_switch_mode_cmd.addr = 0xBB;	/* mode control addr */
		lcm_switch_mode_cmd.val[0] = 0x13;	/* enabel GRAM firstly, ensure writing one frame to GRAM */
		lcm_switch_mode_cmd.val[1] = 0x10;	/* disable video mode secondly */
	} else {		/* C2V */
		lcm_switch_mode_cmd.mode = BURST_VDO_MODE;
		lcm_switch_mode_cmd.addr = 0xBB;
		lcm_switch_mode_cmd.val[0] = 0x03;	/* disable GRAM and enable video mode */
	}
	return (void *)(&lcm_switch_mode_cmd);
#else
	return NULL;
#endif
}


LCM_DRIVER ili9881p_hd_dsi_txd_lcm_drv = {
	.name = "ili9881p_hd_dsi_txd",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.compare_id = lcm_compare_id,
	.init_power = lcm_init_power,
	.resume_power = lcm_resume_power,
	.suspend_power = lcm_suspend_power,
	.esd_check = lcm_esd_check,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = lcm_ata_check,
	.update = lcm_update,
	.switch_mode = lcm_switch_mode,
};
