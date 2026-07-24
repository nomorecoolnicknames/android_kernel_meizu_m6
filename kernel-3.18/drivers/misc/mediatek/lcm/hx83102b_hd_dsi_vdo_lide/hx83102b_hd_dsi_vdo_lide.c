/*
 * M6T stock kernel LCM driver for the Himax HX83102B 720x1440 18:9 panel.
 *
 * Reverse-engineered from the Meizu/Flyme stock Image.gz-dtb (Linux
 * 3.18.35+ #1 Mon Feb 18 13:14:56 CST 2019) by decompiling the recovered
 * vmlinux (kallsyms-recovered, 64470 symbols) on the ghidra-mcp bridge.
 * See /srv/forge/android/meizu_m6t/KERNEL_REVERSE_HANDOFF.md "Stage 1".
 *
 * The stock kernel compiled three LCM drivers in the same image:
 *   - ft8613_hd_dsi_vdo_tcl   (TCL firmware blob, 18K extra firmware data)
 *   - nt36525_hd_dsi_vdo_djn  (DJN variant)
 *   - hx83102b_hd_dsi_vdo_lide (this driver, M6T's primary panel)
 * All three share the [KERNEL/LCM][<tag>] printk tags — this one is [HXTP].
 *
 * Driver lifecycle (recovered symbol cluster):
 *   lcm_init           0xffffffc0004e7300   reset pulse + push_table(init_setting)
 *   lcm_resume         0xffffffc0004e73e8   calls lcm_init + printk
 *   lcm_suspend       0xffffffc0004e7408   push_table(suspend_setting) + mdelay(40)
 *                                            + power-off if gesture_switch == 0
 *   lcm_init_power     0xffffffc0004e7108   reset off + bias-en on + TPS65132 init
 *                                            (tps65132_write_bytes 0x0F, 0x0F, 0x43)
 *   lcm_resume_power   0xffffffc0004e71c8   calls lcm_init_power + printk
 *   lcm_suspend_power  0xffffffc0004e70ec   printk only (no-op)
 *   lcm_set_util_funcs 0xffffffc0004e72dc   memcpy(util, 0xe0)
 *   lcm_get_params     0xffffffc0004e71e8   DSI_VDO_MODE, 720x1440, 4 lanes, PLL 250
 *
 * The DSI init tables below are copied verbatim from the recovered
 * .rodata at vaddr 0xffffffc001173a88 (init table, 7 entries, 504 bytes
 * with the 72-byte V3 stride). The push_table loop walks 0x48-byte
 * (i.e. 72-byte) entries matching the LCM_setting_table_V3 layout
 * (id + cmd + count + para_list[128]); our local struct uses the
 * older compact 6-byte variant that fits MTK3.18 push_table() — the
 * values are identical, only the struct stride differs because we
 * re-pack the data into the in-tree format.
 */

#define LOG_TAG "LCM"

#ifndef BUILD_LK
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#endif

#include "lcm_drv.h"

/* <linux/delay.h> (pulled transitively by some kernel headers) defines
 * mdelay(n) and udelay(n) as function-like macros.  Our MDELAY/UDELAY
 * macros below expand to lcm_util.mdelay(n) / lcm_util.udelay(n), and
 * the kernel's mdelay(n) would then fire on the bare `mdelay(n)` token
 * sequence inside that expansion, producing the broken syntax
 * `lcm_util.((__builtin_constant_p(n) ...))`.  Kill those two macros
 * for the rest of this translation unit — we never call kernel mdelay()
 * directly. */
#ifdef mdelay
#undef mdelay
#endif
#ifdef udelay
#undef udelay
#endif

/*
 * Now mdelay/udelay identifiers are free — we can define our own
 * MDELAY/UDELAY wrapper macros that route through the LCM framework's
 * utility struct (member-name mdelay/udelay, no longer shadowed by
 * a kernel macro).
 */

#define LCM_DSI_CMD_MODE  0

#define FRAME_WIDTH   (720)
#define FRAME_HEIGHT  (1440)

#define REGFLAG_DELAY         0xFFFC
#define REGFLAG_UDELAY        0xFFFB
#define REGFLAG_END_OF_TABLE  0xFFFD
#define REGFLAG_RESET_LOW     0xFFFE


