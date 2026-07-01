#ifndef __TOUCHPANEL_H__
#define __TOUCHPANEL_H__

#include <linux/hrtimer.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/byteorder/generic.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>

#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#include <linux/jiffies.h>

/**********************Custom define begin**********************************************/

#define TPD_HAVE_BUTTON
#define TPD_BUTTON_HEIGH        (40)
#define TPD_KEY_COUNT           (1)
#define TPD_KEYS                {KEY_HOME}

#define TPD_KEYS_DIM            {{360,2000,120,TPD_BUTTON_HEIGH}}

/*********************Custom Define end*************************************************/

#define TPD_NAME    "fts"

#define TPD_TYPE_CAPACITIVE
#define TPD_TYPE_RESISTIVE
#define TPD_POWER_SOURCE

#define TPD_I2C_NUMBER                  0
#define TPD_WAKEUP_TRIAL                60
#define TPD_WAKEUP_DELAY                100

#define VELOCITY_CUSTOM
#define TPD_VELOCITY_CUSTOM_X           15
#define TPD_VELOCITY_CUSTOM_Y           20

#define TPD_DELAY                       (2*HZ/100)
#define TPD_CALIBRATION_MATRIX          {962,0,0,0,1600,0,0,0};

/******************************************************************************/
/*Chip Device Type*/
#define IC_FT5X06                       0
#define IC_FT5606                       1
#define IC_FT5316                       2
#define IC_FT6208                       3
#define IC_FT6x06                       4
#define IC_FT5x06i                      5
#define IC_FT5x36                       6
#define IC_FT5x46                       7

/*register address*/
#define FT_REG_CHIP_ID              0xA3
#define FT_REG_FW_VER               0xA6
#define FT_REG_VENDOR_ID            0xA8
#define FT_REG_LCM_ID               0xAB

#define FT_APP_INFO_ADDR            0xd7f8
#define FT_VENDOR_ID_ADDR           0xd784
#define FT_LCM_ID_ADDR              0xd786

/*max point*/
#define TPD_MAX_POINTS_2            2
#define TPD_MAX_POINTS_5            5
#define TPD_MAX_POINTS_10           10

#define FTS_MAX_TOUCH TPD_MAX_POINTS_10

/*calibration option*/
#define AUTO_CLB_NEED               1
#define AUTO_CLB_NONEED             0

/*debug fuction*/
#define TPD_SYSFS_DEBUG
#define FTS_DEBUG

/*tp power down when suspend*/
//#define TPD_CLOSE_POWER_IN_SLEEP

/*download*/
// #define TPD_AUTO_DOWNLOAD

/*apgrade*/
//#define TPD_AUTO_UPGRADE
//#define TPD_HW_REST

/*proximity*/
//#define TPD_PROXIMITY

/*gesture — re-enabled for graft port (mz_tp_set_mode stubbed in driver.c) */
#define FTS_GESTURE     1

#define MZ_UNKNOWN  0
#define MZ_HAND_MODE    1
#define MZ_GESTURE_MODE 2
#define MZ_COVER_MODE   3
#define MZ_SLEEP_MODE   4

#define MZ_NO_ACTION    0
#define MZ_FAILED   1
#define MZ_SUCCESS  2

#define GESTURE_SWITCH_OPEN          0x50
#define GESTURE_SWITCH_CLOSE         0x51

/*glove*/
//#define FTS_GLOVE
#ifdef FTS_GLOVE
#define GLOVE_SWITCH_OPEN            0x53
#define GLOVE_SWITCH_CLOSE           0x54
#endif

/* MZ_HALL_MODE — disabled for graft port (needs ft5x46_hall_mode_switch) */
//#define MZ_HALL_MODE

/*trace*/
#undef pr_fmt
#define pr_fmt(fmt) "[FTS]" fmt

#define FTS_DEBUG

#ifdef FTS_DEBUG
#define FTS_DBG     printk
#else
#define FTS_DBG(fmt, args...)               do{}while(0)
#endif

/*current chip information for upgrade*/
struct Upgrade_Info
{
    u8  CHIP_ID;
    u8  FTS_NAME[20];
    u8  TPD_MAX_POINTS;
    u8  AUTO_CLB;
    u16 delay_aa;
    u16 delay_55;
    #ifdef TPD_AUTO_DOWNLOAD
    u8  download_id_1;
    u8  download_id_2;
    #endif
    u8  upgrade_id_1;
    u8  upgrade_id_2;
    u16 delay_readid;
    u16 delay_earse_flash;
};

/* ROI — disabled for graft port */
//#define FTS_ROI

#ifdef FTS_ROI
#define FTS_ROI_SWITCH_REG         0xE2
#define FTS_ROI_DATA_ADDR          0xE3

#define ROI_SWITCH_OPEN            0x55
#define ROI_SWITCH_CLOSE           0x56

#define ROI_HEAD_DATA_LENGTH       0
#define ROI_DATA_READ_LENGTH       98
#define ROI_DATA_SEND_LENGTH       98

#define ROI_PAGE_SIZE              1024

#define FTS_ROI_RETRY_CNT          5
#define ROI_ENABLE                 0x01
#define ROI_DISABLE                0x00

#endif

#endif /* TOUCHPANEL_H__ */
