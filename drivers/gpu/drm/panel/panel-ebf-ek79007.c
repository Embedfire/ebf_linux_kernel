#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/display_timing.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>


struct ek79007 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *supply;
	struct backlight_device *backlight;
	bool prepared;
	bool enabled;
};

static struct drm_display_mode default_mode;

/**
 * of_parse_display_timing - parse display_timing entry from device_node
 * @np: device_node with the properties
 **/
static int of_parse_display_timing(const struct device_node *np,
		struct drm_display_mode *dm)
{
	int ret = 0;
	int pixelclock = 0;
	int hactive = 0;
	int vactive = 0;
	int hfront_porch = 0;
	int hback_porch = 0;
	int hsync_len = 0;
	int vback_porch = 0;
	int vfront_porch = 0;
	int vsync_len = 0;		
	int vrefresh = 0;

	ret |= of_property_read_u32(np, "hback-porch", &hback_porch);
	ret |= of_property_read_u32(np, "hfront-porch", &hfront_porch);
	ret |= of_property_read_u32(np, "hactive", &hactive);
	ret |= of_property_read_u32(np, "hsync-len", &hsync_len);
	ret |= of_property_read_u32(np, "vback-porch", &vback_porch);
	ret |= of_property_read_u32(np, "vfront-porch", &vfront_porch);
	ret |= of_property_read_u32(np, "vactive", &vactive);
	ret |= of_property_read_u32(np, "vsync-len", &vsync_len);
	ret |= of_property_read_u32(np, "clock-frequency", &pixelclock);
	ret |= of_property_read_u32(np, "vrefresh", &vrefresh);
	if (ret) {
		pr_err("%pOF: error reading timing properties\n", np);
		return -EINVAL;
	}

	dm->clock = pixelclock / 1000;
	dm->hdisplay = hactive;
	dm->hsync_start = hactive + hback_porch;
	dm->hsync_end = hactive + hback_porch + hsync_len;
	dm->htotal = hactive + hback_porch + hsync_len + hfront_porch;
	dm->vdisplay = vactive;
	dm->vsync_start = vactive + vback_porch;
	dm->vsync_end = vactive + vback_porch + vsync_len;
	dm->vtotal = vactive + vback_porch + vsync_len + vfront_porch;	
	dm->vrefresh = vrefresh;
	dm->width_mm = 68;
	dm->height_mm =121;
	dm->flags = DRM_MODE_FLAG_NHSYNC |
		 DRM_MODE_FLAG_NVSYNC;
	return 0;
}

static inline struct ek79007 *panel_to_ek79007(struct drm_panel *panel)
{
	return container_of(panel, struct ek79007, panel);
}

static int mipi_dsi_dcs_write_memory_start(struct mipi_dsi_device *dsi)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_MEMORY_START, NULL, 0);
	if (err < 0)
		return err;

	return 0;
}

static void ek79007_dcs_write_buf(struct ek79007 *ctx, const void *data,
				  size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int err;

	err = mipi_dsi_dcs_write_buffer(dsi, data, len);
	if (err < 0)
		DRM_ERROR_RATELIMITED("MIPI DSI DCS write buffer failed: %d\n",
				      err);
}

#define dcs_write_seq(ctx, seq...)				\
({								\
	static const u8 d[] = { seq }; \
	ek79007_dcs_write_buf(ctx, d, ARRAY_SIZE(d));\
})

static int ek79007_init_sequence(struct ek79007 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret = 0; 
    dcs_write_seq(ctx, 0x80, 0xAC);
	dcs_write_seq(ctx, 0x81, 0xB8);
	dcs_write_seq(ctx, 0x82, 0x09);
	dcs_write_seq(ctx, 0x83, 0x78);
	dcs_write_seq(ctx, 0x84, 0x7F);
	dcs_write_seq(ctx, 0x85, 0xBB);
	dcs_write_seq(ctx, 0x86, 0x70);
	dcs_write_seq(ctx, 0x11);
	dcs_write_seq(ctx, 0x29);

	ret = mipi_dsi_dcs_set_column_address(dsi, 0,
					      default_mode.hdisplay - 1);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_set_page_address(dsi, 0, default_mode.vdisplay - 1);
	if (ret)
		return ret;	

	return 0;
}


