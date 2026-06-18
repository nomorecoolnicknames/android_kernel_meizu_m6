#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include "fingerprint.h"
//#include <linux/hardware_info.h>
#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
#include <mt_spi.h>
#include <mt_spi_hal.h>
#ifdef CONFIG_APP_INFO
#include <misc/app_info.h>
#endif
char hardware_info[30] = {0};

//extern void adreno_force_waking_gpu(void);
int get_fp_vendor(void);
unsigned int snr_flag = 0;

#if defined (CONFIG_HUAWEI_DSM)
#include <dsm/dsm_pub.h>
static struct dsm_dev dsm_fingerprint =
{
    .name = "dsm_fingerprint",
    .device_name = "fpc",
    .ic_name = "NNN",
    .module_name = "NNN",
    .fops = NULL,
    .buff_size = 1024,
};
static struct dsm_client* fingerprint_dclient = NULL;
#endif

extern int hardwareinfo_set_prop(int cmd, const char* name);

// TODO: Vendor ID should match fp_get_vendor()

enum
{
    FP_VENDOR_INVALID = 0,
    FPC_VENDOR,
    GOODIX_VENDOR,
    LEADCORE_VENDOR,
    CHIPSAIL_VENDOR,
};

#define GOODIX_CHIP_3268   0x3268
#define SILEAD_CHIP_6163     0x6163
typedef enum
{
    SPI_CLK_DISABLE = 0,
    SPI_CLK_ENABLE,
}spi_clk_state;
void fingerprint_gpio_output_dts(struct fp_data* fingerprint, int pin, int level)
{
    // mutex_lock(&spidev_set_gpio_mutex);
    //printk("[fpsensor]fpsensor_gpio_output_dts: gpio= %d, level = %d\n",gpio,level);
    if (pin == FINGERPRINT_RST_PIN)
    {
        if (level)
        { pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_rst_high); }
        else
        { pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_rst_low); }
    }
    else if (pin == FINGERPRINT_SPI_CS_PIN)
    {
        if (level)
        { pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_cs_high); }
        else
        { pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_cs_low); }
    }
    else if (pin == FINGERPRINT_SPI_MO_PIN)
    {
        if (level)
        { pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_mo_high); }
        else
        { pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_mo_low); }
    }
    else if (pin == FINGERPRINT_SPI_CK_PIN)
    {
        if (level)
        { pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_ck_high); }
        else
        { pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_ck_low); }
    }
    else if (pin == FINGERPRINT_SPI_MI_PIN)
    {
        if (level)
        { pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_mi_high); }
        else
        { pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_mi_low); }
    }

    //mutex_unlock(&spidev_set_gpio_mutex);
}


//add for fingerprint autotest
static ssize_t result_show(struct device* device,
                           struct device_attribute* attribute,
                           char* buffer)
{
    struct fp_data* fingerprint = dev_get_drvdata(device);
    return scnprintf(buffer, PAGE_SIZE, "%i\n", fingerprint->autotest_input);
}

static ssize_t result_store(struct device* device,
                            struct device_attribute* attribute,
                            const char* buffer, size_t count)
{
    struct fp_data* fingerprint = dev_get_drvdata(device);
    fingerprint->autotest_input = simple_strtoul(buffer, NULL, 10);
    sysfs_notify(&fingerprint->pf_dev->dev.kobj, NULL, "result");
    return count;
}

static DEVICE_ATTR(result, S_IRUSR | S_IWUSR, result_show, result_store);

/**
 * sysf node to check the interrupt status of the sensor, the interrupt
 * handler should perform sysf_notify to allow userland to poll the node.
 */
static ssize_t irq_get(struct device* device,
                       struct device_attribute* attribute,
                       char* buffer)
{
    struct fp_data* fingerprint = dev_get_drvdata(device);
    int irq = gpio_get_value(fingerprint->irq_gpio);

    if ((1 == irq) && (fp_LCD_POWEROFF == atomic_read(&fingerprint->state)))
    {
        //adreno_force_waking_gpu();
    }

    return scnprintf(buffer, PAGE_SIZE, "%i\n", irq);
}

/**
 * writing to the irq node will just drop a printk message
 * and return success, used for latency measurement.
 */
static ssize_t irq_ack(struct device* device,
                       struct device_attribute* attribute,
                       const char* buffer, size_t count)
{
    //struct fp_data* fingerprint = dev_get_drvdata(device);
    fpc_log_info("[%s]buffer=%s.\n", __func__, buffer);
    return count;
}

static DEVICE_ATTR(irq, S_IRUSR | S_IWUSR, irq_get, irq_ack);
static ssize_t sensor_mode(struct device* device, struct device_attribute* attribute, char* buffer)
{
    struct fp_data* fingerprint = dev_get_drvdata(device);

    return scnprintf(buffer, 30, "%s", fingerprint->sensor_mode);
}

static DEVICE_ATTR(mode, S_IRUSR, sensor_mode, NULL);

static ssize_t read_image_flag_show(struct device* device,
                                    struct device_attribute* attribute,
                                    char* buffer)
{
    struct fp_data* fingerprint = dev_get_drvdata(device);
    return scnprintf(buffer, PAGE_SIZE, "%u", (unsigned int)fingerprint->read_image_flag);
}
static ssize_t read_image_flag_store(struct device* device,
                                     struct device_attribute* attribute,
                                     const char* buffer, size_t count)
{
    struct fp_data* fingerprint = dev_get_drvdata(device);
    fingerprint->read_image_flag = simple_strtoul(buffer, NULL, 10);
    return (ssize_t)count;
}

static DEVICE_ATTR(read_image_flag, S_IRUSR | S_IWUSR, read_image_flag_show, read_image_flag_store);

static ssize_t snr_show(struct device* device,
                        struct device_attribute* attribute,
                        char* buffer)
{
    struct fp_data* fingerprint = dev_get_drvdata(device);
    return scnprintf(buffer, PAGE_SIZE, "%d", fingerprint->snr_stat);
}

static ssize_t snr_store(struct device* device,
                         struct device_attribute* attribute,
                         const char* buffer, size_t count)
{
    struct fp_data* fingerprint = dev_get_drvdata(device);
    fingerprint->snr_stat = simple_strtoul(buffer, NULL, 10);

    if (fingerprint->snr_stat)
    {
        snr_flag = 1;
    }
    else
    { snr_flag = 0; }

    fpc_log_err("snr_store snr_flag = %u\n", snr_flag);
    return count;
}

static DEVICE_ATTR(snr, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, snr_show, snr_store);

static ssize_t nav_show(struct device* device,
                        struct device_attribute* attribute,
                        char* buffer)
{
    struct fp_data* fingerprint = dev_get_drvdata(device);

    if (NULL == fingerprint)
    {return -EINVAL;}

    return scnprintf(buffer, PAGE_SIZE, "%d", fingerprint->nav_stat);
}

