/*
 * Copyright (C) 2018 Renesas Electronics
 *
 * Copyright (C) 2016 Atmel
 *		      Bo Shen <voice.shen@atmel.com>
 *
 * Authors:	      Bo Shen <voice.shen@atmel.com>
 *		      Boris Brezillon <boris.brezillon@free-electrons.com>
 *		      Wu, Songjun <Songjun.Wu@atmel.com>
 *
 *
 * Copyright (C) 2010-2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitfield.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c-mux.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>

#include <sound/hdmi-codec.h>

#define SII902X_TPI_VIDEO_DATA			0x0

#define SII902X_TPI_PIXEL_REPETITION		0x8
#define SII902X_TPI_AVI_PIXEL_REP_BUS_24BIT     BIT(5)
#define SII902X_TPI_AVI_PIXEL_REP_RISING_EDGE   BIT(4)
#define SII902X_TPI_AVI_PIXEL_REP_4X		3
#define SII902X_TPI_AVI_PIXEL_REP_2X		1
#define SII902X_TPI_AVI_PIXEL_REP_NONE		0
#define SII902X_TPI_CLK_RATIO_HALF		(0 << 6)
#define SII902X_TPI_CLK_RATIO_1X		(1 << 6)
#define SII902X_TPI_CLK_RATIO_2X		(2 << 6)
#define SII902X_TPI_CLK_RATIO_4X		(3 << 6)

#define SII902X_TPI_AVI_IN_FORMAT		0x9
#define SII902X_TPI_AVI_INPUT_BITMODE_12BIT	BIT(7)
#define SII902X_TPI_AVI_INPUT_DITHER		BIT(6)
#define SII902X_TPI_AVI_INPUT_RANGE_LIMITED	(2 << 2)
#define SII902X_TPI_AVI_INPUT_RANGE_FULL	(1 << 2)
#define SII902X_TPI_AVI_INPUT_RANGE_AUTO	(0 << 2)
#define SII902X_TPI_AVI_INPUT_COLORSPACE_BLACK	(3 << 0)
#define SII902X_TPI_AVI_INPUT_COLORSPACE_YUV422	(2 << 0)
#define SII902X_TPI_AVI_INPUT_COLORSPACE_YUV444	(1 << 0)
#define SII902X_TPI_AVI_INPUT_COLORSPACE_RGB	(0 << 0)

#define SII902X_TPI_AVI_INFOFRAME		0x0c

#define SII902X_SYS_CTRL_DATA			0x1a
#define SII902X_SYS_CTRL_PWR_DWN		BIT(4)
#define SII902X_SYS_CTRL_AV_MUTE		BIT(3)
#define SII902X_SYS_CTRL_DDC_BUS_REQ		BIT(2)
#define SII902X_SYS_CTRL_DDC_BUS_GRTD		BIT(1)
#define SII902X_SYS_CTRL_OUTPUT_MODE		BIT(0)
#define SII902X_SYS_CTRL_OUTPUT_HDMI		1
#define SII902X_SYS_CTRL_OUTPUT_DVI		0

#define SII902X_REG_CHIPID(n)			(0x1b + (n))

#define SII902X_PWR_STATE_CTRL			0x1e
#define SII902X_AVI_POWER_STATE_MSK		GENMASK(1, 0)
#define SII902X_AVI_POWER_STATE_D(l)		((l) & SII902X_AVI_POWER_STATE_MSK)

#define SII902X_I2S_MAP				0x1f
#define SII902X_I2S_MAP_SWITCH			BIT(7)
#define SII902X_I2S_MAP_SD_MSK			GENMASK(5, 4)
#define SII902X_I2S_MAP_SD(v)			\
	FIELD_PREP(SII902X_I2S_MAP_SD_MSK, v)
#define SII902X_I2S_MAP_DS			BIT(3)
#define SII902X_I2S_MAP_SWAP			BIT(2)
#define SII902X_I2S_MAP_FIFO_MSK		GENMASK(1, 0)
#define SII902X_I2S_MAP_FIFO(v)			\
	FIELD_PREP(SII902X_I2S_MAP_FIFO_MSK, v)

#define SII902X_I2S_CONF			0x20
#define SII902X_I2S_CONF_SCK_STROBING		BIT(7)
#define SII902X_I2S_CONF_MCLK_RATIO_MSK		GENMASK(6, 4)
#define SII902X_I2S_CONF_WS_STROBING		BIT(3)
#define SII902X_I2S_CONF_JUSTIFY		BIT(2)
#define SII902X_I2S_CONF_LSB_DIR		BIT(1)
#define SII902X_I2S_CONF_NO_OFFSET		BIT(0)

#define SII902X_I2S_CS0				0x21

#define SII902X_I2S_CS1				0x22

#define SII902X_I2S_CS2				0x23
#define SII902X_I2S_CS2_SRC_MSK			GENMASK(3, 0)
#define SII902X_I2S_CS2_CHAN_MSK		GENMASK(7, 4)

#define SII902X_I2S_CS3				0x24
#define SII902X_I2S_CS3_FS_MSK			GENMASK(3, 0)
#define SII902X_I2S_CS3_FS(v)			\
	FIELD_PREP(SII902X_I2S_CS3_FS_MSK, v)
#define SII902X_I2S_CS3_ACC_MSK			GENMASK(7, 4)

#define SII902X_I2S_CS4				0x25
#define SII902X_I2S_CS4_WL_MSK			GENMASK(3, 0)
#define SII902X_I2S_CS4_WL(v)			\
	FIELD_PREP(SII902X_I2S_CS4_WL_MSK, v)

#define SII902X_AIF				0x26
#define SII902X_AIF_FMT_MSK			GENMASK(7, 6)
#define SII902X_AIF_FMT(v)			\
	FIELD_PREP(SII902X_AIF_FMT_MSK, v)
#define SII902X_AIF_LAYOUT			BIT(5)
#define SII902X_AIF_MUTE			BIT(4)
#define SII902X_AIF_CODING_MSK			GENMASK(3, 0)
#define SII902X_AIF_CODING(v)			\
	FIELD_PREP(SII902X_AIF_CODING_MSK, v)

#define SII902X_I2S_AUD_FMT			0x27
#define SII902X_I2S_AUD_FMT_SZ_MSK		GENMASK(7, 6)
#define SII902X_I2S_AUD_FMT_SZ(v)		\
	FIELD_PREP(SII902X_I2S_AUD_FMT_SZ_MSK, v)
#define SII902X_I2S_AUD_FMT_FS_MSK		GENMASK(5, 3)
#define SII902X_I2S_AUD_FMT_FS(v)		\
	FIELD_PREP(SII902X_I2S_AUD_FMT_FS_MSK, v)
#define SII902X_I2S_AUD_FMT_FS_HBR		BIT(2)

#define SII902X_INT_ENABLE			0x3c
#define SII902X_INT_STATUS			0x3d
#define SII902X_HOTPLUG_EVENT			BIT(0)
#define SII902X_PLUGGED_STATUS			BIT(2)

#define SII902X_PLL_R1				0x82
#define SII902X_PLL_R1_TCLKSEL_MSK		GENMASK(6, 5)
#define SII902X_PLL_R1_TCLKSEL(v)		\
	FIELD_PREP(SII902X_PLL_R1_TCLKSEL_MSK, v)
#define SII902X_PLL_R2				0x83
#define SII902X_PLL_R2_CLKMUTLCTL_MSK		GENMASK(5, 4)
#define SII902X_PLL_R2_CLKMUTLCTL(v)		\
	FIELD_PREP(SII902X_PLL_R2_CLKMUTLCTL_MSK, v)
#define SII902X_PLL_R3				0x84
#define SII902X_PLL_R3_ACLKCNT_MSK		GENMASK(5, 4)
#define SII902X_PLL_R3_ACLKCNT(v)		\
	FIELD_PREP(SII902X_PLL_R3_ACLKCNT_MSK, v)
#define SII902X_PLL_R3_HLFCLKEN			BIT(1)

#define SII902X_INDEXED_REG_PAGE		0xbc
#define SII902X_INDEXED_REG_IDX			0xbd
#define SII902X_INDEXED_REG_ACCESS		0xbe

#define SII902X_OTHER_IF			0xbf
#define SII902X_OTHER_IF_SEL_MSK		GENMASK(2, 0)
#define SII902X_OTHER_IF_SEL(v)			\
	FIELD_PREP(SII902X_OTHER_IF_SEL_MSK, v)
#define SII902X_OTHER_IF_REPEAT			BIT(6)
#define SII902X_OTHER_IF_ENABLE			BIT(7)

#define SII902X_REG_TPI_RQB			0xc7

#define SII902X_I2C_BUS_ACQUISITION_TIMEOUT_MS	500

#define SII902X_IF_AUDIO			2

/* CEC device */
#define SII902X_CEC_I2C_ADDR			0x30

