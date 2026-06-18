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

#include <mt-plat/charging.h>
#include <mt-plat/upmu_common.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <mt-plat/mt_boot.h>
#include <mt-plat/battery_meter.h>
#include <mach/mt_battery_meter.h>
#include <mach/mt_charging.h>
#include <mach/mt_pmic.h>
#include "sy6923d.h"
#include <mach/mt_sleep.h>
#include "mtk_bif_intf.h"
/* ============================================================ // */
/* Define */
/* ============================================================ // */
#define STATUS_OK    0
#define STATUS_UNSUPPORTED    -1
#define STATUS_FAIL -2
#define GETARRAYNUM(array) (sizeof(array)/sizeof(array[0]))
#define VENDOR_SY6923D      (0x2)
#define VENDOR_PSC5415A      (0x7)
unsigned int vendor_id = 0;
/* ============================================================ // */
/* Global variable */
/* ============================================================ // */

#if defined(MTK_WIRELESS_CHARGER_SUPPORT)
#define WIRELESS_CHARGER_EXIST_STATE 0

#if defined(GPIO_PWR_AVAIL_WLC)
/*K.S.?*/
unsigned int wireless_charger_gpio_number = GPIO_PWR_AVAIL_WLC;
#else
unsigned int wireless_charger_gpio_number = 0;
#endif

#endif

#if defined(CONFIG_POWER_EXT) || defined(CONFIG_MTK_FPGA)
#else
static CHARGER_TYPE g_charger_type = CHARGER_UNKNOWN;
#endif

kal_bool charging_type_det_done = KAL_TRUE;

const unsigned int  VBAT_CV_VTH[]=
{
	BATTERY_VOLT_03_500000_V,   BATTERY_VOLT_03_520000_V,	BATTERY_VOLT_03_540000_V,   BATTERY_VOLT_03_560000_V,
	BATTERY_VOLT_03_580000_V,   BATTERY_VOLT_03_600000_V,	BATTERY_VOLT_03_620000_V,   BATTERY_VOLT_03_640000_V,
	BATTERY_VOLT_03_660000_V,	BATTERY_VOLT_03_680000_V,	BATTERY_VOLT_03_700000_V,	BATTERY_VOLT_03_720000_V,
	BATTERY_VOLT_03_740000_V,	BATTERY_VOLT_03_760000_V,	BATTERY_VOLT_03_780000_V,	BATTERY_VOLT_03_800000_V,
	BATTERY_VOLT_03_820000_V,	BATTERY_VOLT_03_840000_V,	BATTERY_VOLT_03_860000_V,	BATTERY_VOLT_03_880000_V,
	BATTERY_VOLT_03_900000_V,	BATTERY_VOLT_03_920000_V,	BATTERY_VOLT_03_940000_V,	BATTERY_VOLT_03_960000_V,
	BATTERY_VOLT_03_980000_V,	BATTERY_VOLT_04_000000_V,	BATTERY_VOLT_04_020000_V,	BATTERY_VOLT_04_040000_V,
	BATTERY_VOLT_04_060000_V,	BATTERY_VOLT_04_080000_V,	BATTERY_VOLT_04_100000_V,	BATTERY_VOLT_04_120000_V,
	BATTERY_VOLT_04_140000_V,   BATTERY_VOLT_04_160000_V,	BATTERY_VOLT_04_180000_V,   BATTERY_VOLT_04_200000_V,
	BATTERY_VOLT_04_220000_V,   BATTERY_VOLT_04_240000_V,	BATTERY_VOLT_04_260000_V,   BATTERY_VOLT_04_280000_V,
	BATTERY_VOLT_04_300000_V,   BATTERY_VOLT_04_320000_V,	BATTERY_VOLT_04_340000_V,   BATTERY_VOLT_04_360000_V,	
	BATTERY_VOLT_04_380000_V,   BATTERY_VOLT_04_400000_V,	BATTERY_VOLT_04_420000_V,   BATTERY_VOLT_04_440000_V	
	
};

const unsigned int  CS_VTH[]=
{
	CHARGE_CURRENT_650_00_MA,   CHARGE_CURRENT_750_00_MA,	CHARGE_CURRENT_900_00_MA, CHARGE_CURRENT_1000_00_MA,
	CHARGE_CURRENT_1150_00_MA,   CHARGE_CURRENT_1250_00_MA,	CHARGE_CURRENT_1350_00_MA, CHARGE_CURRENT_1500_00_MA
}; 
const unsigned int  CS_VTH2[]=
{
	CHARGE_CURRENT_425_00_MA,   CHARGE_CURRENT_775_00_MA,	CHARGE_CURRENT_900_00_MA, CHARGE_CURRENT_1000_00_MA,
	CHARGE_CURRENT_1300_00_MA,   CHARGE_CURRENT_1450_00_MA,	CHARGE_CURRENT_1700_00_MA, CHARGE_CURRENT_1900_00_MA
};

 const unsigned int  INPUT_CS_VTH[]=
 {
	 CHARGE_CURRENT_100_00_MA,	 CHARGE_CURRENT_500_00_MA,	 CHARGE_CURRENT_800_00_MA, CHARGE_CURRENT_MAX
 }; 

 const unsigned int  VCDT_HV_VTH[]=
 {
	  BATTERY_VOLT_04_200000_V, BATTERY_VOLT_04_250000_V,	  BATTERY_VOLT_04_300000_V,   BATTERY_VOLT_04_350000_V,
	  BATTERY_VOLT_04_400000_V, BATTERY_VOLT_04_450000_V,	  BATTERY_VOLT_04_500000_V,   BATTERY_VOLT_04_550000_V,
	  BATTERY_VOLT_04_600000_V, BATTERY_VOLT_06_000000_V,	  BATTERY_VOLT_06_500000_V,   BATTERY_VOLT_07_000000_V,
	  BATTERY_VOLT_07_500000_V, BATTERY_VOLT_08_500000_V,	  BATTERY_VOLT_09_500000_V,   BATTERY_VOLT_10_500000_V		  
 };

