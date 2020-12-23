/******************************************************************************
 *
 * Copyright (C) 2016-2017 Cadence Design Systems, Inc.
 * All rights reserved worldwide.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ******************************************************************************
 *
 * API_DPTX.c
 *
 ******************************************************************************
 */
#include "API_DPTX.h"
#include "util.h"
#include "opcodes.h"
#include "address.h"
#include "dptx_stream.h"
#include "dptx_framer.h"
#include "source_vif.h"

CDN_API_STATUS CDN_API_DPTX_Read_DPCD(state_struct *state, int numOfBytes,
				      int addr, DPTX_Read_DPCD_response *resp,
				      CDN_BUS_TYPE bus_type)
{
	CDN_API_STATUS ret;
	if (!state->running) {
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX, DPTX_READ_DPCD,
				      2, 2, numOfBytes, 3, addr);
		state->bus_type = bus_type;
		state->rxEnable = 1;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	ret = internal_test_rx_head(state, MB_MODULE_ID_DP_TX,
				   DPTX_DPCD_READ_RESP);
	if (ret != CDN_OK) {
		state->running = 0;
		return ret;
	}
	/* Clean most significant bytes in members of structure used for response. */
	resp->size = 0;
	resp->addr = 0;
	internal_readmsg(state, 3,
			 2, &resp->size, 3, &resp->addr, 0, &resp->buff);
	state->running = 0;
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_Read_DPCD_blocking(state_struct *state,
					       int numOfBytes, int addr,
					       DPTX_Read_DPCD_response *resp,
					       CDN_BUS_TYPE bus_type)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_Read_DPCD
				(state, numOfBytes, addr, resp, bus_type));
}

CDN_API_STATUS CDN_API_DPTX_Read_EDID(state_struct *state, u8 segment,
				      u8 extension,
				      DPTX_Read_EDID_response *resp)
{
	CDN_API_STATUS ret;
	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX, DPTX_GET_EDID,
				      2, 1, segment, 1, extension);
		state->rxEnable = 1;
		state->bus_type = CDN_BUS_TYPE_APB;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	ret = internal_test_rx_head(state, MB_MODULE_ID_DP_TX,
				   DPTX_GET_EDID);
	if (ret != CDN_OK)
		return ret;
	internal_readmsg(state, 3,
			 1, &resp->size, 1, &resp->blockNo, 0, &resp->buff);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_Read_EDID_blocking(state_struct *state, u8 segment,
					       u8 extension,
					       DPTX_Read_EDID_response *resp)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_Read_EDID
				(state, segment, extension, resp));
}

CDN_API_STATUS CDN_API_DPTX_SetHostCap(state_struct *state, u8 maxLinkRate,
				       u8 lanesCount_SSC,
				       u8 maxVoltageSwing,
				       u8 maxPreemphasis,
				       u8 testPatternsSupported,
				       u8 fastLinkTraining,
				       u8 laneMapping, u8 enchanced)
{
	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX,
				      DPTX_SET_HOST_CAPABILITIES, 8, 1,
				      maxLinkRate, 1, lanesCount_SSC, 1,
				      maxVoltageSwing, 1, maxPreemphasis, 1,
				      testPatternsSupported, 1,
				      fastLinkTraining, 1, laneMapping, 1,
				      enchanced);
		state->bus_type = CDN_BUS_TYPE_APB;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_SetHostCap_blocking(state_struct *state,
						u8 maxLinkRate,
						u8 lanesCount_SSC,
						u8 maxVoltageSwing,
						u8 maxPreemphasis,
						u8 testPatternsSupported,
						u8 fastLinkTraining,
						u8 laneMapping, u8 enchanced)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_SetHostCap
				(state, maxLinkRate, lanesCount_SSC,
				 maxVoltageSwing, maxPreemphasis,
				 testPatternsSupported, fastLinkTraining,
				 laneMapping, enchanced));
}

