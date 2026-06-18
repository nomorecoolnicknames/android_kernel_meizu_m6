#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/export.h>
//#include <misc/cust_ldo_setting.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <asm/uaccess.h>
#include <linux/regulator/consumer.h>

#define BOOT_STR_SIZE 32

char ldo_name[BOOT_STR_SIZE];
char ldo_status;
typedef struct mtk_ldo_struct{
		char *name;
		int value;
		int status;
}mtk_ldo_struct;

#define LDO_TEST_MAX_SIZE 14
#define MIN_STR_LENGTH 4
struct mtk_ldo_struct factory_test_ldo[LDO_TEST_MAX_SIZE]={
	{"vcama",     2800000, 0},
	{"vcn28",     2800000, 0},
	{"vcn33_wifi",3300000, 0},
	{"vldo28",    2800000, 0},
	{"vsim1",     1800000, 0},
	{"vsim2",     1800000, 0},
	{"vibr",      2800000, 0},
	{"vmc",       3000000, 0},
	{"vmch",      3000000, 0},
	{"vrf18",     1825000, 0},
	{"vrf12",     1200000, 0},
	{"vcn18",     1800000, 0},
	{"vcamd",     1200000, 0},
	{"vamio",     1800000, 0},
};
static ssize_t mt_bootprof_write(struct file *filp, const char *ubuf, size_t cnt, loff_t *data)
{
	char buf[BOOT_STR_SIZE] = {0};
	size_t copy_size = cnt;
	struct regulator *cust_regulator;
	int i = 0;
	int ret = 0;

	if( NULL == buf )
		return -EFAULT;

	if (cnt >= sizeof(buf))
		copy_size = BOOT_STR_SIZE - 1;

	if( cnt <= MIN_STR_LENGTH)
		return -EFAULT;

	if (copy_from_user(&buf, ubuf, copy_size))
		return -EFAULT;

	memset(ldo_name,0,BOOT_STR_SIZE);
	memcpy(ldo_name,buf,copy_size-3);
	ldo_status = buf[copy_size-2];

	for( i = 0; i < LDO_TEST_MAX_SIZE; i++)
	{
		if(0 == strncmp(ldo_name,factory_test_ldo[i].name,copy_size-3))
		{
			cust_regulator = regulator_get(NULL, ldo_name);
			if (IS_ERR(cust_regulator)) {
				printk("cannot get reg : %s\n",ldo_name);
				return -EFAULT;
			}

			ret = regulator_is_enabled(cust_regulator);
			if( 0 != ret )
			{
				factory_test_ldo[i].status = 1;
			}
			else
			{
				factory_test_ldo[i].status = 0;
			}

			if( ('1' == ldo_status) && (factory_test_ldo[i].status == 0))
			{
				regulator_set_voltage(cust_regulator,factory_test_ldo[i].value,factory_test_ldo[i].value);
				ret = regulator_enable(cust_regulator);
				if (ret != 0) {
					pr_err("LCM: Failed to enable lcm_vgp: %d\n", ret);
					return ret;
				}
				factory_test_ldo[i].status = 1;
			}
			else if( ('0' == ldo_status) && (factory_test_ldo[i].status == 1))
			{
				ret = regulator_disable(cust_regulator);
				if (ret != 0) {
					pr_err("LCM: Failed to enable lcm_vgp: %d\n", ret);
					return ret;
				}
				factory_test_ldo[i].status = 0;
			}
		}
	}

	return cnt;
}

static int cust_ldo_setting_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m,"ldo_name:%s,ldo_status:%c\n",ldo_name,ldo_status);
    return 0;
}

static int cust_ldo_setting_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, cust_ldo_setting_proc_show, NULL);
}

static const struct file_operations cust_ldo_setting_proc_fops =
{
    .open        = cust_ldo_setting_proc_open,
    .read        = seq_read,
    .write       = mt_bootprof_write,
    .llseek      = seq_lseek,
    .release     = single_release,
};

static int __init proc_cust_ldo_setting_init(void)
{
    proc_create("cust_ldo_setting", 0644, NULL, &cust_ldo_setting_proc_fops);
    return 0;
}

module_init(proc_cust_ldo_setting_init);

