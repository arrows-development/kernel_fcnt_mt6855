snd-soc-aw883xx-objs:= \
		aw883xx.o \
		aw883xx_monitor.o \
		aw883xx_bin_parse.o \
		aw883xx_device.o \
		aw883xx_init.o \
		aw883xx_calib.o \
		aw883xx_spin.o \

ifeq ($(TARGET_BUILD_VARIANT),userdebug)
KBUILD_CPPFLAGS += -DBUILD_USERDEBUG
KBUILD_CFLAGS += -DBUILD_USERDEBUG
endif
	
obj-$(CONFIG_SND_SOC_AW883XX)+= snd-soc-aw883xx.o
