/*
 * Copyright 2017 NXP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "imx-hdp.h"
#include "imx-hdmi.h"
#include "imx-dp.h"
#include "../imx-drm.h"

struct drm_display_mode *g_mode;

static const struct drm_display_mode edid_cea_modes = {
	/* 16 - 1920x1080@60Hz */
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,
};

static inline struct imx_hdp *enc_to_imx_hdp(struct drm_encoder *e)
{
	return container_of(e, struct imx_hdp, encoder);
}

static void imx_hdp_plmux_config(struct imx_hdp *hdp, struct drm_display_mode *mode)
{
	u32 val;

	val = 4; /* RGB */
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		val |= 1 << PL_MUX_CTL_VCP_OFFSET;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		val |= 1 << PL_MUX_CTL_HCP_OFFSET;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		val |= 0x2;

	writel(val, hdp->ss_base + CSR_PIXEL_LINK_MUX_CTL);
}

static void imx_hdp_state_init(struct imx_hdp *hdp)
{
	state_struct *state = &hdp->state;

	memset(state, 0, sizeof(state_struct));
	mutex_init(&state->mutex);

	state->mem.regs_base = hdp->regs_base;
	state->mem.ss_base = hdp->ss_base;
	state->rw = hdp->rw;
}

void hdp_phy_reset(u8 reset)
{
	sc_err_t sciErr;
	sc_ipc_t ipcHndl = 0;
	u32 mu_id;

	sciErr = sc_ipc_getMuID(&mu_id);
	if (sciErr != SC_ERR_NONE) {
		pr_err("Cannot obtain MU ID\n");
		return;
	}

	sciErr = sc_ipc_open(&ipcHndl, mu_id);
	if (sciErr != SC_ERR_NONE) {
		pr_err("sc_ipc_open failed! (sciError = %d)\n", sciErr);
		return;
	}

	/* set the pixel link mode and pixel type */
	sc_misc_set_control(ipcHndl, SC_R_HDMI, SC_C_PHY_RESET, reset);
	if (sciErr != SC_ERR_NONE)
		pr_err("SC_R_HDMI PHY reset failed %d!\n", sciErr);

	sc_ipc_close(mu_id);
}

static void clk_set_root(struct imx_hdp *hdp)
{
	sc_ipc_t ipcHndl = hdp->ipcHndl;

	/* set clock to bypass mode, source from av pll */
	/* those clock default source from dig pll */
	/* HDMI DI Pixel Link Mux Clock  */
	sc_pm_set_clock_parent(ipcHndl, SC_R_HDMI, SC_PM_CLK_MISC0, 4);
	/* HDMI DI Pixel Link Clock  */
	sc_pm_set_clock_parent(ipcHndl, SC_R_HDMI, SC_PM_CLK_MISC1, 4);
	/* HDMI DI Pixel Clock  */
	sc_pm_set_clock_parent(ipcHndl, SC_R_HDMI, SC_PM_CLK_MISC3, 4);
}

static void hdp_ipg_clock_set_rate(struct imx_hdp *hdp)
{
	u32 clk_rate;

	if (hdp->is_hdmi == true) {
		/* HDMI */
		clk_set_root(hdp);
		clk_set_rate(hdp->clks.dig_pll, PLL_675MHZ);
		clk_set_rate(hdp->clks.clk_core, PLL_675MHZ/5);
		clk_set_rate(hdp->clks.clk_ipg, PLL_675MHZ/8);
		/* Default pixel clock for HDMI */
		clk_set_rate(hdp->clks.av_pll, 148500000);
	} else {
		/* DP */
		clk_set_rate(hdp->clks.av_pll, 24000000);
		clk_rate = clk_get_rate(hdp->clks.dig_pll);
		printk("dig_pll= %d\n", clk_rate);
		if (clk_rate == PLL_1188MHZ) {
			clk_set_rate(hdp->clks.dig_pll, PLL_1188MHZ);
			clk_set_rate(hdp->clks.clk_core, PLL_1188MHZ/10);
			clk_set_rate(hdp->clks.clk_ipg, PLL_1188MHZ/14);
		} else {
			clk_set_rate(hdp->clks.dig_pll, PLL_675MHZ);
			clk_set_rate(hdp->clks.clk_core, PLL_675MHZ/5);
			clk_set_rate(hdp->clks.clk_ipg, PLL_675MHZ/8);
		}
	}
}

