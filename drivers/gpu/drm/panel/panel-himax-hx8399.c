#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/display_timing.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>


/* User define command list (available only set “SETEXC” command)*/
#define CMD_SETMADCTL   0x36 /* Set Memory access control */
#define CMD_SETAUTO     0xB0 /* Set sequence */
#define CMD_SETPOWER    0xB1 /* Set power */
#define CMD_SETDISP     0xB2 /* Set display related register */
#define CMD_SETCYC      0xB4 /* Set display waveform cycles */
#define CMD_SETVCOM     0xB6 /* Set VCOM voltage */
#define CMD_SETTE       0xB7 /* Set internal TE function */
#define CMD_SETSENSOR   0xB8 /* Set temperature sensor */
#define CMD_SETEXTC     0xB9 /* Set extension command */
#define CMD_SETMIPI     0xBA /* Set MIPI control */
#define CMD_SETOTP      0xBB /* Set OTP */ 
#define CMD_SET_BANK     0xBD /* Set register bank */
#define CMD_SETDGCLUT   0xC1 /* Set DGC LUT */
#define CMD_SETID       0xC3 /* Set ID */
#define CMD_SETDDB      0xC4 /* Set DDB */
#define CMD_SETCABC     0xC9 /* Set CABC control */
#define CMD_SETCABCGAIN 0xCA /* Set CABCGAIN */
#define CMD_SETPANEL    0xCC /* SETPANEL */
#define CMD_SETOFFSET   0xD2 /* Set OFFSET */
#define CMD_SETGIP_0    0xD3 /* Set GIP Option0 */ 
#define CMD_SETGIP_1    0xD5 /* Set GIP Option1 */
#define CMD_SETGIP_2    0xD6 /* Set GIP Option2 */
#define CMD_SETGIP_3    0xD8 /* Set GIP Option3 */
#define CMD_SETGPO      0xD9 /* Set GPO */
#define CMD_SETSCALING  0xDD /* Set SCALING */
#define CMD_SET1BPP     0xDF /* Set 1BPP */
#define CMD_SETGAMMA    0xE0 /* Set gamma curve related setting */
#define CMD_SETCHEMODE_DYN  0xE4 /* Set CHEMODE_DYN */
#define CMD_SETCHE      0xE5 
#define CMD_SETCESEL    0xE6 /* Enable color enhance */
#define CMD_SET_SP_CMD  0xE9 /* SET_SP_CMD */ 
#define CMD_SETREADINDEX 0xFE /* Set SPI Read Index */
#define CMD_GETSPIREAD  0xFF /* SPI Read Command Data */


struct hx8399 {
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

static inline struct hx8399 *panel_to_hx8399(struct drm_panel *panel)
{
	return container_of(panel, struct hx8399, panel);
}

static int mipi_dsi_dcs_write_memory_start(struct mipi_dsi_device *dsi)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_MEMORY_START, NULL, 0);
	if (err < 0)
		return err;

	return 0;
}

static void hx8399_dcs_write_buf(struct hx8399 *ctx, const void *data,
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
	hx8399_dcs_write_buf(ctx, d, ARRAY_SIZE(d));\
})

