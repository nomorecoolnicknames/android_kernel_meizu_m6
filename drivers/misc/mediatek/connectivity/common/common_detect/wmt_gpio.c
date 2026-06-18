/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#include <linux/err.h>

#include "wmt_gpio.h"

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
const PUINT8 gpio_state_name[GPIO_PIN_ID_MAX][GPIO_STATE_MAX] = {{"gpio_ldo_en_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"gpio_ldo_en_in_pulldown",
		""},
	{"gpio_pmuv28_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"gpio_pmuv28_in_pulldown",
		""},
	{"gpio_pmu_en_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"gpio_pmu_en_in_pulldown",
		""},
	{"gpio_rst_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"gpio_rst_in_pulldown",
		""},
	{"",
		"",
		"",
		"",
		"",
		"",
		"",
		"gpio_bgf_eint_in_pull_dis",
		"gpio_bgf_eint_in_pulldown",
		"gpio_bgf_eint_in_pullup"},
	{"",
		"",
		"",
		"",
		"",
		"",
		"",
		"gpio_wifi_eint_in_pull_dis",
		"",
		"gpio_wifi_eint_in_pullup"},
	{"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"gpio_all_eint_in_pulldown",
		"gpio_all_eint_in_pullup"},
	{"gpio_urxd_uart_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"gpio_urxd_gpio_in_pull_dis",
		"",
		"gpio_urxd_gpio_in_pullup"},
	{"gpio_utxd_uart_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""},
	{"gpio_pcm_daiclk_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""},
	{"gpio_pcm_daipcmin_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""},
	{"gpio_pcm_daipcmout_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""},
	{"gpio_pcm_daisync_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""},
	{"gpio_i2s_ck_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""},
	{"gpio_i2s_ws_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""},
	{"gpio_i2s_dat_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""},
	{"gpio_gps_sync_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""},
	{"gpio_gps_lna_pull_dis",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""}
};

const PUINT8 gpio_pin_name[GPIO_PIN_ID_MAX] = {"gpio_combo_ldo_en_pin",
					"gpio_combo_pmuv28_en_pin",
					"gpio_combo_pmu_en_pin",
					"gpio_combo_rst_pin",
					"gpio_combo_bgf_eint_pin",
					"gpio_wifi_eint_pin",
					"gpio_all_eint_pin",
					"gpio_combo_urxd_pin",
					"gpio_combo_utxd_pin",
					"gpio_pcm_daiclk_pin",
					"gpio_pcm_daipcmin_pin",
					"gpio_pcm_daipcmout_pin",
					"gpio_pcm_daisync_pin",
					"gpio_combo_i2s_ck_pin",
					"gpio_combo_i2s_ws_pin",
					"gpio_combo_i2s_dat_pin",
					"gpio_gps_sync_pin",
					"gpio_gps_lna_pin"};

GPIO_CTRL_INFO gpio_ctrl_info;

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
static void m6_wmt_gpio_dump_pin(const char *stage, UINT32 id)
{
	INT32 num;
	int val = -EINVAL;

	if (id >= GPIO_PIN_ID_MAX)
		return;

	num = gpio_ctrl_info.gpio_ctrl_state[id].gpio_num;
	if (num != DEFAULT_PIN_ID && gpio_is_valid(num))
		val = gpio_get_value(num);

	pr_warn("M6 WMT GPIO %s id=%u name=%s num=%d valid=%d val=%d pull_dis=%p in_pd=%p in_pu=%p out_lo=%p out_hi=%p\n",
		stage, id, gpio_pin_name[id], num,
		num != DEFAULT_PIN_ID && gpio_is_valid(num), val,
		gpio_ctrl_info.gpio_ctrl_state[id].gpio_state[GPIO_PULL_DIS],
		gpio_ctrl_info.gpio_ctrl_state[id].gpio_state[GPIO_IN_PULLDOWN],
		gpio_ctrl_info.gpio_ctrl_state[id].gpio_state[GPIO_IN_PULLUP],
		gpio_ctrl_info.gpio_ctrl_state[id].gpio_state[GPIO_OUT_LOW],
		gpio_ctrl_info.gpio_ctrl_state[id].gpio_state[GPIO_OUT_HIGH]);
}

static void m6_wmt_gpio_dump_key_pins(const char *stage)
{
	m6_wmt_gpio_dump_pin(stage, GPIO_COMBO_LDO_EN_PIN);
	m6_wmt_gpio_dump_pin(stage, GPIO_COMBO_PMUV28_EN_PIN);
	m6_wmt_gpio_dump_pin(stage, GPIO_COMBO_PMU_EN_PIN);
	m6_wmt_gpio_dump_pin(stage, GPIO_COMBO_RST_PIN);
	m6_wmt_gpio_dump_pin(stage, GPIO_COMBO_BGF_EINT_PIN);
	m6_wmt_gpio_dump_pin(stage, GPIO_WIFI_EINT_PIN);
}

INT32 wmt_gpio_init(struct platform_device *pdev)
{
	INT32 iret = 0;
	UINT32 i, j;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "mediatek,connectivity-combo");
	pr_warn("M6 WMT GPIO init pdev=%p dev_node=%p combo_node=%p\n",
		pdev, pdev ? pdev->dev.of_node : NULL, node);
	if (!node) {
		for (i = 0; i < GPIO_PIN_ID_MAX; i++)
			gpio_ctrl_info.gpio_ctrl_state[i].gpio_num = DEFAULT_PIN_ID;
		pr_err("wmt_gpio:can't find device tree node!\n");
		m6_wmt_gpio_dump_key_pins("missing-node");
		iret = -1;
		goto err;
	}

	gpio_ctrl_info.pinctrl_info = devm_pinctrl_get(&pdev->dev);
	pr_warn("M6 WMT GPIO pinctrl=%p err=%ld\n",
		gpio_ctrl_info.pinctrl_info,
		IS_ERR(gpio_ctrl_info.pinctrl_info) ?
			PTR_ERR(gpio_ctrl_info.pinctrl_info) : 0L);
	if (!IS_ERR_OR_NULL(gpio_ctrl_info.pinctrl_info)) {
		for (i = 0; i < GPIO_PIN_ID_MAX; i++) {
			gpio_ctrl_info.gpio_ctrl_state[i].gpio_num = of_get_named_gpio(node,
					gpio_pin_name[i], 0);
			if (gpio_ctrl_info.gpio_ctrl_state[i].gpio_num < 0)
				gpio_ctrl_info.gpio_ctrl_state[i].gpio_num = DEFAULT_PIN_ID;
			if (DEFAULT_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[i].gpio_num) {
				for (j = 0; j < GPIO_STATE_MAX; j++) {
					if (0 != strlen(gpio_state_name[i][j])) {
						gpio_ctrl_info.gpio_ctrl_state[i].gpio_state[j] =
							pinctrl_lookup_state(gpio_ctrl_info.pinctrl_info,
									gpio_state_name[i][j]);
					} else
						gpio_ctrl_info.gpio_ctrl_state[i].gpio_state[j] = NULL;
				}
			}
		}

		m6_wmt_gpio_dump_key_pins("after-of-parse");
		pr_err("wmt_gpio: gpio init start!\n");

		if (gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].gpio_state[GPIO_PULL_DIS]) {
			pinctrl_select_state(gpio_ctrl_info.pinctrl_info,
					gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].
					gpio_state[GPIO_PULL_DIS]);
			pr_err("wmt_gpio:set GPIO_COMBO_PMU_EN_PIN to GPIO_PULL_DIS done!\n");
		} else
			pr_err("wmt_gpio:set GPIO_COMBO_PMU_EN_PIN to GPIO_PULL_DIS fail, is NULL!\n");

		if (DEFAULT_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].gpio_num) {
			gpio_direction_output(gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].gpio_num,
					0);
			pr_err("wmt_gpio:set GPIO_COMBO_PMU_EN_PIN out to 0: %d!\n",
					gpio_get_value(gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_PMU_EN_PIN].gpio_num));
		}

		if (gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_state[GPIO_PULL_DIS]) {
			pinctrl_select_state(gpio_ctrl_info.pinctrl_info,
					gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_state[GPIO_PULL_DIS]);
			pr_err("wmt_gpio:set GPIO_COMBO_RST_PIN to GPIO_PULL_DIS done!\n");
		} else
			pr_err("wmt_gpio:set GPIO_COMBO_RST_PIN to GPIO_PULL_DIS fail, is NULL!\n");

		if (DEFAULT_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_num) {
			gpio_direction_output(gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_num,
					0);
			pr_err("wmt_gpio:set GPIO_COMBO_RST_PIN out to 0: %d!\n",
					gpio_get_value(gpio_ctrl_info.gpio_ctrl_state[GPIO_COMBO_RST_PIN].gpio_num));
		}

		if (gpio_ctrl_info.gpio_ctrl_state[GPIO_WIFI_EINT_PIN].gpio_state[GPIO_IN_PULLUP]) {
			pinctrl_select_state(gpio_ctrl_info.pinctrl_info,
					gpio_ctrl_info.gpio_ctrl_state[GPIO_WIFI_EINT_PIN].gpio_state[GPIO_IN_PULLUP]);
			pr_err("wmt_gpio:set GPIO_WIFI_EINT_PIN to GPIO_IN_PULLUP done!\n");
		} else
			pr_err("wmt_gpio:set GPIO_WIFI_EINT_PIN to GPIO_IN_PULLUP fail, is NULL!\n");

		if (gpio_ctrl_info.gpio_ctrl_state[GPIO_PCM_DAICLK_PIN].gpio_state[GPIO_PULL_DIS]) {
			pinctrl_select_state(gpio_ctrl_info.pinctrl_info,
					gpio_ctrl_info.gpio_ctrl_state[GPIO_PCM_DAICLK_PIN].gpio_state[GPIO_PULL_DIS]);
			pr_err("wmt_gpio:set GPIO_PCM_DAICLK_PIN to GPIO_PULL_DIS done!\n");
		} else
			pr_err("wmt_gpio:set GPIO_PCM_DAICLK_PIN to GPIO_PULL_DIS fail, is NULL!\n");

		if (gpio_ctrl_info.gpio_ctrl_state[GPIO_PCM_DAIPCMIN_PIN].gpio_state[GPIO_PULL_DIS]) {
			pinctrl_select_state(gpio_ctrl_info.pinctrl_info,
					gpio_ctrl_info.gpio_ctrl_state[GPIO_PCM_DAIPCMIN_PIN].
					gpio_state[GPIO_PULL_DIS]);
			pr_err("wmt_gpio:set GPIO_PCM_DAIPCMIN_PIN to GPIO_PULL_DIS done!\n");
		} else
			pr_err("wmt_gpio:set GPIO_PCM_DAIPCMIN_PIN to GPIO_PULL_DIS fail, is NULL!\n");

		if (gpio_ctrl_info.gpio_ctrl_state[GPIO_PCM_DAIPCMOUT_PIN].gpio_state[GPIO_PULL_DIS]) {
			pinctrl_select_state(gpio_ctrl_info.pinctrl_info,
					gpio_ctrl_info.gpio_ctrl_state[GPIO_PCM_DAIPCMOUT_PIN].
					gpio_state[GPIO_PULL_DIS]);
			pr_err("wmt_gpio:set GPIO_PCM_DAIPCMOUT_PIN to GPIO_PULL_DIS done!\n");
		} else
			pr_err("wmt_gpio:set GPIO_PCM_DAIPCMOUT_PIN to GPIO_PULL_DIS fail, is NULL!\n");

		if (gpio_ctrl_info.gpio_ctrl_state[GPIO_PCM_DAISYNC_PIN].gpio_state[GPIO_PULL_DIS]) {
			pinctrl_select_state(gpio_ctrl_info.pinctrl_info,
					gpio_ctrl_info.gpio_ctrl_state[GPIO_PCM_DAISYNC_PIN].
					gpio_state[GPIO_PULL_DIS]);
			pr_err("wmt_gpio:set GPIO_PCM_DAISYNC_PIN to GPIO_PULL_DIS done!\n");
		} else
			pr_err("wmt_gpio:set GPIO_PCM_DAISYNC_PIN to GPIO_PULL_DIS fail, is NULL!\n");

		pr_err("wmt_gpio: gpio init done!\n");
		m6_wmt_gpio_dump_key_pins("after-init");
	} else {
		pr_err("wmt_gpio:can't find pinctrl dev!\n");
		iret = -1;
	}
err:
	return iret;
}

INT32 wmt_gpio_deinit(VOID)
{
	INT32 iret = 0;
	UINT32 i;
	UINT32 j;

	for (i = 0; i < GPIO_PIN_ID_MAX; i++) {
		gpio_ctrl_info.gpio_ctrl_state[i].gpio_num = DEFAULT_PIN_ID;
		if (DEFAULT_PIN_ID != gpio_ctrl_info.gpio_ctrl_state[i].gpio_num) {
			for (j = 0; j < GPIO_STATE_MAX; j++) {
				if (0 != strlen(gpio_state_name[i][j]))
					gpio_ctrl_info.gpio_ctrl_state[i].gpio_state[j] = NULL;
			}
		}
	}
	if (gpio_ctrl_info.pinctrl_info) {
		devm_pinctrl_put(gpio_ctrl_info.pinctrl_info);
		gpio_ctrl_info.pinctrl_info = NULL;
	}

	return iret;
}
