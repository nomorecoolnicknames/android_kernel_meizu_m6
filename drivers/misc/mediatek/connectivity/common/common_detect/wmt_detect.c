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

#include <mtk_wcn_cmb_stub.h>
#include <linux/platform_device.h>

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-DETECT]"

#include "wmt_detect.h"
#include "wmt_gpio.h"

#if MTK_WCN_REMOVE_KO
#include "conn_drv_init.h"
#endif
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#define WMT_DETECT_MAJOR 154
#define WMT_DETECT_DEV_NUM 1
#define WMT_DETECT_DRVIER_NAME "mtk_wcn_detect"
#define WMT_DETECT_DEVICE_NAME "wmtdetect"

#ifdef CONFIG_HUAWEI_DSM
#include <dsm/dsm_pub.h>

#define DSM_WIFI_BUF_SIZE           (1024)   /*Byte*/
#define DSM_WIFI_MOD_NAME           "dsm_wifi"
#define DSM_BLUETOOTH_MOD_NAME      "dsm_bluetooth"

int wifi_dsm_register(void);
int wifi_dsm_report_num(int dsm_err_no, char *err_msg, int err_code);
int wifi_dsm_report_info(int error_no, void *log, int size);
#endif

static int INIT_ONE_SHOT= 1;
struct class *pDetectClass = NULL;
struct device *pDetectDev = NULL;
static int gWmtDetectMajor = WMT_DETECT_MAJOR;
static struct cdev gWmtDetectCdev;
unsigned int gWmtDetectDbgLvl = WMT_DETECT_LOG_INFO;
static ENUM_WMT_CHIP_TYPE g_chip_type = WMT_CHIP_TYPE_INVALID;

#ifdef CONFIG_HUAWEI_DSM
static struct dsm_dev wifi_dsm_info = {
    .name = DSM_WIFI_MOD_NAME,
    .fops = NULL,
    .buff_size = DSM_WIFI_BUF_SIZE,
};

static struct dsm_dev bluetooth_dsm_info = {
    .name = DSM_BLUETOOTH_MOD_NAME,
    .fops = NULL,
    .buff_size = DSM_WIFI_BUF_SIZE,
};

struct dsm_client *wifi_dclient = NULL;
struct dsm_client *bluetooth_dclient = NULL;

int wifi_dsm_register(void)
{
    if (NULL != wifi_dclient) {
        pr_debug("wifi_dclient had been register!\n");
        return 0;
    }

    wifi_dclient = dsm_register_client(&wifi_dsm_info);
    if (NULL == wifi_dclient) {
        pr_err("wifi_dclient register failed!\n");
    }

    return 0;
}
EXPORT_SYMBOL(wifi_dsm_register);

int bluetooth_dsm_register(void)
{
    if (NULL != bluetooth_dclient) {
        pr_debug("bluetooth_dclient had been register!\n");
        return 0;
    }

    bluetooth_dclient = dsm_register_client(&bluetooth_dsm_info);
    if (NULL == bluetooth_dclient) {
        pr_err("bluetooth_dclient register failed!\n");
    }

    return 0;
}
EXPORT_SYMBOL(bluetooth_dsm_register);

int wifi_dsm_report_num(int dsm_err_no, char *err_msg, int err_code)
{
    int err = 0;

    if (NULL == wifi_dclient) {
        pr_err("%s wifi_dclient did not register!\n", __func__);
        return 0;
    }

    err = dsm_client_ocuppy(wifi_dclient);
    if (0 != err) {
        pr_err("%s user buffer is busy!\n", __func__);
        return 0;
    }

    pr_err("%s user buffer apply successed, dsm_err_no=%d, err_code=%d!\n",
        __func__, dsm_err_no, err_code);

    err = dsm_client_record(wifi_dclient, "err_msg:%s;err_code:%d;\n",err_msg,err_code);
    if(0 == err) {
         pr_err("%s:dsm_client_record fail\n", __func__);
         return 0;
    }

    dsm_client_notify(wifi_dclient, dsm_err_no);

    return 0;
}
EXPORT_SYMBOL(wifi_dsm_report_num);

