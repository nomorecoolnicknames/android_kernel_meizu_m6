/**
 * The device control driver for sunwave's fingerprint sensor.
 *
 * Copyright (C) 2016 Sunwave Corporation. <http://www.sunwavecorp.com>
 * Copyright (C) 2016 Langson L. <mailto: liangzh@sunwavecorp.com>
 *
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of the GNU General Public License as published by the Free 
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General 
 * Public License for more details.
**/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/input.h>
#include <linux/uaccess.h>

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/spi/spi.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include "mt_spi_hal.h"
#include "mt_spi.h"

#include "sf_ctl.h"

#define MODULE_NAME "sf_ctl"
#define xprintk(level, fmt, args...) printk(level MODULE_NAME": "fmt, ##args)

////////////////////////////////////////////////
/// TrustKernel add 2017-1-4 start
///////////////////////////////////////////////
/*static struct of_device_id sunwave_of_match[] = {
    { .compatible = "sunwave, sunwave_fp", },
    {}
}; */
////////////////////////////////////////////////
/// TrustKernel add 2017-1-4 end
///////////////////////////////////////////////

#ifndef CONFIG_OF
# error "error: this driver 'MODULE_NAME' only support dts."
#endif

/**
 * Define the driver version string.
 * There is NO need to modify 'rXXXX_yyyymmdd', it should be updated automatically
 * by the building script (see the 'Driver-revision' section in 'build.sh').
 */
#define SF_DRV_VERSION "v0.9.1-rXXXX_20170214"

struct sf_ctl_device {
	struct miscdevice miscdev;
	int irq_num;
	struct work_struct work_queue;
	struct input_dev *input;
};

typedef enum {

    STATE_RST_SET,
    STATE_RST_CLR,
    STATE_LDO_CLR,
    STATE_LDO_SET,
    /* Array size */
    SF_PIN_STATE_MAX
} sf_pin_state_t;

static const char *sf_pinctrl_state_names[SF_PIN_STATE_MAX] = {
    "reset_high",
    "reset_low",
    "ldo_low",
    "ldo_high",
};

struct mt_chip_conf sf_spi_conf={
    .setuptime=30,
    .holdtime=30,
    .high_time=50,
    .low_time=50,
    .cs_idletime=10,
    .ulthgh_thrsh=0,
    .cpol=SPI_CPOL_0,
    .cpha=SPI_CPHA_0,
    .rx_mlsb=SPI_MSB,
    .tx_mlsb=SPI_MSB,
    .tx_endian=SPI_LENDIAN,
    .rx_endian=SPI_LENDIAN,
    .com_mod=DMA_TRANSFER,
    .pause=0,
    .deassert=0,
    .ulthigh=0,
    .tckdly=0,
};

static struct pinctrl *sf_pinctrl = NULL;
static struct pinctrl_state *sf_pin_states[SF_PIN_STATE_MAX] = {NULL, };
#if 0
static int sf_ctl_device_power(bool on)
{
    int err = 0;
    //sf_pin_state_t state = on ? SF_PIN_STATE_PWR_ON : SF_PIN_STATE_PWR_OFF;
    

    err = pinctrl_select_state(sf_pinctrl, sf_pin_states[STATE_PW_SET]);
    xprintk(KERN_DEBUG, "%s(..) enter. err=%d\n", __FUNCTION__,err);
    return err;
}
#endif
static int sf_ctl_device_reset(void)
{
	int err = 0;
	printk("%s(..) enter.\n", __FUNCTION__);


    err = pinctrl_select_state(sf_pinctrl, sf_pin_states[STATE_RST_SET]);
    msleep(10);
    err = pinctrl_select_state(sf_pinctrl, sf_pin_states[STATE_RST_CLR]);
    msleep(20);
    err = pinctrl_select_state(sf_pinctrl, sf_pin_states[STATE_RST_SET]);

    return err;
}

