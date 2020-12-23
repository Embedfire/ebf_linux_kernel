/*
 * Copyright 2017-2018 NXP
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
#include <linux/kernel.h>
#ifdef DEBUG_FW_LOAD
#include "mhdp_firmware.h"
#endif
#include "imx-hdp.h"
#include "imx-hdmi.h"
#include "imx-dp.h"

#ifdef DEBUG_FW_LOAD
void dp_fw_load(state_struct *state)
{
	DRM_INFO("loading hdmi firmware\n");
	CDN_API_LoadFirmware(state,
		(u8 *)mhdp_iram0_get_ptr(),
		mhdp_iram0_get_size(),
		(u8 *)mhdp_dram0_get_ptr(),
		mhdp_dram0_get_size());
}
#endif
int dp_fw_init(state_struct *state)
{
	u8 echo_msg[] = "echo test";
	u8 echo_resp[sizeof(echo_msg) + 1];
	struct imx_hdp *hdp = state_to_imx_hdp(state);
	u32 core_rate;
	int ret;
	u8 resp;

	core_rate = clk_get_rate(hdp->clks.clk_core);

	/* configure the clock */
	CDN_API_SetClock(state, core_rate/1000000);
	pr_info("CDN_API_SetClock completed\n");

	cdn_apb_write(state, APB_CTRL << 2, 0);
	DRM_INFO("Started firmware!\n");

	ret = CDN_API_CheckAlive_blocking(state);
	if (ret != 0) {
		DRM_ERROR("CDN_API_CheckAlive failed - check firmware!\n");
		return -ENXIO;
	}

	DRM_INFO("CDN_API_CheckAlive returned ret = %d\n", ret);

	/* turn on IP activity */
	ret = CDN_API_MainControl_blocking(state, 1, &resp);
	DRM_INFO("CDN_API_MainControl_blocking (ret = %d resp = %u)\n",
		ret, resp);

	ret = CDN_API_General_Test_Echo_Ext_blocking(state, echo_msg, echo_resp,
		sizeof(echo_msg), CDN_BUS_TYPE_APB);
	if (strncmp(echo_msg, echo_resp, sizeof(echo_msg)) != 0) {
		DRM_ERROR("CDN_API_General_Test_Echo_Ext_blocking - echo test failed, check firmware!");
		return -ENXIO;
	}
	DRM_INFO("CDN_API_General_Test_Echo_Ext_blocking (ret = %d echo_resp = %s)\n",
		ret, echo_resp);

	/* Line swaping */
	CDN_API_General_Write_Register_blocking(state,
						ADDR_SOURCD_PHY +
						(LANES_CONFIG << 2),
						0x00400000 | hdp->lane_mapping);
	DRM_INFO("CDN_API_General_Write_Register_blockin ... setting LANES_CONFIG\n");

	return 0;
}

int dp_phy_init(state_struct *state, struct drm_display_mode *mode, int format,
		int color_depth)
{
	struct imx_hdp *hdp = state_to_imx_hdp(state);
	int max_link_rate = hdp->link_rate;
	int num_lanes = 4;
	int ret;

	/* reset phy */
	imx_hdp_call(hdp, phy_reset, hdp->ipcHndl, NULL, 0);

	/* PHY initialization while phy reset pin is active */
	AFE_init(state, num_lanes, (ENUM_AFE_LINK_RATE)max_link_rate);
	DRM_INFO("AFE_init\n");

	/* In this point the phy reset should be deactivated */
	imx_hdp_call(hdp, phy_reset, hdp->ipcHndl, NULL, 1);
	DRM_INFO("deasserted reset\n");

	/* PHY power set */
	AFE_power(state, num_lanes, (ENUM_AFE_LINK_RATE)max_link_rate);
	DRM_INFO("AFE_power exit\n");

	/* Video off */
	ret = CDN_API_DPTX_SetVideo_blocking(state, 0);
	DRM_INFO("CDN_API_DPTX_SetVideo_blocking (ret = %d)\n", ret);

	return true;
}

#ifdef DEBUG
void print_header(void)
{
	/*       "0x00000000: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f"*/
	DRM_INFO("          : 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n"
		 );
	DRM_INFO("-----------------------------------------------------------\n"
		 );
}

void print_bytes(unsigned int addr, unsigned char *buf, unsigned int size)
{
	int i, index = 0;
	char line[160];

	if (((size + 11) * 3) > sizeof(line))
		return;

	index += sprintf(line, "0x%08x:", addr);
	for (i = 0; i < size; i++)
		index += sprintf(&line[index], " %02x", buf[i]);
	DRM_INFO("%s\n", line);

}