static ssize_t nav_store(struct device* device,
                         struct device_attribute* attribute,
                         const char* buffer, size_t count)
{
    struct fp_data* fingerprint = dev_get_drvdata(device);

    if (NULL == fingerprint)
    {return -EINVAL;}

    fingerprint->nav_stat = simple_strtoul(buffer, NULL, 10);
    return count;
}

/*lint -save -e* */
static DEVICE_ATTR(nav, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, nav_show, nav_store);
/*lint -restore*/

static ssize_t fingerprint_chip_info_show(struct device* device, struct device_attribute* attribute, char* buf)
{
   return scnprintf(buf, 50, "%s\n", hardware_info);
}

static DEVICE_ATTR(fingerprint_chip_info, S_IRUSR  | S_IRGRP | S_IROTH, fingerprint_chip_info_show, NULL);

static struct attribute* attributes[] =
{
    &dev_attr_irq.attr,
    &dev_attr_fingerprint_chip_info.attr,
    &dev_attr_result.attr,
    &dev_attr_read_image_flag.attr,
    &dev_attr_snr.attr,
    &dev_attr_nav.attr,
    &dev_attr_mode.attr,
    NULL
};

static const struct attribute_group attribute_group =
{
    .attrs = attributes,
};

static irqreturn_t fingerprint_irq_handler(int irq, void* handle)
{
    struct fp_data* fingerprint = handle;

    smp_rmb();

    if (fingerprint->wakeup_enabled )
    {
        wake_lock_timeout(&fingerprint->ttw_wl, msecs_to_jiffies(FPC_TTW_HOLD_TIME));
    }

    sysfs_notify(&fingerprint->pf_dev->dev.kobj, NULL, dev_attr_irq.attr.name);
    return IRQ_HANDLED;
}

void fingerprint_get_navigation_adjustvalue(const struct device* dev, struct fp_data* fp_data)
{
    struct device_node* np;
    unsigned int adjust1 = NAVIGATION_ADJUST_NOREVERSE;
    unsigned int adjust2 = NAVIGATION_ADJUST_NOTURN;

#if 0

    if (!dev || !dev->of_node)
    {
        fpc_log_err("%s failed dev or dev node is NULL\n", __func__);
        return;
    }

    np = dev->of_node;
#else
    np = of_find_compatible_node(NULL, NULL, "huawei,fingerprint");
#endif

    // TODO: Add property in dts

    (void)of_property_read_u32(np, "fingerprint,navigation_adjust1", &adjust1);

    if (adjust1 != NAVIGATION_ADJUST_NOREVERSE && adjust1 != NAVIGATION_ADJUST_REVERSE)
    {
        adjust1 = NAVIGATION_ADJUST_NOREVERSE;
        fpc_log_err("%s navigation_adjust1 set err only support 0 and 1.\n", __func__);
    }

    (void)of_property_read_u32(np, "fingerprint,navigation_adjust2", &adjust2);

    if (adjust2 != NAVIGATION_ADJUST_NOTURN && adjust2 != NAVIGATION_ADJUST_TURN90 &&
        adjust2 != NAVIGATION_ADJUST_TURN180 && adjust2 != NAVIGATION_ADJUST_TURN270)
    {
        adjust2 = NAVIGATION_ADJUST_NOTURN;
        fpc_log_err("%s navigation_adjust2 set err only support 0 90 180 and 270.\n", __func__);
    }

    fp_data->navigation_adjust1 = (int)adjust1;
    fp_data->navigation_adjust2 = (int)adjust2;

    fpc_log_info("%s get navigation_adjust1 = %d, navigation_adjust2 = %d.\n", __func__,
                 fp_data->navigation_adjust1, fp_data->navigation_adjust2);
    return;
}

