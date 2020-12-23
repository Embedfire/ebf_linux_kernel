/*
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 *
 * This code is based on drivers/video/fbdev/mxsfb.c :
 * Copyright (C) 2010 Juergen Beisert, Pengutronix
 * Copyright (C) 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <linux/busfreq-imx.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/of_graph.h>
#include <linux/platform_data/simplefb.h>
#include <video/videomode.h>

#include "mxsfb_drv.h"
#include "mxsfb_regs.h"

#define MXS_SET_ADDR		0x4
#define MXS_CLR_ADDR		0x8
#define MODULE_CLKGATE		BIT(30)
#define MODULE_SFTRST		BIT(31)
/* 1 second delay should be plenty of time for block reset */
#define RESET_TIMEOUT		1000000

static u32 set_hsync_pulse_width(struct mxsfb_drm_private *mxsfb, u32 val)
{
	return (val & mxsfb->devdata->hs_wdth_mask) <<
		mxsfb->devdata->hs_wdth_shift;
}

/*  Print Four-character-code (FOURCC) */
static char *fourcc_to_str(u32 fmt)
{
	/* Use 10 chars so we can simultaneously print two codes */
	static char code[10], *c = &code[0];

	if (c == &code[10])
		c = &code[0];

	*(c++) = (unsigned char)(fmt & 0xff);
	*(c++) = (unsigned char)((fmt >> 8) & 0xff);
	*(c++) = (unsigned char)((fmt >> 16) & 0xff);
	*(c++) = (unsigned char)((fmt >> 24) & 0xff);
	*(c++) = '\0';

	return (c - 5);
}

/* Setup the MXSFB registers for decoding the pixels out of the framebuffer */
static int mxsfb_set_pixel_fmt(struct mxsfb_drm_private *mxsfb, bool update)
{
	struct drm_crtc *crtc = &mxsfb->pipe.crtc;
	struct drm_device *drm = crtc->dev;
	const u32 format = crtc->primary->state->fb->format->format;
	u32 ctrl = 0, ctrl1 = 0;
	bool bgr_format = true;

	if (!update)
		ctrl = CTRL_BYPASS_COUNT | CTRL_MASTER;

	/*
	 * WARNING: The bus width, CTRL_SET_BUS_WIDTH(), is configured to
	 * match the selected mode here. This differs from the original
	 * MXSFB driver, which had the option to configure the bus width
	 * to arbitrary value. This limitation should not pose an issue.
	 */

	if (!update) {
		/* CTRL1 contains IRQ config and status bits, preserve those. */
		ctrl1 = readl(mxsfb->base + LCDC_CTRL1);
		ctrl1 &= CTRL1_CUR_FRAME_DONE_IRQ_EN | CTRL1_CUR_FRAME_DONE_IRQ;
	}

	DRM_DEV_DEBUG_DRIVER(drm->dev, "Setting up %s mode\n",
		fourcc_to_str(format));

	/* Do some clean-up that we might have from a previous mode */
	ctrl &= ~CTRL_SHIFT_DIR(1);
	ctrl &= ~CTRL_SHIFT_NUM(0x3f);
	if (mxsfb->devdata->ipversion >= 4)
		writel(CTRL2_ODD_LINE_PATTERN(0x7) |
		       CTRL2_EVEN_LINE_PATTERN(0x7),
		       mxsfb->base + LCDC_V4_CTRL2 + REG_CLR);

	switch (format) {
	case DRM_FORMAT_RGB565:
		ctrl |= CTRL_SET_WORD_LENGTH(0);
		ctrl1 |= CTRL1_SET_BYTE_PACKAGING(0xf);
		break;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
		/* RGBX - > 0RGB */
		ctrl |= CTRL_SHIFT_DIR(1);
		ctrl |= CTRL_SHIFT_NUM(8);
		bgr_format = false;
		/* Fall through */
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		if (bgr_format) {
			if (mxsfb->devdata->ipversion < 4)
				goto err;
			writel(CTRL2_ODD_LINE_PATTERN(0x5) |
			       CTRL2_EVEN_LINE_PATTERN(0x5),
			       mxsfb->base + LCDC_V4_CTRL2 + REG_SET);
		}
		/* Fall through */
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		ctrl |= CTRL_SET_WORD_LENGTH(3);
		/* Do not use packed pixels = one pixel per word instead. */
		ctrl1 |= CTRL1_SET_BYTE_PACKAGING(0x7);
		break;
	default:
		goto err;
	}

	if (update) {
		writel(ctrl, mxsfb->base + LCDC_CTRL + REG_SET);
		writel(ctrl1, mxsfb->base + LCDC_CTRL1 + REG_SET);
	} else {
		writel(ctrl, mxsfb->base + LCDC_CTRL);
		writel(ctrl1, mxsfb->base + LCDC_CTRL1);
	}

	return 0;

err:
	DRM_DEV_ERROR(drm->dev, "Unhandled pixel format: %s\n",
		      fourcc_to_str(format));
	return -EINVAL;
}