static void dp_pixel_clock_set_rate(struct imx_hdp *hdp)
{
	unsigned int pclock = hdp->video.cur_mode.clock * 1000;
	u32 ret;

	/* 24MHz for DP and pixel clock for HDMI */
	if (hdp->dual_mode == true) {
		clk_set_rate(hdp->clks.clk_pxl, pclock/2);
		clk_set_rate(hdp->clks.clk_pxl_link, pclock/2);
	} else {
		ret = clk_set_rate(hdp->clks.clk_pxl, pclock);
		if (ret < 0)
			printk("clk_pxl set failed T %u,A %lu", pclock, clk_get_rate(hdp->clks.clk_pxl));
		clk_set_rate(hdp->clks.clk_pxl_link, pclock);
	}
	clk_set_rate(hdp->clks.clk_pxl_mux, pclock);
}

static int dp_clock_init(struct imx_hdp *hdp)
{
	struct device *dev = hdp->dev;

	hdp->clks.av_pll = devm_clk_get(dev, "av_pll");
	if (IS_ERR(hdp->clks.av_pll)) {
		dev_err(dev, "failed to get av pll clk\n");
		return PTR_ERR(hdp->clks.av_pll);
	}

	hdp->clks.dig_pll = devm_clk_get(dev, "dig_pll");
	if (IS_ERR(hdp->clks.dig_pll)) {
		dev_err(dev, "failed to get dig pll clk\n");
		return PTR_ERR(hdp->clks.dig_pll);
	}

	hdp->clks.clk_ipg = devm_clk_get(dev, "clk_ipg");
	if (IS_ERR(hdp->clks.clk_ipg)) {
		dev_err(dev, "failed to get dp ipg clk\n");
		return PTR_ERR(hdp->clks.clk_ipg);
	}

	hdp->clks.clk_core = devm_clk_get(dev, "clk_core");
	if (IS_ERR(hdp->clks.clk_core)) {
		dev_err(dev, "failed to get hdp core clk\n");
		return PTR_ERR(hdp->clks.clk_core);
	}

	hdp->clks.clk_pxl = devm_clk_get(dev, "clk_pxl");
	if (IS_ERR(hdp->clks.clk_pxl)) {
		dev_err(dev, "failed to get pxl clk\n");
		return PTR_ERR(hdp->clks.clk_pxl);
	}

	hdp->clks.clk_pxl_mux = devm_clk_get(dev, "clk_pxl_mux");
	if (IS_ERR(hdp->clks.clk_pxl_mux)) {
		dev_err(dev, "failed to get pxl mux clk\n");
		return PTR_ERR(hdp->clks.clk_pxl_mux);
	}

	hdp->clks.clk_pxl_link = devm_clk_get(dev, "clk_pxl_link");
	if (IS_ERR(hdp->clks.clk_pxl_mux)) {
		dev_err(dev, "failed to get pxl link clk\n");
		return PTR_ERR(hdp->clks.clk_pxl_link);
	}

	hdp->clks.clk_hdp = devm_clk_get(dev, "clk_hdp");
	if (IS_ERR(hdp->clks.clk_hdp)) {
		dev_err(dev, "failed to get hdp clk\n");
		return PTR_ERR(hdp->clks.clk_hdp);
	}

	hdp->clks.clk_phy = devm_clk_get(dev, "clk_phy");
	if (IS_ERR(hdp->clks.clk_phy)) {
		dev_err(dev, "failed to get phy clk\n");
		return PTR_ERR(hdp->clks.clk_phy);
	}
	hdp->clks.clk_apb = devm_clk_get(dev, "clk_apb");
	if (IS_ERR(hdp->clks.clk_apb)) {
		dev_err(dev, "failed to get apb clk\n");
		return PTR_ERR(hdp->clks.clk_apb);
	}
	hdp->clks.clk_lis = devm_clk_get(dev, "clk_lis");
	if (IS_ERR(hdp->clks.clk_lis)) {
		dev_err(dev, "failed to get lis clk\n");
		return PTR_ERR(hdp->clks.clk_lis);
	}
	hdp->clks.clk_msi = devm_clk_get(dev, "clk_msi");
	if (IS_ERR(hdp->clks.clk_msi)) {
		dev_err(dev, "failed to get msi clk\n");
		return PTR_ERR(hdp->clks.clk_msi);
	}
	hdp->clks.clk_lpcg = devm_clk_get(dev, "clk_lpcg");
	if (IS_ERR(hdp->clks.clk_lpcg)) {
		dev_err(dev, "failed to get lpcg clk\n");
		return PTR_ERR(hdp->clks.clk_lpcg);
	}
	hdp->clks.clk_even = devm_clk_get(dev, "clk_even");
	if (IS_ERR(hdp->clks.clk_even)) {
		dev_err(dev, "failed to get even clk\n");
		return PTR_ERR(hdp->clks.clk_even);
	}
	hdp->clks.clk_dbl = devm_clk_get(dev, "clk_dbl");
	if (IS_ERR(hdp->clks.clk_dbl)) {
		dev_err(dev, "failed to get dbl clk\n");
		return PTR_ERR(hdp->clks.clk_dbl);
	}
	hdp->clks.clk_vif = devm_clk_get(dev, "clk_vif");
	if (IS_ERR(hdp->clks.clk_vif)) {
		dev_err(dev, "failed to get vif clk\n");
		return PTR_ERR(hdp->clks.clk_vif);
	}
	hdp->clks.clk_apb_csr = devm_clk_get(dev, "clk_apb_csr");
	if (IS_ERR(hdp->clks.clk_apb_csr)) {
		dev_err(dev, "failed to get apb csr clk\n");
		return PTR_ERR(hdp->clks.clk_apb_csr);
	}
	hdp->clks.clk_apb_ctrl = devm_clk_get(dev, "clk_apb_ctrl");
	if (IS_ERR(hdp->clks.clk_apb_ctrl)) {
		dev_err(dev, "failed to get apb ctrl clk\n");
		return PTR_ERR(hdp->clks.clk_apb_ctrl);
	}

	return true;
}

