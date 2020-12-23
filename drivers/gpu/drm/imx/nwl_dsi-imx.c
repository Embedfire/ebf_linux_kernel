/*
 * i.MX drm driver - Northwest Logic MIPI DSI display driver
 *
 * Copyright (C) 2017 NXP
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


#include <drm/bridge/nwl_dsi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mixel-mipi-dsi.h>
#include <linux/regmap.h>
#include <soc/imx8/sc/sci.h>
#include <video/videomode.h>

#include "imx-drm.h"

#define DRIVER_NAME "nwl_dsi-imx"

#define DC_ID(x)	SC_R_DC_ ## x
#define MIPI_ID(x)	SC_R_MIPI_ ## x
#define SYNC_CTRL(x)	SC_C_SYNC_CTRL ## x
#define PXL_VLD(x)	SC_C_PXL_LINK_MST ## x ## _VLD
#define PXL_ADDR(x)	SC_C_PXL_LINK_MST ## x ## _ADDR

/* Possible clocks */
#define CLK_PIXEL	"pixel"
#define CLK_BYPASS	"bypass"
#define CLK_PHYREF	"phy_ref"

/* Possible valid PHY reference clock rates*/
u32 phyref_rates[] =
{
	27000000,
	24000000
};

struct imx_mipi_dsi {
	struct drm_encoder		encoder;
	struct device			*dev;
	struct phy			*phy;

	/* Optional external regs */
	struct regmap			*csr;

	/* Optional clocks */
	struct clk_config		*clk_config;
	size_t				clk_num;

	u32 tx_ulps_reg;
	u32 pxl2dpi_reg;

	u32				phyref_rate;
	u32				instance;
	bool				enabled;
};

struct clk_config {
	const char *id;
	struct clk *clk;
	bool present;
	u32 rate;
};

enum imx_ext_regs {
	IMX_REG_CSR = BIT(0),
};

struct devtype {
	int (*poweron)(struct imx_mipi_dsi *);
	int (*poweroff)(struct imx_mipi_dsi *);
	u32 ext_regs; /* required external registers */
	u32 tx_ulps_reg;
	u32 pxl2dpi_reg;
	u8 max_instances;
	struct clk_config clk_config[3];
};

static int imx8qm_dsi_poweron(struct imx_mipi_dsi *dsi);
static int imx8qm_dsi_poweroff(struct imx_mipi_dsi *dsi);
static struct devtype imx8qm_dev = {
	.poweron = &imx8qm_dsi_poweron,
	.poweroff = &imx8qm_dsi_poweroff,
	.clk_config = {
		{ .id = CLK_PIXEL,  .present = true },
		{ .id = CLK_BYPASS, .present = true },
		{ .id = CLK_PHYREF, .present = true },
	},
	.ext_regs = IMX_REG_CSR,
	.tx_ulps_reg   = 0x00,
	.pxl2dpi_reg   = 0x04,
	.max_instances =    2,
};

static int imx8qxp_dsi_poweron(struct imx_mipi_dsi *dsi);
static int imx8qxp_dsi_poweroff(struct imx_mipi_dsi *dsi);
static struct devtype imx8qxp_dev = {
	.poweron = &imx8qxp_dsi_poweron,
	.poweroff = &imx8qxp_dsi_poweroff,
	.clk_config = {
		{ .id = CLK_PIXEL,  .present = true },
		{ .id = CLK_BYPASS, .present = true },
		{ .id = CLK_PHYREF, .present = true },
	},
	.ext_regs = IMX_REG_CSR,
	.tx_ulps_reg   = 0x30,
	.pxl2dpi_reg   = 0x40,
	.max_instances =    2,
};

static const struct of_device_id imx_nwl_dsi_dt_ids[] = {
	{ .compatible = "fsl,imx8qm-mipi-dsi", .data = &imx8qm_dev, },
	{ .compatible = "fsl,imx8qxp-mipi-dsi", .data = &imx8qxp_dev, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_nwl_dsi_dt_ids);

static inline struct imx_mipi_dsi *encoder_to_dsi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct imx_mipi_dsi, encoder);
}

