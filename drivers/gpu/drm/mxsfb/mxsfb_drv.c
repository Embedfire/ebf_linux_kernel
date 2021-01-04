// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 *
 * This code is based on drivers/video/fbdev/mxsfb.c :
 * Copyright (C) 2010 Juergen Beisert, Pengutronix
 * Copyright (C) 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>
#include <linux/dma-resv.h>
#include <linux/spinlock.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_irq.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "mxsfb_drv.h"
#include "mxsfb_regs.h"

enum mxsfb_devtype {
	MXSFB_V3,
	MXSFB_V4,
};

/*
 * When adding new formats, make sure to update the num_formats from
 * mxsfb_devdata below.
 */
static const u32 mxsfb_formats[] = {
	/* MXSFB_V3 */
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGB565,
	/* MXSFB_V4 */
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_BGR565
};

static const struct mxsfb_devdata mxsfb_devdata[] = {
	[MXSFB_V3] = {
		.transfer_count	= LCDC_V3_TRANSFER_COUNT,
		.cur_buf	= LCDC_V3_CUR_BUF,
		.next_buf	= LCDC_V3_NEXT_BUF,
		.debug0		= LCDC_V3_DEBUG0,
		.hs_wdth_mask	= 0xff,
		.hs_wdth_shift	= 24,
		.ipversion	= 3,
		.num_formats	= 3,
	},
	[MXSFB_V4] = {
		.transfer_count	= LCDC_V4_TRANSFER_COUNT,
		.cur_buf	= LCDC_V4_CUR_BUF,
		.next_buf	= LCDC_V4_NEXT_BUF,
		.debug0		= LCDC_V4_DEBUG0,
		.hs_wdth_mask	= 0x3fff,
		.hs_wdth_shift	= 18,
		.ipversion	= 4,
		.num_formats	= ARRAY_SIZE(mxsfb_formats),
	},
};

static struct mxsfb_drm_private *
drm_pipe_to_mxsfb_drm_private(struct drm_simple_display_pipe *pipe)
{
	return container_of(pipe, struct mxsfb_drm_private, pipe);
}

/**
 * mxsfb_atomic_helper_check - validate state object
 * @dev: DRM device
 * @state: the driver state object
 *
 * On top of the drm imlementation drm_atomic_helper_check,
 * check if the bpp is changed, if so, signal mode_changed,
 * this will trigger disable/enable
 *
 * RETURNS:
 * Zero for success or -errno
 */
static int mxsfb_atomic_helper_check(struct drm_device *dev,
				     struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_state;
	int i, ret;

	ret = drm_atomic_helper_check(dev, state);
	if (ret)
		return ret;

	for_each_new_crtc_in_state(state, crtc, new_state, i) {
		struct drm_plane_state *primary_state;
		int old_bpp = 0;
		int new_bpp = 0;

		if (!crtc->primary || !crtc->primary->old_fb)
			continue;
		primary_state =
			drm_atomic_get_plane_state(state, crtc->primary);
		if (!primary_state || !primary_state->fb)
			continue;
		old_bpp = crtc->primary->old_fb->format->depth;
		new_bpp = primary_state->fb->format->depth;
		if (old_bpp != new_bpp)
			new_state->mode_changed = true;
	}

	return ret;
}

