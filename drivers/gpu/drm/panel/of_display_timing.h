#ifndef __LINUX_OF_DISPLAY_TIMING_H
#define __LINUX_OF_DISPLAY_TIMING_H

#ifdef CONFIG_OF
int of_parse_display_timing(const struct device_node *np,
		struct drm_display_mode *dm);

#else
static inline int of_parse_display_timing(const struct device_node *np,
		struct drm_display_mode *dm)
{
	return -ENOSYS;
}
#endif

#endif