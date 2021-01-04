/** @file mlan_init.h
 *
 *  @brief This file defines the FW initialization data
 *  structures.
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

/******************************************************
Change log:
    10/13/2008: initial version
******************************************************/

#ifndef _MLAN_INIT_H_
#define _MLAN_INIT_H_

/** Tx buffer size for firmware download*/
#define FW_DNLD_TX_BUF_SIZE 2312
/** Rx buffer size for firmware download*/
#define FW_DNLD_RX_BUF_SIZE 2048
/** Max firmware retry */
#define MAX_FW_RETRY 3

/** Firmware has last block */
#define FW_HAS_LAST_BLOCK 0x00000004
/** CMD id for CMD4 */
#define FW_CMD_4 0x00000004
/** CMD id for CMD6 */
#define FW_CMD_6 0x00000006
/** CMD id for CMD7 */
#define FW_CMD_7 0x00000007
/** CMD id for CMD10 */
#define FW_CMD_10 0x0000000a

/** Firmware data transmit size */
#define FW_DATA_XMIT_SIZE (sizeof(FWHeader) + DataLength + sizeof(t_u32))

/** FWHeader */
typedef MLAN_PACK_START struct _FWHeader {
	/** FW download command */
	t_u32 dnld_cmd;
	/** FW base address */
	t_u32 base_addr;
	/** FW data length */
	t_u32 data_length;
	/** FW CRC */
	t_u32 crc;
} MLAN_PACK_END FWHeader;

/** FWData */
typedef MLAN_PACK_START struct _FWData {
	/** FW data header */
	FWHeader fw_header;
	/** FW data sequence number */
	t_u32 seq_num;
	/** FW data buffer */
	t_u8 data[1];
} MLAN_PACK_END FWData;

/** FWSyncHeader */
typedef MLAN_PACK_START struct _FWSyncHeader {
	/** FW sync header command */
	t_u32 cmd;
	/** FW sync header sequence number */
	t_u32 seq_num;
	/** Extended header */
	t_u32 magic;
	/** Chip rev */
	t_u32 chip_rev;
	/** Strap */
	t_u32 strap;
	/** Status */
	t_u32 status;
	/** Offset */
	t_u32 offset;
} MLAN_PACK_END FWSyncHeader;

/** FW Sync pkt */
typedef MLAN_PACK_START struct _FWSyncPkt {
	/** pkt type */
	t_u32 pkt_type;
	/** FW sync header command */
	t_u32 cmd;
	/** FW sync header sequence number */
	t_u32 seq_num;
	/** chip rev */
	t_u32 chip_rev;
	/** fw status */
	t_u32 fw_ready;
} MLAN_PACK_END FWSyncPkt;

#ifdef BIG_ENDIAN_SUPPORT
/** Convert sequence number and command fields
 *  of fwheader to correct endian format
 */
#define endian_convert_syncfwheader(x)                                         \
	{                                                                      \
		(x)->cmd = wlan_le32_to_cpu((x)->cmd);                         \
		(x)->seq_num = wlan_le32_to_cpu((x)->seq_num);                 \
		(x)->status = wlan_le32_to_cpu((x)->status);                   \
		(x)->offset = wlan_le32_to_cpu((x)->offset);                   \
	}
#else
/** Convert sequence number and command fields
 *  of fwheader to correct endian format
 */
#define endian_convert_syncfwheader(x)
#endif /* BIG_ENDIAN_SUPPORT */

#endif /* _MLAN_INIT_H_ */
