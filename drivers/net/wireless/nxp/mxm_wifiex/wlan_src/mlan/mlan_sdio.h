/** @file mlan_sdio.h
 *
 * @brief This file contains definitions for SDIO interface.
 * driver.
 *
 *
 *  Copyright 2014-2020 NXP
 *
 *  This software file (the File) is distributed by NXP
 *  under the terms of the GNU General Public License Version 2, June 1991
 *  (the License).  You may use, redistribute and/or modify the File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 *
 */
/****************************************************
Change log:
****************************************************/

#ifndef _MLAN_SDIO_H
#define _MLAN_SDIO_H

/** Block mode */
#ifndef BLOCK_MODE
#define BLOCK_MODE 1
#endif

/** Fixed address mode */
#ifndef FIXED_ADDRESS
#define FIXED_ADDRESS 0
#endif

/* Host Control Registers */
/** Host Control Registers : Host to Card Event */
#define HOST_TO_CARD_EVENT_REG 0x00
/** Host Control Registers : Host terminates Command 53 */
#define HOST_TERM_CMD53 (0x1U << 2)
/** Host Control Registers : Host without Command 53 finish host */
#define HOST_WO_CMD53_FINISH_HOST (0x1U << 2)
/** Host Control Registers : Host power up */
#define HOST_POWER_UP (0x1U << 1)
/** Host Control Registers : Host power down */
#define HOST_POWER_DOWN (0x1U << 0)

/** Host Control Registers : Upload host interrupt RSR */
#define UP_LD_HOST_INT_RSR (0x1U)
#define HOST_INT_RSR_MASK 0xFF

/** Host Control Registers : Upload command port host interrupt status */
#define UP_LD_CMD_PORT_HOST_INT_STATUS (0x40U)
/** Host Control Registers : Download command port host interrupt status */
#define DN_LD_CMD_PORT_HOST_INT_STATUS (0x80U)

/** Host Control Registers : Upload host interrupt mask */
#define UP_LD_HOST_INT_MASK (0x1U)
/** Host Control Registers : Download host interrupt mask */
#define DN_LD_HOST_INT_MASK (0x2U)
/** Host Control Registers : Cmd port upload interrupt mask */
#define CMD_PORT_UPLD_INT_MASK (0x1U << 6)
/** Host Control Registers : Cmd port download interrupt mask */
#define CMD_PORT_DNLD_INT_MASK (0x1U << 7)
/** Enable Host interrupt mask */
#define HIM_ENABLE                                                             \
	(UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK | CMD_PORT_UPLD_INT_MASK |  \
	 CMD_PORT_DNLD_INT_MASK)
/** Disable Host interrupt mask */
#define HIM_DISABLE 0xff

/** Host Control Registers : Upload host interrupt status */
#define UP_LD_HOST_INT_STATUS (0x1U)
/** Host Control Registers : Download host interrupt status */
#define DN_LD_HOST_INT_STATUS (0x2U)

/** Host Control Registers : Download CRC error */
#define DN_LD_CRC_ERR (0x1U << 2)
/** Host Control Registers : Upload restart */
#define UP_LD_RESTART (0x1U << 1)
/** Host Control Registers : Download restart */
#define DN_LD_RESTART (0x1U << 0)

/** Card Control Registers : Command port upload ready */
#define UP_LD_CP_RDY (0x1U << 6)
/** Card Control Registers : Command port download ready */
#define DN_LD_CP_RDY (0x1U << 7)
/** Card Control Registers : Card I/O ready */
#define CARD_IO_READY (0x1U << 3)
/** Card Control Registers : CIS card ready */
#define CIS_CARD_RDY (0x1U << 2)
/** Card Control Registers : Upload card ready */
#define UP_LD_CARD_RDY (0x1U << 1)
/** Card Control Registers : Download card ready */
#define DN_LD_CARD_RDY (0x1U << 0)