static void mxsfb_set_bus_fmt(struct mxsfb_drm_private *mxsfb)
{
	struct drm_crtc *crtc = &mxsfb->pipe.crtc;
	struct drm_device *drm = crtc->dev;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	u32 reg = 0;

	if (mxsfb->connector->display_info.num_bus_formats)
		bus_format = mxsfb->connector->display_info.bus_formats[0];

	DRM_DEV_DEBUG_DRIVER(mxsfb->dev,
		"Using bus_format: 0x%08X\n", bus_format);

	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		reg = CTRL_SET_BUS_WIDTH(STMLCDIF_16BIT);
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
		reg = CTRL_SET_BUS_WIDTH(STMLCDIF_18BIT);
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
		reg = CTRL_SET_BUS_WIDTH(STMLCDIF_24BIT);
		break;
	default:
		dev_err(drm->dev, "Unknown media bus format %d\n", bus_format);
		break;
	}
	writel(reg, mxsfb->base + LCDC_CTRL + REG_SET);
}

static void mxsfb_enable_controller(struct mxsfb_drm_private *mxsfb)
{
	u32 reg;

	if (mxsfb->clk_disp_axi)
		clk_prepare_enable(mxsfb->clk_disp_axi);
	clk_prepare_enable(mxsfb->clk);
	mxsfb_enable_axi_clk(mxsfb);

	if (mxsfb->devdata->ipversion >= 4)
		writel(CTRL2_OUTSTANDING_REQS(REQ_16),
			mxsfb->base + LCDC_V4_CTRL2 + REG_SET);

	/* If it was disabled, re-enable the mode again */
	writel(CTRL_DOTCLK_MODE, mxsfb->base + LCDC_CTRL + REG_SET);

	/* Enable the SYNC signals first, then the DMA engine */
	reg = readl(mxsfb->base + LCDC_VDCTRL4);
	reg |= VDCTRL4_SYNC_SIGNALS_ON;
	writel(reg, mxsfb->base + LCDC_VDCTRL4);

	writel(CTRL_MASTER, mxsfb->base + LCDC_CTRL + REG_SET);
	writel(CTRL_RUN, mxsfb->base + LCDC_CTRL + REG_SET);

	writel(CTRL1_RECOVERY_ON_UNDERFLOW, mxsfb->base + LCDC_CTRL1 + REG_SET);
}

static void mxsfb_disable_controller(struct mxsfb_drm_private *mxsfb)
{
	u32 reg;

	if (mxsfb->devdata->ipversion >= 4)
		writel(CTRL2_OUTSTANDING_REQS(0x7),
			mxsfb->base + LCDC_V4_CTRL2 + REG_CLR);

	writel(CTRL_RUN, mxsfb->base + LCDC_CTRL + REG_CLR);

	/*
	 * Even if we disable the controller here, it will still continue
	 * until its FIFOs are running out of data
	 */
	writel(CTRL_DOTCLK_MODE, mxsfb->base + LCDC_CTRL + REG_CLR);

	readl_poll_timeout(mxsfb->base + LCDC_CTRL, reg, !(reg & CTRL_RUN),
			   0, 1000);

	writel(CTRL_MASTER, mxsfb->base + LCDC_CTRL + REG_CLR);

	reg = readl(mxsfb->base + LCDC_VDCTRL4);
	reg &= ~VDCTRL4_SYNC_SIGNALS_ON;
	writel(reg, mxsfb->base + LCDC_VDCTRL4);

	mxsfb_disable_axi_clk(mxsfb);

	if (mxsfb->clk_disp_axi)
		clk_disable_unprepare(mxsfb->clk_disp_axi);
	clk_disable_unprepare(mxsfb->clk);
}