static int dp_pixel_clock_enable(struct imx_hdp *hdp)
{
	struct device *dev = hdp->dev;
	int ret;

	ret = clk_prepare_enable(hdp->clks.av_pll);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk pxl error\n", __func__);
		return ret;
	}

	ret = clk_prepare_enable(hdp->clks.clk_pxl);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk pxl error\n", __func__);
		return ret;
	}
	ret = clk_prepare_enable(hdp->clks.clk_pxl_mux);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk pxl mux error\n", __func__);
		return ret;
	}

	ret = clk_prepare_enable(hdp->clks.clk_pxl_link);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk pxl link error\n", __func__);
		return ret;
	}
	return ret;

}

static void dp_pixel_clock_disable(struct imx_hdp *hdp)
{
	clk_disable_unprepare(hdp->clks.clk_pxl);
	clk_disable_unprepare(hdp->clks.clk_pxl_link);
	clk_disable_unprepare(hdp->clks.clk_pxl_mux);
}

static int dp_ipg_clock_enable(struct imx_hdp *hdp)
{
	struct device *dev = hdp->dev;
	int ret;

	ret = clk_prepare_enable(hdp->clks.av_pll);
	if (ret < 0) {
		dev_err(dev, "%s, pre av pll error\n", __func__);
		return ret;
	}
	ret = clk_prepare_enable(hdp->clks.dig_pll);
	if (ret < 0) {
		dev_err(dev, "%s, pre dig pll error\n", __func__);
		return ret;
	}

	ret = clk_prepare_enable(hdp->clks.clk_ipg);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk_ipg error\n", __func__);
		return ret;
	}

	ret = clk_prepare_enable(hdp->clks.clk_core);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk core error\n", __func__);
		return ret;
	}

	ret = clk_prepare_enable(hdp->clks.clk_hdp);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk hdp error\n", __func__);
		return ret;
	}

	ret = clk_prepare_enable(hdp->clks.clk_phy);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk phy\n", __func__);
		return ret;
	}

	ret = clk_prepare_enable(hdp->clks.clk_apb);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk apb error\n", __func__);
		return ret;
	}
	ret = clk_prepare_enable(hdp->clks.clk_lis);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk lis error\n", __func__);
		return ret;
	}
	ret = clk_prepare_enable(hdp->clks.clk_lpcg);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk lpcg error\n", __func__);
		return ret;
	}
	ret = clk_prepare_enable(hdp->clks.clk_msi);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk msierror\n", __func__);
		return ret;
	}
	ret = clk_prepare_enable(hdp->clks.clk_even);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk even error\n", __func__);
		return ret;
	}
	ret = clk_prepare_enable(hdp->clks.clk_dbl);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk dbl error\n", __func__);
		return ret;
	}
	ret = clk_prepare_enable(hdp->clks.clk_vif);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk vif error\n", __func__);
		return ret;
	}
	ret = clk_prepare_enable(hdp->clks.clk_apb_csr);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk apb csr error\n", __func__);
		return ret;
	}
	ret = clk_prepare_enable(hdp->clks.clk_apb_ctrl);
	if (ret < 0) {
		dev_err(dev, "%s, pre clk apb ctrl error\n", __func__);
		return ret;
	}
	return ret;
}