static void sf_ctl_device_event(struct work_struct *ws)
{
    struct sf_ctl_device *sf_ctl_dev =
            container_of(ws, struct sf_ctl_device, work_queue);
    char *uevent_env[2] = { "SPI_STATE=finger", NULL };
    xprintk(KERN_DEBUG, "%s(..) enter.\n", __FUNCTION__);
    
    kobject_uevent_env(&sf_ctl_dev->miscdev.this_device->kobj,
            KOBJ_CHANGE, uevent_env);
}

static irqreturn_t sf_ctl_device_irq(int irq, void *dev_id)
{
	struct sf_ctl_device *sf_ctl_dev = (struct sf_ctl_device*)dev_id;

	disable_irq_nosync(irq);
	xprintk(KERN_DEBUG, "%s(irq = %d, ..) toggled.\n", __FUNCTION__, irq);

	schedule_work(&sf_ctl_dev->work_queue);

	enable_irq(irq);
	return IRQ_HANDLED;
}

static int sf_ctl_report_key_event(struct input_dev *input, sf_key_event_t *kevent)
{
    int err = 0;
    unsigned int key_code = KEY_UNKNOWN;
    xprintk(KERN_DEBUG, "%s(..) enter.\n", __FUNCTION__);

    switch (kevent->key) {
    case SF_KEY_HOME: key_code = KEY_HOME; break;
    case SF_KEY_MENU: key_code = KEY_MENU; break;
    case SF_KEY_BACK: key_code = KEY_BACK; break;
    default: break;
    }

    xprintk(KERN_DEBUG, "%s(..) enter.\n", __FUNCTION__);

    input_report_key(input, key_code, kevent->value);
    input_sync(input);

    xprintk(KERN_DEBUG, "%s(..) leave.\n", __FUNCTION__);
    return err;
}

static const char* sf_ctl_get_version(void)
{
    static char version[SF_DRV_VERSION_LEN] = {'\0', };
    strncpy(version, SF_DRV_VERSION, SF_DRV_VERSION_LEN);
    version[SF_DRV_VERSION_LEN - 1] = '\0';
    return (const char *)version;
}


static int sf_ctl_init_irq(struct sf_ctl_device *sf_ctl_dev)
{
    int err = 0;
    struct device_node *dev_node = NULL;
    xprintk(KERN_DEBUG, "%s(..) enter.\n", __FUNCTION__);


    /* Initialize the INT pin. */
 //   err = pinctrl_select_state(sf_pinctrl, sf_pin_states[STATE_EINT]);

    /* Get the irq number. */
    dev_node = of_find_compatible_node(NULL, NULL, "mediatek, eint_fingerprint-eint");
    if (!dev_node) {
        xprintk(KERN_ERR, "of_find_compatible_node(..) failed.\n");
        return (-ENODEV);
    }
    sf_ctl_dev->irq_num = irq_of_parse_and_map(dev_node, 0);
    xprintk(KERN_INFO, "irq number is %d.\n", sf_ctl_dev->irq_num);
    //sf_ctl_dev->irq_num = 7;
    /* Register interrupt callback. */
    err = request_irq(sf_ctl_dev->irq_num, sf_ctl_device_irq,
        IRQF_TRIGGER_FALLING, "sf-irq", (void*)sf_ctl_dev);
    if (err) {
        xprintk(KERN_ERR, "request_irq(..) = %d.\n", err);
    }
	else{
		xprintk(KERN_ERR, "sf request_irq success!\n");
    }
    return err;
}

////////////////////////////////////////////////////////////////////////////////
// struct file_operations fields.