int bluetooth_dsm_report_num(int dsm_err_no, char *err_msg, int err_code)
{
    int err = 0;

    if (NULL == bluetooth_dclient) {
        pr_err("%s bluetooth_dclient did not register!\n", __func__);
        return 0;
    }

    err = dsm_client_ocuppy(bluetooth_dclient);
    if (0 != err) {
        pr_err("%s user buffer is busy!\n", __func__);
        return 0;
    }

    pr_err("%s user buffer apply successed, dsm_err_no=%d, err_code=%d!\n",
        __func__, dsm_err_no, err_code);

    err = dsm_client_record(bluetooth_dclient, "err_msg:%s;err_code:%d;\n",err_msg,err_code);
    if(0 == err) {
        pr_err("%s:dsm_client_record fail\n", __func__);
        return 0;
    }

    dsm_client_notify(bluetooth_dclient, dsm_err_no);

    return 0;
}
EXPORT_SYMBOL(bluetooth_dsm_report_num);

int wifi_dsm_report_info(int error_no, void *log, int size)
{
    int err = 0;
    int rsize = 0;

    if (NULL == wifi_dclient) {
        pr_err("%s wifi_dclient did not register!\n", __func__);
        return 0;
    }

    if ((error_no < DSM_WIFI_ERR) || (error_no > DSM_WIFI_ROOT_NOT_RIGHT_ERR)
	    || (NULL == log) || (size < 0)) {
        pr_err("%s input param error!\n", __func__);
        return 0;
    }

    err = dsm_client_ocuppy(wifi_dclient);
    if (0 != err) {
        pr_err("%s user buffer is busy!\n", __func__);
        return 0;
    }

    if (size > DSM_WIFI_BUF_SIZE) {
        rsize = DSM_WIFI_BUF_SIZE;
    } else {
        rsize = size;
    }
    err = dsm_client_copy(wifi_dclient, log, rsize);
    if(0 == err) {
        pr_err("%s:dsm_client_copy fail\n", __func__);
        return 0;
    }
    dsm_client_notify(wifi_dclient, error_no);

    return 0;
}
EXPORT_SYMBOL(wifi_dsm_report_info);
#endif

#ifdef MTK_WCN_COMBO_CHIP_SUPPORT
inline unsigned int wmt_plat_get_soc_chipid(void)
{
	WMT_DETECT_INFO_FUNC("no soc chip supported, due to MTK_WCN_SOC_CHIP_SUPPORT is not set.\n");
	return -1;
}
#endif

static int wmt_detect_open(struct inode *inode, struct file *file)
{
	WMT_DETECT_INFO_FUNC("open major %d minor %d (pid %d)\n", imajor(inode), iminor(inode), current->pid);

	return 0;
}

static int wmt_detect_close(struct inode *inode, struct file *file)
{
	WMT_DETECT_INFO_FUNC("close major %d minor %d (pid %d)\n", imajor(inode), iminor(inode), current->pid);

	return 0;
}

static ssize_t wmt_detect_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	WMT_DETECT_INFO_FUNC(" ++\n");
	WMT_DETECT_INFO_FUNC(" --\n");

	return 0;
}

ssize_t wmt_detect_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	WMT_DETECT_INFO_FUNC(" ++\n");
	WMT_DETECT_INFO_FUNC(" --\n");

	//return 0;
	return count;
}

static long wmt_detect_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	WMT_DETECT_INFO_FUNC("cmd (%d),arg(%ld)\n", cmd, arg);

	switch (cmd) {
	case COMBO_IOCTL_GET_CHIP_ID:
		/*just get chipid from sdio-detect module */
		/*check if external combo chip exists or not */
		/*if yes, just return combo chip id */
		/*if no, get soc chipid */
		retval = mtk_wcn_wmt_chipid_query();
		break;

	case COMBO_IOCTL_SET_CHIP_ID:
		mtk_wcn_wmt_set_chipid(arg);

		break;

	case COMBO_IOCTL_EXT_CHIP_PWR_ON:
		retval = wmt_detect_ext_chip_pwr_on();
		break;

	case COMBO_IOCTL_EXT_CHIP_DETECT:
		retval = wmt_detect_ext_chip_detect();
		break;

	case COMBO_IOCTL_EXT_CHIP_PWR_OFF:
		retval = wmt_detect_ext_chip_pwr_off();
		break;

	case COMBO_IOCTL_DO_SDIO_AUDOK:
		retval = sdio_detect_do_autok(arg);
		break;

	case COMBO_IOCTL_GET_SOC_CHIP_ID:
		retval = wmt_plat_get_soc_chipid();
		/*get soc chipid by HAL interface */
		break;

	case COMBO_IOCTL_MODULE_CLEANUP:
        if(INIT_ONE_SHOT){
#if (MTK_WCN_REMOVE_KO)
		    /*deinit SDIO-DETECT module */
		    retval = sdio_detect_exit();
#else
		    WMT_DETECT_INFO_FUNC("no MTK_WCN_REMOVE_KO defined\n");
#endif
		}
		break;

	case COMBO_IOCTL_DO_MODULE_INIT:
         if(INIT_ONE_SHOT){
            INIT_ONE_SHOT = 0;
#if (MTK_WCN_REMOVE_KO)
		/*deinit SDIO-DETECT module */
		    retval = do_connectivity_driver_init(arg);
#else
		    WMT_DETECT_INFO_FUNC("no MTK_WCN_REMOVE_KO defined\n");
#endif
		 }
		break;

	default:
		WMT_DETECT_WARN_FUNC("unknown cmd (%d)\n", cmd);
		retval = 0;
		break;
	}
	return retval;
}
#ifdef CONFIG_COMPAT
static long WMT_compat_detect_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret;

	WMT_DETECT_INFO_FUNC("cmd (%d)\n", cmd);
	ret = wmt_detect_unlocked_ioctl(filp, cmd, arg);
	return ret;
}
#endif
const struct file_operations gWmtDetectFops = {
	.open = wmt_detect_open,
	.release = wmt_detect_close,
	.read = wmt_detect_read,
	.write = wmt_detect_write,
	.unlocked_ioctl = wmt_detect_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = WMT_compat_detect_ioctl,
#endif
};