static void dp_pixel_link_config(struct imx_hdp *hdp)
{
	sc_ipc_t ipcHndl = hdp->ipcHndl;

	/* config dpu1 di0 to hdmi/dp mode */
	sc_misc_set_control(ipcHndl, SC_R_DC_0, SC_C_PXL_LINK_MST1_ADDR, 1);
	sc_misc_set_control(ipcHndl, SC_R_DC_0, SC_C_PXL_LINK_MST1_VLD, 1);
	sc_misc_set_control(ipcHndl, SC_R_DC_0, SC_C_SYNC_CTRL0, 1);
}

static int imx_hdp_deinit(struct imx_hdp *hdp)
{
	u8 bresp;
	u32 ret;

	/* Stop link training */
	CDN_API_DPTX_TrainingControl_blocking(&hdp->state, 0);

	/* Disable HPD event and training */
	CDN_API_DPTX_EnableEvent_blocking(&hdp->state, 0, 0);

	/* turn off hdp controller IP activity 0-standby */
	ret = CDN_API_MainControl_blocking(&hdp->state, 0, &bresp);
	if (ret != CDN_OK)
		return -1;

	return ret;
}

static int imx_get_vic_index(struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < VIC_MODE_COUNT; i++) {
		if (mode->hdisplay == vic_table[i][H_ACTIVE] &&
			mode->vdisplay == vic_table[i][V_ACTIVE] &&
			mode->clock == vic_table[i][PIXEL_FREQ_KHZ])
			return i;
	}
	/* Default 1080p60 */
	printk("default vic 2\n");
	return 2;
}

static void imx_hdp_mode_setup(struct imx_hdp *hdp, struct drm_display_mode *mode)
{
	int dp_vic;

	dp_pixel_clock_set_rate(hdp);
	dp_pixel_clock_enable(hdp);

	imx_hdp_plmux_config(hdp, mode);

	dp_vic = imx_get_vic_index(mode);

	imx_hdp_call(hdp, mode_set, &hdp->state, dp_vic, 1, 8, hdp->link_rate);
}

static int imx_hdp_cable_plugin(struct imx_hdp *hdp)
{
	return 0;
}

static int imx_hdp_cable_plugout(struct imx_hdp *hdp)
{
	dp_pixel_clock_disable(hdp);
	return 0;
}


static void imx_hdp_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *orig_mode,
				    struct drm_display_mode *mode)
{
	struct imx_hdp *hdp = bridge->driver_private;

	printk("%s++\n", __func__);
	mutex_lock(&hdp->mutex);

	memcpy(&hdp->video.cur_mode, mode, sizeof(hdp->video.cur_mode));
	imx_hdp_mode_setup(hdp, mode);
	/* Store the display mode for plugin/DKMS poweron events */
	memcpy(&hdp->video.pre_mode, mode, sizeof(hdp->video.pre_mode));

	mutex_unlock(&hdp->mutex);
}

static void imx_hdp_bridge_disable(struct drm_bridge *bridge)
{
}

static void imx_hdp_bridge_enable(struct drm_bridge *bridge)
{
}