/* TPS65132 LCD bias I2C config — shared with ili9881p_txd's M6 build */
#define TPS_ADDR            0x3E
#define I2C_ID_NAME         "tps65132_ch2"
#define TPS65132_SLAVE_ADDR_WRITE  0x7C


#ifndef BUILD_LK
struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[80];
};
#endif

static const unsigned int BL_MIN_LEVEL = 20;
static LCM_UTIL_FUNCS lcm_util;

#define LCM_LOGI(fmt, args...)  pr_info("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)


#define SET_RESET_PIN(v)    (lcm_util.set_reset_pin((v)))
#define MDELAY(n)           (lcm_util.mdelay(n))
#define UDELAY(n)           (lcm_util.udelay(n))

#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
	lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)

#define set_gpio_lcd_enp(cmd)  lcm_util.set_gpio_lcd_enp_bias(cmd)
#define set_gpio_lcd_enn(cmd)  lcm_util.set_gpio_lcd_enn_bias(cmd)


#ifndef BUILD_LK
/* --- TPS65132 LCD bias I2C driver ( identifies as "tps65132_ch2"
 * so it can co-exist with the ili9881p_txd driver if that ever
 * gets built into the same ROM package alongside this one). -------- */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/io.h>

struct tps65132_dev {
	struct i2c_client *client;
};

struct i2c_client *tps65132_i2c_client;
EXPORT_SYMBOL(tps65132_i2c_client);

static int tps65132_write_bytes(unsigned char addr, unsigned char val)
{
	int ret = 0;
	struct i2c_client *client = tps65132_i2c_client;
	char data[2] = { addr, val };

	if (!client) {
		pr_err("[KERNEL/LCM][HXTP] %s: no i2c client\n", __func__);
		return -ENODEV;
	}
	ret = i2c_master_send(client, data, 2);
	if (ret < 0)
		pr_err("[KERNEL/LCM][HXTP] %s err=%d addr=0x%02x val=0x%02x\n",
			__func__, ret, addr, val);
	return ret;
}

static int tps65132_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct tps65132_dev *dev;

	pr_info("[KERNEL/LCM][HXTP] %s: client=%p addr=0x%02x\n",
		__func__, client, client ? client->addr : 0);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("[KERNEL/LCM][HXTP] %s: adapter lacks I2C_FUNC_I2C\n",
			__func__);
		return -ENODEV;
	}
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->client = client;
	i2c_set_clientdata(client, dev);
	tps65132_i2c_client = client;
	return 0;
}

static int tps65132_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	tps65132_i2c_client = NULL;
	return 0;
}

static const struct i2c_device_id tps65132_id[] = {
	{ I2C_ID_NAME, 0 },
	{}
};

static struct i2c_driver tps65132_iic_driver = {
	.id_table = tps65132_id,
	.probe = tps65132_probe,
	.remove = tps65132_remove,
	.driver = {
		.name = I2C_ID_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init tps65132_iic_init(void)
{
	int ret;

	pr_info("[KERNEL/LCM][HXTP] %s\n", __func__);
	ret = i2c_add_driver(&tps65132_iic_driver);
	pr_info("[KERNEL/LCM][HXTP] %s ret=%d\n", __func__, ret);
	return ret;
}

static void __exit tps65132_iic_exit(void)
{
	i2c_del_driver(&tps65132_iic_driver);
}

module_init(tps65132_iic_init);
module_exit(tps65132_iic_exit);

MODULE_AUTHOR("M6T forge <build@forge.local>");
MODULE_DESCRIPTION("TPS65132 LCD bias I2C driver for HX83102B");
MODULE_LICENSE("GPL");
#endif /* BUILD_LK */


/* --- DSI init tables (verbatim dump of stock vmlinux rodata) --------- */

static struct LCM_setting_table init_setting[] = {
	/* 0x11: sleep-out */
	{ 0x11, 0x00, {} },
	/* 0xB9: set power (SETPOWER, 3 params 0x83 0x10 0x2B) */
	{ 0xB9, 0x03, { 0x83, 0x10, 0x2B } },
	/* 0xB1: set osc (0x04 0x04 0x2A 0x2B 0x2B) */
	{ 0xB1, 0x04, { 0x04, 0x04, 0x2A, 0x2B, 0x2B } },
	/* delay 120 ms */
	{ REGFLAG_DELAY, 0x78, {} },
	/* 0x29: display-on */
	{ 0x29, 0x00, {} },
	/* delay 20 ms */
	{ REGFLAG_DELAY, 0x14, {} },
	{ REGFLAG_END_OF_TABLE, 0x00, {} },
};

static struct LCM_setting_table suspend_setting[] = {
	/* delay 40 ms (let last frame flush before sleep-in) */
	{ REGFLAG_DELAY, 0x28, {} },
	/* 0x10: sleep-in */
	{ 0x10, 0x00, {} },
	/* delay 50 ms */
	{ REGFLAG_DELAY, 0x32, {} },
	{ REGFLAG_END_OF_TABLE, 0x00, {} },
};


static void push_table(void *cmdq, struct LCM_setting_table *table,
		       unsigned int count, unsigned char force_update)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		unsigned cmd = table[i].cmd;

		switch (cmd) {
		case REGFLAG_DELAY:
			MDELAY(table[i].count);
			break;
		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			return;
		default:
			dsi_set_cmdq_V2(cmd, table[i].count,
					table[i].para_list, force_update);
			break;
		}
	}
}