int dump_dpcd(state_struct *state)
{
	int ret;

	DPTX_Read_DPCD_response resp_dpcd;

	print_header();

	ret = CDN_API_DPTX_Read_DPCD_blocking(state, 0x10, 0x0, &resp_dpcd,
					      CDN_BUS_TYPE_APB);
	if (ret) {
		DRM_INFO("_debug: function returned with status %d\n", ret);
		return -1;
	}
	print_bytes(resp_dpcd.addr, resp_dpcd.buff, resp_dpcd.size);

	ret = CDN_API_DPTX_Read_DPCD_blocking(state, 0x10, 0x100, &resp_dpcd,
					      CDN_BUS_TYPE_APB);
	if (ret) {
		DRM_INFO("_debug: function returned with status %d\n", ret);
		return -1;
	}
	print_bytes(resp_dpcd.addr, resp_dpcd.buff, resp_dpcd.size);

	ret = CDN_API_DPTX_Read_DPCD_blocking(state, 0x10, 0x200, &resp_dpcd,
					      CDN_BUS_TYPE_APB);
	if (ret) {
		DRM_INFO("_debug: function returned with status %d\n", ret);
		return -1;
	}
	print_bytes(resp_dpcd.addr, resp_dpcd.buff, resp_dpcd.size);

	ret = CDN_API_DPTX_Read_DPCD_blocking(state, 0x10, 0x210, &resp_dpcd,
					      CDN_BUS_TYPE_APB);
	if (ret) {
		DRM_INFO("_debug: function returned with status %d\n", ret);
		return -1;
	}
	print_bytes(resp_dpcd.addr, resp_dpcd.buff, resp_dpcd.size);

	ret = CDN_API_DPTX_Read_DPCD_blocking(state, 0x10, 0x220, &resp_dpcd,
					      CDN_BUS_TYPE_APB);
	if (ret) {
		DRM_INFO("_debug: function returned with status %d\n", ret);
		return -1;
	}
	print_bytes(resp_dpcd.addr, resp_dpcd.buff, resp_dpcd.size);

	ret = CDN_API_DPTX_Read_DPCD_blocking(state, 0x10, 0x700, &resp_dpcd,
					      CDN_BUS_TYPE_APB);
	if (ret) {
		DRM_INFO("_debug: function returned with status %d\n", ret);
		return -1;
	}
	print_bytes(resp_dpcd.addr, resp_dpcd.buff, resp_dpcd.size);

	ret = CDN_API_DPTX_Read_DPCD_blocking(state, 0x10, 0x710, &resp_dpcd,
					      CDN_BUS_TYPE_APB);
	if (ret) {
		DRM_INFO("_debug: function returned with status %d\n", ret);
		return -1;
	}
	print_bytes(resp_dpcd.addr, resp_dpcd.buff, resp_dpcd.size);

	ret = CDN_API_DPTX_Read_DPCD_blocking(state, 0x10, 0x720, &resp_dpcd,
					      CDN_BUS_TYPE_APB);
	if (ret) {
		DRM_INFO("_debug: function returned with status %d\n", ret);
		return -1;
	}
	print_bytes(resp_dpcd.addr, resp_dpcd.buff, resp_dpcd.size);

	ret = CDN_API_DPTX_Read_DPCD_blocking(state, 0x10, 0x730, &resp_dpcd,
					      CDN_BUS_TYPE_APB);
	if (ret) {
		DRM_INFO("_debug: function returned with status %d\n", ret);
		return -1;
	}

	print_bytes(resp_dpcd.addr, resp_dpcd.buff, resp_dpcd.size);
	return 0;
}
#endif