static enum drm_connector_status
imx_hdp_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static int imx_hdp_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	int num_modes = 0;
#ifdef edid_enable
	struct imx_hdp *hdp = container_of(connector, struct imx_hdp,
					     connector);
	struct edid *edid;

	edid = drm_do_get_edid(connector, hdp->ops->get_edid_block, &hdp->state);
	if (edid) {
		dev_dbg(hdp->dev, "got edid: width[%d] x height[%d]\n",
			edid->width_cm, edid->height_cm);

		printk("edid_head %x,%x,%x,%x,%x,%x,%x,%x\n",
				edid->header[0], edid->header[1], edid->header[2], edid->header[3],
				edid->header[4], edid->header[5], edid->header[6], edid->header[7]);
		drm_mode_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		/* Store the ELD */
		drm_edid_to_eld(connector, edid);
		kfree(edid);
	} else {
		dev_dbg(hdp->dev, "failed to get edid\n");
#endif
		mode = drm_mode_create(connector->dev);
		if (!mode)
			return -EINVAL;
		drm_mode_copy(mode, &edid_cea_modes);
		mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
		num_modes = 1;
#ifdef edid_enable
	}
#endif

	return num_modes;
}

static enum drm_mode_status
imx_hdp_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	enum drm_mode_status mode_status = MODE_OK;

	if (mode->clock > 150000)
		return MODE_CLOCK_HIGH;

	return mode_status;
}

static void imx_hdp_connector_force(struct drm_connector *connector)
{
	struct imx_hdp *hdp = container_of(connector, struct imx_hdp,
					     connector);

	mutex_lock(&hdp->mutex);
	hdp->force = connector->force;
	mutex_unlock(&hdp->mutex);
}

static const struct drm_connector_funcs imx_hdp_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = imx_hdp_connector_detect,
	.destroy = drm_connector_cleanup,
	.force = imx_hdp_connector_force,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs imx_hdp_connector_helper_funcs = {
	.get_modes = imx_hdp_connector_get_modes,
	.mode_valid = imx_hdp_connector_mode_valid,
};

static const struct drm_bridge_funcs imx_hdp_bridge_funcs = {
	.enable = imx_hdp_bridge_enable,
	.disable = imx_hdp_bridge_disable,
	.mode_set = imx_hdp_bridge_mode_set,
};


static void imx_hdp_imx_encoder_disable(struct drm_encoder *encoder)
{
}

static void imx_hdp_imx_encoder_enable(struct drm_encoder *encoder)
{
}

static int imx_hdp_imx_encoder_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(crtc_state);

	printk("%s++\n", __func__);
	imx_crtc_state->bus_format = MEDIA_BUS_FMT_RGB101010_1X30;
	return 0;
}

static const struct drm_encoder_helper_funcs imx_hdp_imx_encoder_helper_funcs = {
	.enable     = imx_hdp_imx_encoder_enable,
	.disable    = imx_hdp_imx_encoder_disable,
	.atomic_check = imx_hdp_imx_encoder_atomic_check,
};

static const struct drm_encoder_funcs imx_hdp_imx_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int mx8mq_hdp_read(struct hdp_mem *mem, unsigned int addr, unsigned int *value)
{
	unsigned int temp;
	void *tmp_addr = mem->regs_base + addr;
	temp = __raw_readl((volatile unsigned int *)tmp_addr);
	*value = temp;
	return 0;
}

static int mx8mq_hdp_write(struct hdp_mem *mem, unsigned int addr, unsigned int value)
{
	void *tmp_addr = mem->regs_base + addr;

	__raw_writel(value, (volatile unsigned int *)tmp_addr);
	return 0;
}

static int mx8mq_hdp_sread(struct hdp_mem *mem, unsigned int addr, unsigned int *value)
{
	unsigned int temp;
	void *tmp_addr = mem->ss_base + addr;
	temp = __raw_readl((volatile unsigned int *)tmp_addr);
	*value = temp;
	return 0;
}

static int mx8mq_hdp_swrite(struct hdp_mem *mem, unsigned int addr, unsigned int value)
{
	void *tmp_addr = mem->ss_base + addr;
	__raw_writel(value, (volatile unsigned int *)tmp_addr);
	return 0;
}