static int ek79007_disable(struct drm_panel *panel)
{
    struct ek79007 *ctx = panel_to_ek79007(panel); 
   
	if (!ctx->enabled)
		return 0;

	backlight_disable(ctx->backlight);

	ctx->enabled = false;

	return 0;
}

static int ek79007_unprepare(struct drm_panel *panel)
{
	struct ek79007 *ctx = panel_to_ek79007(panel);

	if (!ctx->prepared)
		return 0;

	regulator_disable(ctx->supply);

	ctx->prepared = false;
    
	return 0;
}

static int ek79007_prepare(struct drm_panel *panel)
{ 
	struct ek79007 *ctx = panel_to_ek79007(panel);
	int ret = 0;
	ret = regulator_enable(ctx->supply);
	if (ret < 0) {
		DRM_ERROR("failed to enable supply: %d\n", ret);
		return ret;
	}
	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(20);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		msleep(100);
	}

	ctx->prepared = true;

	return 0;
}

static int ek79007_enable(struct drm_panel *panel)
{ 
	struct ek79007 *ctx = panel_to_ek79007(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;      
	if (ctx->enabled)
		return 0;

	ek79007_init_sequence(ctx);

    msleep(100);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	msleep(120);

    ret = mipi_dsi_dcs_write_memory_start(dsi);
 	if (ret)
		return ret; 

	backlight_enable(ctx->backlight);

	ctx->enabled = true;
    
	return 0;
}

static const u32 ctx_bus_formats[] = {
	MEDIA_BUS_FMT_RGB565_1X16,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RGB888_1X24,
};

static const u32 ctx_bus_flags = DISPLAY_FLAGS_DE_LOW  |
				 DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE;
static int ek79007_get_modes(struct drm_panel *panel)
{ 
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		DRM_ERROR("failed to add mode %ux%ux@%u\n",
			  default_mode.hdisplay, default_mode.vdisplay,
			  default_mode.vrefresh);
		return -ENOMEM;
	}
	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm = mode->width_mm;
	panel->connector->display_info.height_mm = mode->height_mm;
 	panel->connector->display_info.bus_flags = ctx_bus_flags;

	drm_display_info_set_bus_formats(&panel->connector->display_info,
					 ctx_bus_formats,
					 ARRAY_SIZE(ctx_bus_formats));  
	return 1;
}

static const struct drm_panel_funcs ek79007_drm_funcs = {
	.disable = ek79007_disable,
	.unprepare = ek79007_unprepare,
	.prepare = ek79007_prepare,
	.enable = ek79007_enable,
	.get_modes = ek79007_get_modes,
};


static int ek79007_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ek79007 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	of_parse_display_timing(dev->of_node, &default_mode);
	
	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(dev, "cannot get reset GPIO: %d\n", ret);
		return ret;
	}

	ctx->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(ctx->supply)) {
		ret = PTR_ERR(ctx->supply);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "cannot get regulator: %d\n", ret);
		return ret;
	}

	ctx->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(ctx->backlight))
		return PTR_ERR(ctx->backlight);

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |MIPI_DSI_MODE_VIDEO_BURST;
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &ek79007_drm_funcs;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach() failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}    

    dev_info(&dsi->dev, "%s\n", __FUNCTION__);

    return 0;
}

static int ek79007_remove(struct mipi_dsi_device *dsi)
{
	struct ek79007 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id ebf_ek79007_of_match[] = {
	{ .compatible = "ebf,ek79007" },
	{ }
};
MODULE_DEVICE_TABLE(of, ebf_ek79007_of_match);

static struct mipi_dsi_driver ebf_ek79007_driver = {
	.probe = ek79007_probe,
	.remove = ek79007_remove,
	.driver = {
		.name = "panel-ebf-ek79007",
		.of_match_table = ebf_ek79007_of_match,
	},
};
module_mipi_dsi_driver(ebf_ek79007_driver);

MODULE_AUTHOR("Embedfire <embedfire@embedfire.com>");
MODULE_DESCRIPTION("DRM Driver for ebf ek79007 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
