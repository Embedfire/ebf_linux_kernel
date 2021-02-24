#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/display_timing.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>



/**
 * of_parse_display_timing - parse display_timing entry from device_node
 * @np: device_node with the properties
 **/
int of_parse_display_timing(const struct device_node *np,
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
#if 0
    pr_info("hback_porch %d ", hback_porch);
    pr_info("hfront_porch %d ", hfront_porch);
    pr_info("hsync_len %d ", hsync_len);
#endif
	dm->clock = pixelclock / 1000;
	dm->hdisplay = hactive;
	dm->hsync_start = hactive + hback_porch;
	dm->hsync_end = hactive + hback_porch + hfront_porch;
	dm->htotal = hactive + hback_porch + hsync_len + hfront_porch;
	dm->vdisplay = vactive;
	dm->vsync_start = vactive + vback_porch;
	dm->vsync_end = vactive + vback_porch + vfront_porch;
	dm->vtotal = vactive + vback_porch + vsync_len + vfront_porch;	
	dm->vrefresh = vrefresh;
#if 0
    pr_info("clock：%d ", dm->clock);
    pr_info("hdisplay %d ", dm->hdisplay);
    pr_info("hsync_start %d ", dm->hsync_start);
    pr_info("hsync_end %d ", dm->hsync_end);
    pr_info("htotal %d ", dm->htotal);
    pr_info("vdisplay %d ", dm->vdisplay);
    pr_info("vsync_start %d ", dm->vsync_start);
    pr_info("vsync_end %d ", dm->vsync_end);
    pr_info("vtotal %d ", dm->vtotal);
    pr_info("vrefresh %d ", dm->vrefresh);
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(of_parse_display_timing);