static int mx8qm_hdp_read(struct hdp_mem *mem, unsigned int addr, unsigned int *value)
{
	unsigned int temp;
	void *tmp_addr = (addr & 0xfff) + mem->regs_base;
	void *off_addr = 0x8 + mem->ss_base;;

	__raw_writel(addr >> 12, off_addr);
	temp = __raw_readl((volatile unsigned int *)tmp_addr);

	*value = temp;
	return 0;
}

static int mx8qm_hdp_write(struct hdp_mem *mem, unsigned int addr, unsigned int value)
{
	void *tmp_addr = (addr & 0xfff) + mem->regs_base;
	void *off_addr = 0x8 + mem->ss_base;;

	__raw_writel(addr >> 12, off_addr);

	__raw_writel(value, (volatile unsigned int *) tmp_addr);

	return 0;
}

static int mx8qm_hdp_sread(struct hdp_mem *mem, unsigned int addr, unsigned int *value)
{
	unsigned int temp;
	void *tmp_addr = (addr & 0xfff) + mem->regs_base;
	void *off_addr = 0xc + mem->ss_base;;

	__raw_writel(addr >> 12, off_addr);

	temp = __raw_readl((volatile unsigned int *)tmp_addr);
	*value = temp;
	return 0;
}

static int mx8qm_hdp_swrite(struct hdp_mem *mem, unsigned int addr, unsigned int value)
{
	void *tmp_addr = (addr & 0xfff) + mem->regs_base;
	void *off_addr = 0xc + mem->ss_base;

	__raw_writel(addr >> 12, off_addr);
	__raw_writel(value, (volatile unsigned int *)tmp_addr);

	return 0;
}

static struct hdp_rw_func imx8qm_rw = {
	.read_reg = mx8qm_hdp_read,
	.write_reg = mx8qm_hdp_write,
	.sread_reg = mx8qm_hdp_sread,
	.swrite_reg = mx8qm_hdp_swrite,
};

static struct hdp_ops imx8qm_dp_ops = {
	.fw_load = dp_fw_load,
	.fw_init = dp_fw_init,
	.phy_init = dp_phy_init,
	.mode_set = dp_mode_set,
	.get_edid_block = dp_get_edid_block,
};

static struct hdp_ops imx8qm_hdmi_ops = {
	.fw_load = hdmi_fw_load,
	.fw_init = hdmi_fw_init,
	.phy_init = hdmi_phy_init,
	.mode_set = hdmi_mode_set,
	.get_edid_block = hdmi_get_edid_block,
};

static struct hdp_devtype imx8qm_dp_devtype = {
	.load_fw = true,
	.is_hdmi = false,
	.ops = &imx8qm_dp_ops,
	.rw = &imx8qm_rw,
};

static struct hdp_devtype imx8qm_hdmi_devtype = {
	.load_fw = true,
	.is_hdmi = true,
	.ops = &imx8qm_hdmi_ops,
	.rw = &imx8qm_rw,
};

static struct hdp_rw_func imx8mq_rw = {
	.read_reg = mx8mq_hdp_read,
	.write_reg = mx8mq_hdp_write,
	.sread_reg = mx8mq_hdp_sread,
	.swrite_reg = mx8mq_hdp_swrite,
};

static struct hdp_ops imx8mq_ops = {
};

static struct hdp_devtype imx8mq_hdmi_devtype = {
	.load_fw = false,
	.is_hdmi = true,
	.ops = &imx8mq_ops,
	.rw = &imx8mq_rw,
};

static const struct of_device_id imx_hdp_dt_ids[] = {
	{ .compatible = "fsl,imx8qm-hdmi", .data = &imx8qm_hdmi_devtype},
	{ .compatible = "fsl,imx8qm-dp", .data = &imx8qm_dp_devtype},
	{ .compatible = "fsl,imx8mq-hdmi", .data = &imx8mq_hdmi_devtype},
	{ }
};
MODULE_DEVICE_TABLE(of, imx_hdp_dt_ids);