#define SII902X_CEC_SETUP			0x8e

enum sii902x_i2s_map_sd {
	SII902X_I2S_MAP_SD0,
	SII902X_I2S_MAP_SD1,
	SII902X_I2S_MAP_SD2,
	SII902X_I2S_MAP_SD3,
};

enum sii902x_i2s_map_fifo {
	SII902X_I2S_MAP_FIFO0,
	SII902X_I2S_MAP_FIFO1,
	SII902X_I2S_MAP_FIFO2,
	SII902X_I2S_MAP_FIFO3,
};

enum sii902x_aif_format {
	SII902X_AIF_FORMAT_SPDIF = 1,
	SII902X_AIF_FORMAT_I2S,
	SII902X_AIF_FORMAT_DSD,
};

enum sii902x_aif_coding {
	SII902X_AIF_CODING_STREAM_HEADER,
	SII902X_AIF_CODING_PCM,
	SII902X_AIF_CODING_AC3,
	SII902X_AIF_CODING_MPEG1,
	SII902X_AIF_CODING_MP3,
	SII902X_AIF_CODING_MPEG2,
	SII902X_AIF_CODING_AAC,
	SII902X_AIF_CODING_DTS,
	SII902X_AIF_CODING_ATRAC,
};

enum sii902x_sample_rate {
	SII902X_SAMPLE_RATE_32000 = 1,
	SII902X_SAMPLE_RATE_44100,
	SII902X_SAMPLE_RATE_48000,
	SII902X_SAMPLE_RATE_88200,
	SII902X_SAMPLE_RATE_96000,
	SII902X_SAMPLE_RATE_176400,
	SII902X_SAMPLE_RATE_192000,
};