int fingerprint_get_dts_data(struct device* dev, struct fp_data* fingerprint)
{
    struct device_node* np;
    struct platform_device* pdev = NULL;
    int ret = 0;
    //   char const *fingerprint_vdd = NULL;
    //  char const *fingerprint_avdd = NULL;
#if 0

    if (!dev || !dev->of_node)
    {
        fpc_log_err("dev or dev node is NULL\n");
        return -EINVAL;
    }

    np = dev->of_node;
#else
    np = of_find_compatible_node(NULL, NULL, "mediatek,fingerprint");
#endif

    if (np)
    {
        pdev = of_find_device_by_node(np);

        if (IS_ERR(pdev))
        {
            fpc_log_err("platform device is null\n");
            return PTR_ERR(pdev);
        }

        fingerprint->pinctrl = devm_pinctrl_get(&pdev->dev);

        if (IS_ERR(fingerprint->pinctrl))
        {
            ret = PTR_ERR(fingerprint->pinctrl);
            fpc_log_err("fpsensor Cannot find fp pinctrl1.\n");
            return -1;
        }

        fingerprint->fp_rst_low = pinctrl_lookup_state(fingerprint->pinctrl, "reset_low");

        if (IS_ERR(fingerprint->fp_rst_low))
        {
            ret = PTR_ERR(fingerprint->fp_rst_low);
            fpc_log_err("fingerprint Cannot find fp pinctrl fp_rst_low!\n");
            return ret;
        }

        fpc_log_info("fingerprint  find fp pinctrl fp_rst_low!\n");
        fingerprint->fp_rst_high = pinctrl_lookup_state(fingerprint->pinctrl, "reset_high");

        if (IS_ERR(fingerprint->fp_rst_high))
        {
            ret = PTR_ERR(fingerprint->fp_rst_high);
            fpc_log_err( "fingerprint Cannot find fp pinctrl fp_rst_high!\n");
            return ret;
        }

        fpc_log_info( "fingerprint  find fp pinctrl fp_rst_high!\n");
		fingerprint->fp_ldo_low = pinctrl_lookup_state(fingerprint->pinctrl, "ldo_low");

        if (IS_ERR(fingerprint->fp_ldo_low))
        {
            ret = PTR_ERR(fingerprint->fp_ldo_low);
            fpc_log_err("fingerprint Cannot find fp pinctrl fp_rst_low!\n");
            return ret;
        }

        fpc_log_info("fingerprint  find fp pinctrl fp_ldo_low!\n");
        fingerprint->fp_ldo_high = pinctrl_lookup_state(fingerprint->pinctrl, "ldo_high");

        if (IS_ERR(fingerprint->fp_ldo_high))
        {
            ret = PTR_ERR(fingerprint->fp_ldo_high);
            fpc_log_err( "fingerprint Cannot find fp pinctrl fp_ldo_high!\n");
            return ret;
        }

        fpc_log_info( "fingerprint  find fp pinctrl fp_ldo_high!\n");
#if 0
        fingerprint->eint_as_int = pinctrl_lookup_state(fingerprint->pinctrl, "irq_eint"); //eint_in_low; eint

        if (IS_ERR(fingerprint->eint_as_int))
        {
            ret = PTR_ERR(fingerprint->eint_as_int);
            fpc_log_err( "fingerprint Cannot find fp pinctrl eint_as_int!\n");
            return ret;
        }

        fpc_log_info( "fingerprint  find fp pinctrl eint_as_int!\n");
#endif
        fingerprint->fp_cs_low  = pinctrl_lookup_state(fingerprint->pinctrl, "fingerprint_spi_cs_low");

        if (IS_ERR(fingerprint->fp_cs_low ))
        {
            ret = PTR_ERR(fingerprint->fp_cs_low );
            fpc_log_err("fingerprint Cannot find fp pinctrl fp_cs_low!\n");
            return ret;
        }

        fpc_log_info("fingerprint  find fp pinctrl fp_cs_low!\n");
        fingerprint->fp_cs_high = pinctrl_lookup_state(fingerprint->pinctrl, "fingerprint_spi_cs_high");

        if (IS_ERR(fingerprint->fp_cs_high))
        {
            ret = PTR_ERR(fingerprint->fp_cs_high);
            fpc_log_err( "fingerprint Cannot find fp pinctrl fp_cs_high!\n");
            return ret;
        }

        fpc_log_info( "fingerprint  find fp pinctrl fp_cs_high!\n");
        fingerprint->fp_mo_high = pinctrl_lookup_state(fingerprint->pinctrl, "fingerprint_spi_mosi_high");

        if (IS_ERR(fingerprint->fp_mo_high))
        {
            ret = PTR_ERR(fingerprint->fp_mo_high);
            fpc_log_err( "fingerprint Cannot find fp pinctrl fp_mo_high!\n");
            return ret;
        }

        fpc_log_info( "fingerprint  find fp pinctrl fp_mo_high!\n");
        fingerprint->fp_mo_low = pinctrl_lookup_state(fingerprint->pinctrl, "fingerprint_spi_mosi_low");

        if (IS_ERR(fingerprint->fp_mo_low))
        {
            ret = PTR_ERR(fingerprint->fp_mo_low);
            fpc_log_err("fingerprint Cannot find fp pinctrl fp_mo_low!\n");
            return ret;
        }

        fpc_log_info("fingerprint find fp pinctrl fp_mo_low!\n");
        fingerprint->fp_mi_high = pinctrl_lookup_state(fingerprint->pinctrl, "fingerprint_spi_miso_high");

        if (IS_ERR(fingerprint->fp_mi_high))
        {
            ret = PTR_ERR(fingerprint->fp_mi_high);
            fpc_log_err( "fingerprint Cannot find fp pinctrl fp_mi_high!\n");
            return ret;
        }

        fpc_log_info( "fingerprint find fp pinctrl fp_mi_high!\n");
        fingerprint->fp_mi_low = pinctrl_lookup_state(fingerprint->pinctrl, "fingerprint_spi_miso_low");

        if (IS_ERR(fingerprint->fp_mi_low))
        {
            ret = PTR_ERR(fingerprint->fp_mi_low);
            fpc_log_err("fingerprint Cannot find fp pinctrl fp_mi_low!\n");
            return ret;
        }

        fpc_log_info("fingerprint find fp pinctrl fp_mi_low!\n");
        fingerprint->fp_ck_high = pinctrl_lookup_state(fingerprint->pinctrl, "fingerprint_spi_mclk_high");

        if (IS_ERR(fingerprint->fp_ck_high))
        {
            ret = PTR_ERR(fingerprint->fp_ck_high);
            fpc_log_err( "fingerprint Cannot find fp pinctrl fp_ck_high!\n");
            return ret;
        }

        fpc_log_info( "fingerprint  find fp pinctrl fp_ck_high!\n");
        fingerprint->fp_ck_low = pinctrl_lookup_state(fingerprint->pinctrl, "fingerprint_spi_mclk_low");

        if (IS_ERR( fingerprint->fp_ck_low))
        {
            ret = PTR_ERR( fingerprint->fp_ck_low);
            fpc_log_err("fingerprint Cannot find fp pinctrl fp_ck_low!\n");
            return ret;
        }

        fpc_log_info("fingerprint find fp pinctrl fp_ck_low!\n");

        fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_SPI_MO_PIN, 0);
        fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_SPI_MI_PIN, 0);
        fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_SPI_CK_PIN, 0);
        fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_SPI_CS_PIN, 0);


    }
    else
    {
        fpc_log_err("fingerprint Cannot find node!\n");
    }

    return 0;
}


#if 0
ret = of_property_read_string(dev->of_node, "fingerprint,vdd", &fingerprint_vdd);

if (ret)
{
    fpc_log_err("failed to get vdd from device tree\n");
}

if (fingerprint_vdd)
{
    fingerprint->vdd = regulator_get(dev, fingerprint_vdd);

    if (IS_ERR(fingerprint->vdd))
    {
        fpc_log_err("failed to get vdd regulator\n");
    }
}

ret = of_property_read_string(dev->of_node, "fingerprint,avdd", &fingerprint_avdd);

if (ret)
{
    fpc_log_err("failed to get avdd from device tree, some project don't need this power\n");
}

if (fingerprint_avdd)
{
    fingerprint->avdd = regulator_get(dev, fingerprint_avdd);

    if (IS_ERR(fingerprint->avdd))
    {
        fpc_log_err("failed to get avdd regulator\n");
    }
}

fingerprint->avdd_en_gpio = of_get_named_gpio(np, "fingerprint,avdd_en_gpio", 0);

if (!gpio_is_valid(fingerprint->avdd_en_gpio))
{
    fpc_log_err("failed to get avdd enable gpio from device tree, some project don't need this gpio\n");
}
else
{
    fpc_log_info("avdd_en_gpio = %d\n", fingerprint->avdd_en_gpio);
}

fingerprint->vdd_en_gpio = of_get_named_gpio(np, "fingerprint,vdd_en_gpio", 0);

if (!gpio_is_valid(fingerprint->vdd_en_gpio))
{
    fpc_log_err("failed to get vdd enable gpio from device tree, some project don't need this gpio\n");
}
else
{
    fpc_log_info("vdd_en_gpio = %d\n", fingerprint->vdd_en_gpio);
}

#endif


int fingerprint_gpio_reset(struct fp_data* fingerprint)
{
    int error = 0;
    int counter = FP_RESET_RETRIES;

    fpc_log_info("Enter!\n");

    while (counter)
    {
        counter--;

        fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_RST_PIN, 1);
        udelay(FP_RESET_HIGH1_US);

        fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_RST_PIN, 0);
        udelay(FP_RESET_LOW_US);

        fingerprint_gpio_output_dts(fingerprint, FINGERPRINT_RST_PIN, 1);
        udelay(FP_RESET_HIGH2_US);

    }

    fpc_log_info("Exit!\n");
    return error;
}