#ifdef hdp_irq
static irqreturn_t imx_hdp_irq_handler(int irq, void *data)
{
	struct imx_hdp *hdp = data;
	u8 eventId;
	u8 HPDevents;
	u8 aux_sts;
	u8 aux_hpd;
	u32 evt;
	u8 hpdevent;

	CDN_API_Get_Event(&hdp->state, &evt);

	if (evt & 0x1) {
		/* HPD event */
		printk("\nevt=%d\n", evt);
		drm_helper_hpd_irq_event(hdp->connector.dev);
		CDN_API_DPTX_ReadEvent_blocking(&hdp->state, &eventId, &HPDevents);
		printk("ReadEvent  ID = %d HPD = %d\n", eventId, HPDevents);
		CDN_API_DPTX_GetHpdStatus_blocking(&hdp->state, &aux_hpd);
		printk("aux_hpd = 0xx\n", aux_hpd);
	} else if (evt & 0x2) {
		/* Link training event */
	} else
		printk(".\r");

	return IRQ_HANDLED;
}
#else
static int hpd_det_worker(void *_dp)
{
	struct imx_hdp *hdp = (struct imx_hdp *) _dp;
	u8 eventId;
	u8 HPDevents;
	u8 aux_hpd;
	u32 evt;

	for (;;) {
		CDN_API_Get_Event(&hdp->state, &evt);
		if (evt & 0x1) {
			/* HPD event */
			CDN_API_DPTX_ReadEvent_blocking(&hdp->state, &eventId, &HPDevents);
			printk("ReadEvent  ID = %d HPD = %d\n", eventId, HPDevents);
			CDN_API_DPTX_GetHpdStatus_blocking(&hdp->state, &aux_hpd);
			if (HPDevents & 0x1) {
				printk("cable plugin\n");
				imx_hdp_cable_plugin(hdp);
				hdp->cable_state = true;
				drm_kms_helper_hotplug_event(hdp->connector.dev);
				imx_hdp_mode_setup(hdp, &hdp->video.cur_mode);
			} else if (HPDevents & 0x2) {
				printk("cable plugout\n");
				hdp->cable_state = false;
				imx_hdp_cable_plugout(hdp);
				drm_kms_helper_hotplug_event(hdp->connector.dev);
			} else
				printk("HPDevent=0x%x\n", HPDevents);
		} else if (evt & 0x2) {
			/* Link training event */
			printk("evt=0x%x\n", evt);
			CDN_API_DPTX_ReadEvent_blocking(&hdp->state, &eventId, &HPDevents);
			printk("ReadEvent  ID = %d HPD = %d\n", eventId, HPDevents);
		} else if (evt & 0xf)
			printk("evt=0x%x\n", evt);

		schedule_timeout_idle(200);
	}

	return 0;
}
#endif

