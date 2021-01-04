/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2010 Juergen Beisert, Pengutronix
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 *
 * i.MX23/i.MX28/i.MX6SX MXSFB LCD controller driver.
 */

#ifndef __MXSFB_REGS_H__
#define __MXSFB_REGS_H__

#define REG_SET	4
#define REG_CLR	8

#define LCDC_CTRL			0x00
#define LCDC_CTRL1			0x10
#define LCDC_V4_CTRL2			0x20
#define LCDC_V3_TRANSFER_COUNT		0x20
#define LCDC_V4_TRANSFER_COUNT		0x30
#define LCDC_V4_CUR_BUF			0x40
#define LCDC_V4_NEXT_BUF		0x50
#define LCDC_V3_CUR_BUF			0x30
#define LCDC_V3_NEXT_BUF		0x40
#define LCDC_TIMING			0x60
#define LCDC_VDCTRL0			0x70
#define LCDC_VDCTRL1			0x80
#define LCDC_VDCTRL2			0x90
#define LCDC_VDCTRL3			0xa0
#define LCDC_VDCTRL4			0xb0
#define LCDC_DVICTRL0			0xc0
#define LCDC_DVICTRL1			0xd0
#define LCDC_DVICTRL2			0xe0
#define LCDC_DVICTRL3			0xf0
#define LCDC_DVICTRL4			0x100
#define LCDC_V4_DATA			0x180
#define LCDC_V3_DATA			0x1b0
#define LCDC_V4_DEBUG0			0x1d0
#define LCDC_V3_DEBUG0			0x1f0
#define LCDC_AS_CTRL			0x210
#define LCDC_AS_BUF			0x220
#define LCDC_AS_NEXT_BUF		0x230

/* reg bit manipulation */
#define REG_PUT(x, h, l) (((x) << (l)) & GENMASK(h, l))
#define REG_GET(x, h, l) (((x) & GENMASK(h, l)) >> (l))

#define SWIZZLE_LE		0 /* Little-Endian or No swap */
#define SWIZZLE_BE		1 /* Big-Endian or swap all */
#define SWIZZLE_HWD		2 /* Swap half-words */
#define SWIZZLE_HWD_BYTE	3 /* Swap bytes within each half-word */

#define CTRL_SFTRST			BIT(31)
#define CTRL_CLKGATE			BIT(30)
#define CTRL_SHIFT_DIR(x)		REG_PUT((x), 26, 26)
#define CTRL_SHIFT_NUM(x)		REG_PUT((x), 25, 21)
#define CTRL_BYPASS_COUNT		BIT(19)
#define CTRL_VSYNC_MODE			BIT(18)
#define CTRL_DOTCLK_MODE		BIT(17)
#define CTRL_DATA_SELECT		BIT(16)
#define CTRL_INPUT_SWIZZLE(x)		REG_PUT((x), 15, 14)
#define CTRL_CSC_SWIZZLE(x)		REG_PUT((x), 13, 12)
#define CTRL_SET_BUS_WIDTH(x)		REG_PUT((x), 11, 10)
#define CTRL_GET_BUS_WIDTH(x)		REG_GET((x), 11, 10)
#define CTRL_BUS_WIDTH_MASK		REG_PUT((0x3), 11, 10)
#define CTRL_SET_WORD_LENGTH(x)		REG_PUT((x), 9, 8)
#define CTRL_GET_WORD_LENGTH(x)		REG_GET((x), 9, 8)
#define CTRL_MASTER			BIT(5)
#define CTRL_DF16			BIT(3)
#define CTRL_DF18			BIT(2)
#define CTRL_DF24			BIT(1)
#define CTRL_RUN			BIT(0)

#define CTRL1_RECOVERY_ON_UNDERFLOW	BIT(24)
#define CTRL1_FIFO_CLEAR		BIT(21)

/*
 * BYTE_PACKAGING
 *
 * This bitfield is used to show which data bytes in a 32-bit word area valid.
 * Default value 0xf indicates that all bytes are valid. For 8-bit transfers,
 * any combination in this bitfield will mean valid data is present in the
 * corresponding bytes. In the 16-bit mode, a 16-bit half-word is valid only if
 * adjacent bits [1:0] or [3:2] or both are 1. A value of 0x0 will mean that
 * none of the bytes are valid and should not be used. For example, set the bit
 * field value to 0x7 if the display data is arranged in the 24-bit unpacked
 * format (A-R-G-B where A value does not have be transmitted).
 */
#define CTRL1_SET_BYTE_PACKAGING(x)	REG_PUT((x), 19, 16)
#define CTRL1_GET_BYTE_PACKAGING(x)	REG_GET((x), 19, 16)

#define CTRL1_CUR_FRAME_DONE_IRQ_EN	BIT(13)
#define CTRL1_CUR_FRAME_DONE_IRQ	BIT(9)

#define CTRL2_OUTSTANDING_REQS(x)	REG_PUT((x), 23, 21)
#define REQ_1	0
#define REQ_2	1
#define REQ_4	2
#define REQ_8	3
#define REQ_16	4