static const struct drm_mode_config_funcs mxsfb_mode_config_funcs = {
	.fb_create		= drm_gem_fb_create,
	.atomic_check		= mxsfb_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs mxsfb_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

enum drm_mode_status mxsfb_pipe_mode_valid(struct drm_crtc *crtc,
					   const struct drm_display_mode *mode)
{
	struct drm_simple_display_pipe *pipe =
		container_of(crtc, struct drm_simple_display_pipe, crtc);
	struct mxsfb_drm_private *mxsfb = drm_pipe_to_mxsfb_drm_private(pipe);
	u32 bpp;
	u64 bw;

	if (!pipe->plane.state->fb)
		bpp = 32;
	else
		bpp = pipe->plane.state->fb->format->depth;

	bw = (u64)mode->clock * 1000;
	bw = bw * mode->hdisplay * mode->vdisplay * (bpp / 8);
	bw = div_u64(bw, mode->htotal * mode->vtotal);

	if (mxsfb->max_bw && (bw > mxsfb->max_bw))
		return MODE_BAD;

	return MODE_OK;
}

static int mxsfb_pipe_check(struct drm_simple_display_pipe *pipe,
			    struct drm_plane_state *plane_state,
			    struct drm_crtc_state *crtc_state)
{
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_framebuffer *old_fb = pipe->plane.state->fb;

	/* force 'mode_changed' when fb pitches changed, since
	 * the pitch related registers configuration of LCDIF
	 * can not be done when LCDIF is running.
	 */
	if (old_fb && likely(!crtc_state->mode_changed)) {
		if (old_fb->pitches[0] != fb->pitches[0])
			crtc_state->mode_changed = true;
	}

	return 0;
}

static void mxsfb_pipe_enable(struct drm_simple_display_pipe *pipe,
			      struct drm_crtc_state *crtc_state,
			      struct drm_plane_state *plane_state)
{
	struct drm_connector *connector;
	struct mxsfb_drm_private *mxsfb = drm_pipe_to_mxsfb_drm_private(pipe);
	struct drm_device *drm = pipe->plane.dev;

	if (!mxsfb->connector) {
		list_for_each_entry(connector,
				    &drm->mode_config.connector_list,
				    head)
			if (connector->encoder == &mxsfb->pipe.encoder) {
				mxsfb->connector = connector;
				break;
			}
	}

	if (!mxsfb->connector) {
		dev_warn(drm->dev, "No connector attached, using default\n");
		mxsfb->connector = &mxsfb->panel_connector;
	}

	pm_runtime_get_sync(drm->dev);
	drm_panel_prepare(mxsfb->panel);
	mxsfb_crtc_enable(mxsfb);
	drm_panel_enable(mxsfb->panel);
}

static void mxsfb_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct mxsfb_drm_private *mxsfb = drm_pipe_to_mxsfb_drm_private(pipe);
	struct drm_device *drm = pipe->plane.dev;
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_pending_vblank_event *event;

	drm_panel_disable(mxsfb->panel);
	mxsfb_crtc_disable(mxsfb);
	drm_panel_unprepare(mxsfb->panel);
	pm_runtime_put_sync(drm->dev);

	spin_lock_irq(&drm->event_lock);
	event = crtc->state->event;
	if (event) {
		crtc->state->event = NULL;
		drm_crtc_send_vblank_event(crtc, event);
	}
	spin_unlock_irq(&drm->event_lock);

	if (mxsfb->connector != &mxsfb->panel_connector)
		mxsfb->connector = NULL;
}

static void mxsfb_pipe_update(struct drm_simple_display_pipe *pipe,
			      struct drm_plane_state *plane_state)
{
	struct mxsfb_drm_private *mxsfb = drm_pipe_to_mxsfb_drm_private(pipe);

	mxsfb_plane_atomic_update(mxsfb, plane_state);
}

static int mxsfb_pipe_enable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct mxsfb_drm_private *mxsfb = drm_pipe_to_mxsfb_drm_private(pipe);
	int ret = 0;

	ret = clk_prepare_enable(mxsfb->clk_axi);
	if (ret)
		return ret;

	/* Clear and enable VBLANK IRQ */
	writel(CTRL1_CUR_FRAME_DONE_IRQ, mxsfb->base + LCDC_CTRL1 + REG_CLR);
	writel(CTRL1_CUR_FRAME_DONE_IRQ_EN, mxsfb->base + LCDC_CTRL1 + REG_SET);
	clk_disable_unprepare(mxsfb->clk_axi);

	return ret;
}

static void mxsfb_pipe_disable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct mxsfb_drm_private *mxsfb = drm_pipe_to_mxsfb_drm_private(pipe);

	if (clk_prepare_enable(mxsfb->clk_axi))
		return;

	/* Disable and clear VBLANK IRQ */
	writel(CTRL1_CUR_FRAME_DONE_IRQ_EN, mxsfb->base + LCDC_CTRL1 + REG_CLR);
	writel(CTRL1_CUR_FRAME_DONE_IRQ, mxsfb->base + LCDC_CTRL1 + REG_CLR);
	clk_disable_unprepare(mxsfb->clk_axi);
}