const unsigned int VINDPM_REG[] = {
	4200, 4280, 4360,4440,4520,4600.4680,4760
};

#ifdef CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT
#ifndef CUST_GPIO_VIN_SEL
#define CUST_GPIO_VIN_SEL 18
#endif
DISO_IRQ_Data DISO_IRQ;
int g_diso_state = 0;
int vin_sel_gpio_number = (CUST_GPIO_VIN_SEL | 0x80000000);
static char *DISO_state_s[8] = {
	"IDLE",
	"OTG_ONLY",
	"USB_ONLY",
	"USB_WITH_OTG",
	"DC_ONLY",
	"DC_WITH_OTG",
	"DC_WITH_USB",
	"DC_USB_OTG",
};
#endif

/* ============================================================ // */
/* function prototype */
/* ============================================================ // */


/* ============================================================ // */
/* extern variable */
/* ============================================================ // */

/* ============================================================ // */
/* extern function */
/* ============================================================ // */
/* extern unsigned int upmu_get_reg_value(unsigned int reg); upmu_common.h, _not_ used */
/* extern bool mt_usb_is_device(void); _not_ used */
/* extern void Charger_Detect_Init(void); _not_ used */
/* extern void Charger_Detect_Release(void); _not_ used */
/* extern int hw_charging_get_charger_type(void);  included in charging.h*/
/* extern void mt_power_off(void); _not_ used */
/* extern unsigned int mt6311_get_chip_id(void); _not_ used*/
/* extern int is_mt6311_exist(void); _not_ used */
/* extern int is_mt6311_sw_ready(void); _not_ used */
static unsigned int charging_error;
static unsigned int charging_get_error_state(void);
static int charging_set_error_state(void *data);
//static int charging_set_vindpm(void *data);
static unsigned int g_input_current;
DEFINE_MUTEX(g_input_current_mutex);

/* ============================================================ // */
unsigned int charging_value_to_parameter(const unsigned int *parameter, const unsigned int array_size,
				       const unsigned int val)
{
	if (val < array_size)
		return parameter[val];

		battery_log(BAT_LOG_CRTI, "Can't find the parameter \r\n");
		return parameter[0];

}

unsigned int charging_parameter_to_value(const unsigned int *parameter, const unsigned int array_size,
				       const unsigned int val)
{
	unsigned int i;

	battery_log(BAT_LOG_FULL, "array_size = %d \r\n", array_size);

	for (i = 0; i < array_size; i++) {
		if (val == *(parameter + i))
			return i;
	}

	battery_log(BAT_LOG_CRTI, "NO register value match \r\n");
	/* TODO: ASSERT(0);    // not find the value */
	return 0;
}

static unsigned int bmt_find_closest_level(const unsigned int *pList, unsigned int number,
					 unsigned int level)
{
	unsigned int i;
	unsigned int max_value_in_last_element;

	if (pList[0] < pList[1])
		max_value_in_last_element = KAL_TRUE;
	else
		max_value_in_last_element = KAL_FALSE;

	if (max_value_in_last_element == KAL_TRUE) {
		for (i = (number - 1); i != 0; i--) {	/* max value in the last element */
			if (pList[i] <= level) {
				battery_log(2, "zzf_%d<=%d     i=%d\n", pList[i], level, i);
				return pList[i];
			}
		}

		battery_log(BAT_LOG_CRTI, "Can't find closest level \r\n");
		return pList[0];
		/* return CHARGE_CURRENT_0_00_MA; */
	} else {
		for (i = 0; i < number; i++) {	/* max value in the first element */
			if (pList[i] <= level)
				return pList[i];
		}

		battery_log(BAT_LOG_CRTI, "Can't find closest level \r\n");
		return pList[number - 1];
		/* return CHARGE_CURRENT_0_00_MA; */
	}
}
#if !defined(CONFIG_POWER_EXT)
static unsigned int is_chr_det(void)
{
	unsigned int val = 0;

#if defined(CONFIG_MTK_PMIC_CHIP_MT6353)
	val = pmic_get_register_value(PMIC_RGS_CHRDET);
#else
	val = pmic_get_register_value(MT6351_PMIC_RGS_CHRDET);
#endif
	battery_log(BAT_LOG_CRTI, "[is_chr_det] %d\n", val);

	return val;
}
#endif

static int charging_hw_init(void *data)
{
	int status = STATUS_OK;

	

	//mt_set_gpio_mode(gpio_number,gpio_on_mode);  
    //mt_set_gpio_dir(gpio_number,gpio_on_dir);
    //mt_set_gpio_out(gpio_number,gpio_on_out);
#if defined(MTK_WIRELESS_CHARGER_SUPPORT)
	if (wireless_charger_gpio_number != 0) {
#ifdef CONFIG_MTK_LEGACY
		mt_set_gpio_mode(wireless_charger_gpio_number, 0);	/* 0:GPIO mode */
		mt_set_gpio_dir(wireless_charger_gpio_number, 0);	/* 0: input, 1: output */
#else
/*K.S. way here*/
#endif
	}
#endif

#ifdef CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT
#ifdef CONFIG_MTK_LEGACY
	mt_set_gpio_mode(vin_sel_gpio_number, 0);	/* 0:GPIO mode */
	mt_set_gpio_dir(vin_sel_gpio_number, 0);	/* 0: input, 1: output */
#else
/*K.S. way here*/
#endif
#endif

	return status;
}

