#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/display_timing.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>


struct jd9365da {
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

static inline struct jd9365da *panel_to_jd9365da(struct drm_panel *panel)
{
	return container_of(panel, struct jd9365da, panel);
}

static int mipi_dsi_dcs_write_memory_start(struct mipi_dsi_device *dsi)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_MEMORY_START, NULL, 0);
	if (err < 0)
		return err;

	return 0;
}

static void jd9365da_dcs_write_buf(struct jd9365da *ctx, const void *data,
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
	jd9365da_dcs_write_buf(ctx, d, ARRAY_SIZE(d));\
})

static int jd9365da_init_sequence(struct jd9365da *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret = 0; 
	dcs_write_seq(ctx, 0xE0, 0x00);
	dcs_write_seq(ctx, 0xE1, 0x93);
	dcs_write_seq(ctx, 0xE2, 0x65);
	dcs_write_seq(ctx, 0xE3, 0xF8);
	dcs_write_seq(ctx, 0x80, 0x03);
	dcs_write_seq(ctx, 0xE0, 0x01);
	dcs_write_seq(ctx, 0x00, 0x00);
	dcs_write_seq(ctx, 0x01, 0x3B);
	dcs_write_seq(ctx, 0x0C, 0x74);
	dcs_write_seq(ctx, 0x17, 0x00);
	dcs_write_seq(ctx, 0x18, 0xAF);
	dcs_write_seq(ctx, 0x19, 0x00);
	dcs_write_seq(ctx, 0x1A, 0x00);
	dcs_write_seq(ctx, 0x1B, 0xAF);
	dcs_write_seq(ctx, 0x1C, 0x00);
	dcs_write_seq(ctx, 0x35, 0x26);
	dcs_write_seq(ctx, 0x37, 0x09);
	dcs_write_seq(ctx, 0x38, 0x04);
	dcs_write_seq(ctx, 0x39, 0x00);
	dcs_write_seq(ctx, 0x3A, 0x01);
	dcs_write_seq(ctx, 0x3C, 0x78);
	dcs_write_seq(ctx, 0x3D, 0xFF);
	dcs_write_seq(ctx, 0x3E, 0xFF);
	dcs_write_seq(ctx, 0x3F, 0x7F);
	dcs_write_seq(ctx, 0x40, 0x06);
	dcs_write_seq(ctx, 0x41, 0xA0);
	dcs_write_seq(ctx, 0x42, 0x81);
	dcs_write_seq(ctx, 0x43, 0x1E);
	dcs_write_seq(ctx, 0x44, 0x0B);
	dcs_write_seq(ctx, 0x45, 0x28);
	dcs_write_seq(ctx, 0x55, 0x02);
	dcs_write_seq(ctx, 0x57, 0x69);
	dcs_write_seq(ctx, 0x59, 0x0A);
	dcs_write_seq(ctx, 0x5A, 0x2A);
	dcs_write_seq(ctx, 0x5B, 0x17);
	dcs_write_seq(ctx, 0x5D, 0x7F);
	dcs_write_seq(ctx, 0x5E, 0x6B);
	dcs_write_seq(ctx, 0x5F, 0x5C);
	dcs_write_seq(ctx, 0x60, 0x4F);
	dcs_write_seq(ctx, 0x61, 0x4D);
	dcs_write_seq(ctx, 0x62, 0x3F);
	dcs_write_seq(ctx, 0x63, 0x42);
	dcs_write_seq(ctx, 0x64, 0x2B);
	dcs_write_seq(ctx, 0x65, 0x44);
	dcs_write_seq(ctx, 0x66, 0x43);
	dcs_write_seq(ctx, 0x67, 0x43);
	dcs_write_seq(ctx, 0x68, 0x63);
	dcs_write_seq(ctx, 0x69, 0x52);
	dcs_write_seq(ctx, 0x6A, 0x5A);
	dcs_write_seq(ctx, 0x6B, 0x4F);
	dcs_write_seq(ctx, 0x6C, 0x4E);
	dcs_write_seq(ctx, 0x6D, 0x20);
	dcs_write_seq(ctx, 0x6E, 0x0F);
	dcs_write_seq(ctx, 0x6F, 0x00);
	dcs_write_seq(ctx, 0x70, 0x7F);
	dcs_write_seq(ctx, 0x71, 0x6B);
	dcs_write_seq(ctx, 0x72, 0x5C);
	dcs_write_seq(ctx, 0x73, 0x4F);
	dcs_write_seq(ctx, 0x74, 0x4D);
	dcs_write_seq(ctx, 0x75, 0x3F);
	dcs_write_seq(ctx, 0x76, 0x42);
	dcs_write_seq(ctx, 0x77, 0x2B);
	dcs_write_seq(ctx, 0x78, 0x44);
	dcs_write_seq(ctx, 0x79, 0x43);
	dcs_write_seq(ctx, 0x7A, 0x43);
	dcs_write_seq(ctx, 0x7B, 0x63);
	dcs_write_seq(ctx, 0x7C, 0x52);
	dcs_write_seq(ctx, 0x7D, 0x5A);
	dcs_write_seq(ctx, 0x7E, 0x4F);
	dcs_write_seq(ctx, 0x7F, 0x4E);
	dcs_write_seq(ctx, 0x80, 0x20);
	dcs_write_seq(ctx, 0x81, 0x0F);
	dcs_write_seq(ctx, 0x82, 0x00);
	dcs_write_seq(ctx, 0xE0, 0x02);
	dcs_write_seq(ctx, 0x00, 0x42);
	dcs_write_seq(ctx, 0x01, 0x42);
	dcs_write_seq(ctx, 0x02, 0x40);
	dcs_write_seq(ctx, 0x03, 0x40);
	dcs_write_seq(ctx, 0x04, 0x5E);
	dcs_write_seq(ctx, 0x05, 0x5E);
	dcs_write_seq(ctx, 0x06, 0x5F);
	dcs_write_seq(ctx, 0x07, 0x5F);
	dcs_write_seq(ctx, 0x08, 0x5F);
	dcs_write_seq(ctx, 0x09, 0x57);
	dcs_write_seq(ctx, 0x0A, 0x57);
	dcs_write_seq(ctx, 0x0B, 0x77);
	dcs_write_seq(ctx, 0x0C, 0x77);
	dcs_write_seq(ctx, 0x0D, 0x47);
	dcs_write_seq(ctx, 0x0E, 0x47);
	dcs_write_seq(ctx, 0x0F, 0x45);
	dcs_write_seq(ctx, 0x10, 0x45);
	dcs_write_seq(ctx, 0x11, 0x4B);
	dcs_write_seq(ctx, 0x12, 0x4B);
	dcs_write_seq(ctx, 0x13, 0x49);
	dcs_write_seq(ctx, 0x14, 0x49);
	dcs_write_seq(ctx, 0x15, 0x5F);
	dcs_write_seq(ctx, 0x16, 0x41);
	dcs_write_seq(ctx, 0x17, 0x41);
	dcs_write_seq(ctx, 0x18, 0x40);
	dcs_write_seq(ctx, 0x19, 0x40);
	dcs_write_seq(ctx, 0x1A, 0x5E);
	dcs_write_seq(ctx, 0x1B, 0x5E);
	dcs_write_seq(ctx, 0x1C, 0x5F);
	dcs_write_seq(ctx, 0x1D, 0x5F);
	dcs_write_seq(ctx, 0x1E, 0x5F);
	dcs_write_seq(ctx, 0x1F, 0x57);
	dcs_write_seq(ctx, 0x20, 0x57);
	dcs_write_seq(ctx, 0x21, 0x77);
	dcs_write_seq(ctx, 0x22, 0x77);
	dcs_write_seq(ctx, 0x23, 0x46);
	dcs_write_seq(ctx, 0x24, 0x46);
	dcs_write_seq(ctx, 0x25, 0x44);
	dcs_write_seq(ctx, 0x26, 0x44);
	dcs_write_seq(ctx, 0x27, 0x4A);
	dcs_write_seq(ctx, 0x28, 0x4A);
	dcs_write_seq(ctx, 0x29, 0x48);
	dcs_write_seq(ctx, 0x2A, 0x48);
	dcs_write_seq(ctx, 0x2B, 0x5F);
	dcs_write_seq(ctx, 0x2C, 0x01);
	dcs_write_seq(ctx, 0x2D, 0x01);
	dcs_write_seq(ctx, 0x2E, 0x00);
	dcs_write_seq(ctx, 0x2F, 0x00);
	dcs_write_seq(ctx, 0x30, 0x1F);
	dcs_write_seq(ctx, 0x31, 0x1F);
	dcs_write_seq(ctx, 0x32, 0x1E);
	dcs_write_seq(ctx, 0x33, 0x1E);
	dcs_write_seq(ctx, 0x34, 0x1F);
	dcs_write_seq(ctx, 0x35, 0x17);
	dcs_write_seq(ctx, 0x36, 0x17);
	dcs_write_seq(ctx, 0x37, 0x37);
	dcs_write_seq(ctx, 0x38, 0x37);
	dcs_write_seq(ctx, 0x39, 0x08);
	dcs_write_seq(ctx, 0x3A, 0x08);
	dcs_write_seq(ctx, 0x3B, 0x0A);
	dcs_write_seq(ctx, 0x3C, 0x0A);
	dcs_write_seq(ctx, 0x3D, 0x04);
	dcs_write_seq(ctx, 0x3E, 0x04);
	dcs_write_seq(ctx, 0x3F, 0x06);
	dcs_write_seq(ctx, 0x40, 0x06);
	dcs_write_seq(ctx, 0x41, 0x1F);
	dcs_write_seq(ctx, 0x42, 0x02);
	dcs_write_seq(ctx, 0x43, 0x02);
	dcs_write_seq(ctx, 0x44, 0x00);
	dcs_write_seq(ctx, 0x45, 0x00);
	dcs_write_seq(ctx, 0x46, 0x1F);
	dcs_write_seq(ctx, 0x47, 0x1F);
	dcs_write_seq(ctx, 0x48, 0x1E);
	dcs_write_seq(ctx, 0x49, 0x1E);
	dcs_write_seq(ctx, 0x4A, 0x1F);
	dcs_write_seq(ctx, 0x4B, 0x17);
	dcs_write_seq(ctx, 0x4C, 0x17);
	dcs_write_seq(ctx, 0x4D, 0x37);
	dcs_write_seq(ctx, 0x4E, 0x37);
	dcs_write_seq(ctx, 0x4F, 0x09);
	dcs_write_seq(ctx, 0x50, 0x09);
	dcs_write_seq(ctx, 0x51, 0x0B);
	dcs_write_seq(ctx, 0x52, 0x0B);
	dcs_write_seq(ctx, 0x53, 0x05);
	dcs_write_seq(ctx, 0x54, 0x05);
	dcs_write_seq(ctx, 0x55, 0x07);
	dcs_write_seq(ctx, 0x56, 0x07);
	dcs_write_seq(ctx, 0x57, 0x1F);
	dcs_write_seq(ctx, 0x58, 0x40);
	dcs_write_seq(ctx, 0x5B, 0x30);
	dcs_write_seq(ctx, 0x5C, 0x00);
	dcs_write_seq(ctx, 0x5D, 0x34);
	dcs_write_seq(ctx, 0x5E, 0x05);
	dcs_write_seq(ctx, 0x5F, 0x02);
	dcs_write_seq(ctx, 0x63, 0x00);
	dcs_write_seq(ctx, 0x64, 0x6A);
	dcs_write_seq(ctx, 0x67, 0x73);
	dcs_write_seq(ctx, 0x68, 0x05);
	dcs_write_seq(ctx, 0x69, 0x08);
	dcs_write_seq(ctx, 0x6A, 0x6A);
	dcs_write_seq(ctx, 0x6B, 0x08);
	dcs_write_seq(ctx, 0x6C, 0x00);
	dcs_write_seq(ctx, 0x6D, 0x00);
	dcs_write_seq(ctx, 0x6E, 0x00);
	dcs_write_seq(ctx, 0x6F, 0x88);
	dcs_write_seq(ctx, 0x75, 0xFF);
	dcs_write_seq(ctx, 0x77, 0xDD);
	dcs_write_seq(ctx, 0x78, 0x2A);
	dcs_write_seq(ctx, 0x79, 0x15);
	dcs_write_seq(ctx, 0x7A, 0x17);
	dcs_write_seq(ctx, 0x7D, 0x14);
	dcs_write_seq(ctx, 0x7E, 0x82);
	dcs_write_seq(ctx, 0xE0, 0x04);
	dcs_write_seq(ctx, 0x00, 0x0E);
	dcs_write_seq(ctx, 0x02, 0xB3);
	dcs_write_seq(ctx, 0x09, 0x61);
	dcs_write_seq(ctx, 0x0E, 0x48);
	dcs_write_seq(ctx, 0xE0, 0x00);
	dcs_write_seq(ctx, 0xE6, 0x02);
	dcs_write_seq(ctx, 0xE7, 0x0C);
	dcs_write_seq(ctx, 0x11, 0x00);
	msleep(120);
	dcs_write_seq(ctx, 0xE0, 0x00);
	dcs_write_seq(ctx, 0x29, 0x00);
	msleep(5);
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


static int jd9365da_disable(struct drm_panel *panel)
{
    struct jd9365da *ctx = panel_to_jd9365da(panel); 
   
	if (!ctx->enabled)
		return 0;

	backlight_disable(ctx->backlight);

	ctx->enabled = false;

	return 0;
}

static int jd9365da_unprepare(struct drm_panel *panel)
{
	struct jd9365da *ctx = panel_to_jd9365da(panel);

	if (!ctx->prepared)
		return 0;

	regulator_disable(ctx->supply);

	ctx->prepared = false;
    
	return 0;
}

static int jd9365da_prepare(struct drm_panel *panel)
{ 
	struct jd9365da *ctx = panel_to_jd9365da(panel);
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

static int jd9365da_enable(struct drm_panel *panel)
{ 
	struct jd9365da *ctx = panel_to_jd9365da(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;      
	if (ctx->enabled)
		return 0;

	jd9365da_init_sequence(ctx);

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
static int jd9365da_get_modes(struct drm_panel *panel)
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

static const struct drm_panel_funcs jd9365da_drm_funcs = {
	.disable = jd9365da_disable,
	.unprepare = jd9365da_unprepare,
	.prepare = jd9365da_prepare,
	.enable = jd9365da_enable,
	.get_modes = jd9365da_get_modes,
};


static int jd9365da_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct jd9365da *ctx;
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
	ctx->panel.funcs = &jd9365da_drm_funcs;

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

static int jd9365da_remove(struct mipi_dsi_device *dsi)
{
	struct jd9365da *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id ebf_jd9365da_of_match[] = {
	{ .compatible = "ebf,jd9365da" },
	{ }
};
MODULE_DEVICE_TABLE(of, ebf_jd9365da_of_match);

static struct mipi_dsi_driver ebf_jd9365da_driver = {
	.probe = jd9365da_probe,
	.remove = jd9365da_remove,
	.driver = {
		.name = "panel-ebf-jd9365da",
		.of_match_table = ebf_jd9365da_of_match,
	},
};
module_mipi_dsi_driver(ebf_jd9365da_driver);

MODULE_AUTHOR("Embedfire <embedfire@embedfire.com>");
MODULE_DESCRIPTION("DRM Driver for ebf jd9365da MIPI DSI panel");
MODULE_LICENSE("GPL v2");