int wmt_detect_ext_chip_pwr_on(void)
{
	/*pre power on external chip */
	/* wmt_plat_pwr_ctrl(FUNC_ON); */
#ifdef MTK_WCN_COMBO_CHIP_SUPPORT
	WMT_DETECT_INFO_FUNC("++\n");
	if (0 != wmt_detect_chip_pwr_ctrl(1))
		return -1;
	if (0 != wmt_detect_sdio_pwr_ctrl(1))
		return -2;
	return 0;
#else
	WMT_DETECT_INFO_FUNC("combo chip is not supported\n");
	return -1;
#endif
}

int wmt_detect_ext_chip_pwr_off(void)
{
	/*pre power off external chip */
	/* wmt_plat_pwr_ctrl(FUNC_OFF); */
#ifdef MTK_WCN_COMBO_CHIP_SUPPORT
	WMT_DETECT_INFO_FUNC("--\n");
	wmt_detect_sdio_pwr_ctrl(0);
	return wmt_detect_chip_pwr_ctrl(0);
#else
	WMT_DETECT_INFO_FUNC("combo chip is not supported\n");
	return 0;
#endif
}

int wmt_detect_ext_chip_detect(void)
{
	int iRet = -1;
	unsigned int chipId = -1;
	/*if there is no external combo chip, return -1 */
	int bgfEintStatus = -1;

	WMT_DETECT_INFO_FUNC("++\n");
	/*wait for a stable time */
	msleep(20);

	/*read BGF_EINT_PIN status */
	bgfEintStatus = wmt_detect_read_ext_cmb_status();

	if (0 == bgfEintStatus) {
		/*external chip does not exist */
		WMT_DETECT_INFO_FUNC("external combo chip not detected\n");
	} else if (1 == bgfEintStatus) {
		/*combo chip exists */
		WMT_DETECT_INFO_FUNC("external combo chip detected\n");

		/*detect chipid by sdio_detect module */
		chipId = sdio_detect_query_chipid(1);
		if (0 <= hif_sdio_is_chipid_valid(chipId))
			WMT_DETECT_INFO_FUNC("valid external combo chip id (0x%x)\n", chipId);
		else
			WMT_DETECT_INFO_FUNC("invalid external combo chip id (0x%x)\n", chipId);
		iRet = 0;
	} else {
		/*Error exists */
		WMT_DETECT_ERR_FUNC("error happens when detecting combo chip\n");
	}
	WMT_DETECT_INFO_FUNC("--\n");
	/*return 0 */
	return iRet;
	/*todo: if there is external combo chip, power on chip return 0 */
}

int wmt_detect_set_chip_type(int chip_id)
{
	switch (chip_id) {
	case 0x6620:
	case 0x6628:
	case 0x6630:
	case 0x6632:
		g_chip_type = WMT_CHIP_TYPE_COMBO;
		break;
	default:
		g_chip_type = WMT_CHIP_TYPE_SOC;
		break;
	}
	return 0;
}
ENUM_WMT_CHIP_TYPE wmt_detect_get_chip_type(void)
{
	return g_chip_type;
}

#ifdef MTK_WCN_COMBO_CHIP_SUPPORT
static int wmt_detect_probe(struct platform_device *pdev)
{
	int ret = 0;

	WMT_DETECT_ERR_FUNC("platform name: %s\n", pdev->name);
	ret = wmt_gpio_init(pdev);
	if (-1 == ret)
		WMT_DETECT_ERR_FUNC("gpio init fail ret:%d\n", ret);
	return ret;
}