static int fingerprint_reset_init(struct fp_data* fingerprint)
{
    int error = 0;
	fpc_log_err("cjy gpio_request\n");
#if 0

    if (gpio_is_valid(fingerprint->rst_gpio))
    {
        fpc_log_info("Assign RESET -> GPIO%d\n", fingerprint->rst_gpio);
        error = gpio_request(fingerprint->rst_gpio, "fingerprint_reset");

        if (error)
        {
            fpc_log_err("gpio_request (reset) failed\n");
            return error;
        }

        // TODO:
        error = gpio_direction_output(fingerprint->rst_gpio, 0);

        if (error)
        {
            fpc_log_err("gpio_direction_output (reset) failed\n");
            return error;
        }
    }
    else
    {
        fpc_log_info("Using soft reset\n");
    }

#endif
    return error;
}

static int fingerprint_irq_init(struct fp_data* fingerprint)
{
    int error = 0;

#if 0

    if (gpio_is_valid(fingerprint->irq_gpio))
    {
        fpc_log_info("Assign IRQ -> GPIO%d\n", fingerprint->irq_gpio);
        error = gpio_request(fingerprint->irq_gpio, "fingerprint_irq");

        if (error)
        {
            fpc_log_err("gpio_request (irq) failed\n");
            return error;
        }

        error = gpio_direction_input(fingerprint->irq_gpio);

        if (error)
        {
            fpc_log_err("gpio_direction_input (irq) failed\n");
            return error;
        }
    }
    else
    {
        fpc_log_err("invalid irq gpio\n");
        return -EINVAL;
    }

    fingerprint->irq = gpio_to_irq(fingerprint->irq_gpio);

    if (fingerprint->irq < 0)
    {
        fpc_log_err("gpio_to_irq failed\n");
        error = fingerprint->irq;
        return error;
    }

#else

    struct device_node* node;
    u32 ints[2] = {0, 0};
    fpc_log_info("Enter!\n");

    //spidev_gpio_as_int(fingerprint);
//    pinctrl_select_state(fingerprint->pinctrl, fingerprint->eint_as_int);


//    node = of_find_compatible_node(NULL, NULL, "huawei,fingerprint");
		node = of_find_compatible_node(NULL, NULL, "mediatek, EINT_FINGERPRINT-eint");
    if (node)
    {
        of_property_read_u32_array( node, "debounce", ints, ARRAY_SIZE(ints));
        gpio_request(ints[0], "fingerprint-irq");
        gpio_set_debounce(ints[0], ints[1]);
        fpc_log_err("[fingerprint]ints[0] = %d,is irq_gpio , ints[1] = %d!!\n", ints[0], ints[1]);
        fingerprint->irq_gpio = ints[0];
        fingerprint->irq = irq_of_parse_and_map(node, 0);  // get irq number

        if (!fingerprint->irq)
        {
            fpc_log_err("fingerprint irq_of_parse_and_map fail!!\n");
            return -EINVAL;
        }

        fpc_log_info(" [fpsensor]fingerprint->irq= %d,fingerprint>irq_gpio = %d\n", fingerprint->irq, fingerprint->irq_gpio);
    }
    else
    {
        fpc_log_err("fingerprint null irq node!!\n");
        return -EINVAL;
    }

#endif
    return error;
}
/* add power by chagnjingyang start*/
static int fingerprint_power_init(struct fp_data* fingerprint , u8 onoff)
{
	/* TODO: LDO configure */
	static int enable = 1;
	int err = 0;
	if (onoff && enable) {
	/* TODO:  set power  according to actual situation  */
//		hwPowerOn(MT6331_POWER_LDO_VIBR, VOL_2800, "fingerprint");
		enable = 0;
#ifdef CONFIG_OF
		err = pinctrl_select_state(fingerprint->pinctrl,fingerprint->fp_ldo_high);
		mdelay(10);

		pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_rst_low);
		mdelay(15);
		pinctrl_select_state(fingerprint->pinctrl, fingerprint->fp_rst_high);
#endif
	}else if (!onoff && !enable) {
//		hwPowerDown(MT6331_POWER_LDO_VIBR, "fingerprint");
#ifdef CONFIG_OF
		err = pinctrl_select_state(fingerprint->pinctrl,fingerprint->fp_ldo_low);
		mdelay(10);
#endif
		enable = 1;
	}
	return err;
}
/* add power by chagnjingyang start*/
#if 0
static int fingerprint_power_init(struct fp_data* fingerprint)
{
    int error = 0;

    fpc_log_info("Enter!\n");

    // TODO: Do not use regulator

    if (fingerprint->avdd)
    {
        error = regulator_set_voltage(fingerprint->avdd, 2800000, 3000000);

        if (error)
        {
            fpc_log_err("set avdd voltage failed\n");
            goto out_err;
        }

        error = regulator_enable(fingerprint->avdd);

        if (error)
        {
            fpc_log_err("enable avdd failed\n");
            goto out_err;
        }
    }
    else
    {
        fpc_log_err("fingerprint->avdd is NULL, some project don't need this power\n");
    }

    if (fingerprint->vdd)
    {
        error = regulator_set_voltage(fingerprint->vdd, 1800000, 1800000);

        if (error)
        {
            fpc_log_err("set vdd voltage failed\n");
            goto out_err;
        }

        error = regulator_enable(fingerprint->vdd);

        if (error)
        {
            fpc_log_err("enable vdd failed\n");
            goto out_err;
        }
    }
    else
    {
        fpc_log_err("fingerprint->vdd is NULL\n");
        return -EINVAL;
    }

    if (gpio_is_valid(fingerprint->avdd_en_gpio))
    {
        fpc_log_info("fingerprint_avdd_en_gpio -> GPIO%d\n", fingerprint->avdd_en_gpio);
        error = gpio_request(fingerprint->avdd_en_gpio, "fingerprint_avdd_en_gpio");

        if (error)
        {
            fpc_log_err("gpio_request (avdd_en_gpio) failed\n");
            goto out_err;
        }

        error = gpio_direction_output(fingerprint->avdd_en_gpio, 1);

        if (error)
        {
            fpc_log_err("gpio_direction_output (avdd_en_gpio) failed\n");
            goto out_err;
        }

        mdelay(100);
    }
    else
    {
        fpc_log_info("fingerprint->avdd_en_gpio is NULL, some project don't need this gpio\n");
    }

    if (gpio_is_valid(fingerprint->vdd_en_gpio))
    {
        fpc_log_info("fingerprint_vdd_en_gpio -> GPIO%d\n", fingerprint->vdd_en_gpio);
        error = gpio_request(fingerprint->vdd_en_gpio, "fingerprint_vdd_en_gpio");

        if (error)
        {
            fpc_log_err("gpio_request (vdd_en_gpio) failed\n");
            goto out_err;
        }

        error = gpio_direction_output(fingerprint->vdd_en_gpio, 1);

        if (error)
        {
            fpc_log_err("gpio_direction_output (vdd_en_gpio) failed\n");
            goto out_err;
        }
    }
    else
    {
        fpc_log_info("fingerprint->vdd_en_gpio is NULL, some project don't need this gpio\n");
    }

    //out_err:
    return error;
}
#endif
static int fingerprint_key_remap_reverse(int key)
{
    switch (key)
    {
        case EVENT_LEFT:
            key = EVENT_RIGHT;
            break;

        case EVENT_RIGHT:
            key = EVENT_LEFT;
            break;

        default:
            break;
    }

    return key;
}