static long sf_ctl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *dev = (struct miscdevice*)filp->private_data;
	struct sf_ctl_device *psf_ctl_dev =
			container_of(dev, struct sf_ctl_device, miscdev);
	int err = 0;
	sf_key_event_t kevent;
	xprintk(KERN_DEBUG, "%s(cmd = 0x%08x, ..)\n", __FUNCTION__, cmd);

	switch (cmd) {

	case SF_IOC_INIT_DRIVER: {
		err = sf_ctl_init_irq(psf_ctl_dev);
		if (err) {
			xprintk(KERN_ERR, "sf_ctl_init_irq failed with %d.\n", err);
			//return err;
		}
                err = 0;
		xprintk(KERN_ERR,"======= sf init finished======\n");
	    break;
	}

	case SF_IOC_DEINIT_DRIVER: {
	    // TODO:
	    break;
	}

	case SF_IOC_RESET_DEVICE: {
		sf_ctl_device_reset();
		break;
	}

    case SF_IOC_ENABLE_IRQ: {
        // TODO:
        break;
    }

    case SF_IOC_DISABLE_IRQ: {
        // TODO:
        break;
    }

    case SF_IOC_ENABLE_SPI_CLK: {
        // TODO:
        break;
    }

    case SF_IOC_DISABLE_SPI_CLK: {
        // TODO:
        break;
    }

    case SF_IOC_ENABLE_POWER: {
        // TODO:
        break;
    }

    case SF_IOC_DISABLE_POWER: {
        // TODO:
        break;
    }

	case SF_IOC_REPORT_KEY_EVENT: {
        if (copy_from_user(&kevent, (sf_key_event_t *)arg, sizeof(sf_key_event_t))) {
            xprintk(KERN_ERR, "copy_from_user(..) failed.\n");
            err = (-EFAULT);
            break;
        }
        err = sf_ctl_report_key_event(psf_ctl_dev->input, &kevent);
	    break;
	}

	case SF_IOC_SYNC_CONFIG: {
	    // TODO:
	    break;
	}

	case SF_IOC_GET_VERSION: {
        if (copy_to_user((void *)arg, sf_ctl_get_version(), SF_DRV_VERSION_LEN)) {
            xprintk(KERN_ERR, "copy_to_user(..) failed.\n");
            err = (-EFAULT);
            break;
        }
	    break;
	}

	default:
		err = (-EINVAL);
		break;
	}
	return err;
}

static int sf_ctl_open(struct inode *inode, struct file *filp)
{
	xprintk(KERN_DEBUG, "%s(..) enter.\n", __FUNCTION__);
	return 0;
}

static int sf_ctl_release(struct inode *inode, struct file *filp)
{
	xprintk(KERN_DEBUG, "%s(..) enter.\n", __FUNCTION__);
	return 0;
}

////////////////////////////////////////////////////////////////////////////////

static struct file_operations sf_ctl_fops = {
	.owner		    = THIS_MODULE,
	.unlocked_ioctl = sf_ctl_ioctl,
	.open		    = sf_ctl_open,
	.release	    = sf_ctl_release,
};

static struct sf_ctl_device sf_ctl_dev = {
	.miscdev = {
		.minor	= MISC_DYNAMIC_MINOR,
		.name	= "sunwave_fp",
		.fops	= &sf_ctl_fops,
	}, 0,
};
#if 1
#ifdef CONFIG_OF
static const struct of_device_id sf_of_match[] = {
	{ .compatible = "fingerprint,fingerprint", },
	//{ .compatible = "mediatek,sunwave-fp", },
	{},
};
MODULE_DEVICE_TABLE(of, sf_of_match);
#endif

static struct spi_board_info spi_board_devs[] __initdata = {
	[0] = {
	.modalias = "sunwave_fp",
	.bus_num = 0,
	.chip_select = 0,
	.mode = SPI_MODE_0,
	.controller_data=&sf_spi_conf,
	},
};