#define CTRL2_ODD_LINE_PATTERN(x)	REG_PUT((x), 18, 16)
#define CTRL2_EVEN_LINE_PATTERN(x)	REG_PUT((x), 14, 12)
#define CTRL2_LINE_PATTERN_RGB	0
#define CTRL2_LINE_PATTERN_RBG	1
#define CTRL2_LINE_PATTERN_GBR	2
#define CTRL2_LINE_PATTERN_GRB	3
#define CTRL2_LINE_PATTERN_BRG	4
#define CTRL2_LINE_PATTERN_BGR	5
#define CTRL2_LINE_PATTERN_CLR	7

#define CTRL2_LCD_RESET			BIT(0)

#define TRANSFER_COUNT_SET_VCOUNT(x)	REG_PUT((x), 31, 16)
#define TRANSFER_COUNT_GET_VCOUNT(x)	REG_GET((x), 31, 16)
#define TRANSFER_COUNT_SET_HCOUNT(x)	REG_PUT((x), 15, 0)
#define TRANSFER_COUNT_GET_HCOUNT(x)	REG_GET((x), 15, 0)

#define VDCTRL0_ENABLE_PRESENT		BIT(28)
#define VDCTRL0_VSYNC_ACT_HIGH		BIT(27)
#define VDCTRL0_HSYNC_ACT_HIGH		BIT(26)
#define VDCTRL0_DOTCLK_ACT_FALLING	BIT(25)
#define VDCTRL0_ENABLE_ACT_HIGH		BIT(24)
#define VDCTRL0_VSYNC_PERIOD_UNIT	BIT(21)
#define VDCTRL0_VSYNC_PULSE_WIDTH_UNIT	BIT(20)
#define VDCTRL0_HALF_LINE		BIT(19)
#define VDCTRL0_HALF_LINE_MODE		BIT(18)
#define VDCTRL0_SET_VSYNC_PULSE_WIDTH(x) REG_PUT((x), 17, 0)
#define VDCTRL0_GET_VSYNC_PULSE_WIDTH(x) REG_GET((x), 17, 0)

#define VDCTRL2_SET_HSYNC_PERIOD(x)	REG_PUT((x), 15, 0)
#define VDCTRL2_GET_HSYNC_PERIOD(x)	REG_GET((x), 15, 0)

#define VDCTRL3_MUX_SYNC_SIGNALS	BIT(29)
#define VDCTRL3_VSYNC_ONLY		BIT(28)
#define SET_HOR_WAIT_CNT(x)		REG_PUT((x), 27, 16)
#define GET_HOR_WAIT_CNT(x)		REG_GET((x), 27, 16)
#define SET_VERT_WAIT_CNT(x)		REG_PUT((x), 15, 0)
#define GET_VERT_WAIT_CNT(x)		REG_GET((x), 15, 0)

#define VDCTRL4_SET_DOTCLK_DLY(x)	REG_PUT((x), 31, 29) /* v4 only */
#define VDCTRL4_GET_DOTCLK_DLY(x)	REG_GET((x), 31, 29) /* v4 only */
#define VDCTRL4_SYNC_SIGNALS_ON		BIT(18)
#define SET_DOTCLK_H_VALID_DATA_CNT(x)	REG_PUT((x), 17, 0)

#define DEBUG0_HSYNC			BIT(26)
#define DEBUG0_VSYNC			BIT(25)

/* pigeon registers for crop */
#define HW_EPDC_PIGEON_12_0		0xb00
#define HW_EPDC_PIGEON_12_1		0xb10
#define HW_EPDC_PIGEON_12_2		0xb20

#define PIGEON_12_0_SET_STATE_MASK(x)	REG_PUT((x), 31, 24)
#define PIGEON_12_0_SET_MASK_CNT(x)	REG_PUT((x), 23, 12)
#define PIGEON_12_0_SET_MASK_CNT_SEL(x)	REG_PUT((x), 11,  8)
#define PIGEON_12_0_SET_OFFSET(x)	REG_PUT((x),  7,  4)
#define PIGEON_12_0_SET_INC_SEL(x)	REG_PUT((x),  3,  2)
#define PIGEON_12_0_POL_ACTIVE_LOW	BIT(1)
#define PIGEON_12_0_EN			BIT(0)

#define PIGEON_12_1_SET_CLR_CNT(x)	REG_PUT((x), 31, 16)
#define PIGEON_12_1_SET_SET_CNT(x)	REG_PUT((x), 15,  0)

#define MXSFB_MIN_XRES			120
#define MXSFB_MIN_YRES			120
#define MXSFB_MAX_XRES			0xffff
#define MXSFB_MAX_YRES			0xffff

#define RED 0
#define GREEN 1
#define BLUE 2
#define TRANSP 3

#define STMLCDIF_8BIT  1 /* pixel data bus to the display is of 8 bit width */
#define STMLCDIF_16BIT 0 /* pixel data bus to the display is of 16 bit width */
#define STMLCDIF_18BIT 2 /* pixel data bus to the display is of 18 bit width */
#define STMLCDIF_24BIT 3 /* pixel data bus to the display is of 24 bit width */

#define MXSFB_SYNC_DATA_ENABLE_HIGH_ACT	BIT(6)
#define MXSFB_SYNC_DOTCLK_FALLING_ACT	BIT(7) /* negative edge sampling */

#endif /* __MXSFB_REGS_H__ */