static int fingerprint_key_remap_turn90(int key)
{
    switch (key)
    {
        case EVENT_LEFT:
            key = EVENT_UP;
            break;

        case EVENT_RIGHT:
            key = EVENT_DOWN;
            break;

        case EVENT_UP:
            key = EVENT_RIGHT;
            break;

        case EVENT_DOWN:
            key = EVENT_LEFT;
            break;

        default:
            break;
    }

    return key;
}

static int fingerprint_key_remap_turn180(int key)
{
    switch (key)
    {
        case EVENT_LEFT:
            key = EVENT_RIGHT;
            break;

        case EVENT_RIGHT:
            key = EVENT_LEFT;
            break;

        case EVENT_UP:
            key = EVENT_DOWN;
            break;

        case EVENT_DOWN:
            key = EVENT_UP;
            break;

        default:
            break;
    }

    return key;
}

static int fingerprint_key_remap_turn270(int key)
{
    switch (key)
    {
        case EVENT_LEFT:
            key = EVENT_DOWN;
            break;

        case EVENT_RIGHT:
            key = EVENT_UP;
            break;

        case EVENT_UP:
            key = EVENT_LEFT;
            break;

        case EVENT_DOWN:
            key = EVENT_RIGHT;
            break;

        default:
            break;
    }

    return key;
}

static int fingerprint_key_remap(const struct fp_data* fingerprint, int key)
{
    if (key != EVENT_RIGHT && key != EVENT_LEFT && key != EVENT_UP && key != EVENT_DOWN)
    {
        return key;
    }

    if (fingerprint->navigation_adjust1 == NAVIGATION_ADJUST_REVERSE)
    {
        key = fingerprint_key_remap_reverse(key);
    }

    switch (fingerprint->navigation_adjust2)
    {
        case NAVIGATION_ADJUST_TURN90:
            key = fingerprint_key_remap_turn90(key);
            break;

        case NAVIGATION_ADJUST_TURN180:
            key = fingerprint_key_remap_turn180(key);
            break;

        case NAVIGATION_ADJUST_TURN270:
            key = fingerprint_key_remap_turn270(key);
            break;

        default:
            break;
    }

    return key;
}

static void fingerprint_input_report(struct fp_data* fingerprint, int key)
{
    key = fingerprint_key_remap(fingerprint, key);
    fpc_log_info("key = %d\n", key);
    input_report_key(fingerprint->input_dev, key, 1);
    input_sync(fingerprint->input_dev);
    input_report_key(fingerprint->input_dev, key, 0);
    input_sync(fingerprint->input_dev);
}

static int fingerprint_open(struct inode* inode, struct file* file)
{
    struct fp_data* fingerprint;
    fpc_log_info("Enter!\n");
    fingerprint = container_of(inode->i_cdev, struct fp_data, cdev);
    file->private_data = fingerprint;
    return 0;
}

static int fingerprint_get_irq_status(struct fp_data* fingerprint)
{
    int status = 0;
    status = gpio_get_value(fingerprint->irq_gpio);
    return status;
}
static void fingerprint_spi_clk_switch(struct spi_device* spi, spi_clk_state ctrl)
{
    static spi_clk_state currentState = SPI_CLK_DISABLE;
	fpc_log_info("cjy duplicate switch :%d of spi clk", ctrl);
    if (currentState == ctrl)
    {
        fpc_log_info("duplicate switch :%d of spi clk", ctrl);
    }
    else
    {
        if (SPI_CLK_DISABLE == ctrl)
        {
            mt_spi_disable_master_clk(spi);
	    currentState = SPI_CLK_DISABLE;
        }
        else
        {
            mt_spi_enable_master_clk(spi);
	    currentState = SPI_CLK_ENABLE;
        }
    }
}
static long fingerprint_ioctl(struct file* file, unsigned int cmd, unsigned long arg)
{

    int error = 0;

    struct fp_data* fingerprint;
    void __user* argp = (void __user*)arg;
    int key;
    int status;
    unsigned int sensor_id;
    char sensor_mode[30] = {0};
    char hardware_info[30] = {0};
    fingerprint = (struct fp_data*)file->private_data;
	if (NULL == fingerprint)
    {
        fpc_log_err("%s fingerprint is NULL\n", __func__);
        return -EFAULT;
    }

    fpc_log_info("dev_fingerprint ioctl cmd : 0x%x\n", cmd);

    if (_IOC_TYPE(cmd) != FP_IOC_MAGIC)
    { return -ENOTTY; }

    switch (cmd)
    {
        case FP_IOC_CMD_ENABLE_IRQ:
            fpc_log_info("FP_IOC_CMD_ENABLE_IRQ\n");

            if (0 == fingerprint->irq_num)
            {
                enable_irq(fingerprint->irq);
                fingerprint->irq_num = 1;
            }

            break;

        case FP_IOC_CMD_DISABLE_IRQ:
            fpc_log_info("FP_IOC_CMD_DISABLE_IRQ\n");

            if (1 == fingerprint->irq_num)
            {
                disable_irq(fingerprint->irq);
                fingerprint->irq_num = 0;
            }

            break;

        case FP_IOC_CMD_SEND_UEVENT:
            if (copy_from_user(&key, argp, sizeof(key)))
            {
                fpc_log_err("copy_from_user failed");
                return -EFAULT;
            }
			if(key < FP_KEY_MIN ||key > FP_KEY_MAX)
            {
                fpc_log_err("%s nav key is %d.\n", __func__ , key);
                return -EFAULT;
            }

            fingerprint_input_report(fingerprint, key);
            fpc_log_info("FP_IOC_CMD_SEND_UEVENT\n");
            break;

        case FP_IOC_CMD_GET_IRQ_STATUS:

            status = fingerprint_get_irq_status(fingerprint);
			if(status != 0 && status != 1)
            {
                fpc_log_err("%s fingerprint irq is %d.\n", __func__ , status);
                return -EFAULT;
            }

            error = copy_to_user(argp, &status, sizeof(status));

            if (error)
            {
                fpc_log_err("copy_to_user failed, error = %d", error);
                return -EFAULT;
            }

            fpc_log_info("FP_IOC_CMD_GET_IRQ_STATUS, status = %d\n", status);
            break;

        case FP_IOC_CMD_SET_WAKELOCK_STATUS:
            if (copy_from_user(&key, argp, sizeof(key)))
            {
                fpc_log_err("copy_from_user failed");
                return -EFAULT;
            }

            if (key == 1)
            {
                fingerprint->wakeup_enabled = true;
            }
            else
            {
                fingerprint->wakeup_enabled = false;
            }

            fpc_log_info("FP_IOC_CMD_SET_WAKELOCK_STATUS key = %d\n", key);

            break;
            case FP_IOC_CMD_GET_SENSORID:
            sensor_id = get_fp_vendor();
            fpc_log_info("sensor_id = %d\n", sensor_id);
            error = copy_to_user(argp,&sensor_id, sizeof(sensor_id));
            if (error)
            {
                fpc_log_err("copy_to_user failed, error = %d", error);
                return -EFAULT;
            }
            break;
        case FP_IOC_CMD_SEND_SENSORID:
            if (copy_from_user(&sensor_id, argp, sizeof(sensor_id)))
            {
                fpc_log_err("copy_from_user failed\n");
                return -EFAULT;
            }

            fingerprint->sensor_id = sensor_id;
            fpc_log_info("sensor_id = %x\n", sensor_id);

            // TODO:
            if (0x3268 == sensor_id)
            {
                strcpy(hardware_info, "Goodix 0x3268");
            }
            else if (0x6163 == sensor_id)
            {
                strcpy(hardware_info, "Silead 0x6163");
            }

            //hardwareinfo_set_prop(HARDWARE_FINGERPRINT,hardware_info);
            break;

        case FP_IOC_CMD_SEND_SENSOR_MODE:
            if (copy_from_user(sensor_mode, argp, sizeof(sensor_mode)))
            {
                fpc_log_err("copy_from_user failed\n");
                return -EFAULT;
            }

            memcpy(fingerprint->sensor_mode, sensor_mode, strlen(sensor_mode) + 1);
            fpc_log_info("sensor_mode = %s\n", sensor_mode);
            break;

            // TODO: add reset IOCTL
        case FP_IOC_CMD_RESET_SENSOR:
            fpc_log_info("chip reset command\n");
            fingerprint_gpio_reset(fingerprint);
            break;
        case FP_IOC_CMD_ENABLE_SPI_CLK:
            fpc_log_info("FP_IOC_CMD_ENABLE_SPI_CLK\n");
            fingerprint_spi_clk_switch(fingerprint->spi, SPI_CLK_ENABLE);
            break;

        case FP_IOC_CMD_DISABLE_SPI_CLK:
            fpc_log_info("FP_IOC_CMD_DISABLE_SPI_CLK\n");
            fingerprint_spi_clk_switch(fingerprint->spi, SPI_CLK_DISABLE);
            break;
        default:
            fpc_log_err("error = -EFAULT\n");
            error = -EFAULT;
            break;
    }

    return error;
}