static void imx_nwl_dsi_set_clocks(struct imx_mipi_dsi *dsi, bool enable)
{
	struct device *dev = dsi->dev;
	const char *id;
	struct clk *clk;
	unsigned long rate;
	size_t i;

	for (i = 0; i < dsi->clk_num; i++) {
		if (!dsi->clk_config[i].present)
			continue;
		id = dsi->clk_config[i].id;
		clk = dsi->clk_config[i].clk;
		rate = dsi->clk_config[i].rate;

		/* BYPASS clk must have the same rate as PHY_REF clk */
		if (!strcmp(id, CLK_BYPASS))
			rate = dsi->phyref_rate;

		if (enable) {
			if (rate > 0)
				clk_set_rate(clk, rate);
			clk_enable(clk);
			rate = clk_get_rate(clk);
			DRM_DEV_DEBUG_DRIVER(dev,
				"Enabled %s clk (rate=%lu)\n", id, rate);
		} else {
			clk_disable(clk);
			DRM_DEV_DEBUG_DRIVER(dev, "Disabled %s clk\n", id);
		}
	}
}

/*
 * v2 is true for QXP
 * On QM, we have 2 DPUs, each one with a MIPI-DSI link
 * On QXP, we have 1 DPU with two MIPI-DSI links
 * Because of this, we will have different initialization
 * paths for MIPI0 and MIPI1 on QM vs QXP
 */
static int imx8q_dsi_poweron(struct imx_mipi_dsi *dsi, bool v2)
{
	struct device *dev = dsi->dev;
	int ret = 0;
	sc_err_t sci_err = 0;
	sc_ipc_t ipc_handle = 0;
	u32 inst = dsi->instance;
	u32 mu_id;
	sc_rsrc_t mipi_id, dc_id;
	sc_ctrl_t mipi_ctrl;

	sci_err = sc_ipc_getMuID(&mu_id);
	if (sci_err != SC_ERR_NONE) {
		DRM_DEV_ERROR(dev, "Failed to get MU ID (%d)\n", sci_err);
		return -ENODEV;
	}
	sci_err = sc_ipc_open(&ipc_handle, mu_id);
	if (sci_err != SC_ERR_NONE) {
		DRM_DEV_ERROR(dev, "Failed to open IPC (%d)\n", sci_err);
		return -ENODEV;
	}

	mipi_id = inst?MIPI_ID(1):MIPI_ID(0);
	dc_id = (!v2 && inst)?DC_ID(1):DC_ID(0);
	DRM_DEV_DEBUG_DRIVER(dev, "MIPI ID: %d DC ID: %d\n",
			     mipi_id,
			     dc_id);

	/* Initialize Pixel Link */
	mipi_ctrl = (v2 && inst)?PXL_ADDR(2):PXL_ADDR(1);
	sci_err = sc_misc_set_control(ipc_handle,
				      dc_id,
				      mipi_ctrl,
				      0);
	if (sci_err != SC_ERR_NONE) {
		DRM_DEV_ERROR(dev,
			"Failed to set SC_C_PXL_LINK_MST%d_ADDR (%d)\n",
			inst,
			sci_err);
		ret = -ENODEV;
		goto err_ipc;
	}

	mipi_ctrl = (v2 && inst)?PXL_VLD(2):PXL_VLD(1);
	sci_err = sc_misc_set_control(ipc_handle,
				      dc_id,
				      mipi_ctrl,
				      1);
	if (sci_err != SC_ERR_NONE) {
		DRM_DEV_ERROR(dev,
			"Failed to set SC_C_PXL_LINK_MST%d_VLD (%d)\n",
			inst + 1,
			sci_err);
		ret = -ENODEV;
		goto err_ipc;
	}

	mipi_ctrl = (v2 && inst)?SYNC_CTRL(1):SYNC_CTRL(0);
	sci_err = sc_misc_set_control(ipc_handle,
				      dc_id,
				      mipi_ctrl,
				      1);
	if (sci_err != SC_ERR_NONE) {
		DRM_DEV_ERROR(dev,
			"Failed to set SC_C_SYNC_CTRL%d (%d)\n",
			inst,
			sci_err);
		ret = -ENODEV;
		goto err_ipc;
	}

	if (v2) {
		sci_err = sc_misc_set_control(ipc_handle,
				mipi_id, SC_C_MODE, 0);
		if (sci_err != SC_ERR_NONE)
			DRM_DEV_ERROR(dev,
				      "Failed to set SC_C_MODE (%d)\n",
				      sci_err);
		sci_err = sc_misc_set_control(ipc_handle,
				mipi_id, SC_C_DUAL_MODE, 0);
		if (sci_err != SC_ERR_NONE)
			DRM_DEV_ERROR(dev,
				      "Failed to set SC_C_DUAL_MODE (%d)\n",
				      sci_err);
		sci_err = sc_misc_set_control(ipc_handle,
				mipi_id, SC_C_PXL_LINK_SEL, inst);
		if (sci_err != SC_ERR_NONE)
			DRM_DEV_ERROR(dev,
				     "Failed to set SC_C_PXL_LINK_SEL (%d)\n",
				     sci_err);
	}

	/* Assert DPI and MIPI bits */
	sci_err = sc_misc_set_control(ipc_handle,
				      mipi_id,
				      SC_C_DPI_RESET,
				      1);
	if (sci_err != SC_ERR_NONE) {
		DRM_DEV_ERROR(dev,
			"Failed to assert DPI reset (%d)\n",
			sci_err);
		ret = -ENODEV;
		goto err_ipc;
	}

	sci_err = sc_misc_set_control(ipc_handle,
				      mipi_id,
				      SC_C_MIPI_RESET,
				      1);
	if (sci_err != SC_ERR_NONE) {
		DRM_DEV_ERROR(dev,
			"Failed to assert MIPI reset (%d)\n",
			sci_err);
		ret = -ENODEV;
		goto err_ipc;
	}

	regmap_write(dsi->csr,
		     dsi->tx_ulps_reg,
		     0);
	regmap_write(dsi->csr,
		     dsi->pxl2dpi_reg,
		     DPI_24_BIT);

	sc_ipc_close(ipc_handle);
	return ret;

err_ipc:
	sc_ipc_close(ipc_handle);
	return ret;
}