static int charging_get_bif_vbat(void *data);

static int charging_dump_register(void *data)
{
	int status = STATUS_OK;

	battery_log(BAT_LOG_FULL, "charging_dump_register\r\n");
	sy6923d_dump_register();

	/*unsigned int vbat;
	   charging_get_bif_vbat(&vbat);
	   battery_log(BAT_LOG_CRTI,"[BIF] vbat=%d mV\n", vbat); */


	return status;
}

static int charging_enable(void *data)
{
	int status = STATUS_OK;
	unsigned int enable = *(unsigned int *) (data);

	if (KAL_TRUE == enable) {
		gpio_set_value(LCT_CHR_EN_PIN, GPIO_OUT_ZERO);
		//sy6923d_set_ce(0);
		sy6923d_set_hz_mode(0);
		sy6923d_set_opa_mode(0);
	} else {
		gpio_set_value(LCT_CHR_EN_PIN, GPIO_OUT_ONE);
		//sy6923d_set_ce(1);
		if (charging_get_error_state())
			//sy6923d_set_hz_mode(0);
	
			battery_log(BAT_LOG_CRTI, "[charging_enable] under test mode: disable charging\n");

		/*sy6923d_set_en_hiz(0x1);*/
	}

	return status;
}

static int charging_set_cv_voltage(void *data)
{

 	unsigned int  status = STATUS_OK;
	unsigned int register_value;
	unsigned int  cv_value = *(unsigned int  *)(data);

	register_value = charging_parameter_to_value(VBAT_CV_VTH, GETARRAYNUM(VBAT_CV_VTH) ,*(unsigned int  *)(data));
	sy6923d_set_oreg(register_value); 

	printk ("charging_set_cv_voltage  cv_value %d  \n",  cv_value);        
    printk ("charging_set_cv_voltage register_value 0x%x  \n",  register_value);        

    sy6923d_dump_register();

	return status;
}


static int charging_get_current(void *data)
{
    unsigned int  status = STATUS_OK;
    unsigned int  array_size;
    unsigned char  reg_value;
	
    //Get current level
    if(vendor_id == VENDOR_SY6923D){
		array_size = GETARRAYNUM(CS_VTH);
		reg_value = sy6923d_get_iocharge();
		*(unsigned int  *)data = charging_value_to_parameter(CS_VTH,array_size,reg_value);
    }else if (vendor_id == VENDOR_PSC5415A){
		array_size = GETARRAYNUM(CS_VTH2);
		reg_value = sy6923d_get_iocharge();
		*(unsigned int  *)data = charging_value_to_parameter(CS_VTH2,array_size,reg_value);
    }
    else
    {
		battery_log(BAT_LOG_CRTI, "charge ic detect failed!!\n");
    }
    return status;
}


static int charging_set_current(void *data)
{
	int status = STATUS_OK;
	unsigned int set_chr_current;
	unsigned int array_size;
	unsigned int register_value;
	unsigned int current_value = *(unsigned int *) data;

//nickygao-test
	if(current_value <= CHARGE_CURRENT_350_00_MA)
	{
		sy6923d_set_io_level(1);
	}
	else
	{
		sy6923d_set_io_level(0);
			if(vendor_id == VENDOR_SY6923D){
					array_size = GETARRAYNUM(CS_VTH);
					set_chr_current = bmt_find_closest_level(CS_VTH, array_size, current_value);
					register_value = charging_parameter_to_value(CS_VTH, array_size ,set_chr_current);
					sy6923d_set_iocharge(register_value);
			}
			else if (vendor_id == VENDOR_PSC5415A){
					array_size = GETARRAYNUM(CS_VTH2);
					set_chr_current = bmt_find_closest_level(CS_VTH2, array_size, current_value);
					register_value = charging_parameter_to_value(CS_VTH2, array_size ,set_chr_current);
					sy6923d_set_iocharge(register_value);
			}
			else
			{
					battery_log(BAT_LOG_CRTI, "charge ic is failed!!\n");
			}
	}
	return status;
}

static int charging_set_input_current(void *data)
{
 	unsigned int  status = STATUS_OK;
	unsigned int  set_chr_current;
	unsigned int  array_size;
	unsigned int  register_value;
	mutex_lock(&g_input_current_mutex);
//nickygao-test
    if(*(unsigned int  *)data > CHARGE_CURRENT_500_00_MA)
    {
        register_value = 0x3;
    }
    else
    {
    	array_size = GETARRAYNUM(INPUT_CS_VTH);
    	set_chr_current = bmt_find_closest_level(INPUT_CS_VTH, array_size, *(unsigned int  *)data);
    	register_value = charging_parameter_to_value(INPUT_CS_VTH, array_size ,set_chr_current);
	g_input_current = set_chr_current;	
    }

    sy6923d_set_input_charging_current(register_value);
    mutex_unlock(&g_input_current_mutex);
	return status;
}

static int charging_get_charging_status(void *data)
{
 	unsigned int  status = STATUS_OK;
	unsigned int  ret_val;

	ret_val = sy6923d_get_chip_status();
	
	if(ret_val == 0x2)/* check if chrg done */
		*(unsigned int  *)data = KAL_TRUE;
	else
		*(unsigned int  *)data = KAL_FALSE;
	
	return status;
}