static int imx_hdp_imx_bind(struct device *dev, struct device *master,
			    void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct imx_hdp *hdp;
	const struct of_device_id *of_id =
			of_match_device(imx_hdp_dt_ids, dev);
	const struct hdp_devtype *devtype = of_id->data;
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;
	struct drm_connector *connector;
	struct resource *res;
	struct task_struct *hpd_worker;
	int irq;
	int ret;
	sc_err_t sciErr;
	u32 core_rate;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdp = devm_kzalloc(&pdev->dev, sizeof(*hdp), GFP_KERNEL);
	if (!hdp)
		return -ENOMEM;

	hdp->dev = &pdev->dev;
	encoder = &hdp->encoder;
	bridge = &hdp->bridge;
	connector = &hdp->connector;

	mutex_init(&hdp->mutex);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "can't get irq number\n");
		return irq;
	}

	/* register map */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdp->regs_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdp->regs_base)) {
		dev_err(dev, "Failed to get HDP CTRL base register\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	hdp->ss_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hdp->ss_base)) {
		dev_err(dev, "Failed to get HDP CRS base register\n");
		return -EINVAL;
	}

	hdp->load_fw = devtype->load_fw;
	hdp->is_hdmi = devtype->is_hdmi;
	hdp->ops = devtype->ops;
	hdp->rw = devtype->rw;

	/* encoder */
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0) {
		return -EPROBE_DEFER;
	}

	/* encoder */
	drm_encoder_helper_add(encoder, &imx_hdp_imx_encoder_helper_funcs);
	drm_encoder_init(drm, encoder, &imx_hdp_imx_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	/* bridge */
	bridge->driver_private = hdp;
	bridge->funcs = &imx_hdp_bridge_funcs;
	ret = drm_bridge_attach(encoder, bridge, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize bridge with drm\n");
		return -EINVAL;
	}

	encoder->bridge = bridge;

	/* connector */
	drm_connector_helper_add(connector,
				 &imx_hdp_connector_helper_funcs);

	drm_connector_init(drm, connector,
			   &imx_hdp_connector_funcs,
			   DRM_MODE_CONNECTOR_HDMIA);

	drm_mode_connector_attach_encoder(connector, encoder);

	dev_set_drvdata(dev, hdp);

	imx_hdp_state_init(hdp);

	sciErr = sc_ipc_getMuID(&hdp->mu_id);
	if (sciErr != SC_ERR_NONE) {
		pr_err("Cannot obtain MU ID\n");
		return -EINVAL;
	}

	sciErr = sc_ipc_open(&hdp->ipcHndl, hdp->mu_id);
	if (sciErr != SC_ERR_NONE) {
		pr_err("sc_ipc_open failed! (sciError = %d)\n", sciErr);
		return -EINVAL;
	}

	hdp->link_rate = AFE_LINK_RATE_1_6;

	hdp->dual_mode = false;

	dp_pixel_link_config(hdp);
	dp_clock_init(hdp);

	hdp_ipg_clock_set_rate(hdp);

	dp_ipg_clock_enable(hdp);

	/* Pixel Format - 1 RGB, 2 YCbCr 444, 3 YCbCr 420 */
	/* bpp (bits per subpixel) - 8 24bpp, 10 30bpp, 12 36bpp, 16 48bpp */
	hdp_phy_reset(0);

	imx_hdp_call(hdp, fw_load, &hdp->state);
	core_rate = clk_get_rate(hdp->clks.clk_core);

	imx_hdp_call(hdp, fw_init, &hdp->state, core_rate);
	if (hdp->is_hdmi == true)
		/* default set hdmi to 1080p60 mode */
		imx_hdp_call(hdp, phy_init, &hdp->state, 2, 1, 8);
	else
		imx_hdp_call(hdp, phy_init, &hdp->state, 4, hdp->link_rate, 0);

#ifdef hdp_irq
	ret = devm_request_threaded_irq(dev, irq,
					NULL, imx_hdp_irq_handler,
					IRQF_IRQPOLL, dev_name(dev), dp);
	if (ret) {
		dev_err(&pdev->dev, "can't claim irq %d\n", irq);
		goto err_irq;
	}
#else
	hpd_worker = kthread_create(hpd_det_worker, hdp, "hdp-hpd");
	if (IS_ERR(hpd_worker)) {
		printk("failed  create hpd thread\n");
	}

	wake_up_process(hpd_worker);	/* avoid contributing to loadavg */
#endif

	return 0;
#ifdef hdp_irq
err_irq:
	drm_encoder_cleanup(encoder);
	return ret;
#endif
}

static void imx_hdp_imx_unbind(struct device *dev, struct device *master,
			       void *data)
{
	struct imx_hdp *hdp = dev_get_drvdata(dev);

	imx_hdp_deinit(hdp);
	sc_ipc_close(hdp->mu_id);
	return;
}

static const struct component_ops imx_hdp_imx_ops = {
	.bind	= imx_hdp_imx_bind,
	.unbind	= imx_hdp_imx_unbind,
};

static int imx_hdp_imx_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &imx_hdp_imx_ops);
}

static int imx_hdp_imx_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &imx_hdp_imx_ops);

	return 0;
}

static struct platform_driver imx_hdp_imx_platform_driver = {
	.probe  = imx_hdp_imx_probe,
	.remove = imx_hdp_imx_remove,
	.driver = {
		.name = "i.mx8-hdp",
		.of_match_table = imx_hdp_dt_ids,
	},
};

module_platform_driver(imx_hdp_imx_platform_driver);

MODULE_AUTHOR("Sandor Yu <Sandor.yu@nxp.com>");
MODULE_DESCRIPTION("IMX8QM DP Display Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dp-hdmi-imx");