static int imx8q_dsi_poweroff(struct imx_mipi_dsi *dsi, bool v2)
{
	struct device *dev = dsi->dev;
	sc_err_t sci_err = 0;
	sc_ipc_t ipc_handle = 0;
	u32 mu_id;
	u32 inst = dsi->instance;
	sc_rsrc_t mipi_id, dc_id;

	mipi_id = (inst)?SC_R_MIPI_1:SC_R_MIPI_0;
	if (v2)
		dc_id = SC_R_DC_0;
	else
		dc_id = (inst)?SC_R_DC_1:SC_R_DC_0;

	/* Deassert DPI and MIPI bits */
	if (sc_ipc_getMuID(&mu_id) == SC_ERR_NONE &&
	    sc_ipc_open(&ipc_handle, mu_id) == SC_ERR_NONE) {
		sci_err = sc_misc_set_control(ipc_handle,
				mipi_id, SC_C_DPI_RESET, 0);
		if (sci_err != SC_ERR_NONE)
			DRM_DEV_ERROR(dev,
				"Failed to deassert DPI reset (%d)\n",
				sci_err);

		sci_err = sc_misc_set_control(ipc_handle,
				mipi_id, SC_C_MIPI_RESET, 0);
		if (sci_err != SC_ERR_NONE)
			DRM_DEV_ERROR(dev,
				"Failed to deassert MIPI reset (%d)\n",
				sci_err);

		sci_err = sc_misc_set_control(ipc_handle,
				dc_id, SC_C_SYNC_CTRL0, 0);
		if (sci_err != SC_ERR_NONE)
			DRM_DEV_ERROR(dev,
				"Failed to reset SC_C_SYNC_CTRL0 (%d)\n",
				sci_err);

		sci_err = sc_misc_set_control(ipc_handle,
				dc_id, SC_C_PXL_LINK_MST1_VLD, 0);
		if (sci_err != SC_ERR_NONE)
			DRM_DEV_ERROR(dev,
				"Failed to reset SC_C_SYNC_CTRL0 (%d)\n",
				sci_err);
	}

	return 0;
}

static int imx8qm_dsi_poweron(struct imx_mipi_dsi *dsi)
{
	return imx8q_dsi_poweron(dsi, false);
}

static int imx8qm_dsi_poweroff(struct imx_mipi_dsi *dsi)
{
	return imx8q_dsi_poweroff(dsi, false);
}

static int imx8qxp_dsi_poweron(struct imx_mipi_dsi *dsi)
{
	return imx8q_dsi_poweron(dsi, true);
}

static int imx8qxp_dsi_poweroff(struct imx_mipi_dsi *dsi)
{
	return imx8q_dsi_poweroff(dsi, true);
}