#if 1
int sunwave_power_on(struct spi_device* spi, bool onoff)
{
   int err = 0;
        printk("%s(..) enter.\n", __FUNCTION__);


    err = pinctrl_select_state(sf_pinctrl, sf_pin_states[STATE_LDO_SET]);
    msleep(10);
    
	return 0;

#if 0
	struct regulator *reg = NULL;
	int ret = 0;
	printk("miles2---> [FUNC]%s [LINE]:%d\n", __FUNCTION__, __LINE__);
	
	if(onoff)
	{
		reg = regulator_get(&(spi->dev), "vfp");
		if(!reg)
		{
			printk("miles2---> [FUNC]%s [LINE]:%d reg is NULL!!!\n", __FUNCTION__, __LINE__);
			return -1;		
		}
	
		ret = regulator_set_voltage(reg, 2800000, 2800000);	/*set 2.8v*/
		if (ret) 
		{
			printk("miles-->vfp regulator_set_voltage(%d) failed!\n", ret);
			return -1;
		}
		ret = regulator_enable(reg);	/*enable regulator*/
		if (ret)
		{
			printk("miles-->vfp regulator_enable() failed!\n");
			return -2;
		}
	}else {
		return -1;//not use for now
	}
	
	return 0;
#endif	
}
#endif 
static int sf_probe(struct spi_device *spi) {
//	struct mt_spi_t *mt_spi = NULL;
	struct spi_device *sf_spi = spi;
	int err = 0;

	printk("lanhai %s enter", __func__);
	
	xprintk(KERN_ERR, "lanhai %s enter\n", __func__);
	err = sunwave_power_on(spi, 1);
        if(err<0)
        {
		printk(" %s sunwave_power_on failed", __func__);
	}
        err = sf_ctl_device_reset();
	/*sf_spi->mode = SPI_MODE_0;
	sf_spi->bits_per_word = 8;
	sf_spi->max_speed_hz = 1 * 1000 * 1000;
	memcpy(&spi_mcc, &spi_ctrdata, sizeof(struct mt_chip_conf));
	sf_spi->controller_data = (void *)&spi_mcc;

	spi_setup(sf_spi); */
#if 0	
	mt_spi = spi_master_get_devdata(sf_spi->master);
	if (!mt_spi) {
        	xprintk(KERN_ERR, "fail to get mediatek spi device.\n");
	       printk("lanhai %s enter", __func__);
        	dump_stack();
       	 return (-ENODEV);
       }

	mt_spi_enable_master_clk(mt_spi); // lanh 2016-11-21
#else
      mt_spi_enable_master_clk(sf_spi);
#endif
	printk("lanhai %s enter", __func__);

	xprintk(KERN_ERR, "lanhai %s leave\n", __func__);
    return err;
}

static int sf_remove(struct spi_device *spi) {
	return 0;
}

static struct spi_driver sf_spi_driver = {
	.driver = {
		.name = "sunwave_fp",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = sf_of_match,
#endif
	},
	.probe = sf_probe,
	.remove = sf_remove,
};
#endif
// see sf_spi.c
extern int  sf_spi_platform_init(void);
extern void sf_spi_platform_exit(void);

////////////////////////////////////////////////////////////////////////////////

static int sf_ctl_init_gpio_pins(void)
{
    int i, err = 0;
    struct platform_device *pdev = NULL;
    struct device_node *dev_node = NULL;
    xprintk(KERN_DEBUG, "%s(..) enter.\n", __FUNCTION__);

    dev_node = of_find_compatible_node(NULL, NULL, "mediatek,fingerprint");
    if (!dev_node) {
        xprintk(KERN_ERR, "of_find_compatible_node(..) failed.\n");
        return (-ENODEV);
    }
    pdev = of_find_device_by_node(dev_node);
    if (!pdev) {
        xprintk(KERN_ERR, "of_find_device_by_node(..) failed.\n");
        return (-ENODEV);
    }

    sf_pinctrl = devm_pinctrl_get(&pdev->dev);
    if (!sf_pinctrl) {
        xprintk(KERN_ERR, "devm_pinctrl_get(..) failed.\n");
        return (-ENODEV);
    }

    for (i = 0; i < SF_PIN_STATE_MAX; ++i) {
        sf_pin_states[i] = pinctrl_lookup_state(sf_pinctrl,
                sf_pinctrl_state_names[i]);
        if (IS_ERR(sf_pin_states[i])) {
    		printk("can't find '%s' pinctrl_state.\n",
                    sf_pinctrl_state_names[i]);
            err = (-ENODEV);
            break;
        }
    }

    if (i < SF_PIN_STATE_MAX) {
        xprintk(KERN_ERR, "%s() failed.\n", __FUNCTION__);
    }

    return err;
}



