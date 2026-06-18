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

/*****************************************************************************
*
* Filename:
* ---------
*   bq25890.h
*
* Project:
* --------
*   Android
*
* Description:
* ------------
*   bq25890 header file
*
* Author:
* -------
*
****************************************************************************/
#ifndef _sy6923d_SW_H_
#define _sy6923d_SW_H_

#if !defined(CONFIG_MTK_LEGACY)
#include <linux/gpio.h>
#else
#include <mt-plat/mt_gpio.h>
#endif

#define LCT_CHR_EN_PIN 100
#define GPIO_OUT_ONE 1
#define GPIO_OUT_ZERO 0
#define GPIO_DIR_OUT 1



#define sy6923d_REG_NUM 7


#define sy6923d_CON0 0x00
#define sy6923d_CON1 0x01
#define sy6923d_CON2 0x02
#define sy6923d_CON3 0x03
#define sy6923d_CON4 0x04
#define sy6923d_CON5 0x05
#define sy6923d_CON6 0x06


/* CON0 */
#define CON4_TMR_RST_MASK   0x1
#define CON4_TMR_RST_SHIFT  7

#define CON0_OTG_MASK 0x1
#define CON0_OTG_SHIFT 7

#define CON0_EN_STAT_MASK 0x1
#define CON0_EN_STAT_SHIFT 6

#define CON0_STAT_MASK 0x3
#define CON0_STAT_SHIFT 4

#define CON0_BOOST_MASK 0x1
#define CON0_BOOST_SHIFT 3

#define CON0_FAULT_MASK 0x7
#define CON0_FAULT_SHIFT 0

//CON1----------------------------------------------------

#define CON1_LIN_LIMIT_MASK 0x3
#define CON1_LIN_LIMIT_SHIFT 6

#define CON1_LOW_V_MASK 0x3
#define CON1_LOW_V_SHIFT 4

#define CON1_TE_MASK 0x1
#define CON1_TE_SHIFT 3

#define CON1_CE_MASK 0x1
#define CON1_CE_SHIFT 2

#define CON1_HZ_MODE_MASK 0x1
#define CON1_HZ_MODE_SHIFT 1

#define CON1_OPA_MODE_MASK 0x1
#define CON1_OPA_MODE_SHIFT 0

//CON2----------------------------------------------------

#define CON2_OREG_MASK 0x3f
#define CON2_OREG_SHIFT 2

#define CON2_OTG_PL_MASK 0x1
#define CON2_OTG_PL_SHIFT 1

#define CON2_OTG_EN_MASK 0x1
#define CON2_OTG_EN_SHIFT 1

//CON3----------------------------------------------------

#define CON3_VENDER_CODE_MASK 0x7
#define CON3_VENDER_CODE_SHIFT 5

#define CON3_PIN_MASK 0x3
#define CON3_PIN_SHIFT 3

#define CON3_REVISION_MASK 0x3
#define CON3_REVISION_SHIFT 0

//CON4----------------------------------------------------

#define CON4_RESET_MASK 0x1
#define CON4_RESET_SHIFT 7

#define CON4_I_CHR_MASK	0x7
#define CON4_I_CHR_SHIFT 4

#define CON4_I_TERM_MASK 0x7
#define CON4_I_TERM_SHIFT 0

//CON5----------------------------------------------------
/*
#define CON5_DIS_VREG_MASK
#define CON5_DIS_VREG_SHIFT 6
*/

#define CON5_IO_LEVEL_MASK 0x1
#define CON5_IO_LEVEL_SHIFT 5

#define CON5_SP_STATUS_MASK 0x1
#define CON5_SP_STATUS_SHIFT 4

#define CON5_EN_LEVEL_MASK 0x1
#define CON5_EN_LEVEL_SHIFT 3

#define CON5_VSP_MASK 0x7
#define CON5_VSP_SHIFT 0

//CON6----------------------------------------------------

#define CON6_ISAFE_MASK 0xf
#define CON6_ISAFE_SHIFT 4

#define CON6_VSAFE_MASK 0xf
#define CON6_VSAFE_SHIFT 0


//CON0----------------------------------------------------

void sy6923d_set_tmr_rst(unsigned int val);
unsigned int sy6923d_get_otg_status(void);
void sy6923d_set_en_stat(unsigned int val);
unsigned int sy6923d_get_chip_status(void);
unsigned int sy6923d_get_boost_status(void);
unsigned int sy6923d_get_fault_status(void);

//CON1----------------------------------------------------
void sy6923d_set_input_charging_current(unsigned int val);
void sy6923d_set_v_low(unsigned int val);
void sy6923d_set_te(unsigned int val);
void sy6923d_set_ce(unsigned int val);
void sy6923d_set_hz_mode(unsigned int val);
void sy6923d_set_opa_mode(unsigned int val);

//CON2----------------------------------------------------
void sy6923d_set_oreg(unsigned int val);
void sy6923d_set_otg_pl(unsigned int val);
void sy6923d_set_otg_en(unsigned int val);

//CON3----------------------------------------------------
unsigned int sy6923d_get_vender_code(void);
unsigned int sy6923d_get_pn(void);
unsigned int sy6923d_get_revision(void);

//CON4----------------------------------------------------
void sy6923d_set_reset(unsigned int val);
void sy6923d_set_iocharge(unsigned int val);
unsigned int sy6923d_get_iocharge(void);
void sy6923d_set_iterm(unsigned int val);

//CON5----------------------------------------------------
//void sy6923d_set_dis_vreg(unsigned int val);
void sy6923d_set_io_level(unsigned int val);
unsigned int sy6923d_get_sp_status(void);
unsigned int sy6923d_get_en_level(void);
void sy6923d_set_vsp(unsigned int val);

//CON6----------------------------------------------------

void sy6923d_set_i_safe(unsigned int val);
void sy6923d_set_v_safe(unsigned int val);


//
void sy6923d_dump_register(void);
unsigned int sy6923d_config_interface_liao (unsigned char RegNum, unsigned char val);
extern int otg_online;


#endif
