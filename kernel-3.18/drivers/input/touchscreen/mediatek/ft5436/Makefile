#
# ft5436 Makefile — ported from stock m681 for graft 3.18.140 kernel
#

# Linux driver folder
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/ft5436/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/

obj-y += focaltech_ctl.o
obj-y += focaltech_driver.o
obj-y += focaltech_ex_fun.o
# self-test disabled — FTS_MCAP_TEST not defined for graft port
#obj-y += self-test/