enum sii902x_sample_width {
	SII902X_SAMPLE_RATE_SIZE_16 = 1,
	SII902X_SAMPLE_RATE_SIZE_20,
	SII902X_SAMPLE_RATE_SIZE_24,
};

struct sii902x_audio_params {
	unsigned int aes_size;
	unsigned int aes_rate;
};

struct sii902x {
	struct i2c_client *i2c;
	struct regmap *regmap;
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct gpio_desc *reset_gpio;
	struct i2c_mux_core *i2cmux;
	struct regulator_bulk_data supplies[2];
	struct platform_device *audio_pdev;
	struct sii902x_audio_params audio;
	struct edid *edid;
};

static int sii902x_read_unlocked(struct i2c_client *i2c, u8 reg, u8 *val)
{
	struct i2c_msg xfer[] = {
		{
			.addr = i2c->addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = i2c->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = val,
		}
	};
	unsigned char xfers = ARRAY_SIZE(xfer);
	int ret, retries = 5;

	do {
		ret = __i2c_transfer(i2c->adapter, xfer, xfers);
		if (ret < 0)
			return ret;
	} while (ret != xfers && --retries);
	return ret == xfers ? 0 : -1;
}

static int sii902x_write_unlocked(struct i2c_client *i2c, u8 reg, u8 val)
{
	u8 data[2] = {reg, val};
	struct i2c_msg xfer = {
		.addr = i2c->addr,
		.flags = 0,
		.len = sizeof(data),
		.buf = data,
	};
	int ret, retries = 5;

	do {
		ret = __i2c_transfer(i2c->adapter, &xfer, 1);
		if (ret < 0)
			return ret;
	} while (!ret && --retries);
	return !ret ? -1 : 0;
}

static int sii902x_update_bits_unlocked(struct i2c_client *i2c, u8 reg, u8 mask,
					u8 val)
{
	int ret;
	u8 status;

	ret = sii902x_read_unlocked(i2c, reg, &status);
	if (ret)
		return ret;
	status &= ~mask;
	status |= val & mask;
	return sii902x_write_unlocked(i2c, reg, status);
}

static inline struct sii902x *bridge_to_sii902x(struct drm_bridge *bridge)
{
	return container_of(bridge, struct sii902x, bridge);
}

static inline struct sii902x *connector_to_sii902x(struct drm_connector *con)
{
	return container_of(con, struct sii902x, connector);
}

static void sii902x_reset(struct sii902x *sii902x)
{
	if (!sii902x->reset_gpio)
		return;

	gpiod_set_value(sii902x->reset_gpio, 1);

	/* The datasheet says treset-min = 100us. Make it 150us to be sure. */
	usleep_range(150, 200);

	gpiod_set_value(sii902x->reset_gpio, 0);
}

static enum drm_connector_status
sii902x_connector_detect(struct drm_connector *connector, bool force)
{
	struct sii902x *sii902x = connector_to_sii902x(connector);
	unsigned int status;

	regmap_read(sii902x->regmap, SII902X_INT_STATUS, &status);

	return (status & SII902X_PLUGGED_STATUS) ?
	       connector_status_connected : connector_status_disconnected;
}