static void imx_nwl_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct imx_mipi_dsi *dsi = encoder_to_dsi(encoder);
	struct device *dev = dsi->dev;
	const struct of_device_id *of_id = of_match_device(imx_nwl_dsi_dt_ids,
							   dev);
	const struct devtype *devtype = of_id->data;
	int ret;


	if (dsi->enabled)
		return;

	DRM_DEV_INFO(dev, "id = %s\n", (dsi->instance)?"DSI1":"DSI0");

	imx_nwl_dsi_set_clocks(dsi, true);

	ret = devtype->poweron(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to power on DSI (%d)\n", ret);
		return;
	}

	dsi->enabled = true;
}

static void imx_nwl_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct imx_mipi_dsi *dsi = encoder_to_dsi(encoder);
	struct device *dev = dsi->dev;
	const struct of_device_id *of_id = of_match_device(imx_nwl_dsi_dt_ids,
							   dev);
	const struct devtype *devtype = of_id->data;

	if (!dsi->enabled)
		return;

	DRM_DEV_INFO(dev, "id = %s\n", (dsi->instance)?"DSI1":"DSI0");

	devtype->poweroff(dsi);

	imx_nwl_dsi_set_clocks(dsi, false);

	dsi->enabled = false;
}

/*
 * This function will try the required phy speed for current mode
 * If the phy speed can be achieved, the phy will save the speed
 * configuration
 */
static int imx_nwl_try_phy_speed(struct imx_mipi_dsi *dsi,
			    struct drm_display_mode *mode)
{
	struct device *dev = dsi->dev;
	unsigned long pixclock;
	unsigned long bit_clk;
	size_t i, num_rates = ARRAY_SIZE(phyref_rates);
	int ret = 0;

	pixclock = mode->clock * 1000;
	bit_clk = nwl_dsi_get_bit_clock(&dsi->encoder, pixclock);

	for (i = 0; i < num_rates; i++) {
		dsi->phyref_rate = phyref_rates[i];
		DRM_DEV_DEBUG_DRIVER(dev, "Trying PHY ref rate: %u\n",
			dsi->phyref_rate);
		ret = mixel_phy_mipi_set_phy_speed(dsi->phy,
			bit_clk,
			dsi->phyref_rate);
		/* Pick the first non-failing rate */
		if (!ret)
			break;
	}
	if (ret < 0) {
		DRM_DEV_DEBUG_DRIVER(dev,
			"Cannot setup PHY for mode: %ux%u @%d kHz\n",
			mode->hdisplay,
			mode->vdisplay,
			mode->clock);
		DRM_DEV_DEBUG_DRIVER(dev, "PHY_REF clk: %u, bit clk: %lu\n",
			dsi->phyref_rate, bit_clk);
	}

	return ret;
}

static int imx_nwl_dsi_encoder_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(crtc_state);
	struct imx_mipi_dsi *dsi = encoder_to_dsi(encoder);

	imx_crtc_state->bus_format = MEDIA_BUS_FMT_RGB101010_1X30;

	/* Try to see if the phy can satisfy the current mode */
	return imx_nwl_try_phy_speed(dsi, &crtc_state->adjusted_mode);
}

static const struct drm_encoder_helper_funcs
imx_nwl_dsi_encoder_helper_funcs = {
	.enable = imx_nwl_dsi_encoder_enable,
	.disable = imx_nwl_dsi_encoder_disable,
	.atomic_check = imx_nwl_dsi_encoder_atomic_check,
};

static void imx_nwl_dsi_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs imx_nwl_dsi_encoder_funcs = {
	.destroy = imx_nwl_dsi_encoder_destroy,
};