/** Card Control Registers : Host power interrupt mask */
#define HOST_POWER_INT_MASK (0x1U << 3)
/** Card Control Registers : Abort card interrupt mask */
#define ABORT_CARD_INT_MASK (0x1U << 2)
/** Card Control Registers : Upload card interrupt mask */
#define UP_LD_CARD_INT_MASK (0x1U << 1)
/** Card Control Registers : Download card interrupt mask */
#define DN_LD_CARD_INT_MASK (0x1U << 0)

/** Card Control Registers : Power up interrupt */
#define POWER_UP_INT (0x1U << 4)
/** Card Control Registers : Power down interrupt */
#define POWER_DOWN_INT (0x1U << 3)

/** Card Control Registers : Power up RSR */
#define POWER_UP_RSR (0x1U << 4)
/** Card Control Registers : Power down RSR */
#define POWER_DOWN_RSR (0x1U << 3)

/** Card Control Registers : SD test BUS 0 */
#define SD_TESTBUS0 (0x1U)
/** Card Control Registers : SD test BUS 1 */
#define SD_TESTBUS1 (0x1U)
/** Card Control Registers : SD test BUS 2 */
#define SD_TESTBUS2 (0x1U)
/** Card Control Registers : SD test BUS 3 */
#define SD_TESTBUS3 (0x1U)

/** Port for registers */
#define REG_PORT 0
/** Port for memory */
#define MEM_PORT 0x10000

/** Card Control Registers : cmd53 new mode */
#define CMD53_NEW_MODE (0x1U << 0)
/** Card Control Registers : cmd53 tx len format 1 (0x10) */
#define CMD53_TX_LEN_FORMAT_1 (0x1U << 4)
/** Card Control Registers : cmd53 tx len format 2 (0x20)*/
#define CMD53_TX_LEN_FORMAT_2 (0x1U << 5)
/** Card Control Registers : cmd53 rx len format 1 (0x40) */
#define CMD53_RX_LEN_FORMAT_1 (0x1U << 6)
/** Card Control Registers : cmd53 rx len format 2 (0x80)*/
#define CMD53_RX_LEN_FORMAT_2 (0x1U << 7)

#define CMD_PORT_RD_LEN_EN (0x1U << 2)
/* Card Control Registers : cmd port auto enable */
#define CMD_PORT_AUTO_EN (0x1U << 0)

/* Command port */
#define CMD_PORT_SLCT 0x8000

/** Misc. Config Register : Auto Re-enable interrupts */
#define AUTO_RE_ENABLE_INT MBIT(4)

/** Enable GPIO-1 as a duplicated signal of interrupt as appear of SDIO_DAT1*/
#define ENABLE_GPIO_1_INT_MODE 0x88
/** Scratch reg 3 2  :     Configure GPIO-1 INT*/
#define SCRATCH_REG_32 0xEE

/** Event header Len*/
#define MLAN_EVENT_HEADER_LEN 8

/** SDIO byte mode size */
#define MAX_BYTE_MODE_SIZE 512

/** The base address for packet with multiple ports aggregation */
#define SDIO_MPA_ADDR_BASE 0x1000

/** SDIO Tx aggregation in progress ? */
#define MP_TX_AGGR_IN_PROGRESS(a) (a->pcard_sd->mpa_tx.pkt_cnt > 0)

/** SDIO Tx aggregation buffer room for next packet ? */
#define MP_TX_AGGR_BUF_HAS_ROOM(a, mbuf, len)                                  \
	(((a->pcard_sd->mpa_tx.buf_len) + len) <=                              \
	 (a->pcard_sd->mpa_tx.buf_size))

/** Copy current packet (SDIO Tx aggregation buffer) to SDIO buffer */
#define MP_TX_AGGR_BUF_PUT(a, mbuf, port)                                      \
	do {                                                                   \
		pmadapter->callbacks.moal_memmove(                             \
			a->pmoal_handle,                                       \
			&a->pcard_sd->mpa_tx.buf[a->pcard_sd->mpa_tx.buf_len], \
			mbuf->pbuf + mbuf->data_offset, mbuf->data_len);       \
		a->pcard_sd->mpa_tx.buf_len += mbuf->data_len;                 \
		a->pcard_sd->mpa_tx.mp_wr_info[a->pcard_sd->mpa_tx.pkt_cnt] =  \
			*(t_u16 *)(mbuf->pbuf + mbuf->data_offset);            \
		if (!a->pcard_sd->mpa_tx.pkt_cnt) {                            \
			a->pcard_sd->mpa_tx.start_port = port;                 \
		}                                                              \
		a->pcard_sd->mpa_tx.ports |= (1 << port);                      \
		a->pcard_sd->mpa_tx.pkt_cnt++;                                 \
	} while (0)