static const struct drm_connector_funcs sii902x_connector_funcs = {
	.detect = sii902x_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int sii902x_get_modes(struct drm_connector *connector)
{
	struct sii902x *sii902x = connector_to_sii902x(connector);
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	struct edid *edid;
	bool hdmi_mode = false;
	int num = 0, ret;

	edid = drm_get_edid(connector, sii902x->i2cmux->adapter[0]);
	drm_connector_update_edid_property(connector, edid);
	if (edid) {
		num = drm_add_edid_modes(connector, edid);
		hdmi_mode = drm_detect_hdmi_monitor(edid);
		kfree(sii902x->edid);
		sii902x->edid = edid;
	}

	ret = drm_display_info_set_bus_formats(&connector->display_info,
					       &bus_format, 1);
	if (ret)
		return ret;

	if (hdmi_mode)
		regmap_update_bits(sii902x->regmap, SII902X_SYS_CTRL_DATA,
				   SII902X_SYS_CTRL_OUTPUT_MODE,
				   SII902X_SYS_CTRL_OUTPUT_HDMI);

	return num;
}

static enum drm_mode_status sii902x_mode_valid(struct drm_connector *connector,
					       struct drm_display_mode *mode)
{
	/* TODO: check mode */

	return MODE_OK;
}

static const struct drm_connector_helper_funcs sii902x_connector_helper_funcs = {
	.get_modes = sii902x_get_modes,
	.mode_valid = sii902x_mode_valid,
};

static void sii902x_bridge_disable(struct drm_bridge *bridge)
{
	struct sii902x *sii902x = bridge_to_sii902x(bridge);

	regmap_update_bits(sii902x->regmap, SII902X_SYS_CTRL_DATA,
			   SII902X_SYS_CTRL_PWR_DWN,
			   SII902X_SYS_CTRL_PWR_DWN);
}

static void sii902x_bridge_enable(struct drm_bridge *bridge)
{
	struct sii902x *sii902x = bridge_to_sii902x(bridge);
	bool hdmi_mode;

	regmap_update_bits(sii902x->regmap, SII902X_PWR_STATE_CTRL,
			   SII902X_AVI_POWER_STATE_MSK,
			   SII902X_AVI_POWER_STATE_D(0));
	regmap_update_bits(sii902x->regmap, SII902X_SYS_CTRL_DATA,
			   SII902X_SYS_CTRL_PWR_DWN, 0);

	if(sii902x->edid) {
		hdmi_mode = drm_detect_hdmi_monitor(sii902x->edid);
		if (hdmi_mode)
			regmap_update_bits(sii902x->regmap,
					   SII902X_SYS_CTRL_DATA,
					   SII902X_SYS_CTRL_OUTPUT_MODE,
					   SII902X_SYS_CTRL_OUTPUT_HDMI);
	}
}

static void sii902x_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adj)
{
	struct sii902x *sii902x = bridge_to_sii902x(bridge);
	struct regmap *regmap = sii902x->regmap;
	u8 buf[HDMI_INFOFRAME_SIZE(AVI)];
	struct hdmi_avi_infoframe frame;
	u16 pixel_clock_10kHz = adj->clock / 10;
	int ret;

	buf[0] = pixel_clock_10kHz & 0xff;
	buf[1] = pixel_clock_10kHz >> 8;
	buf[2] = adj->vrefresh;
	buf[3] = 0x00;
	buf[4] = adj->hdisplay;
	buf[5] = adj->hdisplay >> 8;
	buf[6] = adj->vdisplay;
	buf[7] = adj->vdisplay >> 8;
	buf[8] = SII902X_TPI_CLK_RATIO_1X | SII902X_TPI_AVI_PIXEL_REP_NONE |
		 SII902X_TPI_AVI_PIXEL_REP_BUS_24BIT;
	buf[9] = SII902X_TPI_AVI_INPUT_RANGE_AUTO |
		 SII902X_TPI_AVI_INPUT_COLORSPACE_RGB;

	ret = regmap_bulk_write(regmap, SII902X_TPI_VIDEO_DATA, buf, 10);
	if (ret)
		return;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame, adj, false);
	if (ret < 0) {
		DRM_ERROR("couldn't fill AVI infoframe\n");
		return;
	}

	ret = hdmi_avi_infoframe_pack(&frame, buf, sizeof(buf));
	if (ret < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %d\n", ret);
		return;
	}

	/* Do not send the infoframe header, but keep the CRC field. */
	regmap_bulk_write(regmap, SII902X_TPI_AVI_INFOFRAME,
			  buf + HDMI_INFOFRAME_HEADER_SIZE - 1,
			  HDMI_AVI_INFOFRAME_SIZE + 1);
}