static int wmt_detect_remove(struct platform_device *pdev)
{
	wmt_gpio_deinit();
	return 0;
}
#endif

#ifdef MTK_WCN_COMBO_CHIP_SUPPORT
static struct of_device_id wmt_detect_match[] = {
	{ .compatible = "mediatek,connectivity-combo", },
	{}
};
MODULE_DEVICE_TABLE(of, wmt_detect_match);

static struct platform_driver wmt_detect_driver = {
	.probe = wmt_detect_probe,
	.remove = wmt_detect_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "mediatek,connectivity-combo",
		.of_match_table = wmt_detect_match,
	},
};
#endif

/*module_platform_driver(wmt_detect_driver);*/
static int wmt_detect_driver_init(void)
{
	dev_t devID = MKDEV(gWmtDetectMajor, 0);
	int cdevErr = -1;
	int ret = -1;

	ret = register_chrdev_region(devID, WMT_DETECT_DEV_NUM, WMT_DETECT_DRVIER_NAME);
	if (ret) {
		WMT_DETECT_ERR_FUNC("fail to register chrdev\n");
		return ret;
	}

	cdev_init(&gWmtDetectCdev, &gWmtDetectFops);
	gWmtDetectCdev.owner = THIS_MODULE;

	cdevErr = cdev_add(&gWmtDetectCdev, devID, WMT_DETECT_DEV_NUM);
	if (cdevErr) {
		WMT_DETECT_ERR_FUNC("cdev_add() fails (%d)\n", cdevErr);
		goto err1;
	}

	pDetectClass = class_create(THIS_MODULE, WMT_DETECT_DEVICE_NAME);
	if (IS_ERR(pDetectClass)) {
		WMT_DETECT_ERR_FUNC("class create fail, error code(%ld)\n", PTR_ERR(pDetectClass));
		goto err1;
	}

	pDetectDev = device_create(pDetectClass, NULL, devID, NULL, WMT_DETECT_DEVICE_NAME);
	if (IS_ERR(pDetectDev)) {
		WMT_DETECT_ERR_FUNC("device create fail, error code(%ld)\n", PTR_ERR(pDetectDev));
		goto err2;
	}

	WMT_DETECT_INFO_FUNC("driver(major %d) installed success\n", gWmtDetectMajor);

	/*init SDIO-DETECT module */
	sdio_detect_init();

#ifdef MTK_WCN_COMBO_CHIP_SUPPORT
	ret = platform_driver_register(&wmt_detect_driver);
	if (ret)
		WMT_DETECT_ERR_FUNC("platform driver register fail ret:%d\n", ret);
#endif

#ifdef CONFIG_HUAWEI_DSM
	wifi_dsm_register();
	bluetooth_dsm_register();
#endif

	return 0;

err2:

	if (pDetectClass) {
		class_destroy(pDetectClass);
		pDetectClass = NULL;
	}

err1:

	if (cdevErr == 0)
		cdev_del(&gWmtDetectCdev);

	if (ret == 0) {
		unregister_chrdev_region(devID, WMT_DETECT_DEV_NUM);
		gWmtDetectMajor = -1;
	}

	WMT_DETECT_ERR_FUNC("fail\n");

	return -1;
}

static void wmt_detect_driver_exit(void)
{
	dev_t dev = MKDEV(gWmtDetectMajor, 0);

#ifdef CONFIG_HUAWEI_DSM
	dsm_unregister_client(wifi_dclient, &wifi_dsm_info);
	dsm_unregister_client(bluetooth_dclient, &bluetooth_dsm_info);
#endif
	if (pDetectDev) {
		device_destroy(pDetectClass, dev);
		pDetectDev = NULL;
	}

	if (pDetectClass) {
		class_destroy(pDetectClass);
		pDetectClass = NULL;
	}

	cdev_del(&gWmtDetectCdev);
	unregister_chrdev_region(dev, WMT_DETECT_DEV_NUM);

#if !(MTK_WCN_REMOVE_KO)
/*deinit SDIO-DETECT module*/
	sdio_detect_exit();
#endif

#ifdef MTK_WCN_COMBO_CHIP_SUPPORT
	platform_driver_unregister(&wmt_detect_driver);
#endif

	WMT_DETECT_INFO_FUNC("done\n");
}

module_init(wmt_detect_driver_init);
module_exit(wmt_detect_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhiguo.Niu & Chaozhong.Liang @ MBJ/WCNSE/SS1");

module_param(gWmtDetectMajor, uint, 0);