CDN_API_STATUS CDN_API_DPTX_SetPowerMode(state_struct *state,
					 CDN_API_PWR_MODE mode)
{
	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX,
				      DPTX_SET_POWER_MNG, 1, 1, mode);
		state->bus_type = CDN_BUS_TYPE_APB;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_SetPowerMode_blocking(state_struct *state,
						  CDN_API_PWR_MODE mode)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_SetPowerMode(state, mode));
}

CDN_API_STATUS CDN_API_DPTX_Control(state_struct *state, u32 mode)
{
	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX,
				      DPTX_TRAINING_CONTROL, 1, 1, mode);
		state->bus_type = CDN_BUS_TYPE_APB;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_Control_blocking(state_struct *state, u32 mode)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_Control(state, mode));
}

CDN_API_STATUS CDN_API_DPTX_Write_DPCD(state_struct *state, u32 numOfBytes,
				       u32 addr, u8 *buff,
				       DPTX_Write_DPCD_response *resp,
				       CDN_BUS_TYPE bus_type)
{
	CDN_API_STATUS ret;
	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX,
				      DPTX_WRITE_DPCD, 3, 2, numOfBytes, 3,
				      addr, -numOfBytes, buff);
		state->rxEnable = 1;
		state->bus_type = bus_type;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	ret = internal_test_rx_head(state, MB_MODULE_ID_DP_TX,
				   DPTX_DPCD_WRITE_RESP);
	if (ret != CDN_OK)
		return ret;
	internal_readmsg(state, 2, 2, &resp->size, 3, &resp->addr);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_Write_DPCD_blocking(state_struct *state,
						u32 numOfBytes, u32 addr,
						u8 *buff,
						DPTX_Write_DPCD_response *resp,
						CDN_BUS_TYPE bus_type)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_Write_DPCD
				(state, numOfBytes, addr, buff, resp,
				 bus_type));
}

CDN_API_STATUS CDN_API_DPTX_Read_Register(state_struct *state, u8 base,
					  u8 regNo,
					  DPTX_Read_Register_response *resp)
{
	u16 addr = (base << 8) + (regNo << 2);
	CDN_API_STATUS ret;
	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX,
				      DPTX_READ_REGISTER, 1, 2, addr);
		state->bus_type = CDN_BUS_TYPE_APB;
		state->rxEnable = 1;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	ret = internal_test_rx_head(state, MB_MODULE_ID_DP_TX,
				   DPTX_READ_REGISTER_RESP);
	if (ret != CDN_OK)
		return ret;
	internal_readmsg(state, 3,
			 1, &resp->base, 1, &resp->regNo, 4, &resp->val);
	resp->regNo >>= 2;
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_Read_Register_blocking(state_struct *state,
						   u8 base, u8 regNo,
						   DPTX_Read_Register_response *resp)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_Read_Register
				(state, base, regNo, resp));
}

CDN_API_STATUS CDN_API_DPTX_Write_Register(state_struct *state, u8 base,
					   u8 regNo, u32 val)
{
	u16 addr = (base << 8) + (regNo << 2);
	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX,
				      DPTX_WRITE_REGISTER, 2, 2, addr, 4, val);
		state->bus_type = CDN_BUS_TYPE_APB;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_Write_Register_blocking(state_struct *state,
						    u8 base, u8 regNo, u32 val)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_Write_Register
				(state, base, regNo, val));
}

CDN_API_STATUS CDN_API_DPTX_Write_Field(state_struct *state, u8 base, u8 regNo,
					u8 startBit, u8 bitsNo, u32 val)
{
	u16 addr = (base << 8) + (regNo << 2);
	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX,
				      DPTX_WRITE_FIELD, 4, 2, addr, 1, startBit,
				      1, bitsNo, 4, val);
		state->bus_type = CDN_BUS_TYPE_APB;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_Write_Field_blocking(state_struct *state, u8 base,
						 u8 regNo,
						 u8 startBit,
						 u8 bitsNo, u32 val)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_Write_Field
				(state, base, regNo, startBit, bitsNo, val));
}