static int sii902x_bridge_attach(struct drm_bridge *bridge)
{
	struct sii902x *sii902x = bridge_to_sii902x(bridge);
	struct drm_device *drm = bridge->dev;
	int ret;

	drm_connector_helper_add(&sii902x->connector,
				 &sii902x_connector_helper_funcs);

	if (!drm_core_check_feature(drm, DRIVER_ATOMIC)) {
		dev_err(&sii902x->i2c->dev,
			"sii902x driver is only compatible with DRM devices supporting atomic updates\n");
		return -ENOTSUPP;
	}

	ret = drm_connector_init(drm, &sii902x->connector,
				 &sii902x_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;

	if (sii902x->i2c->irq > 0)
		sii902x->connector.polled = DRM_CONNECTOR_POLL_HPD;
	else
		sii902x->connector.polled = DRM_CONNECTOR_POLL_CONNECT;

	drm_connector_attach_encoder(&sii902x->connector, bridge->encoder);

	return 0;
}

static int sii902x_audio_infoframe_config(struct sii902x *sii902x,
					  struct hdmi_codec_params *params)
{
	u8 buf[HDMI_INFOFRAME_SIZE(AUDIO)];
	int ret;

	ret = hdmi_audio_infoframe_init(&params->cea);
	if (ret) {
		DRM_ERROR("Failed to init audio infoframe\n");
		return ret;
	}

	ret = hdmi_audio_infoframe_pack(&params->cea, buf, sizeof(buf));
	if (ret < 0) {
		DRM_ERROR("failed to pack audio infoframe: %d\n", ret);
		return ret;
	}

	regmap_update_bits(sii902x->regmap,
			   SII902X_OTHER_IF,
			   SII902X_OTHER_IF_SEL_MSK |
			   SII902X_OTHER_IF_REPEAT |
			   SII902X_OTHER_IF_ENABLE,
			   SII902X_OTHER_IF_SEL(SII902X_IF_AUDIO) |
			   SII902X_OTHER_IF_REPEAT |
			   SII902X_OTHER_IF_ENABLE);

	return regmap_bulk_write(sii902x->regmap, SII902X_OTHER_IF + 1, buf,
				 HDMI_INFOFRAME_SIZE(AUDIO) +
				 HDMI_INFOFRAME_HEADER_SIZE - 1);
}

static int sii902x_audio_iec60958_config(struct sii902x *sii902x)
{
	/* Bytes 0,1,2 are let to default setting. Configure bytes 3&4 */
	regmap_update_bits(sii902x->regmap,
			   SII902X_I2S_CS3,
			   SII902X_I2S_CS3_FS_MSK,
			   SII902X_I2S_CS3_FS(sii902x->audio.aes_rate));

	return regmap_update_bits(sii902x->regmap,
				  SII902X_I2S_CS4,
				  SII902X_I2S_CS4_WL_MSK,
				  SII902X_I2S_CS4_WL(sii902x->audio.aes_size));
}

static int sii902x_i2s_configure(struct sii902x *sii902x,
				 struct hdmi_codec_params *params,
				 struct hdmi_codec_daifmt *fmt)
{
	unsigned int rate, size, val, mask;

	/* Configure audio interface */
	regmap_update_bits(sii902x->regmap, SII902X_AIF,
			   SII902X_AIF_FMT_MSK |
			   SII902X_AIF_LAYOUT |
			   SII902X_AIF_MUTE |
			   SII902X_AIF_CODING_MSK,
			   SII902X_AIF_FMT(SII902X_AIF_FORMAT_I2S) |
			   SII902X_AIF_MUTE |
			   SII902X_AIF_CODING(SII902X_AIF_CODING_PCM));

	switch (fmt->fmt) {
	case HDMI_I2S:
		val = SII902X_I2S_CONF_SCK_STROBING;
		break;
	case HDMI_LEFT_J:
		val = SII902X_I2S_CONF_SCK_STROBING |
		      SII902X_I2S_CONF_WS_STROBING |
		      SII902X_I2S_CONF_NO_OFFSET;
		break;
	case HDMI_RIGHT_J:
		val = SII902X_I2S_CONF_SCK_STROBING |
		      SII902X_I2S_CONF_WS_STROBING |
		      SII902X_I2S_CONF_NO_OFFSET |
		      SII902X_I2S_CONF_JUSTIFY;
		break;
	default:
		DRM_ERROR("Unknown protocol %#x\n", fmt->fmt);
		return -EINVAL;
	}
	mask = SII902X_I2S_CONF_NO_OFFSET | SII902X_I2S_CONF_WS_STROBING |
	       SII902X_I2S_CONF_JUSTIFY | SII902X_I2S_CONF_LSB_DIR |
	       SII902X_I2S_CONF_SCK_STROBING;

	if (fmt->frame_clk_inv)
		val ^= SII902X_I2S_CONF_WS_STROBING;

	if (fmt->bit_clk_inv)
		val ^= SII902X_I2S_CONF_SCK_STROBING;

	/* Configure i2s interface */
	regmap_update_bits(sii902x->regmap,
			   SII902X_I2S_CONF, mask, val);

	/*
	 * Configure i2s interface mapping
	 * Assume that only SD0 is used and connected to FIFO0
	 */
	regmap_update_bits(sii902x->regmap,
			   SII902X_I2S_MAP,
			   SII902X_I2S_MAP_SWITCH |
			   SII902X_I2S_MAP_SD_MSK | SII902X_I2S_MAP_FIFO_MSK,
			   SII902X_I2S_MAP_SWITCH |
			   SII902X_I2S_MAP_SD0 | SII902X_I2S_MAP_FIFO0);

	switch (params->sample_rate) {
	case 32000:
		rate = SII902X_SAMPLE_RATE_32000;
		sii902x->audio.aes_rate = IEC958_AES3_CON_FS_32000;
		break;
	case 44100:
		rate = SII902X_SAMPLE_RATE_44100;
		sii902x->audio.aes_rate = IEC958_AES3_CON_FS_44100;
		break;
	case 48000:
		rate = SII902X_SAMPLE_RATE_48000;
		sii902x->audio.aes_rate = IEC958_AES3_CON_FS_48000;
		break;
	case 88200:
		rate = SII902X_SAMPLE_RATE_88200;
		sii902x->audio.aes_rate = IEC958_AES3_CON_FS_88200;
		break;
	case 96000:
		rate = SII902X_SAMPLE_RATE_96000;
		sii902x->audio.aes_rate = IEC958_AES3_CON_FS_96000;
		break;
	case 176400:
		rate = SII902X_SAMPLE_RATE_176400;
		sii902x->audio.aes_rate = IEC958_AES3_CON_FS_176400;
		break;
	case 192000:
		rate = SII902X_SAMPLE_RATE_192000;
		sii902x->audio.aes_rate = IEC958_AES3_CON_FS_192000;
		break;
	default:
		DRM_ERROR("Unknown sampling rate %d\n", params->sample_rate);
		return -EINVAL;
	}

	switch (params->sample_width) {
	case 16:
		size = SII902X_SAMPLE_RATE_SIZE_16;
		sii902x->audio.aes_size = IEC958_AES4_CON_WORDLEN_20_16;
		break;
	case 20:
		size = SII902X_SAMPLE_RATE_SIZE_20;
		sii902x->audio.aes_size = IEC958_AES4_CON_WORDLEN_24_20;
		break;
	case 24:
	case 32:
		size = SII902X_SAMPLE_RATE_SIZE_24;
		sii902x->audio.aes_size = IEC958_AES4_CON_WORDLEN_24_20 |
					  IEC958_AES4_CON_MAX_WORDLEN_24;
		break;
	default:
		DRM_ERROR("Unknown sample width %d\n", params->sample_width);
		return -EINVAL;
	}

	/* Configure i2s interface rate and input/output word length */
	regmap_update_bits(sii902x->regmap,
			   SII902X_INDEXED_REG_PAGE, 0xff, 0x2);
	regmap_update_bits(sii902x->regmap,
			   SII902X_INDEXED_REG_IDX, 0xff, 0x24);
	regmap_update_bits(sii902x->regmap,
			   SII902X_INDEXED_REG_ACCESS, 0x0f,
			   sii902x->audio.aes_size);

	return regmap_update_bits(sii902x->regmap,
				  SII902X_I2S_AUD_FMT,
				  SII902X_I2S_AUD_FMT_FS_MSK |
				  SII902X_I2S_AUD_FMT_SZ_MSK,
				  SII902X_I2S_AUD_FMT_FS(rate) |
				  SII902X_I2S_AUD_FMT_SZ(size));
}

static int sii902x_audio_hw_params(struct device *dev, void *data,
				   struct hdmi_codec_daifmt *fmt,
				   struct hdmi_codec_params *params)
{
	struct sii902x *sii902x = dev_get_drvdata(dev);
	int ret;

	if (fmt->bit_clk_master || fmt->frame_clk_master) {
		DRM_ERROR("Master mode not supported\n");
		return -EINVAL;
	}

	if (fmt->fmt == HDMI_I2S || fmt->fmt == HDMI_RIGHT_J ||
	    fmt->fmt == HDMI_LEFT_J) {
		/* Configure i2s interface */
		ret = sii902x_i2s_configure(sii902x, params, fmt);
		if (ret)
			return ret;

		/* Configure iec958 channel status */
		ret = sii902x_audio_iec60958_config(sii902x);
		if (ret)
			return ret;
	} else {
		DRM_ERROR("Unsupported format 0x%x\n", fmt->fmt);
		return -EINVAL;
	}

	/* Configure audio infoframes */
	return sii902x_audio_infoframe_config(sii902x, params);
}

static int sii902x_audio_digital_mute(struct device *dev,
				      void *data, bool enable)
{
	struct sii902x *sii902x = dev_get_drvdata(dev);
	int ret;

	DRM_DEBUG("%s audio\n", enable ? "mute" : "unmute");

	if (enable)
		ret = regmap_update_bits(sii902x->regmap, SII902X_AIF,
					 SII902X_AIF_MUTE, SII902X_AIF_MUTE);
	else
		ret = regmap_update_bits(sii902x->regmap, SII902X_AIF,
					 SII902X_AIF_MUTE, 0);

	return ret;
}

static int sii902x_audio_get_eld(struct device *dev, void *data,
				 u8 *buf, size_t len)
{
	struct sii902x *sii902x = dev_get_drvdata(dev);
	struct drm_connector *connector = &sii902x->connector;

	if (!sii902x->edid)
		return -ENODEV;

	memcpy(buf, connector->eld, min(sizeof(connector->eld), len));

	return 0;
}

static void sii902x_audio_shutdown(struct device *dev, void *data)
{}

static int sii902x_audio_get_dai_id(struct snd_soc_component *component,
				    struct device_node *endpoint)
{
	struct of_endpoint of_ep;
	int ret;

	ret = of_graph_parse_endpoint(endpoint, &of_ep);
	if (ret < 0)
		return ret;

	/* HDMI sound should be located at reg = <1> */
	if (of_ep.port == 1)
		return 0;

	return -EINVAL;
}

static const struct drm_bridge_funcs sii902x_bridge_funcs = {
	.attach = sii902x_bridge_attach,
	.mode_set = sii902x_bridge_mode_set,
	.disable = sii902x_bridge_disable,
	.enable = sii902x_bridge_enable,
};

static const struct regmap_range sii902x_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xff },
};