static int charging_reset_watch_dog_timer(void *data)
{
	int status = STATUS_OK;

	 //sy6923d_set_tmr_rst(1);

	return status;
}

static int charging_set_hv_threshold(void *data)
{
	int status = STATUS_OK;
	unsigned int set_hv_voltage;
	unsigned int array_size;
	unsigned short int register_value;
	unsigned int voltage = *(unsigned int *) (data);

	array_size = GETARRAYNUM(VCDT_HV_VTH);
	set_hv_voltage = bmt_find_closest_level(VCDT_HV_VTH, array_size, voltage);
	register_value = charging_parameter_to_value(VCDT_HV_VTH, array_size, set_hv_voltage);
#if defined(CONFIG_MTK_PMIC_CHIP_MT6353)
	pmic_set_register_value(PMIC_RG_VCDT_HV_VTH, register_value);
#else
	pmic_set_register_value(MT6351_PMIC_RG_VCDT_HV_VTH, register_value);
#endif

	return status;
}


static int charging_get_hv_status(void *data)
{
	int status = STATUS_OK;

#if defined(CONFIG_MTK_PMIC_CHIP_MT6353)
	*(kal_bool *) (data) = pmic_get_register_value(PMIC_RGS_VCDT_HV_DET);
#else
	*(kal_bool *) (data) = pmic_get_register_value(MT6351_PMIC_RGS_VCDT_HV_DET);
#endif
	return status;
}


static int charging_get_battery_status(void *data)
{
	int status = STATUS_OK;
#if defined(CONFIG_POWER_EXT) || defined(CONFIG_MTK_FPGA)
	*(kal_bool *) (data) = 0;
	battery_log(BAT_LOG_CRTI, "bat exist for evb\n");
#else
	unsigned int val = 0;

#if defined(CONFIG_MTK_PMIC_CHIP_MT6353)
	val = pmic_get_register_value(PMIC_BATON_TDET_EN);
	battery_log(BAT_LOG_FULL, "[charging_get_battery_status] BATON_TDET_EN = %d\n", val);
	if (val) {
		pmic_set_register_value(PMIC_BATON_TDET_EN, 1);
		pmic_set_register_value(PMIC_RG_BATON_EN, 1);
		*(kal_bool *) (data) = pmic_get_register_value(PMIC_RGS_BATON_UNDET);
	} else {
		*(kal_bool *) (data) = KAL_FALSE;
	}
#else
	val = pmic_get_register_value(MT6351_PMIC_BATON_TDET_EN);
	battery_log(BAT_LOG_FULL, "[charging_get_battery_status] BATON_TDET_EN = %d\n", val);
	if (val) {
		pmic_set_register_value(MT6351_PMIC_BATON_TDET_EN, 1);
		pmic_set_register_value(MT6351_PMIC_RG_BATON_EN, 1);
		*(kal_bool *) (data) = pmic_get_register_value(MT6351_PMIC_RGS_BATON_UNDET);
	} else {
		*(kal_bool *) (data) = KAL_FALSE;
	}
#endif
#endif
	return status;
}


static int charging_get_charger_det_status(void *data)
{
	int status = STATUS_OK;

#if defined(CONFIG_MTK_FPGA)
	*(kal_bool *) (data) = 1;
	battery_log(BAT_LOG_CRTI, "chr exist for fpga\n");
#else
#if defined(CONFIG_MTK_PMIC_CHIP_MT6353)
	*(kal_bool *) (data) = pmic_get_register_value_nolock(PMIC_RGS_CHRDET);
#else
	*(kal_bool *) (data) = pmic_get_register_value_nolock(MT6351_PMIC_RGS_CHRDET);
#endif
#endif
	return status;
}


kal_bool charging_type_detection_done(void)
{
	return charging_type_det_done;
}


static int charging_get_charger_type(void *data)
{
	int status = STATUS_OK;

#if defined(CONFIG_POWER_EXT) || defined(CONFIG_MTK_FPGA)
	*(CHARGER_TYPE *) (data) = STANDARD_HOST;
#else
#if defined(MTK_WIRELESS_CHARGER_SUPPORT)
	int wireless_state = 0;

	if (wireless_charger_gpio_number != 0) {
#ifdef CONFIG_MTK_LEGACY
		wireless_state = mt_get_gpio_in(wireless_charger_gpio_number);
#else
/*K.S. way here*/
#endif
		if (wireless_state == WIRELESS_CHARGER_EXIST_STATE) {
			*(CHARGER_TYPE *) (data) = WIRELESS_CHARGER;
			battery_log(BAT_LOG_CRTI, "WIRELESS_CHARGER!\n");
			return status;
		}
	} else {
		battery_log(BAT_LOG_CRTI, "wireless_charger_gpio_number=%d\n", wireless_charger_gpio_number);
	}

	if (g_charger_type != CHARGER_UNKNOWN && g_charger_type != WIRELESS_CHARGER) {
		*(CHARGER_TYPE *) (data) = g_charger_type;
		battery_log(BAT_LOG_CRTI, "return %d!\n", g_charger_type);
		return status;
	}
#endif

	if (is_chr_det() == 0) {
		g_charger_type = CHARGER_UNKNOWN;
		*(CHARGER_TYPE *) (data) = CHARGER_UNKNOWN;
		battery_log(BAT_LOG_CRTI, "[charging_get_charger_type] return CHARGER_UNKNOWN\n");
		return status;
	}

	charging_type_det_done = KAL_FALSE;
	*(CHARGER_TYPE *) (data) = hw_charging_get_charger_type();
	charging_type_det_done = KAL_TRUE;
	g_charger_type = *(CHARGER_TYPE *) (data);

#endif

	return status;
}