CDN_API_STATUS CDN_API_DPTX_EnableEvent(state_struct *state, bool hpd,
					bool training)
{
	uint8_t events = 0;

	if (!state->running) {
		if (!internal_apb_available(state)) {
			return CDN_BSY;
		}

		events |= (hpd ? 1 << DP_TX_EVENT_ENABLE_HPD_BIT : 0);
		events |= (training ? 1 << DP_TX_EVENT_ENABLE_TRAINING_BIT : 0);

		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX, DPTX_ENABLE_EVENT, 2, 1, events, 4, 0);

		state->bus_type = CDN_BUS_TYPE_APB;

		return CDN_STARTED;
	}

	internal_process_messages(state);

	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_EnableEvent_blocking(state_struct *state, bool hpd,
						 bool training)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_EnableEvent(state, hpd, training));
}

CDN_API_STATUS CDN_API_DPTX_ReadEvent(state_struct *state, u8 *LinkeventId,
				      u8 *HPDevents)
{
	CDN_API_STATUS ret;
	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX,
				      DPTX_READ_EVENT, 0);
		state->rxEnable = 1;
		state->bus_type = CDN_BUS_TYPE_APB;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	ret = internal_test_rx_head(state, MB_MODULE_ID_DP_TX,
				   DPTX_READ_EVENT);
	if (ret != CDN_OK)
		return ret;
	internal_readmsg(state, 2, 1, HPDevents, 1, LinkeventId);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_ReadEvent_blocking(state_struct *state,
					       u8 *LinkeventId, u8 *HPDevents)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_ReadEvent
				(state, LinkeventId, HPDevents));
}

#define NUM_OF_SUPPORTED_PIXEL_FREQ 25

u32 CDN_API_Get_PIXEL_FREQ_KHZ_ClosetVal(u32 val, CDN_PROTOCOL_TYPE protocol)
{
	u32 supportedVals[NUM_OF_SUPPORTED_PIXEL_FREQ];
	int i;
	int minI;

	u32 minVal;

	supportedVals[0] = 24719;
	supportedVals[1] = 25000;
	supportedVals[2] = 25175;
	supportedVals[3] = 25200;
	supportedVals[4] = 27000;
	supportedVals[5] = 32358;
	supportedVals[6] = 33750;
	supportedVals[7] = 38250;
	supportedVals[8] = 54000;
	supportedVals[9] = 74250;
	supportedVals[10] = 108000;
	supportedVals[11] = 148500;
	supportedVals[12] = 138750;
	supportedVals[13] = 104750;
	supportedVals[14] = 102500;
	supportedVals[15] = 82000;
	supportedVals[16] = 78500;
	supportedVals[17] = 63500;
	supportedVals[18] = 40000;
	supportedVals[19] = 59340;
	supportedVals[20] = 35000;
	supportedVals[21] = 72000;
	supportedVals[22] = 47000;
	supportedVals[23] = 22250;
	supportedVals[24] = 30750;

	minVal = abs(val - supportedVals[0]);
	minI = 0;
	for (i = 1; i < NUM_OF_SUPPORTED_PIXEL_FREQ; i++) {
		if (abs(val - supportedVals[i]) < minVal) {

			minVal = abs(val - supportedVals[i]);
			minI = i;
		}
	}

	return supportedVals[minI];
}