static struct drm_simple_display_pipe_funcs mxsfb_funcs = {
	.mode_valid	= mxsfb_pipe_mode_valid,
	.check          = mxsfb_pipe_check,
	.enable		= mxsfb_pipe_enable,
	.disable	= mxsfb_pipe_disable,
	.update		= mxsfb_pipe_update,
	.prepare_fb	= drm_gem_fb_simple_display_pipe_prepare_fb,
	.enable_vblank	= mxsfb_pipe_enable_vblank,
	.disable_vblank	= mxsfb_pipe_disable_vblank,
};

static int mxsfb_load(struct drm_device *drm, unsigned long flags)
{
	struct platform_device *pdev = to_platform_device(drm->dev);
	struct mxsfb_drm_private *mxsfb;
	struct resource *res;
	int ret;

	mxsfb = devm_kzalloc(&pdev->dev, sizeof(*mxsfb), GFP_KERNEL);
	if (!mxsfb)
		return -ENOMEM;

	drm->dev_private = mxsfb;
	mxsfb->devdata = &mxsfb_devdata[pdev->id_entry->driver_data];

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mxsfb->base = devm_ioremap_resource(drm->dev, res);
	if (IS_ERR(mxsfb->base))
		return PTR_ERR(mxsfb->base);

	mxsfb->clk = devm_clk_get(drm->dev, NULL);
	if (IS_ERR(mxsfb->clk))
		return PTR_ERR(mxsfb->clk);

	mxsfb->clk_axi = devm_clk_get(drm->dev, "axi");
	if (IS_ERR(mxsfb->clk_axi))
		mxsfb->clk_axi = NULL;

	mxsfb->clk_disp_axi = devm_clk_get(drm->dev, "disp_axi");
	if (IS_ERR(mxsfb->clk_disp_axi))
		mxsfb->clk_disp_axi = NULL;

	of_property_read_u32(drm->dev->of_node, "max-memory-bandwidth",
			     &mxsfb->max_bw);

	ret = dma_set_mask_and_coherent(drm->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	pm_runtime_enable(drm->dev);

	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to initialise vblank\n");
		goto err_vblank;
	}

	/* Modeset init */
	drm_mode_config_init(drm);

	ret = mxsfb_create_output(drm);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to create outputs\n");
		goto err_vblank;
	}

	ret = drm_simple_display_pipe_init(drm, &mxsfb->pipe, &mxsfb_funcs,
			mxsfb_formats, mxsfb->devdata->num_formats, NULL,
			mxsfb->connector);
	if (ret < 0) {
		dev_err(drm->dev, "Cannot setup simple display pipe\n");
		goto err_vblank;
	}

	/*
	 * Attach panel only if there is one.
	 * If there is no panel attach, it must be a bridge. In this case, we
	 * need a reference to its connector for a proper initialization.
	 * We will do this check in pipe->enable(), since the connector won't
	 * be attached to an encoder until then.
	 */

	if (mxsfb->panel) {
		ret = drm_panel_attach(mxsfb->panel, mxsfb->connector);
		if (ret) {
			dev_err(drm->dev, "Cannot connect panel: %d\n", ret);
			goto err_vblank;
		}
	} else if (mxsfb->bridge) {
		ret = drm_simple_display_pipe_attach_bridge(&mxsfb->pipe,
							    mxsfb->bridge);
		if (ret) {
			dev_err(drm->dev, "Cannot connect bridge: %d\n", ret);
			goto err_vblank;
		}
	}

	drm->mode_config.min_width	= MXSFB_MIN_XRES;
	drm->mode_config.min_height	= MXSFB_MIN_YRES;
	drm->mode_config.max_width	= MXSFB_MAX_XRES;
	drm->mode_config.max_height	= MXSFB_MAX_YRES;
	drm->mode_config.funcs		= &mxsfb_mode_config_funcs;
	drm->mode_config.helper_private	= &mxsfb_mode_config_helpers;

	drm_mode_config_reset(drm);

	pm_runtime_get_sync(drm->dev);
	ret = drm_irq_install(drm, platform_get_irq(pdev, 0));
	pm_runtime_put_sync(drm->dev);

	if (ret < 0) {
		dev_err(drm->dev, "Failed to install IRQ handler\n");
		goto err_irq;
	}

	drm_kms_helper_poll_init(drm);

	platform_set_drvdata(pdev, drm);

	drm_helper_hpd_irq_event(drm);

	return 0;