static const struct regmap_access_table sii902x_volatile_table = {
	.yes_ranges = sii902x_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(sii902x_volatile_ranges),
};

static const struct regmap_config sii902x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	/* map up to infoframe data registers. 0xbf-0xde */
	.max_register = 0xde,
	.volatile_table = &sii902x_volatile_table,
	.cache_type = REGCACHE_NONE,
};

static const struct hdmi_codec_ops sii902x_codec_ops = {
	.hw_params = sii902x_audio_hw_params,
	.audio_shutdown = sii902x_audio_shutdown,
	.get_dai_id = sii902x_audio_get_dai_id,
	.digital_mute = sii902x_audio_digital_mute,
	.get_eld = sii902x_audio_get_eld,
};

static int sii902x_register_audio_driver(struct device *dev,
					 struct sii902x *sii902x)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &sii902x_codec_ops,
		.max_i2s_channels = 2,
		.i2s = 1,
	};

	sii902x->audio_pdev = platform_device_register_data(
		dev, HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO,
		&codec_data, sizeof(codec_data));

	if (IS_ERR(sii902x->audio_pdev))
		return PTR_ERR(sii902x->audio_pdev);

	return 0;
}

static irqreturn_t sii902x_interrupt(int irq, void *data)
{
	struct sii902x *sii902x = data;
	unsigned int status = 0;

	regmap_read(sii902x->regmap, SII902X_INT_STATUS, &status);
	regmap_write(sii902x->regmap, SII902X_INT_STATUS, status);

	if ((status & SII902X_HOTPLUG_EVENT) && sii902x->bridge.dev)
		drm_helper_hpd_irq_event(sii902x->bridge.dev);

	return IRQ_HANDLED;
}

