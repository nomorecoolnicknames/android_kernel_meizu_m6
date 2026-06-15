/*
 * AWINIC AW8738 1-wire (pulse-controlled) class-D speaker PA driver.
 *
 * Re-implemented for the Meizu M6 (M711H) LineageOS 3.18 kernel from the
 * reverse-engineered stock Flyme 7.1.2.0G vmlinux (see
 * /srv/forge/work/m6-gate-audio/AW8738_REVERSE.md). The MTK audio HAL
 * (AudioALSACodecDeviceOutSpeakerAWINIC) opens /dev/aw8738 and selects the
 * amp gain/enable mode purely via ioctl; the chip latches its mode from the
 * number of EN rising edges in a short window (1-wire pulse interface).
 *
 * Ground truth (stock binary):
 *   - platform_driver, compatible "mediatek,aw8738-pa"
 *   - char device named "aw8738" -> /dev/aw8738, fops = {ioctl, open, release}
 *   - control pin: gpiolib GPIO14 (stock DTB papins@0 pins=<0xe00>), pinctrl
 *     states "default" + "en_pin"
 *   - ioctl cmd 0x703/0x704 both -> set_mode(arg)
 *   - set_mode: mode 1..10 = enable with `mode` (2us low,2us high) pulses then
 *     msleep(45); mode 11 = EN low (off); else -1
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include <linux/uaccess.h>

#define AW8738_EN_GPIO		14	/* stock: hardcoded gpiolib 14 (DTB 0xe00) */
#define AW8738_DEV_NAME		"aw8738"

#define AW8738_IOC_OPEN		0x703	/* HAL "open"  -> set_mode(arg) */
#define AW8738_IOC_CLOSE	0x704	/* HAL "close" -> set_mode(arg) */

#define AW8738_MODE_MIN		1
#define AW8738_MODE_MAX		10
#define AW8738_MODE_OFF		11

struct aw8738_priv {
	dev_t devno;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;
	struct pinctrl_state *pin_en;
};

static struct aw8738_priv *g_aw8738;

/* set_mode: replicate the stock 1-wire pulse train exactly. */
static int aw8738_set_mode(int mode)
{
	struct gpio_desc *desc;
	int i = 0;

	pr_info("aw8738: set_mode %d\n", mode);

	/* (unsigned)(mode-1) > 0xA  => valid modes are [1..11] */
	if ((unsigned int)(mode - 1) > 0xA) {
		pr_err("aw8738: mode argument is error!\n");
		return -1;
	}

	desc = gpio_to_desc(AW8738_EN_GPIO);
	if (!desc) {
		pr_err("aw8738: gpio_to_desc(%d) failed\n", AW8738_EN_GPIO);
		return -1;
	}

	if (mode == AW8738_MODE_OFF) {
		gpiod_set_raw_value(desc, 0);	/* EN low -> amp off */
		msleep(1);
		return 0;
	}

	/* modes 1..10: emit exactly `mode` EN low->high pulses */
	msleep(1);
	preempt_disable();
	do {
		i++;
		ndelay(2000);			/* 2us */
		gpiod_set_raw_value(desc, 0);	/* EN low */
		ndelay(2000);			/* 2us */
		gpiod_set_raw_value(desc, 1);	/* EN high */
	} while (mode > i);
	preempt_enable();

	msleep(45);				/* settle after pulse train */
	return 0;
}

static long aw8738_ops_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	pr_info("aw8738: ioctl cmd=0x%x arg=%ld\n", cmd, arg);

	switch (cmd) {
	case AW8738_IOC_OPEN:
	case AW8738_IOC_CLOSE:
		return aw8738_set_mode((int)arg);
	default:
		return -1;
	}
}

static int aw8738_ops_open(struct inode *inode, struct file *file)
{
	pr_info("aw8738: open\n");
	return 0;
}

static int aw8738_ops_release(struct inode *inode, struct file *file)
{
	pr_info("aw8738: release\n");
	return 0;
}

static const struct file_operations aw8738_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= aw8738_ops_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= aw8738_ops_ioctl,
#endif
	.open		= aw8738_ops_open,
	.release	= aw8738_ops_release,
};