static int hx8399_init_sequence(struct hx8399 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret = 0; 
    /* set extended command set access enable */
    dcs_write_seq(ctx, CMD_SETEXTC, 0xFF, 0x83, 0x99);
	/* set offset */
	dcs_write_seq(ctx, CMD_SETOFFSET, 0x77);
    /* set power */
    dcs_write_seq(ctx, CMD_SETPOWER, 0x02, 0x04, 0x74, 0x94, 0x01, 0x32,
                    0x33, 0x11, 0x11, 0xAB, 0x4D, 0x56, 0x73, 0x02, 0x02);
    /* 4 LANE */
    dcs_write_seq(ctx, CMD_SETMIPI, 0x63, 0x03);
	/* set display */
    dcs_write_seq(ctx, CMD_SETDISP, 0x00, 0x80, 0x80, 0xAE, 0x05, 0x07,
					0x5A, 0x11, 0x00, 0x00, 0x10, 0x1E, 0x70, 0x03, 0xD4);
    /* Set Memory access control */
    dcs_write_seq(ctx, CMD_SETMADCTL, 0x02);
    /* set cycles */
    dcs_write_seq(ctx, CMD_SETCYC, 0x00, 0xFF, 0x02, 0xC0, 0x02, 0xC0, 0x00,
                    0x00, 0x08, 0x00, 0x04, 0x06, 0x00, 0x32, 0x04, 0x0A, 0x08, 0x21, 0x03, 0x01, 0x00,
                    0x0F, 0xB8, 0x8B, 0x02, 0xC0, 0x02, 0xC0, 0x00, 0x00, 0x08, 0x00, 0x04, 0x06, 0x00, 
                    0x32, 0x04, 0x0A, 0x08, 0x01, 0x00, 0x0F, 0xB8, 0x01);

    dcs_write_seq(ctx, CMD_SETGIP_0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
					0x00, 0x00, 0x10, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
					0x00, 0x00, 0x01, 0x00, 0x05, 0x05, 0x07, 0x00, 0x00, 0x00, 0x05, 0x40);
	msleep(5);
    dcs_write_seq(ctx, CMD_SETGIP_1, 0x18, 0x18, 0x19, 0x19, 0x18, 0x18, 0x21,
					0x20, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x18, 0x18, 0x18, 0x18, 0x18,
					0x18, 0x2F, 0x2F, 0x30, 0x30, 0x31, 0x31, 0x18, 0x18, 0x18, 0x18);
	msleep(5);
    dcs_write_seq(ctx, CMD_SETGIP_2, 0x18, 0x18, 0x19, 0x19, 0x40, 0x40, 0x20, 
		            0x21, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x40, 0x40, 0x40, 0x40, 0x40,
		            0x40, 0x2F, 0x2F, 0x30, 0x30, 0x31, 0x31, 0x40, 0x40, 0x40, 0x40);
	msleep(5);
	dcs_write_seq(ctx, CMD_SETGIP_3, 0xA2, 0xAA, 0x02, 0xA0, 0xA2, 0xA8, 0x02,
		            0xA0, 0xB0, 0x00, 0x00, 0x00, 0xB0, 0x00, 0x00, 0x00);
	dcs_write_seq(ctx, CMD_SET_BANK, 0x01);
	dcs_write_seq(ctx, CMD_SETGIP_3, 0xB0, 0x00, 0x00, 0x00, 0xB0, 0x00, 0x00,
					0x00, 0xE2, 0xAA, 0x03, 0xF0, 0xE2, 0xAA, 0x03, 0xF0);
	dcs_write_seq(ctx, CMD_SET_BANK, 0x02);
	dcs_write_seq(ctx, CMD_SETGIP_3, 0xE2, 0xAA, 0x03, 0xF0, 0xE2, 0xAA, 0x03, 0xF0);
	dcs_write_seq(ctx, CMD_SET_BANK, 0x00);
	dcs_write_seq(ctx, CMD_SETVCOM, 0x8D, 0x8D);	
    dcs_write_seq(ctx, CMD_SETGAMMA, 0x00, 0x0E, 0x19, 0x13, 0x2E, 0x39, 0x48, 
		            0x44, 0x4D, 0x57, 0x5F, 0x66, 0x6C, 0x76, 0x7F, 0x85, 0x8A, 0x95, 0x9A, 0xA4, 0x9B,
		            0xAB, 0xB0, 0x5C, 0x58, 0x64, 0x77, 0x00, 0x0E, 0x19, 0x13, 0x2E, 0x39, 0x48, 0x44,
		            0x4D, 0x57, 0x5F, 0x66, 0x6C, 0x76, 0x7F, 0x85, 0x8A, 0x95, 0x9A, 0xA4, 0x9B, 0xAB,
		            0xB0, 0x5C, 0x58, 0x64, 0x77);

	ret = mipi_dsi_dcs_set_column_address(dsi, 0,
					      default_mode.hdisplay - 1);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_set_page_address(dsi, 0, default_mode.vdisplay - 1);
	if (ret)
		return ret;	

	return 0;
}


static int hx8399_disable(struct drm_panel *panel)
{
    struct hx8399 *ctx = panel_to_hx8399(panel); 
   
	if (!ctx->enabled)
		return 0;

	backlight_disable(ctx->backlight);

	ctx->enabled = false;

	return 0;
}

static int hx8399_unprepare(struct drm_panel *panel)
{
	struct hx8399 *ctx = panel_to_hx8399(panel);

	if (!ctx->prepared)
		return 0;

	regulator_disable(ctx->supply);

	ctx->prepared = false;
    
	return 0;
}

static int hx8399_prepare(struct drm_panel *panel)
{ 
	struct hx8399 *ctx = panel_to_hx8399(panel);
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

static int hx8399_enable(struct drm_panel *panel)
{ 
	struct hx8399 *ctx = panel_to_hx8399(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;      
	if (ctx->enabled)
		return 0;

	hx8399_init_sequence(ctx);

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
static int hx8399_get_modes(struct drm_panel *panel)
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

static const struct drm_panel_funcs hx8399_drm_funcs = {
	.disable = hx8399_disable,
	.unprepare = hx8399_unprepare,
	.prepare = hx8399_prepare,
	.enable = hx8399_enable,
	.get_modes = hx8399_get_modes,
};


static int hx8399_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct hx8399 *ctx;
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
	ctx->panel.funcs = &hx8399_drm_funcs;

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

static int hx8399_remove(struct mipi_dsi_device *dsi)
{
	struct hx8399 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id himax_hx8399_of_match[] = {
	{ .compatible = "himax,hx8399" },
	{ }
};
MODULE_DEVICE_TABLE(of, himax_hx8399_of_match);

static struct mipi_dsi_driver himax_hx8399_driver = {
	.probe = hx8399_probe,
	.remove = hx8399_remove,
	.driver = {
		.name = "panel-himax-hx8399",
		.of_match_table = himax_hx8399_of_match,
	},
};
module_mipi_dsi_driver(himax_hx8399_driver);

MODULE_AUTHOR("Embedfire <embedfire@embedfire.com>");
MODULE_DESCRIPTION("DRM Driver for Himax hx8399 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