#define MP_TX_AGGR_BUF_PUT_SG(a, mbuf, port)                                   \
	do {                                                                   \
		a->pcard_sd->mpa_tx.buf_len += mbuf->data_len;                 \
		a->pcard_sd->mpa_tx.mp_wr_info[a->pcard_sd->mpa_tx.pkt_cnt] =  \
			*(t_u16 *)(mbuf->pbuf + mbuf->data_offset);            \
		a->pcard_sd->mpa_tx.mbuf_arr[a->pcard_sd->mpa_tx.pkt_cnt] =    \
			mbuf;                                                  \
		if (!a->pcard_sd->mpa_tx.pkt_cnt) {                            \
			a->pcard_sd->mpa_tx.start_port = port;                 \
		}                                                              \
		a->pcard_sd->mpa_tx.ports |= (1 << port);                      \
		a->pcard_sd->mpa_tx.pkt_cnt++;                                 \
	} while (0)
/** SDIO Tx aggregation limit ? */
#define MP_TX_AGGR_PKT_LIMIT_REACHED(a)                                        \
	((a->pcard_sd->mpa_tx.pkt_cnt) == (a->pcard_sd->mpa_tx.pkt_aggr_limit))

/** Reset SDIO Tx aggregation buffer parameters */
#define MP_TX_AGGR_BUF_RESET(a)                                                \
	do {                                                                   \
		memset(a, a->pcard_sd->mpa_tx.mp_wr_info, 0,                   \
		       sizeof(a->pcard_sd->mpa_tx.mp_wr_info));                \
		a->pcard_sd->mpa_tx.pkt_cnt = 0;                               \
		a->pcard_sd->mpa_tx.buf_len = 0;                               \
		a->pcard_sd->mpa_tx.ports = 0;                                 \
		a->pcard_sd->mpa_tx.start_port = 0;                            \
	} while (0)

/** SDIO Rx aggregation limit ? */
#define MP_RX_AGGR_PKT_LIMIT_REACHED(a)                                        \
	(a->pcard_sd->mpa_rx.pkt_cnt == a->pcard_sd->mpa_rx.pkt_aggr_limit)

/** SDIO Rx aggregation port limit ? */
/** this is for test only, because port 0 is reserved for control port */
/* #define MP_RX_AGGR_PORT_LIMIT_REACHED(a) (a->curr_rd_port == 1) */

/* receive packets aggregated up to a half of mp_end_port */
/* note: hw rx wraps round only after port (MAX_PORT-1) */
#define MP_RX_AGGR_PORT_LIMIT_REACHED(a)                                       \
	(((a->pcard_sd->curr_rd_port < a->pcard_sd->mpa_rx.start_port) &&      \
	  (((MAX_PORT - a->pcard_sd->mpa_rx.start_port) +                      \
	    a->pcard_sd->curr_rd_port) >= (a->pcard_sd->mp_end_port >> 1))) || \
	 ((a->pcard_sd->curr_rd_port - a->pcard_sd->mpa_rx.start_port) >=      \
	  (a->pcard_sd->mp_end_port >> 1)))

/** SDIO Rx aggregation in progress ? */
#define MP_RX_AGGR_IN_PROGRESS(a) (a->pcard_sd->mpa_rx.pkt_cnt > 0)

/** SDIO Rx aggregation buffer room for next packet ? */
#define MP_RX_AGGR_BUF_HAS_ROOM(a, rx_len)                                     \
	((a->pcard_sd->mpa_rx.buf_len + rx_len) <= a->pcard_sd->mpa_rx.buf_size)