/*
 * The purpose of sii902x_i2c_bypass_select is to enable the pass through
 * mode of the HDMI transmitter. Do not use regmap from within this function,
 * only use sii902x_*_unlocked functions to read/modify/write registers.
 * We are holding the parent adapter lock here, keep this in mind before
 * adding more i2c transactions.
 */
static int sii902x_i2c_bypass_select(struct i2c_mux_core *mux, u32 chan_id)
{
	struct sii902x *sii902x = i2c_mux_priv(mux);
	struct device *dev = &sii902x->i2c->dev;
	unsigned long timeout;
	u8 status;
	int ret;

	ret = sii902x_update_bits_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
					   SII902X_SYS_CTRL_DDC_BUS_REQ,
					   SII902X_SYS_CTRL_DDC_BUS_REQ);

	timeout = jiffies +
		  msecs_to_jiffies(SII902X_I2C_BUS_ACQUISITION_TIMEOUT_MS);
	do {
		ret = sii902x_read_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
					    &status);
		if (ret)
			return ret;
	} while (!(status & SII902X_SYS_CTRL_DDC_BUS_GRTD) &&
		 time_before(jiffies, timeout));

	if (!(status & SII902X_SYS_CTRL_DDC_BUS_GRTD)) {
		dev_err(dev, "Failed to acquire the i2c bus\n");
		return -ETIMEDOUT;
	}

	ret = sii902x_write_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
				     status);
	if (ret)
		return ret;
	return 0;
}

/*
 * The purpose of sii902x_i2c_bypass_deselect is to disable the pass through
 * mode of the HDMI transmitter. Do not use regmap from within this function,
 * only use sii902x_*_unlocked functions to read/modify/write registers.
 * We are holding the parent adapter lock here, keep this in mind before
 * adding more i2c transactions.
 */
static int sii902x_i2c_bypass_deselect(struct i2c_mux_core *mux, u32 chan_id)
{
	struct sii902x *sii902x = i2c_mux_priv(mux);
	struct device *dev = &sii902x->i2c->dev;
	unsigned long timeout;
	unsigned int retries;
	u8 status;
	int ret;

	/*
	 * When the HDMI transmitter is in pass through mode, we need an
	 * (undocumented) additional delay between STOP and START conditions
	 * to guarantee the bus won't get stuck.
	 */
	udelay(30);

	/*
	 * Sometimes the I2C bus can stall after failure to use the
	 * EDID channel. Retry a few times to see if things clear
	 * up, else continue anyway.
	 */
	retries = 5;
	do {
		ret = sii902x_read_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
					    &status);
		retries--;
	} while (ret && retries);
	if (ret) {
		dev_err(dev, "failed to read status (%d)\n", ret);
		return ret;
	}

	ret = sii902x_update_bits_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
					   SII902X_SYS_CTRL_DDC_BUS_REQ |
					   SII902X_SYS_CTRL_DDC_BUS_GRTD, 0);
	if (ret)
		return ret;

	timeout = jiffies +
		  msecs_to_jiffies(SII902X_I2C_BUS_ACQUISITION_TIMEOUT_MS);
	do {
		ret = sii902x_read_unlocked(sii902x->i2c, SII902X_SYS_CTRL_DATA,
					    &status);
		if (ret)
			return ret;
	} while (status & (SII902X_SYS_CTRL_DDC_BUS_REQ |
			   SII902X_SYS_CTRL_DDC_BUS_GRTD) &&
		 time_before(jiffies, timeout));

	if (status & (SII902X_SYS_CTRL_DDC_BUS_REQ |
		      SII902X_SYS_CTRL_DDC_BUS_GRTD)) {
		dev_err(dev, "failed to release the i2c bus\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int sii902x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	unsigned int status = 0;
	struct sii902x *sii902x;
	unsigned char data[2] = { SII902X_CEC_SETUP, 0};
	struct i2c_msg msg = {
		.addr	= SII902X_CEC_I2C_ADDR << 1,
		.flags	= 0,
		.len	= 2,
		.buf	= data,
	};
	u8 chipid[4];
	int ret;

	ret = i2c_check_functionality(client->adapter, I2C_FUNC_I2C);
	if (!ret) {
		dev_err(dev, "I2C adapter not suitable\n");
		return -EIO;
	}

	sii902x = devm_kzalloc(dev, sizeof(*sii902x), GFP_KERNEL);
	if (!sii902x)
		return -ENOMEM;

	sii902x->i2c = client;
	sii902x->regmap = devm_regmap_init_i2c(client, &sii902x_regmap_config);
	if (IS_ERR(sii902x->regmap))
		return PTR_ERR(sii902x->regmap);

	sii902x->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(sii902x->reset_gpio)) {
		dev_err(dev, "Failed to retrieve/request reset gpio: %ld\n",
			PTR_ERR(sii902x->reset_gpio));
		return PTR_ERR(sii902x->reset_gpio);
	}

	sii902x->supplies[0].supply = "iovcc";
	sii902x->supplies[1].supply = "cvcc12";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(sii902x->supplies),
				      sii902x->supplies);
	if (ret) {
		if(ret != -EPROBE_DEFER)
			dev_err(dev, "regulator_bulk_get failed\n");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(sii902x->supplies),
				    sii902x->supplies);
	if (ret) {
		dev_err(dev, "regulator_bulk_enable failed\n");
		return ret;
	}

	sii902x_reset(sii902x);

	ret = regmap_write(sii902x->regmap, SII902X_REG_TPI_RQB, 0x0);
	if (ret)
		goto err_disable_regulator;

	ret = regmap_bulk_read(sii902x->regmap, SII902X_REG_CHIPID(0),
			       &chipid, 4);
	if (ret) {
		dev_err(dev, "regmap_read failed %d\n", ret);
		goto err_disable_regulator;
	}

	if (chipid[0] != 0xb0) {
		dev_err(dev, "Invalid chipid: %02x (expecting 0xb0)\n",
			chipid[0]);
		ret = -EINVAL;
		goto err_disable_regulator;
	}

	/*
	 * By default, CEC must be disabled to allow other CEC devives
	 * to bypass the bridge.
	 */
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		dev_warn(&client->dev, "Failed to disable CEC device!\n");

	/* Clear all pending interrupts */
	regmap_read(sii902x->regmap, SII902X_INT_STATUS, &status);
	regmap_write(sii902x->regmap, SII902X_INT_STATUS, status);

	if (client->irq > 0) {
		regmap_update_bits(sii902x->regmap, SII902X_INT_ENABLE,
				   SII902X_HOTPLUG_EVENT,
				   SII902X_HOTPLUG_EVENT);

		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						sii902x_interrupt,
						IRQF_ONESHOT, dev_name(dev),
						sii902x);
		if (ret)
			goto err_disable_regulator;
	}

	sii902x->bridge.funcs = &sii902x_bridge_funcs;
	sii902x->bridge.of_node = dev->of_node;
	drm_bridge_add(&sii902x->bridge);

	i2c_set_clientdata(client, sii902x);

	sii902x->i2cmux = i2c_mux_alloc(client->adapter, dev,
					1, 0, I2C_MUX_GATE,
					sii902x_i2c_bypass_select,
					sii902x_i2c_bypass_deselect);
	if (!sii902x->i2cmux) {
		dev_err(dev, "failed to allocate I2C mux\n");
		return -ENOMEM;
	}

	sii902x->i2cmux->priv = sii902x;
	ret = i2c_mux_add_adapter(sii902x->i2cmux, 0, 0, 0);
	if (ret) {
		dev_err(dev, "Couldn't add i2c mux adapter\n");
		return ret;
	}

	sii902x_register_audio_driver(dev, sii902x);

	return 0;