/*
 * Clear the bit and poll it cleared.  This is usually called with
 * a reset address and mask being either SFTRST(bit 31) or CLKGATE
 * (bit 30).
 */
static int clear_poll_bit(void __iomem *addr, u32 mask)
{
	u32 reg;

	writel(mask, addr + MXS_CLR_ADDR);
	return readl_poll_timeout(addr, reg, !(reg & mask), 0, RESET_TIMEOUT);
}

static int mxsfb_reset_block(void __iomem *reset_addr)
{
	int ret;

	ret = clear_poll_bit(reset_addr, MODULE_SFTRST);
	if (ret)
		return ret;

	writel(MODULE_CLKGATE, reset_addr + MXS_CLR_ADDR);

	ret = clear_poll_bit(reset_addr, MODULE_SFTRST);
	if (ret)
		return ret;

	return clear_poll_bit(reset_addr, MODULE_CLKGATE);
}

static void mxsfb_crtc_mode_set_nofb(struct mxsfb_drm_private *mxsfb)
{
	struct drm_display_mode *m = &mxsfb->pipe.crtc.state->adjusted_mode;
	const u32 bus_flags = mxsfb->connector->display_info.bus_flags;
	u32 vdctrl0, vsync_pulse_len, hsync_pulse_len;
	int err;

	/*
	 * It seems, you can't re-program the controller if it is still
	 * running. This may lead to shifted pictures (FIFO issue?), so
	 * first stop the controller and drain its FIFOs.
	 */
	mxsfb_enable_axi_clk(mxsfb);

	/* Mandatory eLCDIF reset as per the Reference Manual */
	err = mxsfb_reset_block(mxsfb->base);
	if (err)
		return;

	/* Clear the FIFOs */
	writel(CTRL1_FIFO_CLEAR, mxsfb->base + LCDC_CTRL1 + REG_SET);

	err = mxsfb_set_pixel_fmt(mxsfb, false);
	if (err)
		return;

	clk_set_rate(mxsfb->clk, m->crtc_clock * 1000);

	DRM_DEV_DEBUG_DRIVER(mxsfb->dev,
		"Connector bus_flags: 0x%08X\n", bus_flags);
	DRM_DEV_DEBUG_DRIVER(mxsfb->dev,
		"Mode flags: 0x%08X\n", m->flags);

	writel(TRANSFER_COUNT_SET_VCOUNT(m->crtc_vdisplay) |
	       TRANSFER_COUNT_SET_HCOUNT(m->crtc_hdisplay),
	       mxsfb->base + mxsfb->devdata->transfer_count);

	vsync_pulse_len = m->crtc_vsync_end - m->crtc_vsync_start;

	vdctrl0 = VDCTRL0_ENABLE_PRESENT |	/* Always in DOTCLOCK mode */
		  VDCTRL0_VSYNC_PERIOD_UNIT |
		  VDCTRL0_VSYNC_PULSE_WIDTH_UNIT |
		  VDCTRL0_SET_VSYNC_PULSE_WIDTH(vsync_pulse_len);
	if (m->flags & DRM_MODE_FLAG_PHSYNC)
		vdctrl0 |= VDCTRL0_HSYNC_ACT_HIGH;
	if (m->flags & DRM_MODE_FLAG_PVSYNC)
		vdctrl0 |= VDCTRL0_VSYNC_ACT_HIGH;
	/* Make sure Data Enable is high active by default */
	if (!(bus_flags & DRM_BUS_FLAG_DE_LOW))
		vdctrl0 |= VDCTRL0_ENABLE_ACT_HIGH;
	/*
	 * DRM_BUS_FLAG_PIXDATA_ defines are controller centric,
	 * controllers VDCTRL0_DOTCLK is display centric.
	 * Drive on positive edge       -> display samples on falling edge
	 * DRM_BUS_FLAG_PIXDATA_POSEDGE -> VDCTRL0_DOTCLK_ACT_FALLING
	 */
	if (bus_flags & DRM_BUS_FLAG_PIXDATA_POSEDGE)
		vdctrl0 |= VDCTRL0_DOTCLK_ACT_FALLING;

	writel(vdctrl0, mxsfb->base + LCDC_VDCTRL0);

	mxsfb_set_bus_fmt(mxsfb);

	/* Frame length in lines. */
	writel(m->crtc_vtotal, mxsfb->base + LCDC_VDCTRL1);

	/* Line length in units of clocks or pixels. */
	hsync_pulse_len = m->crtc_hsync_end - m->crtc_hsync_start;
	writel(set_hsync_pulse_width(mxsfb, hsync_pulse_len) |
	       VDCTRL2_SET_HSYNC_PERIOD(m->crtc_htotal),
	       mxsfb->base + LCDC_VDCTRL2);

	writel(SET_HOR_WAIT_CNT(m->crtc_htotal - m->crtc_hsync_start) |
	       SET_VERT_WAIT_CNT(m->crtc_vtotal - m->crtc_vsync_start),
	       mxsfb->base + LCDC_VDCTRL3);

	writel(SET_DOTCLK_H_VALID_DATA_CNT(m->hdisplay),
	       mxsfb->base + LCDC_VDCTRL4);

	if (mxsfb->gem != NULL) {
		writel(mxsfb->gem->paddr,
		       mxsfb->base + mxsfb->devdata->next_buf);
		mxsfb->gem = NULL;
	}

	mxsfb_disable_axi_clk(mxsfb);
}