CDN_API_STATUS CDN_API_DPTX_Set_VIC(state_struct *state, VIC_MODES vicMode,
				    int bitsPerPixel,
				    VIC_NUM_OF_LANES NumOfLanes,
				    VIC_SYMBOL_RATE rate,
				    VIC_PXL_ENCODING_FORMAT pxlencformat,
				    STEREO_VIDEO_ATTR steroVidAttr,
				    BT_TYPE bt_type, int TU)
{
	int min_link_rate;
	int bitsPerPixelCalc;
	int TU_SIZE_reg = 34;
	int val, val_f, val2, val2_f;
	u32 lineThresh;
	u32 pixelClockFreq;
	u32 MSA_MISC_Param, tempForMisc, tempForMisc2;
	u32 oddEvenV_Total;
	u32 DP_FRAMER_SP_Param;
	u32 DP_FRONT_BACK_PORCH_Param;
	u32 DP_BYTE_COUNT_Param;
	u32 MSA_HORIZONTAL_0_Param;
	u32 MSA_HORIZONTAL_1_Param;
	u32 MSA_VERTICAL_0_Param;
	u32 MSA_VERTICAL_1_Param;
	u32 DP_HORIZONTAL_ADDR_Param;
	u32 DP_VERTICAL_0_ADDR_Param;
	u32 DP_VERTICAL_1_ADDR_Param;
	u32 DP_FRAMER_PXL_REPR_Param;
	u32 HSYNC2VSYNC_POL_CTRL_Param = 0;
	u32 BND_HSYNC2VSYNC_Param = 0;
	u32 DP_FRAMER_TU_Param;
	u32 tu_vs_diff = 0;
	VIC_COLOR_DEPTH colorDepth;
	CDN_API_STATUS ret = CDN_OK;

	if (pxlencformat == YCBCR_4_2_2)
		bitsPerPixelCalc = bitsPerPixel * 2;
	else if (pxlencformat == YCBCR_4_2_0)
		bitsPerPixelCalc = bitsPerPixel * 3 / 2;
	else
		bitsPerPixelCalc = bitsPerPixel * 3;

	/* KHz */
	pixelClockFreq =
	    CDN_API_Get_PIXEL_FREQ_KHZ_ClosetVal(vic_table[vicMode]
						 [PIXEL_FREQ_KHZ], CDN_DPTX);

	/* KHz */
	min_link_rate = rate * 995;
	rate *= 1000;

	val = TU_SIZE_reg * pixelClockFreq * bitsPerPixelCalc;
	val_f = val / (NumOfLanes * rate * 8);
	val /= NumOfLanes * rate * 8;

	val2 = TU_SIZE_reg * pixelClockFreq * bitsPerPixelCalc;
	val2_f = val2 / (NumOfLanes * min_link_rate * 8);
	val2 /= NumOfLanes * min_link_rate * 8;

	/* find optimum value for the TU_SIZE */

	while (((val == 1) || (TU_SIZE_reg - val < 2) || (val != val2)
		|| (val_f % 1000 > 850) || (val2_f % 1000 > 850)
		|| (val_f % 1000 < 100) || (val2_f % 1000 < 100))
	       && (TU_SIZE_reg < 64)) {
		TU_SIZE_reg += 2;

		val = TU_SIZE_reg * pixelClockFreq * bitsPerPixelCalc;
		val_f = val / (NumOfLanes * rate * 8);
		val /= NumOfLanes * rate * 8;

		val2 = TU_SIZE_reg * pixelClockFreq * bitsPerPixelCalc;
		val2_f = val2 / (NumOfLanes * min_link_rate * 8);
		val2 /= NumOfLanes * min_link_rate * 8;

/*		pr_info("val=%d, val_f=%d, val2=%d, val2_f=%d\n", val, val_f,
		       val2, val2_f); */
	}

	/* calculate the fixed valid symbols */
	val = TU_SIZE_reg * pixelClockFreq * bitsPerPixelCalc;
	val /= NumOfLanes * rate * 8;

	if (val > 64) {
		return CDN_ERROR_NOT_SUPPORTED;
	}
	DP_FRAMER_TU_Param = (TU_SIZE_reg << 8) + val + (1 << 15);

	tu_vs_diff = 0;
	if ((TU_SIZE_reg - val) <= 3) {
		tu_vs_diff = TU_SIZE_reg - val;
	}

	/* LINE_THRESH set according to zeev presantation */
	lineThresh =
	    ((val + 1) * NumOfLanes - ((pixelClockFreq / rate) * (val + 1) *
				       (bitsPerPixelCalc / 8) -
				       (bitsPerPixelCalc / 8))) /
	    ((bitsPerPixelCalc * NumOfLanes) / 8);
	lineThresh += 2;

	DP_FRAMER_SP_Param =
	    (vic_table[vicMode][I_P] == INTERLACED ? 4 : 0) +
	    (vic_table[vicMode][HSYNC_POL] == ACTIVE_LOW ? 2 : 0) +
	    (vic_table[vicMode][VSYNC_POL] == ACTIVE_LOW ? 1 : 0);
	DP_FRONT_BACK_PORCH_Param =
	    vic_table[vicMode][BACK_PORCH] +
	    (vic_table[vicMode][FRONT_PORCH] << 16);

	DP_BYTE_COUNT_Param =
	    vic_table[vicMode][H_ACTIVE] * (bitsPerPixelCalc) / 8;
	MSA_HORIZONTAL_0_Param =
	    vic_table[vicMode][H_TOTAL] +
	    ((vic_table[vicMode][HSYNC] +
	      vic_table[vicMode][BACK_PORCH]) << 16);
	MSA_HORIZONTAL_1_Param =
	    vic_table[vicMode][HSYNC] +
	    ((vic_table[vicMode][HSYNC_POL] ==
	      ACTIVE_LOW ? 0 : 1) << 15) + (vic_table[vicMode][H_ACTIVE] << 16);

	MSA_VERTICAL_0_Param =
	    (vic_table[vicMode][I_P] == INTERLACED ?
	     ((vic_table[vicMode][V_TOTAL] /
	       2)) : vic_table[vicMode][V_TOTAL]) +
	    ((vic_table[vicMode][VSYNC] + vic_table[vicMode][SOF]) << 16);
	MSA_VERTICAL_1_Param =
	    (vic_table[vicMode][VSYNC] +
	     ((vic_table[vicMode][VSYNC_POL] ==
	       ACTIVE_LOW ? 0 : 1) << 15)) + ((vic_table[vicMode][I_P] ==
					       INTERLACED ?
					       vic_table[vicMode][V_ACTIVE] /
					       2 : vic_table[vicMode][V_ACTIVE])
					      << 16);
	DP_HORIZONTAL_ADDR_Param =
	    ((vic_table[vicMode][H_TOTAL] -
	      vic_table[vicMode][H_BLANK]) << 16) +
	    (vic_table[vicMode][H_BLANK] - vic_table[vicMode][FRONT_PORCH] -
	     vic_table[vicMode][BACK_PORCH]);
	DP_VERTICAL_0_ADDR_Param =
	    (vic_table[vicMode][I_P] ==
	     INTERLACED ? (((vic_table[vicMode][V_TOTAL]) / 2)) :
	     vic_table[vicMode][V_TOTAL]) - (vic_table[vicMode][VSYNC] +
					     vic_table[vicMode][SOF] +
					     vic_table[vicMode][TYPE_EOF]) +
	    ((vic_table[vicMode][VSYNC] + vic_table[vicMode][SOF]) << 16);
	DP_VERTICAL_1_ADDR_Param =
	    (vic_table[vicMode][I_P] ==
	     INTERLACED ? (((vic_table[vicMode][V_TOTAL]) / 2)) :
	     vic_table[vicMode][V_TOTAL]);

	if (vic_table[vicMode][I_P] == INTERLACED)
		BND_HSYNC2VSYNC_Param = 0x3020;
	else
		BND_HSYNC2VSYNC_Param = 0x2000;

	if (vic_table[vicMode][HSYNC_POL] == ACTIVE_LOW) {
		HSYNC2VSYNC_POL_CTRL_Param |= F_HPOL(1);
	}
	if (vic_table[vicMode][VSYNC_POL] == ACTIVE_LOW) {
		HSYNC2VSYNC_POL_CTRL_Param |= F_VPOL(1);
	}

	switch (bitsPerPixel) {
	case 6:
		colorDepth = BCS_6;
		break;
	case 8:
		colorDepth = BCS_8;
		break;
	case 10:
		colorDepth = BCS_10;
		break;
	case 12:
		colorDepth = BCS_12;
		break;
	case 16:
		colorDepth = BCS_16;
		break;
	default:
		colorDepth = BCS_8;
	};

	DP_FRAMER_PXL_REPR_Param = (pxlencformat << 8) + colorDepth;

	switch (pxlencformat) {
	case PXL_RGB:		/*0x1 */
		tempForMisc = 0;
		break;
	case YCBCR_4_4_4:	/*0x2 */
		tempForMisc = 6 + 8 * (bt_type);
		break;
	case YCBCR_4_2_2:	/*0x4 */
		tempForMisc = 5 + 8 * (bt_type);
		break;
	case YCBCR_4_2_0:	/*0x8 */
		tempForMisc = 5;
		break;

	case Y_ONLY:		/*0x10 */
		tempForMisc = 0;
		break;
	default:
		tempForMisc = 0;
	};

	switch (bitsPerPixel) {
	case 6:
		tempForMisc2 = 0;
		break;

	case 8:
		tempForMisc2 = 1;
		break;

	case 10:
		tempForMisc2 = 2;
		break;

	case 12:
		tempForMisc2 = 3;
		break;

	case 16:
		tempForMisc2 = 4;
		break;
	default:
		tempForMisc2 = 1;

	};

	oddEvenV_Total = vic_table[vicMode][V_TOTAL] % 2;
	oddEvenV_Total = 1 - oddEvenV_Total;
	oddEvenV_Total = oddEvenV_Total << 8;
	MSA_MISC_Param =
	    ((tempForMisc * 2) + (32 * tempForMisc2) +
	     ((pxlencformat == Y_ONLY ? 1 : 0) << 14) +
	     ((oddEvenV_Total) * (vic_table[vicMode][I_P])));

	/* 420 has diffrent parameters, enable VSS SDP */
	if (pxlencformat == YCBCR_4_2_0)
		MSA_MISC_Param = 1 << 14;

	switch (state->tmp) {
	case 0:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_SOURCE_VIF,
						BND_HSYNC2VSYNC,
						BND_HSYNC2VSYNC_Param);
		break;
	case 1:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_SOURCE_VIF,
						HSYNC2VSYNC_POL_CTRL,
						HSYNC2VSYNC_POL_CTRL_Param);
		break;
	case 2:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						DP_FRAMER_TU,
						DP_FRAMER_TU_Param);
		break;
	case 3:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						DP_FRAMER_PXL_REPR,
						DP_FRAMER_PXL_REPR_Param);
		break;
	case 4:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						DP_FRAMER_SP,
						DP_FRAMER_SP_Param);
		break;
	case 5:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						DP_FRONT_BACK_PORCH,
						DP_FRONT_BACK_PORCH_Param);
		break;
	case 6:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						DP_BYTE_COUNT,
						DP_BYTE_COUNT_Param);
		break;
	case 7:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						MSA_HORIZONTAL_0,
						MSA_HORIZONTAL_0_Param);
		break;
	case 8:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						MSA_HORIZONTAL_1,
						MSA_HORIZONTAL_1_Param);
		break;
	case 9:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						MSA_VERTICAL_0,
						MSA_VERTICAL_0_Param);
		break;
	case 10:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						MSA_VERTICAL_1,
						MSA_VERTICAL_1_Param);
		break;
	case 11:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						MSA_MISC, MSA_MISC_Param);
		break;
	case 12:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						STREAM_CONFIG, 1);
		break;
	case 13:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						DP_HORIZONTAL,
						DP_HORIZONTAL_ADDR_Param);
		break;
	case 14:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						DP_VERTICAL_0,
						DP_VERTICAL_0_ADDR_Param);
		break;
	case 15:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						DP_VERTICAL_1,
						DP_VERTICAL_1_ADDR_Param);
		break;
	case 16:
		ret =
		    CDN_API_DPTX_Write_Field(state, BASE_DPTX_STREAM, DP_VB_ID,
					     2, 1,
					     ((vic_table[vicMode][I_P] ==
					       INTERLACED ? 1 : 0) << 2));
		break;
	case 17:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						LINE_THRESH, lineThresh);
		break;
	case 18:
		ret =
		    CDN_API_DPTX_Write_Register(state, BASE_DPTX_STREAM,
						RATE_GOVERNOR_STATUS,
						tu_vs_diff << 8);
		break;
	}
	if (!state->tmp && ret == CDN_STARTED)
		return CDN_STARTED;
	switch (ret) {
	case CDN_OK:
		state->tmp++;
		break;
	case CDN_STARTED:
		return CDN_BSY;
		break;
	default:
		return ret;
	}
	if (state->tmp == 19) {
		state->tmp = 0;
		return CDN_OK;
	}
	return CDN_BSY;
}