/** Prepare to copy current packet from card to SDIO Rx aggregation buffer */
#define MP_RX_AGGR_SETUP(a, mbuf, port, rx_len)                                \
	do {                                                                   \
		a->pcard_sd->mpa_rx.buf_len += rx_len;                         \
		if (!a->pcard_sd->mpa_rx.pkt_cnt) {                            \
			a->pcard_sd->mpa_rx.start_port = port;                 \
		}                                                              \
		a->pcard_sd->mpa_rx.ports |= (1 << port);                      \
		a->pcard_sd->mpa_rx.mbuf_arr[a->pcard_sd->mpa_rx.pkt_cnt] =    \
			mbuf;                                                  \
		a->pcard_sd->mpa_rx.len_arr[a->pcard_sd->mpa_rx.pkt_cnt] =     \
			rx_len;                                                \
		a->pcard_sd->mpa_rx.pkt_cnt++;                                 \
	} while (0)

/** Reset SDIO Rx aggregation buffer parameters */
#define MP_RX_AGGR_BUF_RESET(a)                                                \
	do {                                                                   \
		a->pcard_sd->mpa_rx.pkt_cnt = 0;                               \
		a->pcard_sd->mpa_rx.buf_len = 0;                               \
		a->pcard_sd->mpa_rx.ports = 0;                                 \
		a->pcard_sd->mpa_rx.start_port = 0;                            \
	} while (0)

/** aggr buf size 32k  */
#define SDIO_MP_AGGR_BUF_SIZE_32K (32768)
/** max aggr buf size 64k-256 */
#define SDIO_MP_AGGR_BUF_SIZE_MAX (65280)

#ifdef SD8887
static const struct _mlan_sdio_card_reg mlan_reg_sd8887 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0x6C,
	.base_1_reg = 0x6D,
	.poll_reg = 0x5C,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
			   CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_status = DN_LD_HOST_INT_STATUS | UP_LD_HOST_INT_STATUS |
			   DN_LD_CMD_PORT_HOST_INT_STATUS |
			   UP_LD_CMD_PORT_HOST_INT_STATUS,
	.status_reg_0 = 0x90,
	.status_reg_1 = 0x91,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.max_mp_regs = 196,
	.rd_bitmap_l = 0x10,
	.rd_bitmap_u = 0x11,
	.rd_bitmap_1l = 0x12,
	.rd_bitmap_1u = 0x13,
	.wr_bitmap_l = 0x14,
	.wr_bitmap_u = 0x15,
	.wr_bitmap_1l = 0x16,
	.wr_bitmap_1u = 0x17,
	.rd_len_p0_l = 0x18,
	.rd_len_p0_u = 0x19,
	.card_config_2_1_reg = 0xD9,
	.cmd_config_0 = 0xC4,
	.cmd_config_1 = 0xC5,
	.cmd_config_2 = 0xC6,
	.cmd_config_3 = 0xC7,
	.cmd_rd_len_0 = 0xC0,
	.cmd_rd_len_1 = 0xC1,
	.cmd_rd_len_2 = 0xC2,
	.cmd_rd_len_3 = 0xC3,
	.io_port_0_reg = 0xE4,
	.io_port_1_reg = 0xE5,
	.io_port_2_reg = 0xE6,
	.host_int_rsr_reg = 0x04,
	.host_int_mask_reg = 0x08,
	.host_int_status_reg = 0x0C,
	.host_restart_reg = 0x58,
	.card_to_host_event_reg = 0x5C,
	.host_interrupt_mask_reg = 0x60,
	.card_interrupt_status_reg = 0x64,
	.card_interrupt_rsr_reg = 0x68,
	.card_revision_reg = 0xC8,
	.card_ocr_0_reg = 0xD4,
	.card_ocr_1_reg = 0xD5,
	.card_ocr_3_reg = 0xD6,
	.card_config_reg = 0xD7,
	.card_misc_cfg_reg = 0xD8,
	.debug_0_reg = 0xDC,
	.debug_1_reg = 0xDD,
	.debug_2_reg = 0xDE,
	.debug_3_reg = 0xDF,
	.fw_reset_reg = 0x0B6,
	.fw_reset_val = 1,
	.winner_check_reg = 0x90,
};