err_disable_regulator:
	regulator_bulk_disable(ARRAY_SIZE(sii902x->supplies),
			       sii902x->supplies);

	return ret;
}

static int sii902x_remove(struct i2c_client *client)

{
	struct sii902x *sii902x = i2c_get_clientdata(client);

	if (sii902x->i2cmux)
		i2c_mux_del_adapters(sii902x->i2cmux);

	drm_bridge_remove(&sii902x->bridge);

	regulator_bulk_disable(ARRAY_SIZE(sii902x->supplies),
			       sii902x->supplies);

	return 0;
}

static int sii902x_pm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sii902x *sii902x = i2c_get_clientdata(client);

	DRM_DEBUG_DRIVER("\n");

	if (sii902x->reset_gpio)
		gpiod_set_value(sii902x->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(sii902x->supplies),
			       sii902x->supplies);

	return 0;
}

static int sii902x_pm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sii902x *sii902x = i2c_get_clientdata(client);
	unsigned char data[2] = { SII902X_CEC_SETUP, 0};
	struct i2c_msg msg = {
		.addr	= SII902X_CEC_I2C_ADDR << 1,
		.flags	= 0,
		.len	= 2,
		.buf	= data,
	};
	int ret;

	DRM_DEBUG_DRIVER("\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(sii902x->supplies),
				    sii902x->supplies);
	if (ret) {
		DRM_ERROR("regulator_bulk_enable failed\n");
		return ret;
	}

	if (sii902x->reset_gpio)
		gpiod_set_value(sii902x->reset_gpio, 0);

	regmap_write(sii902x->regmap, SII902X_REG_TPI_RQB, 0x00);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		DRM_ERROR("Failed to disable CEC device!\n");

	if (client->irq > 0)
		regmap_update_bits(sii902x->regmap, SII902X_INT_ENABLE,
				   SII902X_HOTPLUG_EVENT,
				   SII902X_HOTPLUG_EVENT);

	return 0;
}

static const struct dev_pm_ops sii902x_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sii902x_pm_suspend, sii902x_pm_resume)
};

static const struct of_device_id sii902x_dt_ids[] = {
	{ .compatible = "sil,sii9022", },
	{ }
};
MODULE_DEVICE_TABLE(of, sii902x_dt_ids);

static const struct i2c_device_id sii902x_i2c_ids[] = {
	{ "sii9022", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sii902x_i2c_ids);

static struct i2c_driver sii902x_driver = {
	.probe = sii902x_probe,
	.remove = sii902x_remove,
	.driver = {
		.name = "sii902x",
		.of_match_table = sii902x_dt_ids,
		.pm = &sii902x_pm_ops,
	},
	.id_table = sii902x_i2c_ids,
};
module_i2c_driver(sii902x_driver);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com>");
MODULE_DESCRIPTION("SII902x RGB -> HDMI bridges");
MODULE_LICENSE("GPL");
