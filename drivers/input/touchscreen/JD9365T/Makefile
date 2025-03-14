#ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/
ccflags-y += -DCONFIG_TOUCHSCREEN_JADARD_DEBUG=y
ccflags-y += -DCONFIG_TOUCHSCREEN_JADARD_SORTING=y
ccflags-y += -D__JADARD_KMODULE__

jd9365t-objs := jadard_module.o
jd9365t-objs += jadard_ic_JD9365T.o
jd9365t-objs += jadard_common.o jadard_platform.o
jd9365t-objs += jadard_debug.o
jd9365t-objs += jadard_sorting.o
obj-m += jd9365t.o