static const struct _mlan_card_info mlan_card_info_sd8887 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_2K,
	.v16_fw_api = 0,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_1X1,
};
#endif

#ifdef SD8897
static const struct _mlan_sdio_card_reg mlan_reg_sd8897 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0x60,
	.base_1_reg = 0x61,
	.poll_reg = 0x50,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
			   CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_status = DN_LD_HOST_INT_STATUS | UP_LD_HOST_INT_STATUS |
			   DN_LD_CMD_PORT_HOST_INT_STATUS |
			   UP_LD_CMD_PORT_HOST_INT_STATUS,
	.status_reg_0 = 0xC0,
	.status_reg_1 = 0xC1,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.max_mp_regs = 184,
	.rd_bitmap_l = 0x04,
	.rd_bitmap_u = 0x05,
	.rd_bitmap_1l = 0x06,
	.rd_bitmap_1u = 0x07,
	.wr_bitmap_l = 0x08,
	.wr_bitmap_u = 0x09,
	.wr_bitmap_1l = 0x0A,
	.wr_bitmap_1u = 0x0B,
	.rd_len_p0_l = 0x0C,
	.rd_len_p0_u = 0x0D,
	.card_config_2_1_reg = 0xCD,
	.cmd_config_0 = 0xB8,
	.cmd_config_1 = 0xB9,
	.cmd_config_2 = 0xBA,
	.cmd_config_3 = 0xBB,
	.cmd_rd_len_0 = 0xB4,
	.cmd_rd_len_1 = 0xB5,
	.cmd_rd_len_2 = 0xB6,
	.cmd_rd_len_3 = 0xB7,
	.io_port_0_reg = 0xD8,
	.io_port_1_reg = 0xD9,
	.io_port_2_reg = 0xDA,
	.host_int_rsr_reg = 0x01,
	.host_int_mask_reg = 0x02,
	.host_int_status_reg = 0x03,
	.host_restart_reg = 0x4C,
	.card_to_host_event_reg = 0x50,
	.host_interrupt_mask_reg = 0x54,
	.card_interrupt_status_reg = 0x58,
	.card_interrupt_rsr_reg = 0x5C,
	.card_revision_reg = 0xBC,
	.card_ocr_0_reg = 0xC8,
	.card_ocr_1_reg = 0xC9,
	.card_ocr_3_reg = 0xCA,
	.card_config_reg = 0xCB,
	.card_misc_cfg_reg = 0xCC,
	.debug_0_reg = 0xD0,
	.debug_1_reg = 0xD1,
	.debug_2_reg = 0xD2,
	.debug_3_reg = 0xD3,
	.fw_reset_reg = 0x0E8,
	.fw_reset_val = 1,
	.winner_check_reg = 0xC0,
};

static const struct _mlan_card_info mlan_card_info_sd8897 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_4K,
	.v16_fw_api = 0,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_2X2,
};
#endif

#if defined(SD8977) || defined(SD8997) || defined(SD8987) ||                   \
	defined(SD9098) || defined(SD9097) || defined(SD8978)