int dp_get_training_status(state_struct *state)
{
	uint32_t evt;
	uint8_t eventId;
	uint8_t HPDevents;

	do {
		do {
			CDN_API_Get_Event(state, &evt);
			if (evt != 0)
				DRM_DEBUG("_Get_Event %d\n", evt);
		} while ((evt & 2) == 0);
		CDN_API_DPTX_ReadEvent_blocking(state, &eventId, &HPDevents);
		DRM_DEBUG("ReadEvent  ID = %d HPD = %d\n", eventId, HPDevents);

		switch (eventId) {
		case 0x01:
			DRM_INFO("INFO: Full link training started\n");
			break;
		case 0x02:
			DRM_INFO("INFO: Fast link training started\n");
			break;
		case 0x04:
			DRM_INFO("INFO: Clock recovery phase finished\n");
			break;
		case 0x08:
			DRM_INFO("INFO: Channel equalization phase finished (this is last part meaning training finished)\n");
			break;
		case 0x10:
			DRM_INFO("INFO: Fast link training finished\n");
			break;
		case 0x20:
			DRM_INFO("ERROR: Clock recovery phase failed\n");
			return -1;
		case 0x40:
			DRM_INFO("ERROR: Channel equalization phase failed\n");
			return -1;
		case 0x80:
			DRM_INFO("ERROR: Fast link training failed\n");
			return -1;
		default:
			DRM_INFO("ERROR: Invalid ID:%x\n", eventId);
			return -1;
		}
	} while (eventId != 0x08 && eventId != 0x10);

	return 0;
}

/* Max Link Rate: 06h (1.62Gbps), 0Ah (2.7Gbps), 14h (5.4Gbps),
 * 1Eh (8.1Gbps)--N/A
 */
void dp_mode_set(state_struct *state,
			struct drm_display_mode *mode,
			int format,
			int color_depth,
			int max_link_rate)
{
	struct imx_hdp *hdp = state_to_imx_hdp(state);
	int ret;
	u8 training_retries = 10;
	/* Set Host capabilities */
	/* Number of lanes and SSC */
	u8 num_lanes = 4;
	u8 ssc = 0;
	u8 scrambler = 0;
	/* Max voltage swing */
	u8 max_vswing = 3;
	u8 force_max_vswing = 0;
	/* Max pre-emphasis */
	u8 max_preemph = 2;
	u8 force_max_preemph = 0;
	/* Supported test patterns mask */
	u8 supp_test_patterns = 0x0F;
	/* AUX training? */
	u8 no_aux_training = 0;
	/* Lane mapping */
	u8 lane_mapping = hdp->lane_mapping; /*  we have 4 lane, so it's OK */

	/* Extended Host capabilities */
	u8 ext_host_cap = 1;
	/* Bits per sub-pixel */
	u8 bits_per_subpixel = 8;
	/* Stereoscopic video */
	STEREO_VIDEO_ATTR stereo = 0;
	/* B/W Balance Type: 0 no data, 1 IT601, 2 ITU709 */
	BT_TYPE bt_type = 0;
	/* Transfer Unit */
	u8 transfer_unit = 64;
	VIC_SYMBOL_RATE sym_rate;
	u8 link_rate;

	if (hdp->is_edp) {
		/* eDP uses device tree link rate and number of lanes */
		link_rate = hdp->edp_link_rate;
		num_lanes = hdp->edp_num_lanes;

		/* use the eDP supported rates */
		switch (max_link_rate) {
		case AFE_LINK_RATE_1_6:
			sym_rate = RATE_1_6;
			break;
		case AFE_LINK_RATE_2_1:
			sym_rate = RATE_2_1;
			break;
		case AFE_LINK_RATE_2_4:
			sym_rate = RATE_2_4;
			break;
		case AFE_LINK_RATE_2_7:
			sym_rate = RATE_2_7;
			break;
		case AFE_LINK_RATE_3_2:
			sym_rate = RATE_3_2;
			break;
		case AFE_LINK_RATE_4_3:
			sym_rate = RATE_4_3;
			break;
		case AFE_LINK_RATE_5_4:
			sym_rate = RATE_5_4;
			break;
			/*case AFE_LINK_RATE_8_1: sym_rate = RATE_8_1; break; */
		default:
			sym_rate = RATE_1_6;
		}
	} else {
		link_rate = max_link_rate;

		switch (max_link_rate) {
		case 0x0a:
			sym_rate = RATE_2_7;
			break;
		case 0x14:
			sym_rate = RATE_5_4;
			break;
		default:
			sym_rate = RATE_1_6;
		}
	}

	ret = CDN_API_DPTX_SetHostCap_blocking(state,
		link_rate,
		(num_lanes & 0x7) | ((ssc & 1) << 3) | ((scrambler & 1) << 4),
		(max_vswing & 0x3) | ((force_max_vswing & 1) << 4),
		(max_preemph & 0x3) | ((force_max_preemph & 1) << 4),
		supp_test_patterns,
		no_aux_training, /* fast link training */
		lane_mapping,
		ext_host_cap
		);
	DRM_INFO("CDN_API_DPTX_SetHostCap_blocking (ret = %d)\n", ret);


	ret = CDN_API_DPTX_Set_VIC_blocking(state,
		mode,
		bits_per_subpixel,
		num_lanes,
		sym_rate,
		format,
		stereo,
		bt_type,
		transfer_unit
		);
	DRM_INFO("CDN_API_DPTX_Set_VIC_blocking (ret = %d)\n", ret);

	do {
		ret = CDN_API_DPTX_TrainingControl_blocking(state, 1);
		DRM_DEBUG("CDN_API_DPTX_TrainingControl_blocking (ret = %d) start\n",
			   ret);
		if (dp_get_training_status(state) == 0)
			break;
		training_retries--;

		ret = CDN_API_DPTX_TrainingControl_blocking(state, 0);
		DRM_DEBUG("CDN_API_DPTX_TrainingControl_blocking (ret = %d) stop\n",
			   ret);
		udelay(1000);

	} while (training_retries > 0);

	/* Set video on */
	ret = CDN_API_DPTX_SetVideo_blocking(state, 1);
	DRM_INFO("CDN_API_DPTX_SetVideo_blocking (ret = %d)\n", ret);

	udelay(1000);

#ifdef DEBUG
	ret = CDN_API_DPTX_ReadLinkStat_blocking(state, &rls);
	DRM_INFO("INFO: Get Read Link Status (ret = %d resp: rate: %d, lanes: %d, vswing 0..3: %d %d %d, preemp 0..3: %d %d %d\n",
		 ret, rls.rate, rls.lanes,
		 rls.swing[0], rls.swing[1], rls.swing[2],
		 rls.preemphasis[0], rls.preemphasis[1],
		 rls.preemphasis[2]);
	dump_dpcd(state);
#endif

}