static int charging_get_is_pcm_timer_trigger(void *data)
{
	int status = STATUS_OK;

#if defined(CONFIG_POWER_EXT) || defined(CONFIG_MTK_FPGA)
	*(kal_bool *) (data) = KAL_FALSE;
#else
	if (slp_get_wake_reason() == WR_PCM_TIMER)
		*(kal_bool *) (data) = KAL_TRUE;
	else
		*(kal_bool *) (data) = KAL_FALSE;

	battery_log(BAT_LOG_CRTI, "slp_get_wake_reason=%d\n", slp_get_wake_reason());
#endif

	return status;
}

static int charging_set_platform_reset(void *data)
{
	int status = STATUS_OK;

#if defined(CONFIG_POWER_EXT) || defined(CONFIG_MTK_FPGA)
#else
	battery_log(BAT_LOG_CRTI, "charging_set_platform_reset\n");
	kernel_restart("battery service reboot system");
#endif

	return status;
}

static int charging_get_platform_boot_mode(void *data)
{
	int status = STATUS_OK;

#if defined(CONFIG_POWER_EXT) || defined(CONFIG_MTK_FPGA)
#else
	*(unsigned int *) (data) = get_boot_mode();

	battery_log(BAT_LOG_CRTI, "get_boot_mode=%d\n", get_boot_mode());
#endif

	return status;
}

static int charging_set_power_off(void *data)
{
	int status = STATUS_OK;

#if defined(CONFIG_POWER_EXT) || defined(CONFIG_MTK_FPGA)
#else
	/*added dump_stack to see who the caller is */
	dump_stack();
	battery_log(BAT_LOG_CRTI, "charging_set_power_off\n");
	kernel_power_off();
#endif

	return status;
}
/******************caozhg add this func need check below this line*********************/
static int charging_get_power_source(void *data)
{
	int status = STATUS_OK;

#if 0				/* #if defined(MTK_POWER_EXT_DETECT) */
	if (MT_BOARD_PHONE == mt_get_board_type())
		*(kal_bool *) data = KAL_FALSE;
	else
		*(kal_bool *) data = KAL_TRUE;
#else
	*(kal_bool *) data = KAL_FALSE;
#endif

	return status;
}

static int charging_get_csdac_full_flag(void *data)
{
	return STATUS_UNSUPPORTED;
}

static int charging_set_ta_current_pattern(void *data)
{
	return STATUS_OK;
}
/*
static int charging_set_vindpm(void *data)
{
	int status = STATUS_OK;
	unsigned int v = *(unsigned int *) data;

	sy6923d_set_vsp(v);

	return status;
}
*/

static int charging_set_vbus_ovp_en(void *data)
{
	int status = STATUS_OK;
	unsigned int e = *(unsigned int *) data;

#if defined(CONFIG_MTK_PMIC_CHIP_MT6353)
	pmic_set_register_value(PMIC_RG_VCDT_HV_EN, e);
#else
	pmic_set_register_value(MT6351_PMIC_RG_VCDT_HV_EN, e);
#endif

	return status;
}

static int charging_get_bif_vbat(void *data)
{
	int ret = 0;
	u32 vbat = 0;

	ret = mtk_bif_get_vbat(&vbat);
	if (ret < 0)
		return STATUS_UNSUPPORTED;

	*((u32 *)data) = vbat;

	return STATUS_OK;
}

static int charging_get_bif_tbat(void *data)
{
	int ret = 0, tbat = 0;

	ret = mtk_bif_get_tbat(&tbat);
	if (ret < 0)
		return STATUS_UNSUPPORTED;

	*((int *)data) = tbat;

	return STATUS_OK;
}