static const struct _mlan_sdio_card_reg mlan_reg_sd8977_sd8997 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0xf8,
	.base_1_reg = 0xf9,
	.poll_reg = 0x5C,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
			   CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_status = DN_LD_HOST_INT_STATUS | UP_LD_HOST_INT_STATUS |
			   DN_LD_CMD_PORT_HOST_INT_STATUS |
			   UP_LD_CMD_PORT_HOST_INT_STATUS,
	.status_reg_0 = 0xe8,
	.status_reg_1 = 0xe9,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.max_mp_regs = 196,
	.rd_bitmap_l = 0x10,
	.rd_bitmap_u = 0x11,
	.rd_bitmap_1l = 0x12,
	.rd_bitmap_1u = 0x13,
	.wr_bitmap_l = 0x14,
	.wr_bitmap_u = 0x15,
	.wr_bitmap_1l = 0x16,
	.wr_bitmap_1u = 0x17,
	.rd_len_p0_l = 0x18,
	.rd_len_p0_u = 0x19,
	.card_config_2_1_reg = 0xD9,
	.cmd_config_0 = 0xC4,
	.cmd_config_1 = 0xC5,
	.cmd_config_2 = 0xC6,
	.cmd_config_3 = 0xC7,
	.cmd_rd_len_0 = 0xC0,
	.cmd_rd_len_1 = 0xC1,
	.cmd_rd_len_2 = 0xC2,
	.cmd_rd_len_3 = 0xC3,
	.io_port_0_reg = 0xE4,
	.io_port_1_reg = 0xE5,
	.io_port_2_reg = 0xE6,
	.host_int_rsr_reg = 0x04,
	.host_int_mask_reg = 0x08,
	.host_int_status_reg = 0x0C,
	.host_restart_reg = 0x58,
	.card_to_host_event_reg = 0x5C,
	.host_interrupt_mask_reg = 0x60,
	.card_interrupt_status_reg = 0x64,
	.card_interrupt_rsr_reg = 0x68,
	.card_revision_reg = 0xC8,
	.card_ocr_0_reg = 0xD4,
	.card_ocr_1_reg = 0xD5,
	.card_ocr_3_reg = 0xD6,
	.card_config_reg = 0xD7,
	.card_misc_cfg_reg = 0xD8,
	.debug_0_reg = 0xDC,
	.debug_1_reg = 0xDD,
	.debug_2_reg = 0xDE,
	.debug_3_reg = 0xDF,
	.fw_reset_reg = 0x0EE,
	.fw_reset_val = 0x99,
	.fw_dnld_offset_0_reg = 0xEC,
	.fw_dnld_offset_1_reg = 0xED,
	.fw_dnld_offset_2_reg = 0xEE,
	.fw_dnld_offset_3_reg = 0xEF,
	.fw_dnld_status_0_reg = 0xE8,
	.fw_dnld_status_1_reg = 0xE9,
	.winner_check_reg = 0xFC,
};

#ifdef SD8997
static const struct _mlan_card_info mlan_card_info_sd8997 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_4K,
	.v16_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_2X2,
};
#endif

#ifdef SD9097
static const struct _mlan_card_info mlan_card_info_sd9097 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_4K,
	.v16_fw_api = 1,
	.v17_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_2X2,
};
#endif
#ifdef SD9098
static const struct _mlan_card_info mlan_card_info_sd9098 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_4K,
	.v16_fw_api = 1,
	.v17_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_2X2,
};
#endif

#if defined(SD8977) || defined(SD8978)
static const struct _mlan_card_info mlan_card_info_sd8977 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_2K,
	.v16_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_1X1,
};
#endif

#ifdef SD8987
static const struct _mlan_card_info mlan_card_info_sd8987 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_2K,
	.v16_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_1X1,
};
#endif
#endif

/** Probe and initialization function */
mlan_status wlan_sdio_probe(pmlan_adapter pmadapter);
mlan_status wlan_get_sdio_device(pmlan_adapter pmadapter);

mlan_status wlan_send_mp_aggr_buf(mlan_adapter *pmadapter);

mlan_status wlan_re_alloc_sdio_rx_mpa_buffer(mlan_adapter *pmadapter);

void wlan_decode_spa_buffer(mlan_adapter *pmadapter, t_u8 *buf, t_u32 len);
t_void wlan_sdio_deaggr_rx_pkt(pmlan_adapter pmadapter, mlan_buffer *pmbuf);
/** Transfer data to card */
mlan_status wlan_sdio_host_to_card(mlan_adapter *pmadapter, t_u8 type,
				   mlan_buffer *mbuf, mlan_tx_param *tx_param);
mlan_status wlan_set_sdio_gpio_int(pmlan_private priv);
mlan_status wlan_cmd_sdio_gpio_int(pmlan_private pmpriv,
				   HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
				   t_void *pdata_buf);
mlan_status wlan_reset_fw(pmlan_adapter pmadapter);

#endif /* _MLAN_SDIO_H */