int dp_get_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	DPTX_Read_EDID_response edidResp;
	state_struct *state = data;
	CDN_API_STATUS ret = 0;

	memset(&edidResp, 0, sizeof(edidResp));
	switch (block) {
	case 0:
		ret = CDN_API_DPTX_Read_EDID_blocking(state, 0, 0, &edidResp);
		break;
	case 1:
		ret = CDN_API_DPTX_Read_EDID_blocking(state, 0, 1, &edidResp);
		break;
	case 2:
		ret = CDN_API_DPTX_Read_EDID_blocking(state, 1, 0, &edidResp);
		break;
	case 3:
		ret = CDN_API_DPTX_Read_EDID_blocking(state, 1, 1, &edidResp);
		break;
	default:
		DRM_WARN("EDID block %x read not support\n", block);
	}

	memcpy(buf, edidResp.buff, 128);

	return ret;
}

int dp_get_hpd_state(state_struct *state, u8 *hpd)
{
	int ret;

	ret = CDN_API_DPTX_GetHpdStatus_blocking(state, hpd);
	return ret;
}

int dp_phy_init_t28hpc(state_struct *state,
		       struct drm_display_mode *mode,
		       int format,
		       int color_depth)
{
	struct imx_hdp *hdp = state_to_imx_hdp(state);
	int max_link_rate = hdp->link_rate;
	int num_lanes = 4;
	int ret;
	u8 lane_mapping = hdp->lane_mapping;
	/* reset phy */
	imx_hdp_call(hdp, phy_reset, 0, &hdp->mem, 0);

	if (hdp->is_edp) {
		max_link_rate = hdp->edp_link_rate;
		num_lanes = hdp->edp_num_lanes;
	}

	/* Line swaping */
	CDN_API_General_Write_Register_blocking(state,
						ADDR_SOURCD_PHY +
						(LANES_CONFIG << 2),
						0x00400000 | lane_mapping);
	DRM_INFO("CDN_API_General_Write_Register_blocking ... setting LANES_CONFIG %x\n",
		 lane_mapping);

	/* PHY initialization while phy reset pin is active */
	afe_init_t28hpc(state, num_lanes, (ENUM_AFE_LINK_RATE)max_link_rate);
	DRM_INFO("AFE_init\n");

	/* In this point the phy reset should be deactivated */
	imx_hdp_call(hdp, phy_reset, 0, &hdp->mem, 1);
	DRM_INFO("deasserted reset\n");

	/* PHY power set */
	afe_power_t28hpc(state, num_lanes, (ENUM_AFE_LINK_RATE)max_link_rate);
	DRM_INFO("AFE_power exit\n");

	/* Video off */
	ret = CDN_API_DPTX_SetVideo_blocking(state, 0);
	DRM_INFO("CDN_API_DPTX_SetVideo_blocking (ret = %d)\n", ret);

	return true;
}