err_irq:
	drm_panel_detach(mxsfb->panel);
err_vblank:
	pm_runtime_disable(drm->dev);

	return ret;
}

static void mxsfb_unload(struct drm_device *drm)
{
	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);

	pm_runtime_get_sync(drm->dev);
	drm_irq_uninstall(drm);
	pm_runtime_put_sync(drm->dev);

	drm->dev_private = NULL;

	pm_runtime_disable(drm->dev);
}

static void mxsfb_irq_preinstall(struct drm_device *drm)
{
	struct mxsfb_drm_private *mxsfb = drm->dev_private;

	mxsfb_pipe_disable_vblank(&mxsfb->pipe);
}

static irqreturn_t mxsfb_irq_handler(int irq, void *data)
{
	struct drm_device *drm = data;
	struct mxsfb_drm_private *mxsfb = drm->dev_private;
	u32 reg;

	clk_prepare_enable(mxsfb->clk_axi);

	reg = readl(mxsfb->base + LCDC_CTRL1);

	if (reg & CTRL1_CUR_FRAME_DONE_IRQ)
		drm_crtc_handle_vblank(&mxsfb->pipe.crtc);

	writel(CTRL1_CUR_FRAME_DONE_IRQ, mxsfb->base + LCDC_CTRL1 + REG_CLR);

	clk_disable_unprepare(mxsfb->clk_axi);

	return IRQ_HANDLED;
}

DEFINE_DRM_GEM_CMA_FOPS(fops);

static struct drm_driver mxsfb_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.irq_handler		= mxsfb_irq_handler,
	.irq_preinstall		= mxsfb_irq_preinstall,
	.irq_uninstall		= mxsfb_irq_preinstall,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.dumb_create		= drm_gem_cma_dumb_create,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,
	.fops	= &fops,
	.name	= "mxsfb-drm",
	.desc	= "MXSFB Controller DRM",
	.date	= "20160824",
	.major	= 1,
	.minor	= 0,
};

static const struct platform_device_id mxsfb_devtype[] = {
	{ .name = "imx23-fb", .driver_data = MXSFB_V3, },
	{ .name = "imx28-fb", .driver_data = MXSFB_V4, },
	{ .name = "imx6sx-fb", .driver_data = MXSFB_V4, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, mxsfb_devtype);

static const struct of_device_id mxsfb_dt_ids[] = {
	{ .compatible = "fsl,imx23-lcdif", .data = &mxsfb_devtype[0], },
	{ .compatible = "fsl,imx28-lcdif", .data = &mxsfb_devtype[1], },
	{ .compatible = "fsl,imx6sx-lcdif", .data = &mxsfb_devtype[2], },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxsfb_dt_ids);

static int mxsfb_probe(struct platform_device *pdev)
{
	struct drm_device *drm;
	const struct of_device_id *of_id =
			of_match_device(mxsfb_dt_ids, &pdev->dev);
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	if (of_id)
		pdev->id_entry = of_id->data;

	drm = drm_dev_alloc(&mxsfb_driver, &pdev->dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	ret = mxsfb_load(drm, 0);
	if (ret)
		goto err_free;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_unload;

	drm_fbdev_generic_setup(drm, 32);

	return 0;

err_unload:
	mxsfb_unload(drm);
err_free:
	drm_dev_put(drm);

	return ret;
}

static int mxsfb_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_dev_unregister(drm);
	mxsfb_unload(drm);
	drm_dev_put(drm);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mxsfb_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int mxsfb_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm);
}
#endif

static const struct dev_pm_ops mxsfb_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mxsfb_suspend, mxsfb_resume)
};

static struct platform_driver mxsfb_platform_driver = {
	.probe		= mxsfb_probe,
	.remove		= mxsfb_remove,
	.id_table	= mxsfb_devtype,
	.driver	= {
		.name		= "mxsfb",
		.of_match_table	= mxsfb_dt_ids,
		.pm		= &mxsfb_pm_ops,
	},
};

module_platform_driver(mxsfb_platform_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale MXS DRM/KMS driver");
MODULE_LICENSE("GPL");