static int charging_diso_init(void *data)
{
	int status = STATUS_OK;

#if defined(MTK_DUAL_INPUT_CHARGER_SUPPORT)
	struct device_node *node;
	DISO_ChargerStruct *pDISO_data = (DISO_ChargerStruct *) data;

	int ret;
	/* Initialization DISO Struct */
	pDISO_data->diso_state.cur_otg_state = DISO_OFFLINE;
	pDISO_data->diso_state.cur_vusb_state = DISO_OFFLINE;
	pDISO_data->diso_state.cur_vdc_state = DISO_OFFLINE;

	pDISO_data->diso_state.pre_otg_state = DISO_OFFLINE;
	pDISO_data->diso_state.pre_vusb_state = DISO_OFFLINE;
	pDISO_data->diso_state.pre_vdc_state = DISO_OFFLINE;

	pDISO_data->chr_get_diso_state = KAL_FALSE;

	pDISO_data->hv_voltage = VBUS_MAX_VOLTAGE;

	/* Initial AuxADC IRQ */
	DISO_IRQ.vdc_measure_channel.number = AP_AUXADC_DISO_VDC_CHANNEL;
	DISO_IRQ.vusb_measure_channel.number = AP_AUXADC_DISO_VUSB_CHANNEL;
	DISO_IRQ.vdc_measure_channel.period = AUXADC_CHANNEL_DELAY_PERIOD;
	DISO_IRQ.vusb_measure_channel.period = AUXADC_CHANNEL_DELAY_PERIOD;
	DISO_IRQ.vdc_measure_channel.debounce = AUXADC_CHANNEL_DEBOUNCE;
	DISO_IRQ.vusb_measure_channel.debounce = AUXADC_CHANNEL_DEBOUNCE;

	/* use default threshold voltage, if use high voltage,maybe refine */
	DISO_IRQ.vusb_measure_channel.falling_threshold = VBUS_MIN_VOLTAGE / 1000;
	DISO_IRQ.vdc_measure_channel.falling_threshold = VDC_MIN_VOLTAGE / 1000;
	DISO_IRQ.vusb_measure_channel.rising_threshold = VBUS_MIN_VOLTAGE / 1000;
	DISO_IRQ.vdc_measure_channel.rising_threshold = VDC_MIN_VOLTAGE / 1000;

	node = of_find_compatible_node(NULL, NULL, "mediatek,AUXADC");
	if (!node) {
		battery_log(BAT_LOG_CRTI, "[diso_adc]: of_find_compatible_node failed!!\n");
	} else {
		pDISO_data->irq_line_number = irq_of_parse_and_map(node, 0);
		battery_log(BAT_LOG_FULL, "[diso_adc]: IRQ Number: 0x%x\n",
			    pDISO_data->irq_line_number);
	}

	mt_irq_set_sens(pDISO_data->irq_line_number, MT_EDGE_SENSITIVE);
	mt_irq_set_polarity(pDISO_data->irq_line_number, MT_POLARITY_LOW);

	ret = request_threaded_irq(pDISO_data->irq_line_number, diso_auxadc_irq_handler,
				   pDISO_data->irq_callback_func, IRQF_ONESHOT, "DISO_ADC_IRQ",
				   NULL);

	if (ret) {
		battery_log(BAT_LOG_CRTI, "[diso_adc]: request_irq failed.\n");
	} else {
		set_vdc_auxadc_irq(DISO_IRQ_DISABLE, 0);
		set_vusb_auxadc_irq(DISO_IRQ_DISABLE, 0);
		battery_log(BAT_LOG_FULL, "[diso_adc]: diso_init success.\n");
	}

#if defined(MTK_DISCRETE_SWITCH) && defined(MTK_DSC_USE_EINT)
	battery_log(BAT_LOG_CRTI, "[diso_eint]vdc eint irq registitation\n");
	mt_eint_set_hw_debounce(CUST_EINT_VDC_NUM, CUST_EINT_VDC_DEBOUNCE_CN);
	mt_eint_registration(CUST_EINT_VDC_NUM, CUST_EINTF_TRIGGER_LOW, vdc_eint_handler, 0);
	mt_eint_mask(CUST_EINT_VDC_NUM);
#endif
#endif

	return status;
}

static int charging_get_diso_state(void *data)
{
	int status = STATUS_OK;

#if defined(MTK_DUAL_INPUT_CHARGER_SUPPORT)
	int diso_state = 0x0;
	DISO_ChargerStruct *pDISO_data = (DISO_ChargerStruct *) data;

	_get_diso_interrupt_state();
	diso_state = g_diso_state;
	battery_log(BAT_LOG_FULL, "[do_chrdet_int_task] current diso state is %s!\n",
		    DISO_state_s[diso_state]);
	if (((diso_state >> 1) & 0x3) != 0x0) {
		switch (diso_state) {
		case USB_ONLY:
			set_vdc_auxadc_irq(DISO_IRQ_DISABLE, 0);
			set_vusb_auxadc_irq(DISO_IRQ_DISABLE, 0);
#ifdef MTK_DISCRETE_SWITCH
#ifdef MTK_DSC_USE_EINT
			mt_eint_unmask(CUST_EINT_VDC_NUM);
#else
			set_vdc_auxadc_irq(DISO_IRQ_ENABLE, 1);
#endif
#endif
			pDISO_data->diso_state.cur_vusb_state = DISO_ONLINE;
			pDISO_data->diso_state.cur_vdc_state = DISO_OFFLINE;
			pDISO_data->diso_state.cur_otg_state = DISO_OFFLINE;
			break;
		case DC_ONLY:
			set_vdc_auxadc_irq(DISO_IRQ_DISABLE, 0);
			set_vusb_auxadc_irq(DISO_IRQ_DISABLE, 0);
			set_vusb_auxadc_irq(DISO_IRQ_ENABLE, DISO_IRQ_RISING);
			pDISO_data->diso_state.cur_vusb_state = DISO_OFFLINE;
			pDISO_data->diso_state.cur_vdc_state = DISO_ONLINE;
			pDISO_data->diso_state.cur_otg_state = DISO_OFFLINE;
			break;
		case DC_WITH_USB:
			set_vdc_auxadc_irq(DISO_IRQ_DISABLE, 0);
			set_vusb_auxadc_irq(DISO_IRQ_DISABLE, 0);
			set_vusb_auxadc_irq(DISO_IRQ_ENABLE, DISO_IRQ_FALLING);
			pDISO_data->diso_state.cur_vusb_state = DISO_ONLINE;
			pDISO_data->diso_state.cur_vdc_state = DISO_ONLINE;
			pDISO_data->diso_state.cur_otg_state = DISO_OFFLINE;
			break;
		case DC_WITH_OTG:
			set_vdc_auxadc_irq(DISO_IRQ_DISABLE, 0);
			set_vusb_auxadc_irq(DISO_IRQ_DISABLE, 0);
			pDISO_data->diso_state.cur_vusb_state = DISO_OFFLINE;
			pDISO_data->diso_state.cur_vdc_state = DISO_ONLINE;
			pDISO_data->diso_state.cur_otg_state = DISO_ONLINE;
			break;
		default:	/* OTG only also can trigger vcdt IRQ */
			pDISO_data->diso_state.cur_vusb_state = DISO_OFFLINE;
			pDISO_data->diso_state.cur_vdc_state = DISO_OFFLINE;
			pDISO_data->diso_state.cur_otg_state = DISO_ONLINE;
			battery_log(BAT_LOG_FULL, " switch load vcdt irq triggerd by OTG Boost!\n");
			break;	/* OTG plugin no need battery sync action */
		}
	}

	if (DISO_ONLINE == pDISO_data->diso_state.cur_vdc_state)
		pDISO_data->hv_voltage = VDC_MAX_VOLTAGE;
	else
		pDISO_data->hv_voltage = VBUS_MAX_VOLTAGE;
#endif

	return status;
}