CDN_API_STATUS CDN_API_DPTX_Set_VIC_blocking(state_struct *state,
					     VIC_MODES vicMode,
					     int bitsPerPixel,
					     VIC_NUM_OF_LANES NumOfLanes,
					     VIC_SYMBOL_RATE rate,
					     VIC_PXL_ENCODING_FORMAT
					     pxlencformat,
					     STEREO_VIDEO_ATTR steroVidAttr,
					     BT_TYPE bt_type, int TU)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_Set_VIC
				(state, vicMode, bitsPerPixel, NumOfLanes, rate,
				 pxlencformat, steroVidAttr, bt_type, TU));
}

CDN_API_STATUS CDN_API_DPTX_SetVideo(state_struct *state, u8 mode)
{
	internal_macro_command_tx(state, MB_MODULE_ID_DP_TX, DPTX_SET_VIDEO,
				  CDN_BUS_TYPE_APB, 1, 1, mode);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_SetVideo_blocking(state_struct *state, u8 mode)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_SetVideo(state, mode));
}

CDN_API_STATUS CDN_API_DPTX_ReadLinkStat(state_struct *state,
					 S_LINK_STAT *stat)
{
	internal_macro_command_txrx(state, MB_MODULE_ID_DP_TX,
				    DPTX_READ_LINK_STAT, CDN_BUS_TYPE_APB, 0);
	internal_readmsg(state, 10, 1, &stat->rate, 1, &stat->lanes, 1,
			 &stat->swing[0], 1, &stat->preemphasis[0], 1,
			 &stat->swing[1], 1, &stat->preemphasis[1], 1,
			 &stat->swing[2], 1, &stat->preemphasis[2], 1,
			 &stat->swing[3], 1, &stat->preemphasis[3]);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_ReadLinkStat_blocking(state_struct *state,
						  S_LINK_STAT *stat)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_ReadLinkStat(state, stat));
}