static int fingerprint_release(struct inode* inode, struct file* file)
{
    fpc_log_info("Enter!\n");
    return 0;
}
#if 0
static int fingerprint_get_module_info(struct fp_data* fingerprint)
{
    int error = 0;
    //int pd_value = 0, pu_value = 0;

    error = gpio_request(fingerprint->moduleID_gpio, "fingerprint_moduleID");

    if (error)
    {
        fpc_log_err("gpio_request (moduleID) failed\n");
        return error;
    }

    // TODO: Get pinctrl state from dts file

    fingerprint->pctrl = devm_pinctrl_get(&fingerprint->spi->dev);

    if (IS_ERR(fingerprint->pctrl))
    {
        fpc_log_err("failed to get moduleid pin\n");
        error = -EINVAL;
        return error;
    }

    fingerprint->pins_default = pinctrl_lookup_state(fingerprint->pctrl, "default");

    if (IS_ERR(fingerprint->pins_default))
    {
        fpc_log_err("failed to lookup state fingerprint_moduleid_default\n");
        error = -EINVAL;
        goto error_pinctrl_put;
    }

    fingerprint->pins_idle = pinctrl_lookup_state(fingerprint->pctrl, "idle");

    if (IS_ERR(fingerprint->pins_idle))
    {
        fpc_log_err("failed to lookup state fingerprint_moduleid_idle\n");
        error = -EINVAL;
        goto error_pinctrl_put;
    }

    error = pinctrl_select_state(fingerprint->pctrl, fingerprint->pins_default);

    if (error < 0)
    {
        fpc_log_err("failed to select state fingerprint_moduleid_default\n");
        error = -EINVAL;
        goto error_pinctrl_put;
    }

    udelay(10);

    pu_value = gpio_get_value_cansleep(fingerprint->moduleID_gpio);
    fpc_log_info("PU module id gpio = %d \n", pu_value);


    error = pinctrl_select_state(fingerprint->pctrl, fingerprint->pins_idle);

    if (error < 0)
    {
        fpc_log_err("failed to select state fingerprint_moduleid_idle\n");
        error = -EINVAL;
        return error;
    }

    udelay(10);

    pd_value = gpio_get_value_cansleep(fingerprint->moduleID_gpio);
    fpc_log_info("PD module id gpio = %d \n", pd_value);

    if (pu_value == pd_value)
    {
        if (pu_value == 1)
        {
            fingerprint->module_vendor_info = MODULEID_HIGH;
            fpc_log_info("fingerprint moduleID pin is HIGH\n");
        }

        if (pd_value == 0)
        {
            fingerprint->module_vendor_info = MODULEID_LOW;
            fpc_log_info("fingerprint moduleID pin is LOW\n");
        }
    }
    else
    {
        fingerprint->module_vendor_info = MODULEID_FLOATING;
        fpc_log_info("fingerprint moduleID pin is FLOATING\n");
    }

    return error;

error_pinctrl_put:
    devm_pinctrl_put(fingerprint->pctrl);

    return error;
}
#endif

static ssize_t fpsensor_read(struct file* filp, char __user* buf, size_t count, loff_t* f_pos)
{
    fpc_log_err("kp Not support read opertion in TEE version\n");
    return -EFAULT;
}

static const struct file_operations fingerprint_fops =
{
    .owner			= THIS_MODULE,
    .open			= fingerprint_open,
    .release		= fingerprint_release,
    .unlocked_ioctl	= fingerprint_ioctl,
    .read =        fpsensor_read,
};

#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block* self, unsigned long event, void* data)
{
    struct fb_event* evdata = data;
    int* blank;
    struct fp_data* fingerprint = container_of(self, struct fp_data, fb_notify);

    if (evdata && evdata->data && fingerprint)
    {
        if (event == FB_EVENT_BLANK)
        {
            blank = evdata->data;

            if (*blank == FB_BLANK_UNBLANK)
            { atomic_set(&fingerprint->state, fp_LCD_UNBLANK); }
            else if (*blank == FB_BLANK_POWERDOWN)
            { atomic_set(&fingerprint->state, fp_LCD_POWEROFF); }
        }
    }

    return 0;
}
#endif