static int sf_ctl_init_input(void)
{
    int err = 0;
    xprintk(KERN_DEBUG, "%s(..) enter.\n", __FUNCTION__);

    sf_ctl_dev.input = input_allocate_device();
    if (!sf_ctl_dev.input) {
        xprintk(KERN_ERR, "input_allocate_device(..) failed.\n");
        return (-ENOMEM);
    }
    sf_ctl_dev.input->name = "sf-keys";

    __set_bit(EV_KEY  , sf_ctl_dev.input->evbit );
    __set_bit(KEY_HOME, sf_ctl_dev.input->keybit);
    __set_bit(KEY_MENU, sf_ctl_dev.input->keybit);
    __set_bit(KEY_BACK, sf_ctl_dev.input->keybit);

    err = input_register_device(sf_ctl_dev.input);
    if (err) {
        xprintk(KERN_ERR, "input_register_device(..) = %d.\n", err);
        input_free_device(sf_ctl_dev.input);
        sf_ctl_dev.input = NULL;
        return (-ENODEV);
    }

    xprintk(KERN_DEBUG, "%s(..) leave.\n", __FUNCTION__);
    return err;
}



static int __init sf_ctl_driver_init(void)
{
	int err = 0;
	xprintk(KERN_ERR, "%s sf_ctl_driver_init start...", __func__);
	/* Initialize the GPIO pins. */

	

	/* Initialize the GPIO pins. */
	err = sf_ctl_init_gpio_pins();
    if (err) {
        xprintk(KERN_ERR, "sf_ctl_init_gpio_pins failed with %d.\n", err);
        return err;
    }

    /* Initialize the interrupt callback. */
/*
    err = sf_ctl_init_irq(&sf_ctl_dev);
    if (err) {
        xprintk(KERN_ERR, "sf_ctl_init_irq failed with %d.\n", err);
        return err;
    }
*/
    /* Initialize the input subsystem. */
    err = sf_ctl_init_input();
    if (err) {
        xprintk(KERN_ERR, "sf_ctl_init_input failed with %d.\n", err);

        free_irq(sf_ctl_dev.irq_num, (void*)&sf_ctl_dev);
        return err;
    }


       // err = sf_ctl_device_power(true);
      	spi_register_board_info(spi_board_devs, ARRAY_SIZE(spi_board_devs));
	err = spi_register_driver(&sf_spi_driver); 
	if (err < 0) {
		xprintk(KERN_ERR, "%s, Failed to register SPI driver.\n", __func__);
		return -EINVAL;
	} 
	xprintk(KERN_ERR, "%s spi register success", __func__);
	/* Register as a miscellaneous device. */
	err = misc_register(&sf_ctl_dev.miscdev);
	if (err) {
        xprintk(KERN_ERR, "misc_register(..) = %d.\n", err);

        input_unregister_device(sf_ctl_dev.input);
        free_irq(sf_ctl_dev.irq_num, (void*)&sf_ctl_dev);
    	return err;
    }
    INIT_WORK(&sf_ctl_dev.work_queue, sf_ctl_device_event);

//    err = sf_spi_platform_init();
	xprintk(KERN_INFO, "sunwave fingerprint device control driver registered.\n");
	xprintk(KERN_INFO, "driver version: '%s'.\n", sf_ctl_get_version());
	return err;
}

static void __exit sf_ctl_driver_exit(void)
{
    if (sf_ctl_dev.input) {
        input_unregister_device(sf_ctl_dev.input);
    }

    if (sf_ctl_dev.irq_num >= 0) {
    	free_irq(sf_ctl_dev.irq_num, (void*)&sf_ctl_dev);
    }
	misc_deregister(&sf_ctl_dev.miscdev);

//	sf_spi_platform_exit();
	xprintk(KERN_INFO, "sunwave fingerprint device control driver released.\n");
}

module_init(sf_ctl_driver_init);
module_exit(sf_ctl_driver_exit);

MODULE_DESCRIPTION("The device control driver for sunwave's fingerprint sensor.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Langson L. <liangzh@sunwavecorp.com>");