static unsigned int charging_get_error_state(void)
{
	return charging_error;
}

static int charging_set_hiz_swchr(void *data);
static int charging_set_error_state(void *data)
{
	int status = STATUS_OK;

	charging_error = *(unsigned int *) (data);
	charging_set_hiz_swchr(&charging_error);

	return status;
}

static int charging_set_chrind_ck_pdn(void *data)
{
	int status = STATUS_OK;
	unsigned int pwr_dn;

	pwr_dn = *(unsigned int *) data;

#if defined(CONFIG_MTK_PMIC_CHIP_MT6353)
	pmic_set_register_value(PMIC_CLK_DRV_CHRIND_CK_PDN, pwr_dn);
#else
	pmic_set_register_value(PMIC_RG_DRV_CHRIND_CK_PDN, pwr_dn);
#endif

	return status;
}

static int charging_sw_init(void *data)
{
	int status = STATUS_OK;
	static bool charging_init_flag = KAL_FALSE;
	/*put here anything needed to be init upon battery_common driver probe*/
	mtk_bif_init();
	battery_log(BAT_LOG_CRTI, "%s: set register 6 to 0xff\n", __func__);
	sy6923d_config_interface_liao(0x06,0xff); //set isafe and vsafe
    //sy6923d_config_interface_liao(0x00,0xC0);	//kick chip watch dog
	//sy6923d_config_interface_liao(0x01,0xc8);	//TE=1, CE=0, HZ_MODE=0, OPA_MODE=0
	//sy6923d_config_interface_liao(0x05,0x04);  //special voltage = 4.52V
    //sy6923d_config_interface_liao(0x04,0x4A); //146mA //termination current=121mA
	if ( !charging_init_flag ) {   
        	sy6923d_config_interface_liao(0x04,0xc1); //121mA
			charging_init_flag = KAL_TRUE;
	}
	if(vendor_id == VENDOR_PSC5415A)
		sy6923d_config_interface_liao(0x06,0xff);  //psc5415a security register will be reset,sy6923d will not.
	sy6923d_config_interface_liao(0x01,0xc8);	//TE=1, CE=0, HZ_MODE=0, OPA_MODE=0
	sy6923d_config_interface_liao(0x02,0xb6);	//TE=1, CE=0, HZ_MODE=0, OPA_MODE=0
	sy6923d_config_interface_liao(0x05,0x04);  //special voltage = 4.52V
	vendor_id = sy6923d_get_vender_code();

	return status;
}

static int charging_enable_safetytimer(void *data)
{
	int status = STATUS_OK;
	//unsigned int en;

	//en = *(unsigned int *) data;
	//sy6923d_en_chg_timer(en);
	
	return status;
}

static int charging_set_hiz_swchr(void *data)/*caozhg-check*/
{
	int status = STATUS_OK;
/*
	unsigned int en;
	unsigned int vindpm;

	en = *(unsigned int *) data;
	if (en == 1)
		vindpm = 0x7F;
	else
		vindpm = 0x13;

	sy6923d_set_vsp(&vindpm);
*/
	return status;
}

static int charging_get_is_power_path_enable(void *data);
static int charging_set_vindpm_voltage(void *data)
{
	int status = STATUS_OK;
	unsigned int vindpm;
	unsigned int array_size;
	bool is_power_path_enable = false;
	int ret = 0;

	array_size = GETARRAYNUM(VINDPM_REG);
	vindpm = bmt_find_closest_level(VINDPM_REG, array_size, *(unsigned int *) data);
	vindpm = charging_parameter_to_value(VINDPM_REG, array_size, vindpm);

	/*
	 * Since BQ25896 uses vindpm to turn off power path
	 * If power path is disabled, do not adjust mivr
	 */
	ret = charging_get_is_power_path_enable(&is_power_path_enable);
	if (ret == 0 && !is_power_path_enable) {
		battery_log(BAT_LOG_CRTI,
			"%s: power path is disable, skip setting vindpm = %d\n",
			__func__, vindpm);
		return 0;
	}
	sy6923d_set_vsp(vindpm);
	/*sy6923d_set_en_hiz(en);*/

	return status;
}

static int charging_set_ta20_reset(void *data)
{
	return -ENOTSUPP;
}


static int charging_set_ta20_current_pattern(void *data)
{
	return -ENOTSUPP;
}

static int charging_set_dp(void *data)
{
	unsigned int status = STATUS_OK;

#ifndef CONFIG_POWER_EXT
	unsigned int en;

	en = *(int *) data;
	hw_charging_enable_dp_voltage(en);
#endif

	return status;
}

