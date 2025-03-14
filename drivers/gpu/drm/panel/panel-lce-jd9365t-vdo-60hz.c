// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <linux/delay.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <linux/hqsysfs.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif

#include "../mediatek/mediatek_v2/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"

#include "../../../misc/mediatek/gate_ic/gate_i2c.h"
#include "lcd_bias.h"
#include "leds_aw99703.h"

/* enable this to check panel self -bist pattern */
/* #define PANEL_BIST_PATTERN */
/****************TPS65132***********/
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
//#include "lcm_i2c.h"

#define BIAS_OUT_VALUE		(5800)
static char bl_tb0[] = { 0x51, 0x0F, 0xFF };
bool jd_gesture_flag;
EXPORT_SYMBOL(jd_gesture_flag);

extern void lcd_bias_set_vspn(unsigned int en, unsigned int seq, unsigned int value);
extern void aw99703_bl_en(unsigned int level);

//TO DO: You have to do that remove macro BYPASSI2C and solve build error
//otherwise voltage will be unstable
struct lce {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *tp_rst_gpio;
	struct gpio_desc *bias_pos;
	struct gpio_desc *bias_neg;
	bool prepared;
	bool enabled;

	unsigned int gate_ic;

	int error;
};

#define lce_dcs_write_seq(ctx, seq...)                                         \
	({                                                                     \
		const u8 d[] = { seq };                                        \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lce_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

#define lce_dcs_write_seq_static(ctx, seq...)                                  \
	({                                                                     \
		static const u8 d[] = { seq };                                 \
		lce_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

static inline struct lce *panel_to_lce(struct drm_panel *panel)
{
	return container_of(panel, struct lce, panel);
}

static struct regulator *reg_vtp_1p8;
static unsigned int lcm_get_reg_vtp_1p8(void)
{
	unsigned int volt = 0;

	if (regulator_is_enabled(reg_vtp_1p8))
		volt = regulator_get_voltage(reg_vtp_1p8);

	return volt;
}

static unsigned int lcm_enable_reg_vtp_1p8(int en)
{
	unsigned int ret = 0, volt = 0;
	static bool vddio_enable_flg = false;

	pr_info("[lcd_info]%s +\n", __func__);
	if (en) {
		if (!vddio_enable_flg) {
			if (!IS_ERR_OR_NULL(reg_vtp_1p8)) {
				ret = regulator_set_voltage(reg_vtp_1p8, 1800000, 1800000);
				if (ret < 0)
					pr_info("set voltage disp_bias_pos fail, ret = %d\n", ret);

				ret = regulator_enable(reg_vtp_1p8);
				pr_info("[lh]Enable the Regulator vufs1p8ret=%d.\n", ret);
				volt = lcm_get_reg_vtp_1p8();
				pr_info("[lh]get the Regulator vufs1p8 =%d.\n", volt);
				vddio_enable_flg = true;
			}
		}
	} else {
		if (vddio_enable_flg) {
			if (!IS_ERR_OR_NULL(reg_vtp_1p8)) {
				ret = regulator_disable(reg_vtp_1p8);
				pr_info("[lh]disable the Regulator vufs1p8 ret=%d.\n", ret);
				vddio_enable_flg = false;
			}
		}
	}

	pr_info("[lcd_info]%s -\n", __func__);

	return ret;
}

#ifdef PANEL_SUPPORT_READBACK
static int lce_dcs_read(struct lce *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret,
			 cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lce_panel_get_data(struct lce *ctx)
{
	u8 buffer[3] = { 0 };
	static int ret;

	pr_info("%s+\n", __func__);

	if (ret == 0) {
		ret = lce_dcs_read(ctx, 0x0A, buffer, 1);
		pr_info("%s  0x%08x\n", __func__, buffer[0] | (buffer[1] << 8));
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

static void lce_dcs_write(struct lce *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
}

static void lce_panel_init(struct lce *ctx)
{
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "lcd_reset", GPIOD_OUT_HIGH);
	ctx->tp_rst_gpio = devm_gpiod_get(ctx->dev, "tp_reset", GPIOD_OUT_HIGH);

	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(4 * 1000, 5 * 1000);
	gpiod_set_value(ctx->tp_rst_gpio, 0);
	usleep_range(4 * 1000, 5 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10 * 1000, 15 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	lce_dcs_write_seq_static(ctx, 0xDF,0x93,0x83,0x16);
	lce_dcs_write_seq_static(ctx, 0xDE,0x00);
	lce_dcs_write_seq_static(ctx, 0xB3,0x10,0x80,0x85,0x00,0x65,0x1E,0x00,0x00,0x00,0x00,0x50,0x50,0x3C,0x3C,0x11,0x11,0x5A,0x5A);
	lce_dcs_write_seq_static(ctx, 0xB2,0x01,0x23,0x60,0x88,0x24);
	lce_dcs_write_seq_static(ctx, 0xB6,0x0C,0x99,0xAE);
	lce_dcs_write_seq_static(ctx, 0xB9,0x3C,0x33,0x35);
	lce_dcs_write_seq_static(ctx, 0xBB,0x0C,0x32,0x43,0x64,0x46,0x20,0x20);
	lce_dcs_write_seq_static(ctx, 0xBD,0x00,0x66);
	lce_dcs_write_seq_static(ctx, 0xC0,0x00,0xAD,0x00,0xAD,0x00);
	lce_dcs_write_seq_static(ctx, 0xCB,0x60,0x54,0x4B,0x3B,0x2F,0x2D,0x1F,0x26,0x15,0x32,0x35,0x38,0x57,0x46,0x4F,0x41,0x3F,0x33,0x23,0x17,0x06,0x60,0x54,0x4B,0x3B,0x2F,0x2D,0x1F,0x26,0x15,0x32,0x35,0x38,0x57,0x46,0x4F,0x41,0x3F,0x33,0x23,0x17,0x06);
	lce_dcs_write_seq_static(ctx, 0xCC,0x33);
	lce_dcs_write_seq_static(ctx, 0xCD,0x10,0x10);
	lce_dcs_write_seq_static(ctx, 0xBF,0x5A,0x46,0x33,0xC3);
	lce_dcs_write_seq_static(ctx, 0xC3,0x1B,0x00,0x06,0x16,0xA8,0x00,0x9D,0x08,0x16,0x04,0x0E,0x08,0x16);
	lce_dcs_write_seq_static(ctx, 0xC5,0x30,0x0C);
	lce_dcs_write_seq_static(ctx, 0xC6,0x00,0xB5,0xB4,0x14);
	lce_dcs_write_seq_static(ctx, 0xCE,0x01,0x3C,0x14,0x3C,0x3C,0x3C,0x14,0x14,0x14,0x14,0x3C,0x3C,0x14,0x3C,0x3C,0x3C,0x3C,0x14,0x14,0x14,0x14,0x14,0x14);
	lce_dcs_write_seq_static(ctx, 0xCF,0x00,0xC0,0x00,0xC0,0xC0,0xC0,0x00,0x00,0x00,0x00,0xC0,0xC0,0x00,0x00,0xC0,0xC0,0xC0);
	lce_dcs_write_seq_static(ctx, 0xD0,0x00,0x10,0x1F,0x1E,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x0A,0x08,0x1F,0x1F,0x06,0x04,0x00,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x55,0x55,0x54);
	lce_dcs_write_seq_static(ctx, 0xD1,0x00,0x11,0x1F,0x1E,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x0B,0x09,0x1F,0x1F,0x07,0x05,0x01,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x55,0x55,0x54);
	lce_dcs_write_seq_static(ctx, 0xD2,0x00,0x01,0x1F,0x1F,0x1E,0x1F,0x1F,0x1F,0x1F,0x1F,0x05,0x07,0x1F,0x1F,0x09,0x0B,0x11,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x00,0x00,0x00);
	lce_dcs_write_seq_static(ctx, 0xD3,0x00,0x00,0x1F,0x1F,0x1E,0x1F,0x1F,0x1F,0x1F,0x1F,0x04,0x06,0x1F,0x1F,0x08,0x0A,0x10,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x00,0x00,0x00);
	lce_dcs_write_seq_static(ctx, 0xD4,0x10,0x00,0x00,0x0D,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x04,0x00,0x10,0xA6,0x2C,0x00,0x04,0x02,0x01,0x00,0x01,0x00,0x04,0x01,0x01,0x11,0x60,0x03,0x00,0x05,0x01,0x01,0x00,0x00,0x00,0x00,0x0A,0x06,0x61,0x00,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x81,0x03,0xD4);
	lce_dcs_write_seq_static(ctx, 0xD5,0x02,0x10,0x01,0x00,0x01,0x00,0x00,0x00,0x00,0x43,0x00,0x00,0x00,0x07,0x32,0x5A,0x00,0x48,0x3B,0x00,0x02,0x1E,0x02,0x72,0x00,0x13,0x08);
	lce_dcs_write_seq_static(ctx, 0xD7,0x00,0x90,0x92,0x90,0x92,0x79,0x79,0x79,0x79,0x79,0x79,0x79,0x79,0x80,0x80,0x79,0x79,0x79,0x79);
	lce_dcs_write_seq_static(ctx, 0xDE,0x01);
	lce_dcs_write_seq_static(ctx, 0xB5,0x6F,0x00,0x01,0x00,0x00);
	lce_dcs_write_seq_static(ctx, 0xC5,0x22,0x12);
	lce_dcs_write_seq_static(ctx, 0xDE,0x02);
	lce_dcs_write_seq_static(ctx, 0xB3,0x20,0xA0,0xEA,0x5F,0x4B);
	lce_dcs_write_seq_static(ctx, 0xB7,0x44,0x00,0x88,0x00,0x03,0x00,0x6E,0x10);
	lce_dcs_write_seq_static(ctx, 0xBD,0x14);
	lce_dcs_write_seq_static(ctx, 0xC1,0x00,0x40,0x20,0x00,0x14,0x14,0x14,0x14);
	lce_dcs_write_seq_static(ctx, 0xC2,0x42,0x70,0x01,0x01,0xE0,0x73,0xF8);
	lce_dcs_write_seq_static(ctx, 0xC3,0x20,0xFB);
	lce_dcs_write_seq_static(ctx, 0xC6,0x05);
	lce_dcs_write_seq_static(ctx, 0xE5,0x00,0xE6,0xE5,0x02,0x27,0x42,0x27,0x42,0x09,0x00);
	lce_dcs_write_seq_static(ctx, 0xE6,0x10,0x10,0xB4);
	lce_dcs_write_seq_static(ctx, 0xEC,0x01,0x7F,0x22);
	lce_dcs_write_seq_static(ctx, 0xDE,0x03);
	lce_dcs_write_seq_static(ctx, 0xD1,0x00);
	lce_dcs_write_seq_static(ctx, 0xDE,0x00);
	lce_dcs_write_seq_static(ctx, 0xE9,0x00,0x23);
	lce_dcs_write_seq_static(ctx, 0x35,0x00);

    lce_dcs_write_seq_static(ctx, 0x51,0x00,0x00);    
    lce_dcs_write_seq_static(ctx, 0x53,0x2C);
    lce_dcs_write_seq_static(ctx, 0x55,0x00);

	lce_dcs_write_seq_static(ctx, 0x11,0x00);
    msleep(20);
	gpiod_set_value(ctx->tp_rst_gpio, 1);
	msleep(20);
	devm_gpiod_put(ctx->dev, ctx->tp_rst_gpio);
    msleep(80);
	lce_dcs_write_seq_static(ctx, 0x29, 0x00);
    msleep(10);

	lce_dcs_write_seq(ctx, bl_tb0[0], bl_tb0[1], bl_tb0[2]);

	pr_info("%s-\n", __func__);
}

static int lce_disable(struct drm_panel *panel)
{
	struct lce *ctx = panel_to_lce(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int lce_unprepare(struct drm_panel *panel)
{
	struct lce *ctx = panel_to_lce(panel);

	if (!ctx->prepared)
		return 0;

	msleep(1);
	lce_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(50);
	lce_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(100);

	if( !jd_gesture_flag){
//		lce_dcs_write_seq_static(ctx, 0xDE,0x00);
//		lce_dcs_write_seq_static(ctx, 0xBA,0x01);
//		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "lcd_reset", GPIOD_OUT_HIGH);
//		gpiod_set_value(ctx->reset_gpio, 0);
//		devm_gpiod_put(ctx->dev, ctx->reset_gpio);

		usleep_range(3 * 1000, 8 * 1000);
		lcd_bias_set_vspn(0, 1, BIAS_OUT_VALUE);
		usleep_range(5 * 1000, 8 * 1000);
	}
//	lcm_enable_reg_vtp_1p8(0);
	ctx->error = 0;
	ctx->prepared = false;

	return 0;
}

static int lce_prepare(struct drm_panel *panel)
{
	struct lce *ctx = panel_to_lce(panel);
	int ret;

	pr_info("hpy%s+\n", __func__);
	if (ctx->prepared)
		return 0;

	ret = lcm_enable_reg_vtp_1p8(1);
	usleep_range(8 * 1000, 10 * 1000);
	lcd_bias_set_vspn(1, 0, BIAS_OUT_VALUE);
	msleep(3);
	lce_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0) {
		lce_unprepare(panel);
		pr_info("%s11111-\n", __func__);
	}
	ctx->prepared = true;
#ifdef PANEL_SUPPORT_READBACK
	lce_panel_get_data(ctx);
#endif

#ifdef VENDOR_EDIT
	// shifan@bsp.tp 20191226 add for loading tp fw when screen lighting on
	lcd_queue_load_tp_fw();
#endif

	pr_info("%s-\n", __func__);
	return ret;
}

static int lce_enable(struct drm_panel *panel)
{
	struct lce *ctx = panel_to_lce(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = 83564,
	.hdisplay = 720,
	.hsync_start = 720 + 20,//HFP
	.hsync_end = 720 + 20 + 20,//HSA
	.htotal = 720 + 20 + 20 + 22,//HBP
	.vdisplay = 1560,
	.vsync_start = 1560 + 200,//VFP
	.vsync_end = 1560 + 200 + 2,//VSA
	.vtotal = 1560 + 200 + 2 + 19,//VBP
};

#if 0
static const struct drm_display_mode performance_mode_30hz = {
	.clock = 352928,
	.hdisplay = 1200,
	.hsync_start = 1200 + 60,//HFP
	.hsync_end = 1200 + 60 + 20,//HSA
	.htotal = 1200 + 60 + 20 + 46,//HBP
	.vdisplay = 2000,
	.vsync_start = 2000 + 6852,//VFP
	.vsync_end = 2000 + 6852 + 8,//VSA
	.vtotal = 2000 + 6852 + 8 + 12,//VBP
}

static const struct drm_display_mode performance_mode_60hz = {
	.clock = 352928,
	.hdisplay = 1200,
	.hsync_start = 1200 + 60,//HFP
	.hsync_end = 1200 + 60 + 20,//HSA
	.htotal = 1200 + 60 + 20 + 46,//HBP
	.vdisplay = 2000,
	.vsync_start = 2000 + 2416,//VFP
	.vsync_end = 2000 + 2416 + 8,//VSA
	.vtotal = 2000 + 2416 + 8 + 12,//VBP
};
#endif

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.change_fps_by_vfp_send_cmd = 0,
	.vfp_low_power = 200,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x1c,
	},
	.phy_timcon = {
		.hs_trail = 6,
		.clk_trail = 6,
	},
	.ssc_enable = 0,
	.lane_swap_en = 0,
	.lane_swap[0][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[0][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[0][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[0][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[0][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[0][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[1][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[1][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[1][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[1][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
/*	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2400,
		.pic_width = 1080,
		.slice_height = 8,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 512,
		.dec_delay = 526,
		.scale_value = 32,
		.increment_interval = 187,
		.decrement_interval = 7,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 3511,
		.slice_bpg_offset = 3255,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
#if 0
		.rc_buf_thresh[0] = 14,
		.rc_buf_thresh[1] = 28,
		.rc_buf_thresh[2] = 42,
		.rc_buf_thresh[3] = 56,
		.rc_buf_thresh[4] = 70,
		.rc_buf_thresh[5] = 84,
		.rc_buf_thresh[6] = 98,
		.rc_buf_thresh[7] = 105,
		.rc_buf_thresh[8] = 112,
		.rc_buf_thresh[9] = 119,
		.rc_buf_thresh[10] = 121,
		.rc_buf_thresh[11] = 123,
		.rc_buf_thresh[12] = 125,
		.rc_buf_thresh[13] = 126,
		.rc_range_parameters[0].range_min_qp = 0,
		.rc_range_parameters[0].range_max_qp = 4,
		.rc_range_parameters[0].range_bpg_offset = 2,
		.rc_range_parameters[1].range_min_qp = 0,
		.rc_range_parameters[1].range_max_qp = 4,
		.rc_range_parameters[1].range_bpg_offset = 0,
		.rc_range_parameters[2].range_min_qp = 1,
		.rc_range_parameters[2].range_max_qp = 5,
		.rc_range_parameters[2].range_bpg_offset = 0,
		.rc_range_parameters[3].range_min_qp = 1,
		.rc_range_parameters[3].range_max_qp = 6,
		.rc_range_parameters[3].range_bpg_offset = -2,
		.rc_range_parameters[4].range_min_qp = 3,
		.rc_range_parameters[4].range_max_qp = 7,
		.rc_range_parameters[4].range_bpg_offset = -4,
		.rc_range_parameters[5].range_min_qp = 3,
		.rc_range_parameters[5].range_max_qp = 7,
		.rc_range_parameters[5].range_bpg_offset = -6,
		.rc_range_parameters[6].range_min_qp = 3,
		.rc_range_parameters[6].range_max_qp = 7,
		.rc_range_parameters[6].range_bpg_offset = -8,
		.rc_range_parameters[7].range_min_qp = 3,
		.rc_range_parameters[7].range_max_qp = 8,
		.rc_range_parameters[7].range_bpg_offset = -8,
		.rc_range_parameters[8].range_min_qp = 3,
		.rc_range_parameters[8].range_max_qp = 9,
		.rc_range_parameters[8].range_bpg_offset = -8,
		.rc_range_parameters[9].range_min_qp = 3,
		.rc_range_parameters[9].range_max_qp = 10,
		.rc_range_parameters[9].range_bpg_offset = -10,
		.rc_range_parameters[10].range_min_qp = 5,
		.rc_range_parameters[10].range_max_qp = 11,
		.rc_range_parameters[10].range_bpg_offset = -10,
		.rc_range_parameters[11].range_min_qp = 5,
		.rc_range_parameters[11].range_max_qp = 12,
		.rc_range_parameters[11].range_bpg_offset = -12,
		.rc_range_parameters[12].range_min_qp = 5,
		.rc_range_parameters[12].range_max_qp = 13,
		.rc_range_parameters[12].range_bpg_offset = -12,
		.rc_range_parameters[13].range_min_qp = 7,
		.rc_range_parameters[13].range_max_qp = 13,
		.rc_range_parameters[13].range_bpg_offset = -12,
		.rc_range_parameters[14].range_min_qp = 13,
		.rc_range_parameters[14].range_max_qp = 15,
		.rc_range_parameters[14].range_bpg_offset = -12
#endif
	},
*/
	//.data_rate_khz = 808000,
	.data_rate = 530,
	.lfr_enable = 0,
	.lfr_minimum_fps = 60,
/*
	.dyn_fps = {
		.switch_en = 0,
		.vact_timing_fps = 60,
		.dfps_cmd_table[0] = {0, 4, {0xB9, 0x83, 0x10, 0x21} },
		.dfps_cmd_table[1] = {0, 2, {0xE2, 0x00} },
		.dfps_cmd_table[2] = {0, 2, {0xB9, 0x00} },
	},
*/
	/* following MIPI hopping parameter might cause screen mess */
	.dyn = {
		.switch_en = 1,
		.pll_clk = 275,
		.hfp = 50,
	},
	.corner_pattern_height = ROUND_CORNER_H_TOP,
	.corner_pattern_tp_size = sizeof(top_rc_pattern),
	.corner_pattern_lt_addr = (void *)top_rc_pattern,
	//.rotate = 0,
};
#if 0
static struct mtk_panel_params ext_params_30hz = {
	.change_fps_by_vfp_send_cmd = 1,
	.vfp_low_power = 6852,
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.ssc_enable = 0,
	.lane_swap_en = 0,
	.lane_swap[0][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[0][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[0][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[0][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[0][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[0][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[1][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[1][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[1][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[1][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 18,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2000,
		.pic_width = 1200,
		.slice_height = 5,
		.slice_width = 600,
		.chunk_size = 600,
		.xmit_delay = 512,
		.dec_delay = 556,
		.scale_value = 32,
		.increment_interval = 117,
		.decrement_interval = 8,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 6144,
		.slice_bpg_offset = 4686,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		.rc_buf_thresh[0] = 14,
		.rc_buf_thresh[1] = 28,
		.rc_buf_thresh[2] = 42,
		.rc_buf_thresh[3] = 56,
		.rc_buf_thresh[4] = 70,
		.rc_buf_thresh[5] = 84,
		.rc_buf_thresh[6] = 98,
		.rc_buf_thresh[7] = 105,
		.rc_buf_thresh[8] = 112,
		.rc_buf_thresh[9] = 119,
		.rc_buf_thresh[10] = 121,
		.rc_buf_thresh[11] = 123,
		.rc_buf_thresh[12] = 125,
		.rc_buf_thresh[13] = 126,
		.rc_range_parameters[0].range_min_qp = 0,
		.rc_range_parameters[0].range_max_qp = 4,
		.rc_range_parameters[0].range_bpg_offset = 2,
		.rc_range_parameters[1].range_min_qp = 0,
		.rc_range_parameters[1].range_max_qp = 4,
		.rc_range_parameters[1].range_bpg_offset = 0,
		.rc_range_parameters[2].range_min_qp = 1,
		.rc_range_parameters[2].range_max_qp = 5,
		.rc_range_parameters[2].range_bpg_offset = 0,
		.rc_range_parameters[3].range_min_qp = 1,
		.rc_range_parameters[3].range_max_qp = 6,
		.rc_range_parameters[3].range_bpg_offset = -2,
		.rc_range_parameters[4].range_min_qp = 3,
		.rc_range_parameters[4].range_max_qp = 7,
		.rc_range_parameters[4].range_bpg_offset = -4,
		.rc_range_parameters[5].range_min_qp = 3,
		.rc_range_parameters[5].range_max_qp = 7,
		.rc_range_parameters[5].range_bpg_offset = -6,
		.rc_range_parameters[6].range_min_qp = 3,
		.rc_range_parameters[6].range_max_qp = 7,
		.rc_range_parameters[6].range_bpg_offset = -8,
		.rc_range_parameters[7].range_min_qp = 3,
		.rc_range_parameters[7].range_max_qp = 8,
		.rc_range_parameters[7].range_bpg_offset = -8,
		.rc_range_parameters[8].range_min_qp = 3,
		.rc_range_parameters[8].range_max_qp = 9,
		.rc_range_parameters[8].range_bpg_offset = -8,
		.rc_range_parameters[9].range_min_qp = 3,
		.rc_range_parameters[9].range_max_qp = 10,
		.rc_range_parameters[9].range_bpg_offset = -10,
		.rc_range_parameters[10].range_min_qp = 5,
		.rc_range_parameters[10].range_max_qp = 11,
		.rc_range_parameters[10].range_bpg_offset = -10,
		.rc_range_parameters[11].range_min_qp = 5,
		.rc_range_parameters[11].range_max_qp = 12,
		.rc_range_parameters[11].range_bpg_offset = -12,
		.rc_range_parameters[12].range_min_qp = 5,
		.rc_range_parameters[12].range_max_qp = 13,
		.rc_range_parameters[12].range_bpg_offset = -12,
		.rc_range_parameters[13].range_min_qp = 7,
		.rc_range_parameters[13].range_max_qp = 13,
		.rc_range_parameters[13].range_bpg_offset = -12,
		.rc_range_parameters[14].range_min_qp = 13,
		.rc_range_parameters[14].range_max_qp = 13,
		.rc_range_parameters[14].range_bpg_offset = -12
	},
	.data_rate_khz = 960298,
	.data_rate = 960,
	.lfr_enable = 0,
	.lfr_minimum_fps = 30,
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 30,
		.dfps_cmd_table[0] = {0, 4, {0xB9, 0x83, 0x10, 0x21} },
		.dfps_cmd_table[1] = {0, 2, {0xE2, 0x20} },
		.dfps_cmd_table[2] = {0, 2, {0xB9, 0x00} },
	},
	/* following MIPI hopping parameter might cause screen mess */
	.dyn = {
		.switch_en = 1,
		.vfp = 6852,
	},
	.corner_pattern_height = ROUND_CORNER_H_TOP,
	.corner_pattern_tp_size = sizeof(top_rc_pattern),
	.corner_pattern_lt_addr = (void *)top_rc_pattern,
	.rotate = 1,
};

static struct mtk_panel_params ext_params_60hz = {
	.change_fps_by_vfp_send_cmd = 1,
	.vfp_low_power = 2416,
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.ssc_enable = 0,
	.lane_swap_en = 0,
	.lane_swap[0][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[0][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[0][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[0][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[0][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[0][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_0] = MIPITX_PHY_LANE_0,
	.lane_swap[1][MIPITX_PHY_LANE_1] = MIPITX_PHY_LANE_1,
	.lane_swap[1][MIPITX_PHY_LANE_2] = MIPITX_PHY_LANE_3,
	.lane_swap[1][MIPITX_PHY_LANE_3] = MIPITX_PHY_LANE_2,
	.lane_swap[1][MIPITX_PHY_LANE_CK] = MIPITX_PHY_LANE_CK,
	.lane_swap[1][MIPITX_PHY_LANE_RX] = MIPITX_PHY_LANE_0,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 18,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2000,
		.pic_width = 1200,
		.slice_height = 5,
		.slice_width = 600,
		.chunk_size = 600,
		.xmit_delay = 512,
		.dec_delay = 556,
		.scale_value = 32,
		.increment_interval = 117,
		.decrement_interval = 8,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 6144,
		.slice_bpg_offset = 4686,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		.rc_buf_thresh[0] = 14,
		.rc_buf_thresh[1] = 28,
		.rc_buf_thresh[2] = 42,
		.rc_buf_thresh[3] = 56,
		.rc_buf_thresh[4] = 70,
		.rc_buf_thresh[5] = 84,
		.rc_buf_thresh[6] = 98,
		.rc_buf_thresh[7] = 105,
		.rc_buf_thresh[8] = 112,
		.rc_buf_thresh[9] = 119,
		.rc_buf_thresh[10] = 121,
		.rc_buf_thresh[11] = 123,
		.rc_buf_thresh[12] = 125,
		.rc_buf_thresh[13] = 126,
		.rc_range_parameters[0].range_min_qp = 0,
		.rc_range_parameters[0].range_max_qp = 4,
		.rc_range_parameters[0].range_bpg_offset = 2,
		.rc_range_parameters[1].range_min_qp = 0,
		.rc_range_parameters[1].range_max_qp = 4,
		.rc_range_parameters[1].range_bpg_offset = 0,
		.rc_range_parameters[2].range_min_qp = 1,
		.rc_range_parameters[2].range_max_qp = 5,
		.rc_range_parameters[2].range_bpg_offset = 0,
		.rc_range_parameters[3].range_min_qp = 1,
		.rc_range_parameters[3].range_max_qp = 6,
		.rc_range_parameters[3].range_bpg_offset = -2,
		.rc_range_parameters[4].range_min_qp = 3,
		.rc_range_parameters[4].range_max_qp = 7,
		.rc_range_parameters[4].range_bpg_offset = -4,
		.rc_range_parameters[5].range_min_qp = 3,
		.rc_range_parameters[5].range_max_qp = 7,
		.rc_range_parameters[5].range_bpg_offset = -6,
		.rc_range_parameters[6].range_min_qp = 3,
		.rc_range_parameters[6].range_max_qp = 7,
		.rc_range_parameters[6].range_bpg_offset = -8,
		.rc_range_parameters[7].range_min_qp = 3,
		.rc_range_parameters[7].range_max_qp = 8,
		.rc_range_parameters[7].range_bpg_offset = -8,
		.rc_range_parameters[8].range_min_qp = 3,
		.rc_range_parameters[8].range_max_qp = 9,
		.rc_range_parameters[8].range_bpg_offset = -8,
		.rc_range_parameters[9].range_min_qp = 3,
		.rc_range_parameters[9].range_max_qp = 10,
		.rc_range_parameters[9].range_bpg_offset = -10,
		.rc_range_parameters[10].range_min_qp = 5,
		.rc_range_parameters[10].range_max_qp = 11,
		.rc_range_parameters[10].range_bpg_offset = -10,
		.rc_range_parameters[11].range_min_qp = 5,
		.rc_range_parameters[11].range_max_qp = 12,
		.rc_range_parameters[11].range_bpg_offset = -12,
		.rc_range_parameters[12].range_min_qp = 5,
		.rc_range_parameters[12].range_max_qp = 13,
		.rc_range_parameters[12].range_bpg_offset = -12,
		.rc_range_parameters[13].range_min_qp = 7,
		.rc_range_parameters[13].range_max_qp = 13,
		.rc_range_parameters[13].range_bpg_offset = -12,
		.rc_range_parameters[14].range_min_qp = 13,
		.rc_range_parameters[14].range_max_qp = 13,
		.rc_range_parameters[14].range_bpg_offset = -12
	},
	.data_rate_khz = 960298,
	.data_rate = 960,
	.lfr_enable = 0,
	.lfr_minimum_fps = 60,
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 60,
		.dfps_cmd_table[0] = {0, 4, {0xB9, 0x83, 0x10, 0x21} },
		.dfps_cmd_table[1] = {0, 2, {0xE2, 0x10} },
		.dfps_cmd_table[2] = {0, 2, {0xB9, 0x00} },
	},
	/* following MIPI hopping parameter might cause screen mess */
	.dyn = {
		.switch_en = 1,
		.vfp = 2416,
	},
	.corner_pattern_height = ROUND_CORNER_H_TOP,
	.corner_pattern_tp_size = sizeof(top_rc_pattern),
	.corner_pattern_lt_addr = (void *)top_rc_pattern,
	.rotate = 1,
};
#endif

static int panel_ata_check(struct drm_panel *panel)
{
	/* Customer test by own ATA tool */
	return 1;
}

static int lce_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	unsigned int bl_level = level;

	if (level > 255)
		level = 255;

	if (level == 255)
		bl_level = 3840;
	else if (level > 0)
		bl_level = level * 3192 / 253;
	else
		bl_level = 0;
	bl_tb0[1] = (bl_level >> 8) & 0x0F;
	bl_tb0[2] = (bl_level & 0xFF);

	pr_info("%s level = %d, backlight = %d,bl_tb0[1] = 0x%x,bl_tb0[2] = 0x%x\n",
		__func__, level, bl_level, bl_tb0[1], bl_tb0[2]);
	if (!cb)
		return -1;
//	aw99703_bl_en(bl_level);
	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	return 0;
}

struct drm_display_mode *get_mode_by_id_hfp(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}

static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id_hfp(connector, mode);

	if (ext && m && drm_mode_vrefresh(m) == 60)
		ext->params = &ext_params;
//	else if (ext && m && drm_mode_vrefresh(m) == 30)
//		ext->params = &ext_params_30hz;
//	else if (ext && m && drm_mode_vrefresh(m) == 60)
//		ext->params = &ext_params_60hz;
	else
		ret = 1;

	return ret;
}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lce *ctx = panel_to_lce(panel);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "lcd_reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lce_setbacklight_cmdq,
	.ext_param_set = mtk_panel_ext_param_set,
	.ata_check = panel_ata_check,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	/**
	 * @prepare: the time (in milliseconds) that it takes for the panel to
	 *	   become ready and start receiving video data
	 * @enable: the time (in milliseconds) that it takes for the panel to
	 *	  display the first valid frame after starting to receive
	 *	  video data
	 * @disable: the time (in milliseconds) that it takes for the panel to
	 *	   turn the display off (no content is visible)
	 * @unprepare: the time (in milliseconds) that it takes for the panel
	 *		 to power itself down completely
	 */
	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int lce_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
//	struct drm_display_mode *mode2;
//	struct drm_display_mode *mode3;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 default_mode.hdisplay, default_mode.vdisplay,
			 drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);
#if 0
	mode2 = drm_mode_duplicate(connector->dev, &performance_mode_30hz);
	if (!mode2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_30hz.hdisplay, performance_mode_30hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_30hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode2);

	mode3 = drm_mode_duplicate(connector->dev, &performance_mode_60hz);
	if (!mode3) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_60hz.hdisplay, performance_mode_60hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_60hz));
		return -ENOMEM;
	}

	drm_mode_set_name(mode3);
	mode3->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode3);

	connector->display_info.width_mm = 150;
	connector->display_info.height_mm = 249;
#endif

	return 1;
}

static const struct drm_panel_funcs lce_drm_funcs = {
	.disable = lce_disable,
	.unprepare = lce_unprepare,
	.prepare = lce_prepare,
	.enable = lce_enable,
	.get_modes = lce_get_modes,
};

static int lce_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct lce *ctx;
	int ret;

	pr_info("%s+ lce,jd9365t,vdo,60hz\n", __func__);

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct lce), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET |
			MIPI_DSI_CLOCK_NON_CONTINUOUS;

	reg_vtp_1p8 = regulator_get(dev, "1p8");
	if (IS_ERR(reg_vtp_1p8)) {
		dev_info(dev, "%s[lh]: cannot get reg_vufs18 %ld\n",
			__func__, PTR_ERR(reg_vtp_1p8));
	}
	lcm_enable_reg_vtp_1p8(1);

	ctx->reset_gpio = devm_gpiod_get(dev, "lcd_reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "cannot get reset-gpios %ld\n",
			 PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

	ctx->prepared = true;
	ctx->enabled = true;
	drm_panel_init(&ctx->panel, dev, &lce_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;

#endif

	hq_regiser_hw_info(HWID_LCM, "incell,vendor:lce,IC:jd9365t");
	pr_info("%s- lce,jd9365t,vdo,60hz\n", __func__);

	return ret;
}

static int lce_remove(struct mipi_dsi_device *dsi)
{
	struct lce *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_detach(ext_ctx);
	mtk_panel_remove(ext_ctx);
#endif

	return 0;
}

static const struct of_device_id lce_of_match[] = {
	{
	    .compatible = "lce,jd9365t,vdo,60hz",
	},
	{}
};

MODULE_DEVICE_TABLE(of, lce_of_match);

static struct mipi_dsi_driver lce_driver = {
	.probe = lce_probe,
	.remove = lce_remove,
	.driver = {
		.name = "panel-lce-jd9365t-vdo-60hz",
		.owner = THIS_MODULE,
		.of_match_table = lce_of_match,
	},
};

module_mipi_dsi_driver(lce_driver);

MODULE_AUTHOR("kangyawei <kangyawei5@huaqin.com>");
MODULE_DESCRIPTION("LCE JD9365T VDO 60HZ LCD Panel Driver");
MODULE_LICENSE("GPL v2");