static int aw8738_drv_probe(struct platform_device *pdev)
{
	struct aw8738_priv *priv;
	struct gpio_desc *desc;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* char device: /dev/aw8738 */
	ret = alloc_chrdev_region(&priv->devno, 0, 1, AW8738_DEV_NAME);
	if (ret) {
		pr_err("aw8738: alloc_chrdev_region fail %d\n", ret);
		return ret;
	}
	pr_info("aw8738: major: %d, minor: %d\n",
		MAJOR(priv->devno), MINOR(priv->devno));

	cdev_init(&priv->cdev, &aw8738_fops);
	priv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->cdev, priv->devno, 1);
	if (ret) {
		pr_err("aw8738: cdev_add fail %d\n", ret);
		goto err_cdev;
	}

	priv->class = class_create(THIS_MODULE, AW8738_DEV_NAME);
	if (IS_ERR(priv->class)) {
		ret = PTR_ERR(priv->class);
		goto err_class;
	}
	priv->device = device_create(priv->class, NULL, priv->devno, NULL,
				     AW8738_DEV_NAME);
	if (IS_ERR(priv->device)) {
		ret = PTR_ERR(priv->device);
		goto err_device;
	}

	/* control pin: pinctrl states default + en_pin, plus gpiolib 14 */
	priv->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(priv->pinctrl)) {
		pr_err("aw8738: devm_pinctrl_get error\n");
		ret = PTR_ERR(priv->pinctrl);
		goto err_pinctrl;
	}
	priv->pin_default = pinctrl_lookup_state(priv->pinctrl, "default");
	if (IS_ERR(priv->pin_default))
		pr_err("aw8738: pinctrl_lookup_state default error\n");
	priv->pin_en = pinctrl_lookup_state(priv->pinctrl, "en_pin");
	if (IS_ERR(priv->pin_en))
		pr_err("aw8738: pinctrl_lookup_state en_pin error\n");
	else
		pinctrl_select_state(priv->pinctrl, priv->pin_en);

	if (gpio_request(AW8738_EN_GPIO, "aw8738_en")) {
		pr_err("aw8738: request gpio %d error! pa control pin init fail\n",
		       AW8738_EN_GPIO);
	} else {
		desc = gpio_to_desc(AW8738_EN_GPIO);
		if (desc) {
			gpiod_direction_output_raw(desc, 0);
			gpiod_set_raw_value(desc, 0);	/* start low (off) */
		}
	}

	g_aw8738 = priv;
	platform_set_drvdata(pdev, priv);
	pr_info("aw8738: probe done (/dev/aw8738, EN gpio %d)\n", AW8738_EN_GPIO);
	return 0;

err_pinctrl:
	device_destroy(priv->class, priv->devno);
err_device:
	class_destroy(priv->class);
err_class:
	cdev_del(&priv->cdev);
err_cdev:
	unregister_chrdev_region(priv->devno, 1);
	return ret;
}

static int aw8738_drv_remove(struct platform_device *pdev)
{
	struct aw8738_priv *priv = platform_get_drvdata(pdev);

	if (!priv)
		return 0;
	gpio_free(AW8738_EN_GPIO);
	device_destroy(priv->class, priv->devno);
	class_destroy(priv->class);
	cdev_del(&priv->cdev);
	unregister_chrdev_region(priv->devno, 1);
	g_aw8738 = NULL;
	return 0;
}

static const struct of_device_id aw8738_of_match[] = {
	{ .compatible = "mediatek,aw8738-pa" },
	{ },
};
MODULE_DEVICE_TABLE(of, aw8738_of_match);

static struct platform_driver aw8738_driver = {
	.probe	= aw8738_drv_probe,
	.remove	= aw8738_drv_remove,
	.driver	= {
		.name		= "aw8738",
		.owner		= THIS_MODULE,
		.of_match_table	= aw8738_of_match,
	},
};

static int __init aw8738_init(void)
{
	return platform_driver_register(&aw8738_driver);
}

static void __exit aw8738_exit(void)
{
	platform_driver_unregister(&aw8738_driver);
}

module_init(aw8738_init);
module_exit(aw8738_exit);

MODULE_DESCRIPTION("AWINIC AW8738 1-wire speaker PA (Meizu M6, RE from stock)");
MODULE_LICENSE("GPL");