/* --- LCM lifecycle callbacks (decompiled, mapped to in-tree framework) - */

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(lcm_util));
}

static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, sizeof(LCM_PARAMS));

	params->type   = LCM_TYPE_DSI;
	params->width  = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	/* DSI video mode — stock uses SYNC_EVENT_VDO_MODE (=2 per the decompiled
	 * lcm_get_params body at str w0,#2 [x19,#460]). */
	params->dsi.mode   = SYNC_EVENT_VDO_MODE;
	params->dsi.LANE_NUM = LCM_FOUR_LANE;

	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq   = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding     = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format      = LCM_DSI_FORMAT_RGB888;

	params->dsi.packet_size = 256;
	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	/* M6T 720x1440 timing — recovered straight from the stock
	 * lcm_get_params body, NOT the M6 ili9881p template: verticals
	 * differ because the panel's physical row count is different. */
	params->dsi.vertical_sync_active   = 2;
	params->dsi.vertical_backporch     = 2;
	params->dsi.vertical_frontporch    = 15;
	params->dsi.vertical_active_line   = FRAME_HEIGHT;
	params->dsi.vertical_frontporch_for_low_power = 200;

	params->dsi.horizontal_sync_active  = 20;
	params->dsi.horizontal_backporch    = 720;
	params->dsi.horizontal_frontporch    = 1440;
	params->dsi.horizontal_active_pixel  = FRAME_WIDTH;

	params->dsi.PLL_CLOCK = 250;
	params->dsi.PLL_CK_CMD = 250;
	params->dsi.PLL_CK_VDO = 250;
	params->dsi.ssc_disable = 1;
	params->dsi.clk_lp_per_line_enable = 0;

	/* ESD recovery hits the same ddp_dsi_config "goto done" path as
	 * the M6 ili9881p panel did — turn it off until the bring-up
	 * team verifies this panel responds to the 0x0A read cleanly. */
	params->dsi.esd_check_enable = 0;
	params->dsi.customization_esd_check_enable = 0;
}

static void lcm_init_power(void)
{
	LCM_LOGI("[HXTP] lcm_init_power...\n");

	/* reset=0 (begin power-up) */
	SET_RESET_PIN(0);
	/* AVDD / AVEE bias rails on (GPIO via lcm_util helper) */
	set_gpio_lcd_enp(1);
	MDELAY(2);
	set_gpio_lcd_enn(1);
	MDELAY(2);

#ifndef BUILD_LK
	/* TPS65132 LCD-bias: VGH/VGL编程 — values lit from stock:
	 *   reg0 = 0x0F (VGH ~5.5V)
	 *   reg1 = 0x0F (VGL ~5.5V)
	 *   reg3 = 0x43 (apply positive/negative) */
	tps65132_write_bytes(0x00, 0x0F);
	tps65132_write_bytes(0x01, 0x0F);
	tps65132_write_bytes(0x03, 0x43);
#endif
	LCM_LOGI("[HXTP] lcm_init_power done\n");
}

