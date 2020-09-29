#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

static int hx8394_probe(struct mipi_dsi_device *dsi)
{
    dev_info(dev, "%s\n", __FUNC__);
    return 0;
}

static int hx8394_remove(struct mipi_dsi_device *dsi)
{
	/*struct rm68200 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
    */
	return 0;
}

static const struct of_device_id himax_hx8394_of_match[] = {
	{ .compatible = "himax,hx8394" },
	{ }
};
MODULE_DEVICE_TABLE(of, himax_hx8394_of_match);

static struct mipi_dsi_driver himax_hx8394_driver = {
	.probe = hx8394_probe,
	.remove = hx8394_remove,
	.driver = {
		.name = "panel-himax-hx8394",
		.of_match_table = himax_hx8394_of_match,
	},
};
module_mipi_dsi_driver(himax_hx8394_driver);

MODULE_AUTHOR("Embedfire <embedfire@embedfire.com>");
MODULE_DESCRIPTION("DRM Driver for Himax hx8394 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