static int charging_get_charger_temperature(void *data)
{
	return -ENOTSUPP;
}

static int charging_set_boost_current_limit(void *data)
{
	return -ENOTSUPP;
}

static int charging_enable_otg(void *data)
{
	int ret = 0;
	unsigned int enable = 0;
	
	enable = *((unsigned int *)data);
	//if(enable == 0)
		//otg_online = 0;
	sy6923d_set_opa_mode(enable);
	sy6923d_set_otg_en(enable);
	sy6923d_dump_register();
	return ret;
}

static int charging_enable_power_path(void *data)
{
	return -ENOTSUPP;
}

static int charging_get_bif_is_exist(void *data)
{
	int bif_exist = 0;

	bif_exist = mtk_bif_is_hw_exist();
	*(bool *)data = bif_exist;

	return 0;
}

static int charging_get_input_current(void *data)
{
	int ret = 0;

	*((u32 *)data) = g_input_current;

	return ret;
}

static int charging_enable_direct_charge(void *data)
{
	return -ENOTSUPP;
}

static int charging_get_is_power_path_enable(void *data)
{
	return -ENOTSUPP;
}

static int charging_get_is_safetytimer_enable(void *data)
{
	return -ENOTSUPP;
}

static int charging_set_pwrstat_led_en(void *data)
{
	int ret = 0;
	unsigned int led_en;

	led_en = *(unsigned int *) data;

	pmic_set_register_value(PMIC_CHRIND_MODE, 0x2); /* register mode */
	pmic_set_register_value(PMIC_CHRIND_EN_SEL, !led_en); /* 0: Auto, 1: SW */
	pmic_set_register_value(PMIC_CHRIND_EN, led_en);

	return ret;
}

static int charging_get_ibus(void *data)
{
	return -ENOTSUPP;
}

static int charging_get_vbus(void *data)
{
	return -ENOTSUPP;
}

static int charging_reset_dc_watch_dog_timer(void *data)
{
	return -ENOTSUPP;
}

static int charging_run_aicl(void *data)
{
	return -ENOTSUPP;
}

static int charging_set_ircmp_resistor(void *data)
{
	return -ENOTSUPP;
}

static int charging_set_ircmp_volt_clamp(void *data)
{
	return -ENOTSUPP;
}
static int charging_get_sp_status(void *data)
{
	unsigned int  ret_val;
	unsigned int status = STATUS_OK;

	ret_val = sy6923d_get_sp_status();

	if(ret_val == 0x1)
		*(unsigned int  *)data = KAL_TRUE;
	else
		*(unsigned int  *)data = KAL_FALSE;

	return status;
}

static int charging_set_hz_mode(void *data)
{
	unsigned int status = STATUS_OK;
	unsigned int enable = *(unsigned int *) (data);

	sy6923d_set_hz_mode(enable);

	return status;
}

static int charging_set_reset(void *data)
{
	unsigned int status = STATUS_OK;
	sy6923d_set_reset(1);
	return status;
}

static int (*const charging_func[CHARGING_CMD_NUMBER]) (void *data) = {
	charging_hw_init, charging_dump_register, charging_enable, charging_set_cv_voltage,
	charging_get_current, charging_set_current, charging_set_input_current,
	charging_get_charging_status, charging_reset_watch_dog_timer,
	charging_set_hv_threshold, charging_get_hv_status, charging_get_battery_status,
	charging_get_charger_det_status, charging_get_charger_type,
	charging_get_is_pcm_timer_trigger, charging_set_platform_reset,
	charging_get_platform_boot_mode, charging_set_power_off,
	charging_get_power_source, charging_get_csdac_full_flag,
	charging_set_ta_current_pattern, charging_set_error_state, charging_diso_init,
	charging_get_diso_state, charging_set_vindpm_voltage, charging_set_vbus_ovp_en,
	charging_get_bif_vbat, charging_set_chrind_ck_pdn, charging_sw_init, charging_enable_safetytimer,
	charging_set_hiz_swchr, charging_get_bif_tbat, charging_set_ta20_reset,
	charging_set_ta20_current_pattern, charging_set_dp, charging_get_charger_temperature,
	charging_set_boost_current_limit, charging_enable_otg, charging_enable_power_path,
	charging_get_bif_is_exist, charging_get_input_current, charging_enable_direct_charge,
	charging_get_is_power_path_enable, charging_get_is_safetytimer_enable,
	charging_set_pwrstat_led_en, charging_get_ibus, charging_get_vbus,
	charging_reset_dc_watch_dog_timer, charging_run_aicl, charging_set_ircmp_resistor,
	charging_set_ircmp_volt_clamp,charging_get_sp_status,
	charging_set_hz_mode,
	charging_set_reset,
};

/*
* FUNCTION
*        Internal_chr_control_handler
*
* DESCRIPTION
*         This function is called to set the charger hw
*
* CALLS
*
* PARAMETERS
*        None
*
* RETURNS
*
*
* GLOBALS AFFECTED
*       None
*/
int chr_control_interface(CHARGING_CTRL_CMD cmd, void *data)
{
	int status;
	if (cmd < CHARGING_CMD_NUMBER) {
		if (charging_func[cmd] != NULL) {
			status = charging_func[cmd](data);
		}
		else {
			battery_log(BAT_LOG_CRTI, "[chr_control_interface]cmd:%d not supported\n", cmd);
			status = STATUS_UNSUPPORTED;
		}
	} else
		status = STATUS_UNSUPPORTED;

	return status;
}