static int imx_nwl_dsi_bind(struct device *dev,
			struct device *master,
			void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct imx_mipi_dsi *dsi;
	struct device_node *np = dev->of_node;
	const struct of_device_id *of_id = of_match_device(imx_nwl_dsi_dt_ids,
							   dev);
	const struct devtype *devtype = of_id->data;
	struct resource *res;
	int irq;
	struct clk *clk;
	const char *clk_id;
	size_t i, clk_config_sz;
	int ret;

	if (!np)
		return -ENODEV;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dev = dev;

	dsi->instance = of_alias_get_id(np, "mipi_dsi");
	if (dsi->instance < 0) {
		dev_err(dev, "No mipi_dsi alias found!");
		return dsi->instance;
	}
	if (dsi->instance > devtype->max_instances - 1) {
		dev_err(dev, "Too many instances! (cur: %d, max: %d)\n",
			dsi->instance, devtype->max_instances);
		return -ENODEV;
	}

	DRM_DEV_INFO(dev, "id = %s\n", (dsi->instance)?"DSI1":"DSI0");

	dsi->phy = devm_phy_get(dev, "dphy");
	if (IS_ERR(dsi->phy)) {
		ret = PTR_ERR(dsi->phy);
		dev_err(dev, "Could not get PHY (%d)\n", ret);
		return ret;
	}

	/* Look for optional clocks */
	dsi->clk_num = ARRAY_SIZE(devtype->clk_config);
	dsi->clk_config = devm_kcalloc(dev,
				dsi->clk_num,
				sizeof(struct clk_config),
				GFP_KERNEL);
	clk_config_sz = dsi->clk_num * sizeof(struct clk_config);
	memcpy(dsi->clk_config, devtype->clk_config, clk_config_sz);

	for (i = 0; i < dsi->clk_num; i++) {
		if (!dsi->clk_config[i].present)
			continue;

		clk_id = dsi->clk_config[i].id;
		clk = devm_clk_get(dev, clk_id);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			dev_err(dev, "Failed to get %s clock (%d)\n",
				clk_id, ret);
			return ret;
		}
		clk_prepare(clk);

		dsi->clk_config[i].clk = clk;
	}

	/* Look for optional regmaps */
	dsi->csr = syscon_regmap_lookup_by_phandle(np, "csr");
	if (IS_ERR(dsi->csr) && (devtype->ext_regs & IMX_REG_CSR)) {
		ret = PTR_ERR(dsi->csr);
		dev_err(dev, "Failed to get CSR regmap (%d)\n", ret);
		return ret;
	}

	dsi->tx_ulps_reg = devtype->tx_ulps_reg;
	dsi->pxl2dpi_reg = devtype->pxl2dpi_reg;

	platform_set_drvdata(pdev, dsi);

	ret = imx_drm_encoder_parse_of(drm, &dsi->encoder, dev->of_node);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EBUSY;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		DRM_DEV_ERROR(dev, "Failed to get device IRQ!\n");
		return -EINVAL;
	}

	drm_encoder_helper_add(&dsi->encoder,
			       &imx_nwl_dsi_encoder_helper_funcs);
	ret = drm_encoder_init(drm,
			       &dsi->encoder,
			       &imx_nwl_dsi_encoder_funcs,
			       DRM_MODE_ENCODER_DSI,
			       NULL);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to init DSI encoder (%d)\n", ret);
		return ret;
	}

	/* Now, bind our NWL MIPI-DSI bridge */
	ret = nwl_dsi_bind(dev, &dsi->encoder, dsi->phy, res, irq);

	if (ret)
		drm_encoder_cleanup(&dsi->encoder);

	return ret;
}

static void imx_nwl_dsi_unbind(struct device *dev,
			   struct device *master,
			   void *data)
{
	struct imx_mipi_dsi *dsi = dev_get_drvdata(dev);

	DRM_DEV_INFO(dev, "id = %s\n", (dsi->instance)?"DSI1":"DSI0");

	imx_nwl_dsi_encoder_disable(&dsi->encoder);

	drm_encoder_cleanup(&dsi->encoder);

	nwl_dsi_unbind(dsi->encoder.bridge);
}

static const struct component_ops imx_nwl_dsi_component_ops = {
	.bind	= imx_nwl_dsi_bind,
	.unbind	= imx_nwl_dsi_unbind,
};

static int imx_nwl_dsi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &imx_nwl_dsi_component_ops);
}

static int imx_nwl_dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &imx_nwl_dsi_component_ops);

	return 0;
}

static struct platform_driver imx_nwl_dsi_driver = {
	.probe		= imx_nwl_dsi_probe,
	.remove		= imx_nwl_dsi_remove,
	.driver		= {
		.of_match_table = imx_nwl_dsi_dt_ids,
		.name	= DRIVER_NAME,
	},
};

module_platform_driver(imx_nwl_dsi_driver);

MODULE_AUTHOR("NXP Semiconductor");
MODULE_DESCRIPTION("i.MX Northwest Logic MIPI-DSI driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