static int fingerprint_probe(struct spi_device* spi)
{
    struct device* dev = &spi->dev;
    int error = 0;
    //int irqf;
#if 0
    struct device_node* np = dev->of_node;
#else
    struct device_node* np = of_find_compatible_node(NULL, NULL, "mediatek,fingerprint");
#endif
    struct fp_data* fingerprint = devm_kzalloc(dev, sizeof(*fingerprint), GFP_KERNEL);

    if (!fingerprint)
    {
        fpc_log_err("failed to allocate memory for struct fp_data\n");
        error = -ENOMEM;
        goto exit;
    }

    if (!np)
    {
        fpc_log_err("dev->of_node not found\n");
        error = -EINVAL;
        goto exit;
    }


#if defined (CONFIG_HUAWEI_DSM)

    if (!fingerprint_dclient)
    {
        fingerprint_dclient = dsm_register_client(&dsm_fingerprint);
    }

#endif

    fpc_log_info("fingerprint driver v3.0 for Android M\n");

    fingerprint->dev = dev;
    dev_set_drvdata(dev, fingerprint);
    fingerprint->spi = spi;
    fingerprint->spi->mode = SPI_MODE_0;
    fingerprint->spi->bits_per_word = 8;
    fingerprint->spi->chip_select = 0;
    spi_setup(spi);
    fingerprint_spi_clk_switch(fingerprint->spi, SPI_CLK_ENABLE);
    fingerprint_get_dts_data(&spi->dev, fingerprint);
	fpc_log_info("cjy fingerprint_get_dts_data\n");
#if defined(CONFIG_FB)
    fingerprint->fb_notify.notifier_call = NULL;
#endif
    atomic_set(&fingerprint->state, fp_UNINIT);

    error = fingerprint_irq_init(fingerprint);
    fpc_log_info("cjy fingerprint_irq_init\n");
    if (error)
    {
        fpc_log_err("fingerprint_irq_init failed, error = %d\n", error);
        goto exit;
    }

//    fingerprint_get_navigation_adjustvalue(&spi->dev, fingerprint);

    error = fingerprint_reset_init(fingerprint);
    fpc_log_err("cjy fingerprint_reset_init gpio_request\n");
    if (error)
    {
        fpc_log_err("fingerprint_reset_init failed, error = %d\n", error);
        goto exit;
    }


    error = fingerprint_power_init(fingerprint , 1);

    if (error)
    {
        fpc_log_err("fingerprint_power_init failed, error = %d\n", error);
        goto exit;
    }
#if 0
    error = fingerprint_get_module_info(fingerprint);

    if (error < 0)
    {
        fpc_log_err("unknow vendor info, error = %d\n", error);
    }

#endif
    fingerprint->class = class_create(THIS_MODULE, FP_CLASS_NAME);
	fpc_log_err("cjy class create\n");
    error = alloc_chrdev_region(&fingerprint->devno, 0, 1, FP_DEV_NAME);
	fpc_log_err("cjy alloc_chrdev_region\n");
    if (error)
    {
        fpc_log_err("alloc_chrdev_region failed, error = %d\n", error);
        goto exit;
    }

    fingerprint->device = device_create(fingerprint->class, NULL, fingerprint->devno,
                                        NULL, "%s", FP_DEV_NAME);
	fpc_log_err("cjy device_create\n");
    cdev_init(&fingerprint->cdev, &fingerprint_fops);
	fpc_log_err("cjy cdev_init\n");
    fingerprint->cdev.owner = THIS_MODULE;
	fpc_log_err("cjy this module\n");
    error = cdev_add(&fingerprint->cdev, fingerprint->devno, 1);
	fpc_log_err("cjy cdev_add\n");
    if (error)
    {
        fpc_log_err("cdev_add failed, error = %d\n", error);
        goto exit;
    }

    fingerprint->input_dev = devm_input_allocate_device(dev);
	fpc_log_err("cjy devm input allocate\n");
    if (!fingerprint->input_dev)
    {
        error = -ENOMEM;
        fpc_log_err("devm_input_allocate_device failed, error = %d\n", error);
        goto exit;
    }
	fpc_log_err("cjy fingerprint start\n");
    fingerprint->input_dev->name = "fingerprint";
	fpc_log_err("cjy fingerprint end\n");
    /* Also register the key for wake up */
    input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_UP);
    input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_DOWN);
    input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_LEFT);
    input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_RIGHT);
    input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_CLICK);
    input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_HOLD);
    input_set_capability(fingerprint->input_dev, EV_KEY, EVENT_DCLICK);
    set_bit(EV_KEY, fingerprint->input_dev->evbit);
    set_bit(EVENT_UP, fingerprint->input_dev->evbit);
    set_bit(EVENT_DOWN, fingerprint->input_dev->evbit);
    set_bit(EVENT_LEFT, fingerprint->input_dev->evbit);
    set_bit(EVENT_RIGHT, fingerprint->input_dev->evbit);
    set_bit(EVENT_CLICK, fingerprint->input_dev->evbit);
    set_bit(EVENT_HOLD, fingerprint->input_dev->evbit);
    set_bit(EVENT_DCLICK, fingerprint->input_dev->evbit);
	fpc_log_err("cjy input_register_device start\n");
    error = input_register_device(fingerprint->input_dev);
	fpc_log_err("cjy input_register_device last\n");
    if (error)
    {
        fpc_log_err("input_register_device failed, error = %d\n", error);
        goto exit;
    }
	fpc_log_err("cjy wakeup_enabled start\n");
    fingerprint->wakeup_enabled = true;
	fpc_log_err("cjy wakeup_enabled end\n");
    memset(fingerprint->sensor_mode, 0, 30);
	fpc_log_err("cjy platform_device_alloc start\n");
    fingerprint->pf_dev = platform_device_alloc(FP_DEV_NAME, -1);
	fpc_log_err("cjy platform_device_alloc end\n");
    if (!fingerprint->pf_dev)
    {
        error = -ENOMEM;
        fpc_log_err("platform_device_alloc failed, error = %d\n", error);
        goto exit;
    }
#if 1
	fpc_log_err("cjy platform_device_alloc start\n");
    error = platform_device_add(fingerprint->pf_dev);
	fpc_log_err("cjy platform_device_alloc end\n");
    if (error)
    {
        fpc_log_err("platform_device_add failed, error = %d\n", error);
//        platform_device_del(fingerprint->pf_dev);
        platform_device_put(fingerprint->pf_dev);
        goto exit;
    }
    else
    {
        dev_set_drvdata(&fingerprint->pf_dev->dev, fingerprint);
	fpc_log_err("cjy sysfs_create_group start\n");
        error = sysfs_create_group(&fingerprint->pf_dev->dev.kobj, &attribute_group);
	fpc_log_err("cjy sysfs_create_group end\n");
        if (error)
        {
            fpc_log_err("sysfs_create_group failed, error = %d\n", error);
            goto exit;
        }
    }