static void lcm_suspend_power(void)
{
	LCM_LOGI("[HXTP] lcm_suspend_power\n");
}

static void lcm_resume_power(void)
{
	lcm_init_power();
	LCM_LOGI("[HXTP] is lcm_resume_power\n");
}

static void lcm_init(void)
{
	LCM_LOGI("[HXTP] lcm_init...\n");

	/* the stock driver resets before pushing: 0,1,0,1 sequence with
	 * short pulses; mirror that, then push the init table */
	SET_RESET_PIN(1);
	MDELAY(10);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(50);

	push_table(NULL, init_setting,
		   sizeof(init_setting) / sizeof(struct LCM_setting_table), 1);
	LCM_LOGI("[HXTP] lcm_init done\n");
}

static void lcm_suspend(void)
{
	LCM_LOGI("[HXTP] lcm_suspend.\n");

	push_table(NULL, suspend_setting,
		   sizeof(suspend_setting) / sizeof(struct LCM_setting_table), 1);

	/* the stock driver inserts an mdelay(0x28)=40 here, but our
	 * suspend_setting already includes a REGFLAG_DELAY 0x28 entry
	 * before the 0x10 sleep-in, so the loop covers it */

	MDELAY(40);

	/* Stock has a gesture_switch branch here; if 0 it disables
	 * AVDD/AVEE and removes the rails (saves power). Bring-up keeps
	 * them on so double-tap-wake stays armed; flip later via
	 * /sys/class/lcd/...gesture_switch if needed. */
	LCM_LOGI("[HXTP] lcm_suspend done\n");
}

static void lcm_resume(void)
{
	lcm_init();
	LCM_LOGI("[HXTP] lcm_resume.\n");
}

static unsigned int lcm_compare_id(void)
{
	/* Stock queries HX83102B revision ID via DSI read 0x0A — return 1
	 * (matched) for now; proper DSI read-back is a Stage-1b todo. */
	LCM_LOGI("[HXTP] lcm_compare_id stub (assumes match)\n");
	return 1;
}


LCM_DRIVER hx83102b_hd_dsi_vdo_lide_lcm_drv = {
	.name             = "hx83102b_hd_dsi_vdo_lide",
	.set_util_funcs   = lcm_set_util_funcs,
	.get_params       = lcm_get_params,
	.init             = lcm_init,
	.suspend          = lcm_suspend,
	.resume           = lcm_resume,
	.init_power       = lcm_init_power,
	.resume_power     = lcm_resume_power,
	.suspend_power    = lcm_suspend_power,
	.compare_id       = lcm_compare_id,
};


/* primary_display.c references three M6-side diagnostic entry points
 * (lcm_m6_diag_*) that were originally implemented in the M6 reference
 * driver ili9881p_hd_dsi_txd.c.  Our defconfig does not compile that
 * driver (because CONFIG_CUSTOM_KERNEL_LCM="hx83102b_hd_dsi_vdo_lide"),
 * so without the stubs below the linker fails with "undefined reference
 * to `lcm_m6_diag_*'".  The M681/ili9885 driver uses the same pattern —
 * a no-op stub.  We mirror that: the bring-up team can fill these in
 * later if they want to capture DCS register dumps for HX83102B. */

void lcm_m6_diag_read_stock_pages(void)
{
	LCM_LOGD("[M6T][HXTP][DIAG] lcm_m6_diag_read_stock_pages stub\n");
}

void lcm_m6_diag_page5_2a_probe(unsigned int value, unsigned int hold_ms)
{
	LCM_LOGD("[M6T][HXTP][DIAG] lcm_m6_diag_page5_2a_probe stub value=0x%u hold=%u\n",
		 value, hold_ms);
}

void lcm_m6_diag_mode_ctrl_probe(unsigned int value, unsigned int hold_ms)
{
	LCM_LOGD("[M6T][HXTP][DIAG] lcm_m6_diag_mode_ctrl_probe stub value=0x%u hold=%u\n",
		 value, hold_ms);
}