CDN_API_STATUS CDN_API_DPTX_TrainingControl(state_struct *state, u8 val)
{
	internal_macro_command_tx(state, MB_MODULE_ID_DP_TX,
				  DPTX_TRAINING_CONTROL, CDN_BUS_TYPE_APB, 1, 1,
				  val);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_TrainingControl_blocking(state_struct *state,
						     u8 val)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_TrainingControl(state, val));
}

CDN_API_STATUS CDN_API_DPTX_GetLastAuxStatus(state_struct *state, u8 *resp)
{
	internal_macro_command_txrx(state, MB_MODULE_ID_DP_TX,
				    DPTX_GET_LAST_AUX_STAUS, CDN_BUS_TYPE_APB,
				    0);
	internal_readmsg(state, 1, 1, resp);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_GetLastAuxStatus_blocking(state_struct *state,
						      u8 *resp)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_GetLastAuxStatus(state, resp));
}

CDN_API_STATUS CDN_API_DPTX_GetHpdStatus(state_struct *state, u8 *resp)
{
	internal_macro_command_txrx(state, MB_MODULE_ID_DP_TX, DPTX_HPD_STATE,
				    CDN_BUS_TYPE_APB, 0);
	internal_readmsg(state, 1, 1, resp);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_GetHpdStatus_blocking(state_struct *state,
						  u8 *resp)
{

	internal_block_function(&state->mutex, CDN_API_DPTX_GetHpdStatus(state, resp));
}

CDN_API_STATUS CDN_API_DPTX_ForceLanes(state_struct *state, u8 linkRate,
				       u8 numOfLanes,
				       u8 voltageSwing_l0,
				       u8 preemphasis_l0,
				       u8 voltageSwing_l1,
				       u8 preemphasis_l1,
				       u8 voltageSwing_l2,
				       u8 preemphasis_l2,
				       u8 voltageSwing_l3,
				       u8 preemphasis_l3, u8 pattern, u8 ssc)
{
	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;
		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX,
				      DPTX_FORCE_LANES, 12, 1, linkRate, 1,
				      numOfLanes, 1, voltageSwing_l0, 1,
				      preemphasis_l0, 1, voltageSwing_l1, 1,
				      preemphasis_l1, 1, voltageSwing_l2, 1,
				      preemphasis_l2, 1, voltageSwing_l3, 1,
				      preemphasis_l3, 1, pattern, 1, ssc);
		state->bus_type = CDN_BUS_TYPE_APB;
		return CDN_STARTED;
	}
	internal_process_messages(state);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_ForceLanes_blocking(state_struct *state,
						u8 linkRate, u8 numOfLanes,
						u8 voltageSwing_l0,
						u8 preemphasis_l0,
						u8 voltageSwing_l1,
						u8 preemphasis_l1,
						u8 voltageSwing_l2,
						u8 preemphasis_l2,
						u8 voltageSwing_l3,
						u8 preemphasis_l3, u8 pattern,
						u8 ssc)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_ForceLanes_blocking
				(state, linkRate, numOfLanes, voltageSwing_l0,
				 preemphasis_l0, voltageSwing_l1,
				 preemphasis_l1, voltageSwing_l2,
				 preemphasis_l2, voltageSwing_l3,
				 preemphasis_l3, pattern, ssc));
}

CDN_API_STATUS CDN_API_DPTX_SetDbg(state_struct *state, uint32_t dbg_cfg)
{
	uint8_t buf[sizeof(uint32_t)];

	if (!state->running) {
		if (!internal_apb_available(state))
			return CDN_BSY;

		buf[0] = (uint8_t) (dbg_cfg);
		buf[1] = (uint8_t) (dbg_cfg >> 8);
		buf[2] = (uint8_t) (dbg_cfg >> 16);
		buf[3] = (uint8_t) (dbg_cfg >> 24);

		internal_tx_mkfullmsg(state, MB_MODULE_ID_DP_TX, DPTX_DBG_SET,
				      1, -sizeof(buf), buf);

		state->bus_type = CDN_BUS_TYPE_APB;

		return CDN_STARTED;
	}

	internal_process_messages(state);

	return CDN_OK;
}

CDN_API_STATUS CDN_API_DPTX_SetDbg_blocking(state_struct *state,
					    uint32_t dbg_cfg)
{
	internal_block_function(&state->mutex, CDN_API_DPTX_SetDbg(state, dbg_cfg));
}
