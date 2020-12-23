/*
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

#ifndef __NWL_DSI__
#define __NWL_DSI__

#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <linux/phy/phy.h>

/* RGB bit distribution as specified by the DPI specification */
enum dpi_pixel_format {
	DPI_16_BIT_565_PACKED,
	DPI_16_BIT_565_ALIGNED,
	DPI_16_BIT_565_SHIFTED,
	DPI_18_BIT_PACKED,
	DPI_18_BIT_ALIGNED,
	DPI_24_BIT
};

/*
 * Just some helper functions to add/remove a bridge into/from encoder bridge
 * chain.
 */
bool nwl_dsi_add_bridge(struct drm_encoder *encoder,
			struct drm_bridge *next_bridge);

bool nwl_dsi_del_bridge(struct drm_encoder *encoder,
			struct drm_bridge *bridge);

unsigned long nwl_dsi_get_bit_clock(struct drm_bridge *bridge,
	unsigned long pixclock);

#endif /* __NWL_DSI_H__ */