void mxsfb_crtc_enable(struct mxsfb_drm_private *mxsfb)
{
	if (mxsfb->enabled)
		return;

	if (mxsfb->devdata->flags & MXSFB_FLAG_BUSFREQ)
		request_bus_freq(BUS_FREQ_HIGH);

	writel(0, mxsfb->base + LCDC_CTRL);
	mxsfb_crtc_mode_set_nofb(mxsfb);
	mxsfb_enable_controller(mxsfb);

	mxsfb->enabled = true;
}

void mxsfb_crtc_disable(struct mxsfb_drm_private *mxsfb)
{
	if (!mxsfb->enabled)
		return;

	mxsfb_disable_controller(mxsfb);

	if (mxsfb->devdata->flags & MXSFB_FLAG_BUSFREQ)
		release_bus_freq(BUS_FREQ_HIGH);

	mxsfb->enabled = false;
}

void mxsfb_plane_atomic_update(struct mxsfb_drm_private *mxsfb,
			       struct drm_plane_state *old_state)
{
	struct drm_simple_display_pipe *pipe = &mxsfb->pipe;
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *drm = crtc->dev;
	struct drm_framebuffer *fb = pipe->plane.state->fb;
	struct drm_framebuffer *old_fb = old_state->fb;
	struct drm_pending_vblank_event *event;
	struct drm_gem_cma_object *gem;

	if (!crtc)
		return;

	spin_lock_irq(&crtc->dev->event_lock);
	event = crtc->state->event;
	if (event) {
		crtc->state->event = NULL;

		if (drm_crtc_vblank_get(crtc) == 0) {
			drm_crtc_arm_vblank_event(crtc, event);
		} else {
			drm_crtc_send_vblank_event(crtc, event);
		}
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	if (!fb)
		return;

	gem = drm_fb_cma_get_gem_obj(fb, 0);

	if (!mxsfb->enabled) {
		mxsfb->gem = gem;
		return;
	}

	/*
	 * TODO: Currently, we only support pixel format change, but we need
	 * also to care about size changes too
	 */
	if (old_fb->format->format != fb->format->format) {
		DRM_DEV_DEBUG_DRIVER(drm->dev,
				     "Switching pixel format: %s -> %s\n",
				      fourcc_to_str(old_fb->format->format),
				      fourcc_to_str(fb->format->format));
		mxsfb_set_pixel_fmt(mxsfb, true);
	}

	mxsfb_enable_axi_clk(mxsfb);
	writel(gem->paddr, mxsfb->base + mxsfb->devdata->next_buf);
	mxsfb_disable_axi_clk(mxsfb);
}