#endif
    // TODO: Add property in  dts


    //device_init_wakeup(dev, 1);
    wake_lock_init(&fingerprint->ttw_wl, WAKE_LOCK_SUSPEND, "fpc_ttw_wl");
	fpc_log_err("cjy wake_lock_init\n");
    mutex_init(&fingerprint->lock);
	fpc_log_err("cjy mutex_init\n");
#if 0
    irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND;
    error = devm_request_threaded_irq(dev, fingerprint->irq,
                                      NULL, fingerprint_irq_handler, irqf,
                                      "fingerprint", fingerprint);
#else
    error = request_threaded_irq(fingerprint->irq, NULL, fingerprint_irq_handler,
                                 IRQF_TRIGGER_RISING | IRQF_ONESHOT, "fingerprint", fingerprint);
	fpc_log_err("cjy request_threaded_irq\n");
#endif

    if (error)
    {
        fpc_log_err("failed to request irq %d\n", fingerprint->irq);
        goto exit;
    }

    disable_irq(fingerprint->irq);
    fingerprint->irq_num = 0;
	fpc_log_err("cjy disable_irq\n");
    //fpc_log_info("requested irq %d\n", fingerprint->irq);

    /* Request that the interrupt should be wakeable */
    //    enable_irq_wake(fingerprint->irq);
    // fingerprint->wakeup_enabled = true;
    fingerprint->snr_stat = 0;

	fpc_log_err("cjy notifier_call\n");
#if defined(CONFIG_FB)

    if (fingerprint->fb_notify.notifier_call == NULL)
    {
        fingerprint->fb_notify.notifier_call = fb_notifier_callback;
        fb_register_client(&fingerprint->fb_notify);
    }

#endif

    fpc_log_info("fingerprint probe is successful!\n");
    fingerprint_spi_clk_switch(fingerprint->spi, SPI_CLK_DISABLE);
    return error;

exit:
    fpc_log_info("fingerprint probe failed!\n");
#if defined (CONFIG_HUAWEI_DSM)

    if (error && !dsm_client_ocuppy(fingerprint_dclient))
    {
        dsm_client_record(fingerprint_dclient, "fingerprint_probe failed, error = %d\n", error);
        dsm_client_notify(fingerprint_dclient, DSM_FINGERPRINT_PROBE_FAIL_ERROR_NO);
    }

#endif
    return error;
}

static int fingerprint_remove(struct spi_device* spi)
{
    struct  fp_data* fingerprint = dev_get_drvdata(&spi->dev);
    fpc_log_info("Enter!\n");
    sysfs_remove_group(&fingerprint->pf_dev->dev.kobj, &attribute_group);
    cdev_del(&fingerprint->cdev);
    unregister_chrdev_region(fingerprint->devno, 1);
    input_free_device(fingerprint->input_dev);
    mutex_destroy(&fingerprint->lock);
    wake_lock_destroy(&fingerprint->ttw_wl);
#if defined(CONFIG_FB)

    if (fingerprint->fb_notify.notifier_call != NULL)
    {
        fingerprint->fb_notify.notifier_call = NULL;
        fb_unregister_client(&fingerprint->fb_notify);
    }

#endif
    return 0;
}

static int fingerprint_suspend(struct device* dev)
{
    fpc_log_info("Enter!\n");
    return 0;
}

static int fingerprint_resume(struct device* dev)
{
    fpc_log_info("Enter!\n");
    return 0;
}

static const struct dev_pm_ops fingerprint_pm =
{
    .suspend = fingerprint_suspend,
    .resume = fingerprint_resume
};

static struct mt_chip_conf fpsensor_spi_conf =
{
    .setuptime = 20,
    .holdtime = 20,
    .high_time = 50,
    .low_time = 50,
    .cs_idletime = 5,
    .rx_mlsb = 1,
    .tx_mlsb = 1,
    .tx_endian = 0,
    .rx_endian = 0,
    .cpol = 0,
    .cpha = 0,
    .com_mod = FIFO_TRANSFER,
    .pause = 1,
    .finish_intr = 1,
    .deassert = 0,
    .tckdly = 0,
};

static struct spi_board_info spi_fp_board_info[] __initdata =
{
    [0] = {
        .modalias = "fingerprint",
        .bus_num = 0,
        .chip_select = 0,
        .mode = SPI_MODE_0,
        .controller_data = &fpsensor_spi_conf, //&spi_conf
    },
};

// TODO: All following should consider platform
static struct of_device_id fingerprint_of_match[] =
{
    //{ .compatible = "fpc,fingerprint", },
//    { .compatible = "huawei,fingerprint", },
    { .compatible = "mediatek,fingerprint", },
    {}
};

MODULE_DEVICE_TABLE(of, fingerprint_of_match);

static struct spi_driver fingerprint_driver =
{
    .driver = {
        .name	= "fingerprint",
        .owner	= THIS_MODULE,
        .of_match_table = fingerprint_of_match,
        .pm = &fingerprint_pm
    },
    .probe  = fingerprint_probe,
    .remove = fingerprint_remove
};
static int __init fingerprint_init(void)
{
    int fp_vendor = get_fp_vendor();
    int num = 0;
    switch (fp_vendor)
    {
        case 1:
            strcpy(hardware_info, "Goodix fpsensor");
            app_info_set("fp-sensor", "FPC sensor");
            printk("it's fpc sensor\n");
            break;

        case 2:
            strcpy(hardware_info, "SILEAD GSL6165 TRULY");
            app_info_set("fp-sensor", "SILEAD GSL6165");
            printk("it's silead sensor\n");
            break;

        case 3:
            strcpy(hardware_info, "GOODIX GF3258 HOLITECH");
            app_info_set("fp-sensor", "GOODIX GF3258");
            printk("it's goodix sensor\n");
            break;
        case 4:
            strcpy(hardware_info, "chipsailing CS2811");
            app_info_set("fp-sensor", "chipsailing CS2811");
            printk("it's goodix sensor\n");
            break;
        default:
            strcpy(hardware_info, "unknown");
            app_info_set("fp-sensor", "NO sensor");
            printk("it's no sensor\n");
            break;
    }
    printk(KERN_ERR "%s: FingerPring vendor is %s, vendor id is %d\n", __func__, hardware_info, fp_vendor);

    if((fp_vendor == 2)||(fp_vendor == 3)||(fp_vendor == 4))
    {
        printk("it's fingerprint sensor\n");
        num = 1;
    }
    if(num != 1)
    {
        printk("it's no fingerprint sensor\n");
        return 0;
    }

    spi_register_board_info(spi_fp_board_info, ARRAY_SIZE(spi_fp_board_info));

    if (spi_register_driver(&fingerprint_driver))
    { return -EINVAL; }

    return 0;
}

static void __exit fingerprint_exit(void)
{
    fpc_log_info("Enter!\n");
    spi_unregister_driver(&fingerprint_driver);
}

module_init(fingerprint_init);
module_exit(fingerprint_exit);

MODULE_LICENSE("GPL v2");
