/*
 * Copyright 2017 NXP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cache.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdesc.h>
#include <linux/fb.h>
#include <linux/freezer.h>
#include <linux/kfifo.h>
#include <linux/kthread.h>
#include <linux/regulator/consumer.h>
#include <linux/scatterlist.h>
#include <linux/videodev2.h>
#include <video/mxc_edid.h>
#include <linux/workqueue.h>

#include "mxc_dispdrv.h"

/* sub engines address start offset */
#define HDR_CHAN1_START		0x00000
#define HDR_CHAN2_START		0x04000
#define HDR_CHAN3_START		0x08000
#define HDR_OUT_START		0x0C000
#define DOLBY_VISION_START	0x10000
#define DOLBY_GRAPHIC_START	0x11000
#define DOLBY_OUTPUT_START	0x12000
#define DEC400D_CHAN1_START	0x15000
#define DTRC_CHAN2_START	0x16000
#define DTRC_CHAN3_START	0x17000
#define DPR_CHAN1_START		0x18000
#define DPR_CHAN2_START		0x19000
#define DPR_CHAN3_START		0x1A000
#define SUBSAM_START		0x1B000
#define SCALER_CHAN1_START	0x1C000
#define SCALER_CHAN2_START	0x1C400
#define SCALER_CHAN3_START	0x1C800
#define DTG_START		0x20000
#define WR_SCL_START		0x21000
#define RD_SRC_START		0x22000
#define CTX_LD_START		0x23000
#define LUT_LD_START		0x24000
#define IRQ_STEER_START		0x2D000
#define LPCG_START		0x2E000
#define BLK_CTRL_START		0x2F000

#define DCSS_MODE_SCALE_EN	(1 << 0)
#define DCSS_MODE_CSC_EN	(1 << 1)
#define DCSS_MODE_RESOLVE_EN	(1 << 2)
#define DCSS_MODE_DECOMPRESS_EN	(1 << 3)
#define DCSS_MODE_HDR10_EN	(1 << 4)
#define DCSS_MODE_DOLBY_EN	(1 << 5)

/* define several clocks rate */
#define DISP_AXI_RATE		800000000
#define DISP_APB_RATE		133000000
#define DISP_RTRAM_RATE		400000000
#define DISP_PIXEL_RATE		100000000

#define TILE_TYPE_LINEAR	0x0
#define TILE_TYPE_GPU_STANDARD	0x1
#define TILE_TYPE_GPU_SUPER	0x2
#define TILE_TYPE_VPU_2PYUV420	0x3
#define TILE_TYPE_VPU_2PVP9	0x4

#define PIXEL_STORE_NONCOMPRESS	0x1
#define PIXEL_STORE_COMPRESS	0x2

#define CSC_MODE_BYPASS		0x0
#define CSC_MODE_YUV2RGB	0x1
#define CSC_MODE_RGB2YUV	0x2

#define HSYNC_ACTIVE_HIGH	0x1
#define VSYNC_ACTIVE_HIGH	0x1
#define DE_ACTIVE_HIGH		0x1

/* pixel format macros */
#define PIX_FMT_A8R8G8B8	0x1
#define PIX_FMT_A2R10G10B10	0x2
#define PIX_FMT_1PYUV422	0x3
#define PIX_FMT_2PYUV420_8	0x4
#define PIX_FMT_2PYUV420_10	0x5
#define PIX_FMT_2PVP9_8		0x6
#define PIX_FMT_2PVP9_10	0x7
#define PIX_FMT_AYUV444		0x8

/* more formats fourcc define */
#define V4L2_PIX_FMT_A2R10G10B10 v4l2_fourcc('B', 'A', '3', '0') /* 32 ARGB-2-10-10-10 */

#define MAX_WIDTH		4096
#define MAX_HEIGHT		4096

#define RED			0
#define GREEN			1
#define BLUE			2
#define TRANSP			3

#define CTXLD_TYPE_DB		0x1
#define CTXLD_TYPE_SB		0x2

#define DCSS_REGS_SIZE		0x30000
#define DCSS_CFIFO_SIZE		0x100000			/* power of 2 */
#define DCSS_IRQS_NUM		32

/* define registers offset */
#define CTXLD_CTRL_STATUS	0x0
#define CTXLD_CTRL_STATUS_SET	0x4
#define CTXLD_CTRL_STATUS_CLR	0x8
#define CTXLD_CTRL_STATUS_TOG	0xC

#define CTXLD_DB_BASE_ADDR	0x10
#define CTXLD_DB_COUNT		0x14
#define CTXLD_SB_BASE_ADDR	0x18
#define CTXLD_SB_COUNT		0x1C

/* define dcss state */
#define DCSS_STATE_RESET	0x0
#define DCSS_STATE_RUNNING	0x1
#define DCSS_STATE_STOP		0x2

/* io memory blocks number */
#define IORESOURCE_MEM_NUM	0x2

#define USE_CTXLD		0x1

#define NAME_LEN		32

/* TODO: DCSS IRQs indexes, more added later */
#define IRQ_DPR_CH1		3
#define IRQ_DPR_CH2		4
#define IRQ_DPR_CH3		5
#define IRQ_CTX_LD		6
#define IRQ_DEC400D_CH1		7
#define IRQ_DTRC_CH2		8
#define IRQ_DTRC_CH3		9

/* ctxld irqs status */
#define RD_ERR			(1 << 16)
#define DB_COMP			(1 << 17)
#define SB_HP_COMP		(1 << 18)
#define SB_LP_COMP		(1 << 19)
#define AHB_ERR			(1 << 22)

/* ctxld irqs mask */
#define RD_ERR_EN		(1 << 2)
#define DB_COMP_EN		(1 << 3)
#define SB_HP_COMP_EN		(1 << 4)
#define SB_LP_COMP_EN		(1 << 5)
#define AHB_ERR_EN		(1 << 8)

/* channels */
#define DCSS_CHAN_MAIN		0
#define DCSS_CHAN_SECONDARY	1
#define DCSS_CHAN_THIRD		2
/* all channels are disabled */
#define DCSS_CHAN_NULL		3

/**
 * kfifo_to_end_len - returns the size from 'out' to buffer end
 * this is a kfifo extend interface as required
 */
#define kfifo_to_end_len(fifo) ((kfifo_size(fifo)) - ((fifo)->kfifo.in & (fifo)->kfifo.mask))

/* TODO: */
struct coordinate {
	uint32_t x;
	uint32_t y;
};

struct rectangle {
	uint32_t ulc_x;
	uint32_t ulc_y;
	uint32_t lrc_x;
	uint32_t lrc_y;
};

struct pix_fmt_info {
	uint32_t fourcc;
	uint32_t bpp;
	bool is_yuv;
};

struct dcss_pixmap {
	uint32_t channel_id;
	uint32_t width;
	uint32_t height;
	uint32_t bits_per_pixel;
	uint32_t pitch;
	struct rectangle crop;		/* active area */
	uint32_t format;
	uint32_t tile_type;		/* see TILE_TYPE_* macros */
	uint32_t pixel_store;		/* see PIXEL_STORE_* macros */
	uint32_t flags;
	dma_addr_t paddr;
};

/* Display state format in DRAM used by CTX_LD */
struct ctxld_unit {
	uint32_t reg_value;
	uint32_t reg_offset;
};

/* ctxld buffer */
struct cbuffer{
	void *sb_addr;
	void *db_addr;
	uint32_t sb_len;	/* buffer length in elements */
	uint32_t db_len;
	uint32_t sb_data_len;	/* data length in elements   */
	uint32_t db_data_len;
	uint32_t esize;		/* size per element */
};

struct ctxld_commit {
	struct list_head list;
	struct work_struct work;
	void *data;
	uint32_t sb_data_len;
	uint32_t sb_hp_data_len;
	uint32_t db_data_len;
	uint32_t sb_trig_pos;
	uint32_t db_trig_pos;
};

struct ctxld_fifo {
	uint32_t size;
	void *vaddr;
	dma_addr_t dma_handle;
	DECLARE_KFIFO_PTR(fifo, struct ctxld_unit);
	struct scatterlist sgl[1];
	uint32_t sgl_num;
	/* one consumer and multiple producer */
	spinlock_t wlock;
	struct completion complete;
};

/* Define Scaler Coefficients Array */
#define PHASE_NUM	16
#define TAP_NUM		7

#define COEFF_LUMA_VERTICAL	0x0
#define COEFF_LUMA_HORIZONTAL	0x1
#define COEFF_CHROMA_VERTICAL	0x2
#define COEFF_CHROMA_HORIZONTAL	0x3

#define LUMA_VERTICAL_OFF	0x0
#define LUMA_HORIZONTAL_OFF	0xC0
#define CHROMA_VERTICAL_OFF	0x180
#define CHROMA_HORIZONTAL_OFF	0x280

struct scaler_coeff_array {
	uint16_t luma_vertical[PHASE_NUM][TAP_NUM];
	uint16_t luma_horizontal[PHASE_NUM][TAP_NUM];
	uint16_t chroma_vertical[PHASE_NUM][TAP_NUM];
	uint16_t chroma_horizontal[PHASE_NUM][TAP_NUM];
};

/* channel info: 3 channels in DCSS */
struct dcss_channel_info {
	uint32_t channel_id;
	uint32_t channel_en;		/* channel 1 enable by default */
	struct platform_device *pdev;
	struct fb_info *fb_info;
	struct dcss_pixmap input;
	struct rectangle ch_pos;	/* display position in dtg for one channel */
	struct cbuffer cb;
	uint32_t hdr10_in_addr;
	uint32_t decomp_addr;
	uint32_t dpr_addr;
	uint32_t scaler_addr;
	int blank;			/* see FB_BLANK_* macros */
	uint32_t csc_mode;		/* see CSC_MODE_* macros */

	void *dev_data;			/* pointer to dcss_info */
};

struct dcss_channels {
	struct dcss_channel_info chan_info[3];
	uint32_t hdr10_out_addr;
	uint32_t subsam_addr;
	uint32_t dtg_addr;
	uint32_t wrscl_addr;
	uint32_t rdsrc_addr;
	uint32_t ctxld_addr;
	uint32_t lutld_addr;
	uint32_t hdmi_phy_addr;
	uint32_t irq_steer_addr;
	uint32_t lpcg_addr;
	uint32_t blk_ctrl_addr;
};

/* display info: output to monitor */
struct dcss_info {
	struct platform_device *pdev;
	void __iomem *base;
	void __iomem *blkctl_base;
	struct list_head ctxld_list;	/* manage context loader */
	spinlock_t llock;		/* list lock: for ctxld_list */
	int irqs[DCSS_IRQS_NUM];
	uint32_t irqs_num;
	uint32_t dcss_state;		/* see DCSS_STATE_* macros */
	struct clk *clk_axi;
	struct clk *clk_apb;
	struct clk *clk_rtram;
	struct clk *clk_dtrc;
	struct clk *clk_pix;
	struct regulator *power;
	struct ctxld_fifo cfifo;
	struct task_struct *handler;
	struct workqueue_struct *ctxld_wq;
	struct dcss_pixmap *output;
	struct dcss_channels chans;	/* maximum 3 channels
					 * TODO: better change to layer
					 */
	const struct fb_videomode *dft_disp_mode;	/* Default display mode */
	uint32_t tile_type;		/* see TILE_TYPE_* macros */
	uint32_t pixel_store;		/* see PIXEL_STORE_* macros */
	uint32_t csc_mode;		/* see CSC_MODE_* macros */
	uint32_t mode_flags;		/* see DCSS_MODE_* macros */
	uint32_t sync_flags;		/* see FB_SYNC_* macros   */
	uint32_t hsync_pol;
	uint32_t vsync_pol;
	uint32_t de_pol;
	char disp_dev[NAME_LEN];
	struct mxc_dispdrv_handle *dispdrv;

	atomic_t flush;
};

const struct fb_videomode imx_cea_mode[100] = {
	/* #1: 640x480p@59.94/60Hz 4:3 */
	[1] = {
		NULL, 60, 640, 480, 39722, 48, 16, 33, 10, 96, 2, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_4_3, 0,
	},
	/* #2: 720x480p@59.94/60Hz 4:3 */
	[2] = {
		NULL, 60, 720, 480, 37037, 60, 16, 30, 9, 62, 6, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_4_3, 0,
	},
	/* #3: 720x480p@59.94/60Hz 16:9 */
	[3] = {
		NULL, 60, 720, 480, 37037, 60, 16, 30, 9, 62, 6, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #4: 1280x720p@59.94/60Hz 16:9 */
	[4] = {
		NULL, 60, 1280, 720, 13468, 220, 110, 20, 5, 40, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0
	},
	/* #5: 1920x1080i@59.94/60Hz 16:9 */
	[5] = {
		NULL, 60, 1920, 1080, 13763, 148, 88, 15, 2, 44, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_INTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #6: 720(1440)x480iH@59.94/60Hz 4:3 */
	[6] = {
		NULL, 60, 1440, 480, 18554/*37108*/, 114, 38, 15, 4, 124, 3, 0,
		FB_VMODE_INTERLACED | FB_VMODE_ASPECT_4_3, 0,
	},
	/* #7: 720(1440)x480iH@59.94/60Hz 16:9 */
	[7] = {
		NULL, 60, 1440, 480, 18554/*37108*/, 114, 38, 15, 4, 124, 3, 0,
		FB_VMODE_INTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #8: 720(1440)x240pH@59.94/60Hz 4:3 */
	[8] = {
		NULL, 60, 1440, 240, 37108, 114, 38, 15, 4, 124, 3, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_4_3, 0,
	},
	/* #9: 720(1440)x240pH@59.94/60Hz 16:9 */
	[9] = {
		NULL, 60, 1440, 240, 37108, 114, 38, 15, 4, 124, 3, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #14: 1440x480p@59.94/60Hz 4:3 */
	[14] = {
		NULL, 60, 1440, 480, 18500, 120, 32, 30, 9, 124, 6, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_4_3, 0,
	},
	/* #15: 1440x480p@59.94/60Hz 16:9 */
	[15] = {
		NULL, 60, 1440, 480, 18500, 120, 32, 30, 9, 124, 6, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #16: 1920x1080p@60Hz 16:9 */
	[16] = {
		NULL, 60, 1920, 1080, 6734, 148, 88, 36, 4, 44, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #17: 720x576pH@50Hz 4:3 */
	[17] = {
		NULL, 50, 720, 576, 37037, 68, 12, 39, 5, 64, 5, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_4_3, 0,
	},
	/* #18: 720x576pH@50Hz 16:9 */
	[18] = {
		NULL, 50, 720, 576, 37037, 68, 12, 39, 5, 64, 5, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #19: 1280x720p@50Hz */
	[19] = {
		NULL, 50, 1280, 720, 13468, 220, 440, 20, 5, 40, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #20: 1920x1080i@50Hz */
	[20] = {
		NULL, 50, 1920, 1080, 13480, 148, 528, 15, 5, 528, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_INTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #23: 720(1440)x288pH@50Hz 4:3 */
	[23] = {
		NULL, 50, 1440, 288, 37037, 138, 24, 19, 2, 126, 3, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_4_3, 0,
	},
	/* #24: 720(1440)x288pH@50Hz 16:9 */
	[24] = {
		NULL, 50, 1440, 288, 37037, 138, 24, 19, 2, 126, 3, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #29: 720(1440)x576pH@50Hz 4:3 */
	[29] = {
		NULL, 50, 1440, 576, 18518, 136, 24, 39, 5, 128, 5, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_4_3, 0,
	},
	/* #30: 720(1440)x576pH@50Hz 16:9 */
	[30] = {
		NULL, 50, 1440, 576, 18518, 136, 24, 39, 5, 128, 5, 0,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #31: 1920x1080p@50Hz */
	[31] = {
		NULL, 50, 1920, 1080, 6734, 148, 528, 36, 4, 44, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #32: 1920x1080p@23.98/24Hz */
	[32] = {
		NULL, 24, 1920, 1080, 13468, 148, 638, 36, 4, 44, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #33: 1920x1080p@25Hz */
	[33] = {
		NULL, 25, 1920, 1080, 13468, 148, 528, 36, 4, 44, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #34: 1920x1080p@30Hz */
	[34] = {
		NULL, 30, 1920, 1080, 13468, 148, 88, 36, 4, 44, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0,
	},
	/* #41: 1280x720p@100Hz 16:9 */
	[41] = {
		NULL, 100, 1280, 720, 6734, 220, 440, 20, 5, 40, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0
	},
	/* #47: 1280x720p@119.88/120Hz 16:9 */
	[47] = {
		NULL, 120, 1280, 720, 6734, 220, 110, 20, 5, 40, 5,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0
	},
	/* #95: 3840x2160p@30Hz 16:9 */
	[95] = {
		NULL, 30, 3840, 2160, 3367, 296, 176, 72, 8, 88, 10,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0
	},
	/* #97: 3840x2160p@60Hz 16:9 */
	[97] = {
		NULL, 30, 3840, 2160, 1684, 296, 176, 72, 8, 88, 10,
		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		FB_VMODE_NONINTERLACED | FB_VMODE_ASPECT_16_9, 0
	},
};

static const struct of_device_id dcss_dt_ids[] ={
	{ .compatible = "fsl,imx8mq-dcss", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dcss_dt_ids);

static void dcss_free_fbmem(struct fb_info *fbi);
static int dcss_open(struct fb_info *fbi, int user);
static int dcss_check_var(struct fb_var_screeninfo *var,
			  struct fb_info *fbi);
static int dcss_set_par(struct fb_info *fbi);
static int dcss_setcolreg(unsigned regno, unsigned red, unsigned green,
		unsigned blue, unsigned transp, struct fb_info *info);
static int dcss_blank(int blank, struct fb_info *fbi);
static int dcss_pan_display(struct fb_var_screeninfo *var,
			    struct fb_info *fbi);

static struct fb_ops dcss_ops = {
	.owner = THIS_MODULE,
        .fb_open        = dcss_open,
	.fb_check_var	= dcss_check_var,
	.fb_set_par	= dcss_set_par,
	.fb_setcolreg	= dcss_setcolreg,
	.fb_blank	= dcss_blank,
	.fb_pan_display	= dcss_pan_display,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

/* TODO: more added later */
static const struct pix_fmt_info formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_ARGB32,
		.bpp = 32,
		.is_yuv = false,
	}, {
		.fourcc = V4L2_PIX_FMT_A2R10G10B10,
		.bpp = 32,
		.is_yuv = false,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.bpp = 16,
		.is_yuv = true,
	}, {
		.fourcc = V4L2_PIX_FMT_YVYU,
		.bpp = 16,
		.is_yuv = true,
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.bpp = 16,
		.is_yuv = true,
	}, {
		.fourcc = V4L2_PIX_FMT_VYUY,
		.bpp = 16,
		.is_yuv = true,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV32,
		.bpp = 32,
		.is_yuv = true,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.bpp = 8,
		.is_yuv = true,
	},
};

static const struct fb_bitfield def_a8r8g8b8[] = {
	[RED] = {
		.offset = 16,
		.length = 8,
		.msb_right = 0,
	},
	[GREEN] = {
		.offset = 8,
		.length = 8,
		.msb_right = 0,
	},
	[BLUE] = {
		.offset = 0,
		.length = 8,
		.msb_right = 0,
	},
	[TRANSP] = {
		.offset = 24,
		.length = 8,
		.msb_right = 0,
	}
};

static const struct fb_bitfield def_a2r10g10b10[] = {
	[RED] = {
		.offset = 20,
		.length = 10,
	},
	[GREEN] = {
		.offset = 10,
		.length = 10,
	},
	[BLUE] = {
		.offset = 0,
		.length = 10,
	},
	[TRANSP] = {
		.offset = 30,
		.length = 2,
	}
};

struct scaler_coeff_array coeffs = {
	.luma_vertical = {
		{0, 0, 0, 1027, 0, 0, 0},
		{0, 0, 85, 802, 139, 0, 0},
		{0, 0, 65, 786, 175, 1, 0},
		{0, 0, 49, 760, 218, 1, 0},
		{0, 0, 36, 723, 266, 2, 0},
		{0, 0, 26, 678, 320, 3, 0},
		{0, 0, 19, 625, 379, 4, 0},
		{0, 0, 13, 566, 441, 6, 0},
		{0, 0, 9, 504, 504, 9, 0},
		{0, 6, 441, 566, 13, 0, 0},
		{0, 4, 379, 625, 19, 0, 0},
		{0, 3, 320, 678, 26, 0, 0},
		{0, 2, 266, 723, 36, 0, 0},
		{0, 1, 218, 760, 49, 0, 0},
		{0, 1, 175, 786, 65, 0, 0},
		{0, 0, 139, 802, 85, 0, 0},
	},
	.luma_horizontal = {
		{0, 0, 0, 1024, 0, 0, 0},
		{0, 0, 84, 800, 139, 0, 0},
		{0, 0, 64, 784, 175, 1, 0},
		{0, 0, 48, 757, 217, 1, 0},
		{0, 0, 36, 721, 265, 2, 0},
		{0, 0, 26, 676, 319, 3, 0},
		{0, 0, 19, 623, 378, 4, 0},
		{0, 0, 13, 565, 440, 6, 0},
		{0, 0, 9, 503, 503, 9, 0},
		{0, 6, 440, 565, 13, 0, 0},
		{0, 4, 378, 623, 19, 0, 0},
		{0, 3, 319, 676, 26, 0, 0},
		{0, 2, 265, 721, 36, 0, 0},
		{0, 1, 217, 757, 48, 0, 0},
		{0, 1, 175, 784, 64, 0, 0},
		{0, 0, 139, 800, 84, 0, 0},
	},
	.chroma_vertical = {
		{0, 0, 0, 1027, 0, 0, 0},
		{0, 0, 85, 802, 139, 0, 0},
		{0, 0, 65, 786, 175, 1, 0},
		{0, 0, 49, 760, 218, 1, 0},
		{0, 0, 36, 723, 266, 2, 0},
		{0, 0, 26, 678, 320, 3, 0},
		{0, 0, 19, 625, 379, 4, 0},
		{0, 0, 13, 566, 441, 6, 0},
		{0, 0, 9, 504, 504, 9, 0},
		{0, 6, 441, 566, 13, 0, 0},
		{0, 4, 379, 625, 19, 0, 0},
		{0, 3, 320, 678, 26, 0, 0},
		{0, 2, 266, 723, 36, 0, 0},
		{0, 1, 218, 760, 49, 0, 0},
		{0, 1, 175, 786, 65, 0, 0},
		{0, 0, 139, 802, 85, 0, 0},
	},
	.chroma_horizontal = {
		{0, 0, 0, 1024, 0, 0, 0},
		{0, 0, 84, 800, 139, 0, 0},
		{0, 0, 64, 784, 175, 1, 0},
		{0, 0, 48, 757, 217, 1, 0},
		{0, 0, 36, 721, 265, 2, 0},
		{0, 0, 26, 676, 319, 3, 0},
		{0, 0, 19, 623, 378, 4, 0},
		{0, 0, 13, 565, 440, 6, 0},
		{0, 0, 9, 503, 503, 9, 0},
		{0, 6, 440, 565, 13, 0, 0},
		{0, 4, 378, 623, 19, 0, 0},
		{0, 3, 319, 676, 26, 0, 0},
		{0, 2, 265, 721, 36, 0, 0},
		{0, 1, 217, 757, 48, 0, 0},
		{0, 1, 175, 784, 64, 0, 0},
		{0, 0, 139, 800, 84, 0, 0},
	},
};

static const struct pix_fmt_info *get_fmt_info(uint32_t fourcc)
{
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].fourcc == fourcc)
			return &formats[i];
	}

	return NULL;
}

/* TODO: writel ? */
#define fill_unit(uint, offset, value) {		\
	unit->reg_value  = value;			\
	unit->reg_offset = offset;			\
}

static void fill_sb(struct cbuffer *cb,
		    uint32_t offset,
		    uint32_t value)
{
	struct ctxld_unit *unit = NULL;

	BUG_ON(!cb);

	if (unlikely(cb->sb_data_len == cb->sb_len)) {
		/* sb is full */
		BUG_ON(1);
	}

	unit = (struct ctxld_unit *)(cb->sb_addr + cb->sb_data_len * cb->esize);

	fill_unit(unit, offset, value);
	cb->sb_data_len++;
}

#if 0
static void fill_db(struct cbuffer *cb,
		    uint32_t offset,
		    uint32_t value)
{
	struct ctxld_unit *unit = NULL;

	BUG_ON(!cb);

	if (unlikely(cb->db_data_len == cb->db_len)) {
		/* db is full */
		BUG_ON(1);
	}

	unit = (struct ctxld_unit *)(cb->db_addr + cb->db_data_len * cb->esize);

	fill_unit(unit, offset, value);
	cb->db_data_len++;
}
#endif

static void coeff_array_fill(int type,
			     uint32_t base,
			     struct cbuffer *cb)
{
	uint32_t i, offset;
	uint16_t (*array)[TAP_NUM];

	switch (type) {
	case COEFF_LUMA_VERTICAL:
		offset = LUMA_VERTICAL_OFF;
		array  = coeffs.luma_vertical;
		break;
	case COEFF_LUMA_HORIZONTAL:
		offset = LUMA_HORIZONTAL_OFF;
		array  = coeffs.luma_horizontal;
		break;
	case COEFF_CHROMA_VERTICAL:
		offset = CHROMA_VERTICAL_OFF;
		array  = coeffs.chroma_vertical;
		break;
	case COEFF_CHROMA_HORIZONTAL:
		offset = CHROMA_HORIZONTAL_OFF;
		array  = coeffs.chroma_horizontal;
		break;
	default:
		return;
	}

	for (i = 0; i < PHASE_NUM; i++) {
		fill_sb(cb, base + 0x80 + offset + (i << 2),
			(array[i][0] & 0xfff) << 16 |
			(array[i][1] & 0xfff) << 4  |
			(array[i][2] & 0xf00) >> 8);

		fill_sb(cb, base + 0xC0 + offset + (i << 2),
			(array[i][2] & 0x0ff) << 20 |
			(array[i][3] & 0xfff) << 8  |
			(array[i][4] & 0xff0) >> 4);

		fill_sb(cb, base + 0x100 + offset + (i << 2),
			(array[i][4] & 0x00f) << 24 |
			(array[i][5] & 0xfff) << 12 |
			(array[i][6] & 0xfff));
	}
}

static void scaler_coeff_config(struct dcss_channel_info *cinfo)
{
	struct cbuffer *cb = &cinfo->cb;

	/* config Luma Vertical Coefficients */
	coeff_array_fill(COEFF_LUMA_VERTICAL, cinfo->scaler_addr, cb);

	/* config Luma Horizontal Coefficients */
	coeff_array_fill(COEFF_LUMA_HORIZONTAL, cinfo->scaler_addr, cb);

	/* config Chroma Vertical Coefficients */
	coeff_array_fill(COEFF_CHROMA_VERTICAL, cinfo->scaler_addr, cb);

	/* config Chroma Horizontal Coefficients */
	coeff_array_fill(COEFF_CHROMA_HORIZONTAL, cinfo->scaler_addr, cb);
}

static void ctxld_fifo_info_print(struct ctxld_fifo *cfifo)
{
	pr_debug("%s: print kfifo info: **********\n", __func__);
	pr_debug("in = 0x%x, out = 0x%x, mask = 0x%x\n",
		 cfifo->fifo.kfifo.in,
		 cfifo->fifo.kfifo.out,
		 cfifo->fifo.kfifo.mask);
}

static int ctxld_fifo_alloc(struct device *dev,
			    struct ctxld_fifo *cfifo,
			    uint32_t fifo_size)
{
	if (!cfifo || fifo_size < 2)
		return -EINVAL;

	fifo_size = roundup_pow_of_two(fifo_size);
	cfifo->vaddr = dma_alloc_coherent(dev, fifo_size,
					  &cfifo->dma_handle,
					  GFP_DMA | GFP_KERNEL);
	if (!cfifo->vaddr) {
		dev_err(dev, "allocate ctxld fifo failed\n");
		return -ENOMEM;
	}

	cfifo->size = fifo_size;
	kfifo_init(&cfifo->fifo,
		   page_address(phys_to_page(dma_to_phys(dev, cfifo->dma_handle))),
		   fifo_size);

	/* TODO: sgl num can be changed if required */
	cfifo->sgl_num = 1;

	spin_lock_init(&cfifo->wlock);
	init_completion(&cfifo->complete);

	return 0;
}

static void ctxld_fifo_free(struct device *dev,
			    struct ctxld_fifo *cfifo)
{
	if (!cfifo)
		return;

	/* TODO: wait fifo flush empty */

	kfifo_reset(&cfifo->fifo);

	dma_free_coherent(dev, cfifo->size,
			  cfifo->vaddr,
			  cfifo->dma_handle);

	cfifo->size  = 0;
	cfifo->vaddr = NULL;
	cfifo->dma_handle = 0;

	memset(cfifo->sgl, 0x0, sizeof(*cfifo->sgl));
	cfifo->sgl_num = 0;
}

static int dcss_clks_get(struct dcss_info *info)
{
	int ret = 0;
	struct platform_device *pdev = info->pdev;

	info->clk_axi = devm_clk_get(&pdev->dev, "axi");
	if (IS_ERR(info->clk_axi)) {
		ret = PTR_ERR(info->clk_axi);
		goto out;
	}

	info->clk_apb = devm_clk_get(&pdev->dev, "apb");
	if (IS_ERR(info->clk_apb)) {
		ret = PTR_ERR(info->clk_apb);
		goto put_axi;
	}

	info->clk_rtram = devm_clk_get(&pdev->dev, "rtram");
	if (IS_ERR(info->clk_rtram)) {
		ret = PTR_ERR(info->clk_rtram);
		goto put_apb;
	}

	info->clk_dtrc = devm_clk_get(&pdev->dev, "dtrc");
	if (IS_ERR(info->clk_dtrc)) {
		ret = PTR_ERR(info->clk_dtrc);
		goto put_rtram;
	}

	info->clk_pix = devm_clk_get(&pdev->dev, "pix");
	if (IS_ERR(info->clk_pix)) {
		ret = PTR_ERR(info->clk_pix);
		goto put_dtrc;
	}

	goto out;

put_dtrc:
	devm_clk_put(&pdev->dev, info->clk_dtrc);
put_rtram:
	devm_clk_put(&pdev->dev, info->clk_rtram);
put_apb:
	devm_clk_put(&pdev->dev, info->clk_apb);
put_axi:
	devm_clk_put(&pdev->dev, info->clk_axi);
out:
	return ret;
}

#if 0
static void dcss_clks_put(struct dcss_info *info)
{
	struct platform_device *pdev = info->pdev;

	devm_clk_put(&pdev->dev, info->clk_axi);
	devm_clk_put(&pdev->dev, info->clk_apb);
	devm_clk_put(&pdev->dev, info->clk_rtram);
	devm_clk_put(&pdev->dev, info->clk_dtrc);
	devm_clk_put(&pdev->dev, info->clk_pix);
}
#endif

static void fb_var_to_pixmap(struct dcss_pixmap *pixmap,
			     struct fb_var_screeninfo *var)
{
	BUG_ON(!pixmap || !var);

	pixmap->width  = var->xres;
	pixmap->height = var->yres;
	pixmap->bits_per_pixel = var->bits_per_pixel;
	pixmap->pitch  = var->width * (var->bits_per_pixel >> 3);
	pixmap->crop.ulc_x = 0;
	pixmap->crop.ulc_y = 0;
	pixmap->crop.lrc_x = var->xres;
	pixmap->crop.lrc_y = var->yres;
	pixmap->format = var->grayscale;

	/* TODO possible passed through 'reserved' ? */
	pixmap->tile_type = TILE_TYPE_LINEAR;
	pixmap->pixel_store = PIXEL_STORE_NONCOMPRESS;
}

static int fill_one_chan_info(uint32_t chan_id,
			      struct dcss_channel_info *info)
{
	void *dsb;	/* mem to store ctxld units */
	struct cbuffer *cb;
	struct platform_device *pdev;

	BUG_ON(!info || IS_ERR(info));
	pdev = info->pdev;

	if (chan_id > 2) {
		dev_err(&pdev->dev, "incorrect channel number: %d\n", chan_id);
		return -EINVAL;
	}

	info->channel_id = chan_id;
	info->channel_en = (chan_id == 0) ? 1 : 0;
	info->blank = FB_BLANK_NORMAL;

	switch (chan_id) {
	case 0:
		info->hdr10_in_addr = HDR_CHAN1_START;
		info->decomp_addr = DEC400D_CHAN1_START;
		info->dpr_addr = DPR_CHAN1_START;
		info->scaler_addr = SCALER_CHAN1_START;
		break;
	case 1:
		info->hdr10_in_addr = HDR_CHAN2_START;
		info->decomp_addr = DTRC_CHAN2_START;
		info->dpr_addr = DPR_CHAN2_START;
		info->scaler_addr = SCALER_CHAN2_START;
		break;
	case 2:
		info->hdr10_in_addr = HDR_CHAN3_START;
		info->decomp_addr = DTRC_CHAN3_START;
		info->dpr_addr = DPR_CHAN3_START;
		info->scaler_addr = SCALER_CHAN3_START;
		break;
	default:
		return -EINVAL;
	}

	/* dsb is used to hold double & single buffer units */
	dsb = devm_kzalloc(&pdev->dev, DCSS_REGS_SIZE << 1, GFP_KERNEL);
	if (!dsb)
		return -ENOMEM;

	/* init cbuffer struct */
	cb = &info->cb;
	cb->esize = sizeof(struct ctxld_unit);

	cb->sb_addr = dsb;
	cb->sb_len  = DCSS_REGS_SIZE / cb->esize;
	cb->sb_data_len = 0x0;

	cb->db_addr = dsb + DCSS_REGS_SIZE;
	cb->db_len  = DCSS_REGS_SIZE / cb->esize;
	cb->db_data_len = 0x0;

	return 0;
}

/* allocate fb info for one channel */
static int alloc_one_fbinfo(struct dcss_channel_info *cinfo)
{
	struct platform_device *pdev;

	BUG_ON(!cinfo || IS_ERR(cinfo));

	pdev = cinfo->pdev;
	if (!pdev)
		return -ENODEV;

	cinfo->fb_info = framebuffer_alloc(0, &pdev->dev);
	if (!cinfo->fb_info) {
		dev_err(&pdev->dev, "failed to alloc fb info for channel %d\n",
			cinfo->channel_id);
		return -ENOMEM;
	}

	cinfo->fb_info->par = cinfo;
	INIT_LIST_HEAD(&cinfo->fb_info->modelist);

	return 0;
}

static struct fb_info *get_one_fbinfo(uint32_t ch_id,
				      struct dcss_channels *chans)
{
	struct dcss_channel_info *cinfo;

	if (ch_id > 2)
		return NULL;

	cinfo = &chans->chan_info[ch_id];

	return cinfo->fb_info;
}

static int init_chan_pixmap(struct dcss_channel_info *cinfo)
{
	struct dcss_pixmap *pixmap;
	struct fb_info *fbi;
	struct fb_var_screeninfo *var;

	BUG_ON(!cinfo || IS_ERR(cinfo));

	pixmap = &cinfo->input;
	fbi = cinfo->fb_info;
	var = &fbi->var;

	fb_var_to_pixmap(pixmap, var);

	return 0;
}

static int init_ch_pos(struct dcss_channel_info *cinfo)
{
	struct rectangle *pos;
	struct fb_info *fbi;
	struct fb_var_screeninfo *var;

	/* TODO: init ch_pos with var temporarily */
	pos = &cinfo->ch_pos;
	fbi = cinfo->fb_info;
	var = &fbi->var;

	pos->ulc_x = var->left_margin + var->hsync_len - 1;
	pos->ulc_y = var->upper_margin + var->lower_margin +
		     var->vsync_len - 1;
	pos->lrc_x = var->xres + var->left_margin +
		     var->hsync_len - 1;
	pos->lrc_y = var->yres + var->upper_margin +
		     var->lower_margin + var->vsync_len - 1;

	return 0;
}

static int dcss_init_chans(struct dcss_info *info)
{
	struct dcss_channels *dcss_chans = &info->chans;

	/* init sharable info between chans */
	dcss_chans->hdr10_out_addr = HDR_OUT_START;
	dcss_chans->subsam_addr = SUBSAM_START;
	dcss_chans->dtg_addr   = DTG_START;
	dcss_chans->wrscl_addr = WR_SCL_START;
	dcss_chans->rdsrc_addr = RD_SRC_START;
	dcss_chans->ctxld_addr = CTX_LD_START;
	dcss_chans->lutld_addr = LUT_LD_START;
	dcss_chans->hdmi_phy_addr  = 0;
	dcss_chans->irq_steer_addr = IRQ_STEER_START;
	dcss_chans->lpcg_addr = LPCG_START;
	dcss_chans->blk_ctrl_addr  = BLK_CTRL_START;

	return 0;
}

static int dcss_init_fbinfo(struct fb_info *fbi)
{
	int ret;
	struct dcss_channel_info *cinfo = fbi->par;
	struct dcss_info *info = cinfo->dev_data;
	struct fb_fix_screeninfo *fix = &fbi->fix;
	struct fb_var_screeninfo *var = &fbi->var;
	struct fb_modelist *modelist;

	fbi->fbops = &dcss_ops;

	/* init fix screeninfo */
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->ypanstep  = 1;
	fix->ywrapstep = 1;
	fix->visual = FB_VISUAL_TRUECOLOR;
	fix->accel = FB_ACCEL_NONE;

	ret = fb_add_videomode(info->dft_disp_mode, &fbi->modelist);
	if (ret)
		return ret;

	/* init var screeninfo */
	modelist = list_first_entry(&fbi->modelist,
			struct fb_modelist, list);
	fb_videomode_to_var(var, &modelist->mode);
	var->nonstd = 0;
	var->activate = FB_ACTIVATE_NOW;
	var->vmode = FB_VMODE_NONINTERLACED;
	/* default format */
	var->grayscale = V4L2_PIX_FMT_ARGB32;

	/* Allocate memory buffer: Maybe need alignment */
	fix->smem_len = (fix->line_length * var->yres_virtual > SZ_32M) ?
			 fix->line_length * var->yres_virtual : SZ_32M;
	fbi->screen_base = dma_alloc_writecombine(fbi->device, fix->smem_len,
						  (dma_addr_t *)&fix->smem_start,
						  GFP_DMA | GFP_KERNEL);
	if (!fbi->screen_base) {
		dev_err(fbi->device, "Unable to alloc fb memory\n");
		fix->smem_len = 0;
		fix->smem_start = 0;
		return -ENOMEM;
	}
	dev_dbg(fbi->device, "%s: smem_start = 0x%lx, screen_base = 0x%p\n",
			      __func__, fix->smem_start, fbi->screen_base);

	fbi->screen_size = fix->smem_len;

	fbi->pseudo_palette = devm_kzalloc(fbi->device,
					   sizeof(u32) * 16, GFP_KERNEL);
	if (!fbi->pseudo_palette) {
		dev_err(fbi->device, "alloc pseudo_palette failed\n");
		dcss_free_fbmem(fbi);
		return -ENOMEM;
	}


	/* clear screen content */
	memset((void*)fbi->screen_base, 0x0, fix->smem_len);

	if (dcss_check_var(var, fbi)) {
		devm_kfree(fbi->device, fbi->pseudo_palette);
		dcss_free_fbmem(fbi);
		return -EINVAL;
	}

	return 0;
}

static void dcss_free_fbmem(struct fb_info *fbi)
{
	struct fb_fix_screeninfo *fix = &fbi->fix;

	dma_free_writecombine(fbi->device, fix->smem_len,
			      fbi->screen_base,
			      (dma_addr_t)fix->smem_start);

	fbi->screen_base = NULL;
	fix->smem_start  = 0;
	fix->smem_len    = 0;
}

static int dcss_clks_enable(struct dcss_info *info)
{
	int ret = 0;
	struct platform_device *pdev = info->pdev;

	/* TODO: Add return value check */
	ret = clk_prepare_enable(info->clk_axi);
	if (ret) {
		dev_err(&pdev->dev, "enable axi clock failed\n");
		return ret;
	}

	ret = clk_prepare_enable(info->clk_apb);
	if (ret) {
		dev_err(&pdev->dev, "enable apb clock failed\n");
		goto disable_axi;
	}

	ret = clk_prepare_enable(info->clk_rtram);
	if (ret) {
		dev_err(&pdev->dev, "enable rtram clock failed\n");
		goto disable_apb;
	}

	ret = clk_prepare_enable(info->clk_dtrc);
	if (ret) {
		dev_err(&pdev->dev, "enable dtrc clock failed\n");
		goto disable_rtram;
	}

	ret = clk_prepare_enable(info->clk_pix);
	if (ret) {
		dev_err(&pdev->dev, "enable pix clock failed\n");
		goto disable_dtrc;
	}

	goto out;

disable_dtrc:
	clk_disable_unprepare(info->clk_dtrc);
disable_rtram:
	clk_disable_unprepare(info->clk_rtram);
disable_apb:
	clk_disable_unprepare(info->clk_apb);
disable_axi:
	clk_disable_unprepare(info->clk_axi);
out:
	return ret;
}

#if 0
static void dcss_clks_disable(struct dcss_info *info)
{
	clk_disable_unprepare(info->clk_axi);
	clk_disable_unprepare(info->clk_apb);
	clk_disable_unprepare(info->clk_rtram);
	clk_disable_unprepare(info->clk_dtrc);
	clk_disable_unprepare(info->clk_pix);
}
#endif

static int dcss_clks_rate_set(struct dcss_info *info)
{
	int ret;
	uint32_t pix_clk_rate;
	struct platform_device *pdev  = info->pdev;

	/* TODO: axi, abp, rtrm clock rate are set by uboot already */
	ret = clk_set_rate(info->clk_axi, DISP_AXI_RATE);
	if (ret) {
		dev_err(&pdev->dev, "set axi clock rate failed\n");
		return ret;
	}

	ret = clk_set_rate(info->clk_apb, DISP_APB_RATE);
	if (ret) {
		dev_err(&pdev->dev, "set apb clock rate failed\n");
		return ret;
	}

	ret = clk_set_rate(info->clk_rtram, DISP_RTRAM_RATE);
	if (ret) {
		dev_err(&pdev->dev, "set rtram clock rate failed\n");
		return ret;
	}

	pix_clk_rate = PICOS2KHZ(info->dft_disp_mode->pixclock) * 1000U;
	dev_dbg(&pdev->dev, "%s: pix clock rate = %u\n", __func__, pix_clk_rate);

	ret = clk_set_rate(info->clk_pix, pix_clk_rate);
	if (ret) {
		dev_err(&pdev->dev, "set pixel clock rate failed, ret = %d\n", ret);
		return ret;
	}

	return 0;
}

static int dcss_dec400d_config(struct dcss_info *info,
			       bool decompress, bool resolve)
{
	struct dcss_channel_info *chan_info;
	struct cbuffer *cb;

	if (resolve == true)
		return -EINVAL;

	/* dec400d always in channel 1 */
	chan_info = &info->chans.chan_info[0];
	cb = &chan_info->cb;

	if (decompress == true) {
		/* TODO: configure decompress */
		;
	} else {
		/* TODO: configure bypass */
		;
	}

	return 0;
}

static int dcss_dtrc_config(uint32_t dtrc_ch,
			    struct dcss_info *info,
			    bool decompress,
			    bool resolve)
{
	struct platform_device *pdev = info->pdev;
	struct dcss_channel_info *chan_info;
	struct cbuffer *cb;

	if (dtrc_ch != 1 && dtrc_ch != 2) {
		dev_err(&pdev->dev, "invalid dtrc channel number\n");
		return -EINVAL;
	}

	chan_info = &info->chans.chan_info[dtrc_ch];
	cb = &chan_info->cb;

	if (!decompress && !resolve) {
#if USE_CTXLD
		/* Bypass DTRC */
		fill_sb(cb, chan_info->decomp_addr + 0xc8, 0x2);
#else
		writel(0x2, info->base + chan_info->decomp_addr + 0xc8);
#endif
	} else {
		/* TODO: decompress & resolve config */
		;
	}

	return 0;
}

static int dcss_decomp_config(uint32_t decomp_ch, struct dcss_info *info)
{
	bool need_decomp, need_resolve;
	struct platform_device *pdev = info->pdev;
	struct dcss_channel_info *chan_info;
	struct dcss_pixmap *input;

	if (decomp_ch > 2) {
		dev_err(&pdev->dev, "invalid decompression channel number\n");
		return -EINVAL;
	}

	chan_info = &info->chans.chan_info[decomp_ch];
	input = &chan_info->input;

	switch (input->pixel_store) {
	case PIXEL_STORE_NONCOMPRESS:
		need_decomp = false;
		break;
	case PIXEL_STORE_COMPRESS:
		need_decomp = true;
		break;
	default:
		dev_err(&pdev->dev, "invalid pixel store type\n");
		return -EINVAL;
	}

	switch (input->tile_type) {
	case TILE_TYPE_LINEAR:
	case TILE_TYPE_GPU_STANDARD:
	case TILE_TYPE_GPU_SUPER:
		need_resolve = false;
		break;
	case TILE_TYPE_VPU_2PYUV420:
	case TILE_TYPE_VPU_2PVP9:
		need_resolve = true;
		break;
	default:
		dev_err(&pdev->dev, "invalid buffer tile type\n");
		return -EINVAL;
	}

	switch (decomp_ch) {
	case 0:		/* DEC400D */
		dcss_dec400d_config(info, need_decomp, need_resolve);
		break;
	case 1:		/* DTRC1   */
	case 2:		/* DTRC2   */
		dcss_dtrc_config(decomp_ch, info,
				 need_decomp, need_resolve);
		break;
	default:
		dev_err(&pdev->dev, "invalid ch num = %d\n", decomp_ch);
		break;
	}

	return 0;
}

static int dcss_dpr_config(uint32_t dpr_ch, struct dcss_info *info)
{
	uint32_t pitch;
	bool need_resolve = false;
	struct platform_device *pdev = info->pdev;
	struct dcss_channels *chans = &info->chans;
	struct dcss_channel_info *chan_info;
	struct fb_info *fbi;
	struct fb_fix_screeninfo *fix;
	struct fb_var_screeninfo *var;
	struct dcss_pixmap *input;
	struct cbuffer *cb;

	if (dpr_ch > 2) {
		dev_err(&pdev->dev, "invalid dpr channel number\n");
		return -EINVAL;
	}

	chan_info = &chans->chan_info[dpr_ch];
	fbi = chan_info->fb_info;
	fix = &fbi->fix;
	var = &fbi->var;
	input = &chan_info->input;

	if (dpr_ch == 0) {
		switch (input->tile_type) {
		case TILE_TYPE_LINEAR:
			need_resolve = false;
			break;
		case TILE_TYPE_GPU_STANDARD:
		case TILE_TYPE_GPU_SUPER:
			need_resolve = true;
			break;
		default:
			return -EINVAL;
		}
	}
	/* For channel 2,3, 'Tile Resolve' will be done by DTRC */

#if !USE_CTXLD
	writel(fix->smem_start, info->base + chan_info->dpr_addr + 0xc0);
	writel(0x2, info->base + chan_info->dpr_addr + 0x90);
	writel(var->xres, info->base + chan_info->dpr_addr + 0xa0);
	writel(var->yres, info->base + chan_info->dpr_addr + 0xb0);

	/* TODO: second plane config for YUV2P formats */
	writel(fix->smem_start + var->xres * var->yres,
	       info->base + chan_info->dpr_addr + 0x110);
	writel(var->xres, info->base + chan_info->dpr_addr + 0xf0);
	writel(var->yres, info->base + chan_info->dpr_addr + 0x100);

	/* TODO: calculate pitch for different formats */
	pitch = (var->xres * (var->bits_per_pixel >> 3)) << 16;
	writel(pitch, info->base + chan_info->dpr_addr + 0x70);

	if (!need_resolve) {
		/* Bypass resolve */
		writel(0xe4203, info->base + chan_info->dpr_addr + 0x50);
	} else {
		/* configure resolve */
		;
	}

	writel(0x38, info->base + chan_info->dpr_addr + 0x200);
	writel(0x4, info->base + chan_info->dpr_addr + 0x0);

	/* Trigger DPR on */
	writel(0x4, info->base + chan_info->dpr_addr + 0x0);
	writel(0x5, info->base + chan_info->dpr_addr + 0x0);
#else
	cb = &chan_info->cb;

	fill_sb(cb, chan_info->dpr_addr + 0xc0, fix->smem_start);
	fill_sb(cb, chan_info->dpr_addr + 0x90, 0x2);
	fill_sb(cb, chan_info->dpr_addr + 0xa0, input->width);
	fill_sb(cb, chan_info->dpr_addr + 0xb0, input->height);

	/* TODO: calculate pitch for different formats */
	/* config pitch */
	pitch = (var->xres * (var->bits_per_pixel >> 3)) << 16;
	fill_sb(cb, chan_info->dpr_addr + 0x70, pitch);

	if (!need_resolve) {
		/* Bypass resolve */
		fill_sb(cb, chan_info->dpr_addr + 0x50, 0xe4203);
	} else {
		/* TODO: configure resolve */
		;
	}

	fill_sb(cb, chan_info->dpr_addr + 0x200, 0x38);

	/* Trigger DPR on */
	fill_sb(cb, chan_info->dpr_addr + 0x0, 0x5);
#endif

	return 0;
}

static int dcss_scaler_config(uint32_t scaler_ch, struct dcss_info *info)
{
	struct platform_device *pdev = info->pdev;
	struct dcss_channels *chans = &info->chans;
	struct dcss_channel_info *chan_info;
	struct fb_info *fbi;
	struct fb_var_screeninfo *var;
	struct dcss_pixmap *input;
	struct cbuffer *cb;

	if (scaler_ch > 2) {
		dev_err(&pdev->dev, "invalid scaler channel number\n");
		return -EINVAL;
	}

	chan_info = &chans->chan_info[scaler_ch];
	fbi = chan_info->fb_info;
	var = &fbi->var;
	input = &chan_info->input;
#if !USE_CTXLD
	writel(0x0, info->base + chan_info->scaler_addr + 0x8);
	writel(0x0, info->base + chan_info->scaler_addr + 0xc);
	writel(0x2, info->base + chan_info->scaler_addr + 0x10);	/* src format */
	writel(0x2, info->base + chan_info->scaler_addr + 0x14);	/* dst format */

	writel((var->xres - 1) | (var->yres - 1) << 16,
		info->base + chan_info->scaler_addr + 0x18);	/* src resolution */
	writel((var->xres - 1) | (var->yres - 1) << 16,
		info->base + chan_info->scaler_addr + 0x1c);
	writel((var->xres - 1) | (var->yres - 1) << 16,
		info->base + chan_info->scaler_addr + 0x20);	/* dst resolution */
	writel((var->xres - 1) | (var->yres - 1) << 16,
		info->base + chan_info->scaler_addr + 0x24);
	writel(0x0, info->base + chan_info->scaler_addr + 0x28);
	writel(0x0, info->base + chan_info->scaler_addr + 0x2c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x30);
	writel(0x0, info->base + chan_info->scaler_addr + 0x34);
	writel(0x0, info->base + chan_info->scaler_addr + 0x38);
	writel(0x0, info->base + chan_info->scaler_addr + 0x3c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x40);
	writel(0x0, info->base + chan_info->scaler_addr + 0x44);

	/* scale ratio: ###.#_####_####_#### */
	writel(0x0, info->base + chan_info->scaler_addr + 0x48);
	writel(0x2000, info->base + chan_info->scaler_addr + 0x4c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x50);
	writel(0x2000, info->base + chan_info->scaler_addr + 0x54);
	writel(0x0, info->base + chan_info->scaler_addr + 0x58);
	writel(0x2000, info->base + chan_info->scaler_addr + 0x5c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x60);
	writel(0x2000, info->base + chan_info->scaler_addr + 0x64);

	writel(0x0, info->base + chan_info->scaler_addr + 0x80);

	writel(0x40000, info->base + chan_info->scaler_addr + 0xc0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x100);
	writel(0x0, info->base + chan_info->scaler_addr + 0x84);
	writel(0x0, info->base + chan_info->scaler_addr + 0xc4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x104);
	writel(0x0, info->base + chan_info->scaler_addr + 0x88);
	writel(0x0, info->base + chan_info->scaler_addr + 0xc8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x108);
	writel(0x0, info->base + chan_info->scaler_addr + 0x8c);
	writel(0x0, info->base + chan_info->scaler_addr + 0xcc);
	writel(0x0, info->base + chan_info->scaler_addr + 0x10c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x90);
	writel(0x0, info->base + chan_info->scaler_addr + 0xd0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x110);
	writel(0x0, info->base + chan_info->scaler_addr + 0x94);
	writel(0x0, info->base + chan_info->scaler_addr + 0xd4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x114);
	writel(0x0, info->base + chan_info->scaler_addr + 0x98);
	writel(0x0, info->base + chan_info->scaler_addr + 0xd8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x118);
	writel(0x0, info->base + chan_info->scaler_addr + 0x9c);
	writel(0x0, info->base + chan_info->scaler_addr + 0xdc);
	writel(0x0, info->base + chan_info->scaler_addr + 0x11c);
	writel(0x0, info->base + chan_info->scaler_addr + 0xa0);
	writel(0x0, info->base + chan_info->scaler_addr + 0xe0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x120);
	writel(0x0, info->base + chan_info->scaler_addr + 0xa4);
	writel(0x0, info->base + chan_info->scaler_addr + 0xe4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x124);
	writel(0x0, info->base + chan_info->scaler_addr + 0xa8);
	writel(0x0, info->base + chan_info->scaler_addr + 0xe8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x128);
	writel(0x0, info->base + chan_info->scaler_addr + 0xac);
	writel(0x0, info->base + chan_info->scaler_addr + 0xec);
	writel(0x0, info->base + chan_info->scaler_addr + 0x12c);
	writel(0x0, info->base + chan_info->scaler_addr + 0xb0);
	writel(0x0, info->base + chan_info->scaler_addr + 0xf0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x130);
	writel(0x0, info->base + chan_info->scaler_addr + 0xb4);
	writel(0x0, info->base + chan_info->scaler_addr + 0xf4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x134);
	writel(0x0, info->base + chan_info->scaler_addr + 0xb8);
	writel(0x0, info->base + chan_info->scaler_addr + 0xf8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x138);
	writel(0x0, info->base + chan_info->scaler_addr + 0xbc);
	writel(0x0, info->base + chan_info->scaler_addr + 0xfc);
	writel(0x0, info->base + chan_info->scaler_addr + 0x13c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x140);

	writel(0x40000, info->base + chan_info->scaler_addr + 0x180);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1c0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x144);
	writel(0x0, info->base + chan_info->scaler_addr + 0x184);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1c4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x148);
	writel(0x0, info->base + chan_info->scaler_addr + 0x188);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1c8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x14c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x18c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1cc);
	writel(0x0, info->base + chan_info->scaler_addr + 0x150);
	writel(0x0, info->base + chan_info->scaler_addr + 0x190);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1d0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x154);
	writel(0x0, info->base + chan_info->scaler_addr + 0x194);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1d4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x158);
	writel(0x0, info->base + chan_info->scaler_addr + 0x198);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1d8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x15c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x19c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1dc);
	writel(0x0, info->base + chan_info->scaler_addr + 0x160);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1a0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1e0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x164);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1a4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1e4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x168);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1a8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1e8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x16c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1ac);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1ec);
	writel(0x0, info->base + chan_info->scaler_addr + 0x170);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1b0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1f0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x174);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1b4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1f4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x178);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1b8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1f8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x17c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1bc);
	writel(0x0, info->base + chan_info->scaler_addr + 0x1fc);

	writel(0x0, info->base + chan_info->scaler_addr + 0x300);
	writel(0x0, info->base + chan_info->scaler_addr + 0x340);
	writel(0x0, info->base + chan_info->scaler_addr + 0x380);
	writel(0x0, info->base + chan_info->scaler_addr + 0x304);
	writel(0x0, info->base + chan_info->scaler_addr + 0x344);
	writel(0x0, info->base + chan_info->scaler_addr + 0x384);
	writel(0x0, info->base + chan_info->scaler_addr + 0x308);
	writel(0x0, info->base + chan_info->scaler_addr + 0x348);
	writel(0x0, info->base + chan_info->scaler_addr + 0x388);
	writel(0x0, info->base + chan_info->scaler_addr + 0x30c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x34c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x38c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x310);
	writel(0x0, info->base + chan_info->scaler_addr + 0x350);
	writel(0x0, info->base + chan_info->scaler_addr + 0x390);
	writel(0x0, info->base + chan_info->scaler_addr + 0x314);
	writel(0x0, info->base + chan_info->scaler_addr + 0x354);
	writel(0x0, info->base + chan_info->scaler_addr + 0x394);
	writel(0x0, info->base + chan_info->scaler_addr + 0x318);
	writel(0x0, info->base + chan_info->scaler_addr + 0x358);
	writel(0x0, info->base + chan_info->scaler_addr + 0x398);
	writel(0x0, info->base + chan_info->scaler_addr + 0x31c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x35c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x39c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x320);
	writel(0x0, info->base + chan_info->scaler_addr + 0x360);
	writel(0x0, info->base + chan_info->scaler_addr + 0x3a0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x324);
	writel(0x0, info->base + chan_info->scaler_addr + 0x364);
	writel(0x0, info->base + chan_info->scaler_addr + 0x3a4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x328);
	writel(0x0, info->base + chan_info->scaler_addr + 0x368);
	writel(0x0, info->base + chan_info->scaler_addr + 0x3a8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x32c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x36c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x3ac);
	writel(0x0, info->base + chan_info->scaler_addr + 0x330);
	writel(0x0, info->base + chan_info->scaler_addr + 0x370);
	writel(0x0, info->base + chan_info->scaler_addr + 0x3b0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x334);
	writel(0x0, info->base + chan_info->scaler_addr + 0x374);
	writel(0x0, info->base + chan_info->scaler_addr + 0x3b4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x338);
	writel(0x0, info->base + chan_info->scaler_addr + 0x378);
	writel(0x0, info->base + chan_info->scaler_addr + 0x3b8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x33c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x37c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x3bc);

	writel(0x0, info->base + chan_info->scaler_addr + 0x200);
	writel(0x0, info->base + chan_info->scaler_addr + 0x240);
	writel(0x0, info->base + chan_info->scaler_addr + 0x280);
	writel(0x0, info->base + chan_info->scaler_addr + 0x204);
	writel(0x0, info->base + chan_info->scaler_addr + 0x244);
	writel(0x0, info->base + chan_info->scaler_addr + 0x284);
	writel(0x0, info->base + chan_info->scaler_addr + 0x208);
	writel(0x0, info->base + chan_info->scaler_addr + 0x248);
	writel(0x0, info->base + chan_info->scaler_addr + 0x288);
	writel(0x0, info->base + chan_info->scaler_addr + 0x20c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x24c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x28c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x210);
	writel(0x0, info->base + chan_info->scaler_addr + 0x250);
	writel(0x0, info->base + chan_info->scaler_addr + 0x290);
	writel(0x0, info->base + chan_info->scaler_addr + 0x214);
	writel(0x0, info->base + chan_info->scaler_addr + 0x254);
	writel(0x0, info->base + chan_info->scaler_addr + 0x294);
	writel(0x0, info->base + chan_info->scaler_addr + 0x218);
	writel(0x0, info->base + chan_info->scaler_addr + 0x258);
	writel(0x0, info->base + chan_info->scaler_addr + 0x298);
	writel(0x0, info->base + chan_info->scaler_addr + 0x21c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x25c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x29c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x220);
	writel(0x0, info->base + chan_info->scaler_addr + 0x260);
	writel(0x0, info->base + chan_info->scaler_addr + 0x2a0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x224);
	writel(0x0, info->base + chan_info->scaler_addr + 0x264);
	writel(0x0, info->base + chan_info->scaler_addr + 0x2a4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x228);
	writel(0x0, info->base + chan_info->scaler_addr + 0x268);
	writel(0x0, info->base + chan_info->scaler_addr + 0x2a8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x22c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x26c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x2ac);
	writel(0x0, info->base + chan_info->scaler_addr + 0x230);
	writel(0x0, info->base + chan_info->scaler_addr + 0x270);
	writel(0x0, info->base + chan_info->scaler_addr + 0x2b0);
	writel(0x0, info->base + chan_info->scaler_addr + 0x234);
	writel(0x0, info->base + chan_info->scaler_addr + 0x274);
	writel(0x0, info->base + chan_info->scaler_addr + 0x2b4);
	writel(0x0, info->base + chan_info->scaler_addr + 0x238);
	writel(0x0, info->base + chan_info->scaler_addr + 0x278);
	writel(0x0, info->base + chan_info->scaler_addr + 0x2b8);
	writel(0x0, info->base + chan_info->scaler_addr + 0x23c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x27c);
	writel(0x0, info->base + chan_info->scaler_addr + 0x2bc);

	/* Trigger Scaler on */
	writel(0x11, info->base + chan_info->scaler_addr + 0x0);
#else
	cb = &chan_info->cb;

	fill_sb(cb, chan_info->scaler_addr + 0x8, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0xc, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x10, 0x2);	/* src format */
	fill_sb(cb, chan_info->scaler_addr + 0x14, 0x2);	/* dst format */

	fill_sb(cb, chan_info->scaler_addr + 0x18,
		(input->height - 1) << 16 | (input->width - 1)); /*src resolution*/
	fill_sb(cb, chan_info->scaler_addr + 0x1c,
		(input->height - 1) << 16 | (input->width - 1));

	fill_sb(cb, chan_info->scaler_addr + 0x20,
		(var->yres - 1) << 16 | (var->xres - 1));
	fill_sb(cb, chan_info->scaler_addr + 0x24,
		(var->yres - 1) << 16 | (var->xres - 1));

	fill_sb(cb, chan_info->scaler_addr + 0x28, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x2c, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x30, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x34, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x38, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x3c, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x40, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x44, 0x0);

	/* scale ratio: ###.#_####_####_#### */
	fill_sb(cb, chan_info->scaler_addr + 0x48, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x4c, 0x2000);
	fill_sb(cb, chan_info->scaler_addr + 0x50, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x54, 0x2000);
	fill_sb(cb, chan_info->scaler_addr + 0x58, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x5c, 0x2000);
	fill_sb(cb, chan_info->scaler_addr + 0x60, 0x0);
	fill_sb(cb, chan_info->scaler_addr + 0x64, 0x2000);

	scaler_coeff_config(chan_info);

	/* Trigger Scaler on */
	fill_sb(cb, chan_info->scaler_addr + 0x0, 0x11);
#endif
	return 0;
}

static int dcss_dtg_start(struct dcss_info *info)
{
	uint32_t dtg_lrc_x, dtg_lrc_y;
	uint32_t dis_ulc_x, dis_ulc_y;
	uint32_t dis_lrc_x, dis_lrc_y;
	struct dcss_channels *chans = &info->chans;
	struct dcss_channel_info *chan_info;
	struct fb_info *fbi;
	struct fb_var_screeninfo *var;
	struct dcss_pixmap *input;
	struct cbuffer *cb;

	chan_info = &chans->chan_info[0];
	fbi = chan_info->fb_info;
	var = &fbi->var;
	input = &chan_info->input;
	cb = &chan_info->cb;

	/* Display Timing Config */
	dtg_lrc_x = var->xres + var->left_margin +
		var->right_margin + var->hsync_len - 1;
	dtg_lrc_y = var->yres + var->upper_margin +
		var->lower_margin + var->vsync_len - 1;
	writel(dtg_lrc_y << 16 | dtg_lrc_x, info->base + chans->dtg_addr + 0x4);

	/* global output timing */
	dis_ulc_x = var->left_margin + var->hsync_len - 1;
	dis_ulc_y = var->upper_margin + var->lower_margin +
		var->vsync_len - 1;
	writel(dis_ulc_y << 16 | dis_ulc_x, info->base + chans->dtg_addr + 0x8);

	dis_lrc_x = var->xres + var->left_margin +
		var->hsync_len - 1;
	dis_lrc_y = var->yres + var->upper_margin +
		var->lower_margin + var->vsync_len - 1;
	writel(dis_lrc_y << 16 | dis_lrc_x, info->base + chans->dtg_addr + 0xc);

	writel(0xff000100, info->base + chans->dtg_addr + 0x0);

	return 0;
}

static void dtg_channel_timing_config(struct dcss_channel_info *cinfo)
{
	struct cbuffer *cb;
	uint32_t ch_ulc_reg, ch_lrc_reg;
	struct fb_info *fbi = cinfo->fb_info;
	struct rectangle *pos = &cinfo->ch_pos;
	struct platform_device *pdev = cinfo->pdev;
	struct dcss_info *info = cinfo->dev_data;
	struct dcss_channels *chans = &info->chans;

	switch (fbi->node) {
	case 0:
		ch_ulc_reg = 0x10;
		ch_lrc_reg = 0x14;
		break;
	case 1:
		ch_ulc_reg = 0x18;
		ch_lrc_reg = 0x1c;
		break;
	case 2:
		ch_ulc_reg = 0x20;
		ch_lrc_reg = 0x24;
		break;
	default:
		dev_err(&pdev->dev, "%s: invalid channel number %d\n",
			__func__, fbi->node);
		return;
	}

	cb = &cinfo->cb;

	fill_sb(cb, chans->dtg_addr + ch_ulc_reg,
		pos->ulc_y << 16 | pos->ulc_x);
	fill_sb(cb, chans->dtg_addr + ch_lrc_reg,
		pos->lrc_y << 16 | pos->lrc_x);
}

static void dtg_global_timing_config(struct dcss_info *info)
{
	struct cbuffer *cb;
	uint32_t dtg_lrc_x, dtg_lrc_y;
	uint32_t dis_ulc_x, dis_ulc_y;
	uint32_t dis_lrc_x, dis_lrc_y;
	struct dcss_channels *chans = &info->chans;
	struct dcss_channel_info *cmain;
	const struct fb_videomode *dmode = info->dft_disp_mode;

	/* only main channel can change dtg timings */
	cmain = &chans->chan_info[DCSS_CHAN_MAIN];
	cb = &cmain->cb;

	/* Display Timing config */
	dtg_lrc_x = dmode->xres + dmode->left_margin +
		    dmode->right_margin + dmode->hsync_len - 1;
	dtg_lrc_y = dmode->yres + dmode->upper_margin +
		    dmode->lower_margin + dmode->vsync_len - 1;
	fill_sb(cb, chans->dtg_addr + 0x4, dtg_lrc_y << 16 | dtg_lrc_x);

	/* Active Region Timing config*/
	dis_ulc_x = dmode->left_margin  + dmode->hsync_len - 1;
	dis_ulc_y = dmode->upper_margin + dmode->lower_margin +
		    dmode->vsync_len - 1;
	fill_sb(cb, chans->dtg_addr + 0x8, dis_ulc_y << 16 | dis_ulc_x);

	dis_lrc_x = dmode->xres + dmode->left_margin +
		    dmode->hsync_len - 1;
	dis_lrc_y = dmode->yres + dmode->upper_margin +
		    dmode->lower_margin + dmode->vsync_len - 1;
	fill_sb(cb, chans->dtg_addr + 0xc, dis_lrc_y << 16 | dis_lrc_x);
}

static int dcss_dtg_config(uint32_t ch_id, struct dcss_info *info)
{
	uint32_t global_alpha;
	struct platform_device *pdev = info->pdev;
	struct dcss_channels *chans = &info->chans;
	struct dcss_channel_info *cinfo;
	struct cbuffer *cb;

	if (ch_id > 2) {
		dev_err(&pdev->dev, "invalid channel id\n");
		return -EINVAL;
	}

	cinfo = &chans->chan_info[ch_id];
	cb = &cinfo->cb;

#if USE_CTXLD
	if (ch_id == DCSS_CHAN_MAIN)
		dtg_global_timing_config(info);

	/* TODO: Channel Timing Config */
	dtg_channel_timing_config(cinfo);

	/* Trigger DTG on */
	if (ch_id == DCSS_CHAN_MAIN) {
		/* TODO: use global alpha temporarily */
		global_alpha = 0xff;
		/* db and sb trigger positions */
		fill_sb(cb, chans->dtg_addr + 0x28, 0xb000a);
		fill_sb(cb, chans->dtg_addr + 0x0, 0xff00018c);
	}
#endif
	return 0;
}

static int dcss_subsam_config(struct dcss_info *info)
{
	uint32_t hsync_pol, vsync_pol, de_pol;
	uint32_t disp_lrc_x, disp_lrc_y;
	uint32_t hsync_start, hsync_end;
	uint32_t vsync_start, vsync_end;
	uint32_t de_ulc_x, de_ulc_y;
	uint32_t de_lrc_x, de_lrc_y;
	struct dcss_channels *chans = &info->chans;
	struct dcss_channel_info *chan_info;
	struct fb_info *fbi;
	struct fb_var_screeninfo *var;
	struct cbuffer *cb;

	/* using channel 0 by default */
	chan_info = &chans->chan_info[0];
	fbi = chan_info->fb_info;
	var = &fbi->var;
	cb = &chan_info->cb;

	/* TODO: for 1080p only */
	hsync_pol = 1;
	vsync_pol = 1;
	de_pol = 1;

#if USE_CTXLD
	/* 3 tap fir filters coefficients */
	fill_sb(cb, chans->subsam_addr + 0x70, 0x41614161);
	fill_sb(cb, chans->subsam_addr + 0x80, 0x03ff0000);
	fill_sb(cb, chans->subsam_addr + 0x90, 0x03ff0000);
#else
	writel(0x41614161, info->base + chans->subsam_addr + 0x70);
	writel(0x03ff0000, info->base + chans->subsam_addr + 0x80);
	writel(0x03ff0000, info->base + chans->subsam_addr + 0x90);
#endif

	/* Timing Config */
	disp_lrc_x = var->xres + var->left_margin +
		     var->right_margin + var->hsync_len - 1;
	disp_lrc_y = var->yres + var->upper_margin +
		     var->lower_margin + var->vsync_len - 1;
#if USE_CTXLD
	fill_sb(cb, chans->subsam_addr + 0x10,
		disp_lrc_y << 16 | disp_lrc_x);
#else
	writel(disp_lrc_y << 16 | disp_lrc_x,
	       info->base + chans->subsam_addr + 0x10);
#endif

	/* horizontal sync will be asserted when
	 * horizontal count == START
	 */
	hsync_start = var->xres + var->left_margin +
		      var->right_margin + var->hsync_len - 1;
	hsync_end   = var->hsync_len - 1;
#if USE_CTXLD
	fill_sb(cb, chans->subsam_addr + 0x20,
		(hsync_pol << 31) | hsync_end << 16 | hsync_start);
#else
	writel((hsync_pol << 31) | hsync_end << 16 | hsync_start,
	       info->base + chans->subsam_addr + 0x20);
#endif

	vsync_start = var->lower_margin - 1;
	vsync_end   = var->lower_margin + var->vsync_len - 1;
#if USE_CTXLD
	fill_sb(cb, chans->subsam_addr + 0x30,
		(vsync_pol << 31) | vsync_end << 16 | vsync_start);
#else
	writel((vsync_pol << 31) | vsync_end << 16 | vsync_start,
	       info->base + chans->subsam_addr + 0x30);
#endif

	de_ulc_x = var->left_margin + var->hsync_len - 1;
	de_ulc_y = var->upper_margin + var->lower_margin +
		   var->vsync_len;
#if USE_CTXLD
	fill_sb(cb, chans->subsam_addr + 0x40,
		(de_pol << 31) | de_ulc_y << 16 | de_ulc_x);
#else
	writel((de_pol << 31) | de_ulc_y << 16 | de_ulc_x,
	       info->base + chans->subsam_addr + 0x40);
#endif

	de_lrc_x = var->xres + var->left_margin +
		   var->hsync_len - 1;
	de_lrc_y = var->yres + var->upper_margin +
		   var->lower_margin + var->vsync_len - 1;
#if USE_CTXLD
	fill_sb(cb, chans->subsam_addr + 0x50,
		de_lrc_y << 16 | de_lrc_x);

	/* Trigger Subsam on */
	fill_sb(cb, chans->subsam_addr + 0x0, 0x1);
#else
	writel(de_lrc_y << 16 | de_lrc_x,
	       info->base + chans->subsam_addr + 0x50);
	writel(0x1, info->base + chans->subsam_addr + 0x0);
#endif

	return 0;
}

static int dcss_hdr10_input_config(uint32_t hdr_ch, struct dcss_info *info)
{
	struct platform_device *pdev = info->pdev;
	struct dcss_channels *chans;
	struct dcss_channel_info *chan_info;
	struct cbuffer *cb;

	if (hdr_ch > 2) {
		dev_err(&pdev->dev, "invalid hdr channel id\n");
		return -EINVAL;
	}

	chans = &info->chans;
	chan_info = &chans->chan_info[hdr_ch];
	cb = &chan_info->cb;

#if USE_CTXLD
	/* disable float-to-fixed converter */
	fill_sb(cb, chan_info->hdr10_in_addr + 0x3874, 0x0);
	/* disable LUT */
	fill_sb(cb, chan_info->hdr10_in_addr + 0x3080, 0x0);
	/* disable CSCA */
	fill_sb(cb, chan_info->hdr10_in_addr + 0x3000, 0x0);
	/* disable CSCB */
	fill_sb(cb, chan_info->hdr10_in_addr + 0x3800, 0x0);
#else
	writel(0x0, info->base + chan_info->hdr10_in_addr + 0x3874);
	writel(0x0, info->base + chan_info->hdr10_in_addr + 0x3080);
	writel(0x0, info->base + chan_info->hdr10_in_addr + 0x3000);
	writel(0x0, info->base + chan_info->hdr10_in_addr + 0x3800);
#endif

	return 0;
}

static int dcss_hdr10_output_config(struct dcss_info *info)
{
	struct dcss_channels *chans;
	struct dcss_channel_info *chan_info;
	struct cbuffer *cb;

	chans = &info->chans;
	/* using channel 0 by default */
	chan_info = &chans->chan_info[0];
	cb = &chan_info->cb;

#if USE_CTXLD
	fill_sb(cb, chans->hdr10_out_addr + 0x3004, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3008, 0x1);
	fill_sb(cb, chans->hdr10_out_addr + 0x300c, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3010, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3014, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3018, 0x1);
	fill_sb(cb, chans->hdr10_out_addr + 0x301c, 0x1);
	fill_sb(cb, chans->hdr10_out_addr + 0x3020, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3024, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3028, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x302c, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3030, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3034, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3038, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x303c, 0x0);

	fill_sb(cb, chans->hdr10_out_addr + 0x3040, 0xffffffff);
	fill_sb(cb, chans->hdr10_out_addr + 0x3044, 0xffffffff);
	fill_sb(cb, chans->hdr10_out_addr + 0x3048, 0xffffffff);
	fill_sb(cb, chans->hdr10_out_addr + 0x304c, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3050, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3054, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3058, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x305c, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3060, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3064, 0x0);
	fill_sb(cb, chans->hdr10_out_addr + 0x3068, 0xffffffff);
	fill_sb(cb, chans->hdr10_out_addr + 0x306c, 0xffffffff);
	fill_sb(cb, chans->hdr10_out_addr + 0x3070, 0xffffffff);
	fill_sb(cb, chans->hdr10_out_addr + 0x3074, 0x0);

	fill_sb(cb, chans->hdr10_out_addr + 0x3000, 0x3);
#else
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3004);
	writel(0x1, info->base + chans->hdr10_out_addr + 0x3008);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x300c);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3010);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3014);
	writel(0x1, info->base + chans->hdr10_out_addr + 0x3018);
	writel(0x1, info->base + chans->hdr10_out_addr + 0x301c);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3020);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3024);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3028);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x302c);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3030);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3034);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3038);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x303c);
	writel(0xffffffff, info->base + chans->hdr10_out_addr + 0x3040);
	writel(0xffffffff, info->base + chans->hdr10_out_addr + 0x3044);
	writel(0xffffffff, info->base + chans->hdr10_out_addr + 0x3048);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x304c);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3050);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3054);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3058);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x305c);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3060);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3064);
	writel(0xffffffff, info->base + chans->hdr10_out_addr + 0x3068);
	writel(0xffffffff, info->base + chans->hdr10_out_addr + 0x306c);
	writel(0xffffffff, info->base + chans->hdr10_out_addr + 0x3070);
	writel(0x0, info->base + chans->hdr10_out_addr + 0x3074);

	writel(0x3, info->base + chans->hdr10_out_addr + 0x3000);
#endif

	return 0;
}

static void ctxld_irq_unmask(uint32_t irq_en, struct dcss_info *info)
{
	struct dcss_channels *chans = &info->chans;

	writel(irq_en, info->base + chans->ctxld_addr + CTXLD_CTRL_STATUS_SET);
}

static void dcss_ctxld_config(struct work_struct *work)
{
	int ret;
	uint32_t ctxld_ctrl = 0;
	uint32_t dsb_len, nsgl, esize;
	struct dcss_info *info;
	struct platform_device *pdev;
	struct dcss_channels *chans;
	struct ctxld_commit *cc;
	struct ctxld_fifo *cfifo;

	cc = container_of(work, struct ctxld_commit, work);
	info = (struct dcss_info *)cc->data;
	pdev = info->pdev;
	chans = &info->chans;
	cfifo = &info->cfifo;
	dsb_len = cc->sb_data_len + cc->db_data_len;
	esize = sizeof(struct ctxld_unit);

	sg_init_table(cfifo->sgl, cfifo->sgl_num);
	nsgl = kfifo_dma_out_prepare(&cfifo->fifo, cfifo->sgl,
				     cfifo->sgl_num, dsb_len);
	BUG_ON(!nsgl);

	if (nsgl == 1) {
		if (cfifo->sgl[0].length != dsb_len * esize)
			BUG_ON(1);
	}

	/* configure sb buffer */
	if (cc->sb_data_len) {
		/* cfifo first store sb and than store db */
		writel(phys_to_dma(&pdev->dev, sg_phys(cfifo->sgl)),
		       info->base + chans->ctxld_addr + CTXLD_SB_BASE_ADDR);
		writel(cc->sb_hp_data_len |
		       (cc->sb_data_len - cc->sb_hp_data_len),
		       info->base + chans->ctxld_addr + CTXLD_SB_COUNT);
	}

	if (cc->db_data_len) {
		writel(cfifo->dma_handle + cfifo->sgl[0].offset +
		       cc->sb_data_len * kfifo_esize(&cfifo->fifo),
		       info->base + chans->ctxld_addr + CTXLD_DB_BASE_ADDR);
		writel(cc->db_data_len,
		       info->base + chans->ctxld_addr + CTXLD_DB_COUNT);
	}

	ctxld_irq_unmask(DB_COMP_EN, info);
	ctxld_irq_unmask(SB_HP_COMP_EN, info);
	ctxld_irq_unmask(SB_LP_COMP_EN, info);
	ctxld_irq_unmask(AHB_ERR_EN, info);

	ctxld_ctrl = readl(info->base + chans->ctxld_addr + CTXLD_CTRL_STATUS);
	ctxld_ctrl |= (1 << 0);

	writel(ctxld_ctrl, info->base + chans->ctxld_addr + CTXLD_CTRL_STATUS);

	/* wait finish */
	reinit_completion(&cfifo->complete);
	ret = wait_for_completion_timeout(&cfifo->complete, HZ);
	if (!ret)	/* timeout */
		dev_err(&pdev->dev, "wait ctxld finish timeout\n");

	ctxld_fifo_info_print(cfifo);
	kfifo_dma_out_finish(&cfifo->fifo,
		(cc->sb_data_len + cc->db_data_len) * kfifo_esize(&cfifo->fifo));
	ctxld_fifo_info_print(cfifo);

	kfree(cc);

	dev_dbg(&pdev->dev, "finish ctxld config\n");
}

static int commit_to_fifo(uint32_t channel,
			  struct dcss_info *info)
{
	unsigned long irqflags;
	uint32_t count = 0, commit_size;
	struct platform_device *pdev = info->pdev;
	struct dcss_channels *chans;
	struct dcss_channel_info *chan_info;
	struct ctxld_fifo *cfifo;
	struct ctxld_commit *cc;
	struct cbuffer *cb;
	struct ctxld_unit *unit = NULL;

	if (channel > 2 || !info)
		return -EINVAL;

	cc = (struct ctxld_commit *)kzalloc(sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;
	INIT_LIST_HEAD(&cc->list);

	cfifo = &info->cfifo;
	chans = &info->chans;
	chan_info = &chans->chan_info[channel];
	cb = &chan_info->cb;
	commit_size = cb->sb_data_len + cb->db_data_len;

#if 0
	spin_lock_irqsave(&cfifo->wlock, irqflags);

	if (unlikely(commit_size > kfifo_to_end_len(&cfifo->fifo))) {
		atomic_set(&info->flush, 1);
		spin_unlock_irqrestore(&cfifo->wlock, irqflags);
		/* TODO: Wait fifo flush empty to avoid fifo wrap */
		flush_workqueue(info->ctxld_wq);
	} else
		spin_unlock_irqrestore(&cfifo->wlock, irqflags);

#endif
	spin_lock_irqsave(&cfifo->wlock, irqflags);

	unit = (struct ctxld_unit *)cb->sb_addr;

	if (cb->sb_data_len) {
		count = kfifo_in(&cfifo->fifo, cb->sb_addr, cb->sb_data_len);
		if (count != cb->sb_data_len) {
			/* TODO: this case should be completely ignored */
			dev_err(&pdev->dev, "write sb data mismatch\n");
			count = kfifo_out(&cfifo->fifo, cb->sb_addr, count);
			BUG_ON(1);
		}
		cc->sb_data_len = count;
	}

	if (cb->db_data_len) {
		count = kfifo_in(&cfifo->fifo, cb->db_addr, cb->db_data_len);
		if (count != cb->db_data_len) {
			/* TODO: this case should be completely ignored */
			dev_err(&pdev->dev, "write db data mismatch\n");
			count = kfifo_out(&cfifo->fifo, cb->db_addr, count);
			BUG_ON(1);
		}
		cc->db_data_len = count;
	}

	/* TODO: this can be refined */
	__dma_flush_area(cfifo->fifo.kfifo.data, cfifo->size);

	ctxld_fifo_info_print(cfifo);

	/* empty sb and db buffer */
	cb->db_data_len = 0;
	cb->sb_data_len = 0;

	spin_unlock_irqrestore(&cfifo->wlock, irqflags);

	/* queue the work to workqueue */
	cc->data = info;
	INIT_WORK(&cc->work, dcss_ctxld_config);
	queue_work(info->ctxld_wq, &cc->work);

	return 0;
}

static int dcss_open(struct fb_info *fbi, int user)
{
	int fb_node = fbi->node;
	struct dcss_channel_info *cinfo = fbi->par;
	struct dcss_info *info = cinfo->dev_data;
	struct dcss_channels *chans = &info->chans;
	struct dcss_channel_info *chan_info;
	struct cbuffer *cb;

	if (fb_node < 0 || fb_node > 2)
		BUG_ON(1);

	chan_info = &chans->chan_info[fb_node];
	cb = &chan_info->cb;

	if (fb_node == 0)
		return 0;

	return 0;
}

static int dcss_check_var(struct fb_var_screeninfo *var,
			  struct fb_info *fbi)
{
	uint32_t fb_size;
	struct dcss_channel_info *cinfo = fbi->par;
	struct dcss_info *info = cinfo->dev_data;
	struct platform_device *pdev = info->pdev;
	const struct fb_bitfield *rgb = NULL;
	const struct pix_fmt_info *format = NULL;
	struct fb_fix_screeninfo *fix = &fbi->fix;

	if (var->xres > MAX_WIDTH || var->yres > MAX_HEIGHT) {
		dev_err(&pdev->dev, "unsupport display resolution\n");
		return -EINVAL;
	}

	if (var->xres_virtual > var->xres) {
		dev_err(&pdev->dev, "stride not supported\n");
		return -EINVAL;
	}

	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	switch (var->grayscale) {
	case 0:		/* TODO: color */
		break;
	case 1:		/* grayscale */
		return -EINVAL;
	default:	/* fourcc */
		format = get_fmt_info(var->grayscale);
		if (!format) {
			dev_err(&pdev->dev, "unsupport pixel format\n");
			return -EINVAL;
		}
		var->bits_per_pixel = format->bpp;
	}
	fix->line_length = var->xres * (var->bits_per_pixel >> 3);
	fb_size = var->yres_virtual * fix->line_length;

	if (fb_size > fix->smem_len) {
		dev_err(&pdev->dev, "exceeds fb size limit!\n");
		return -ENOMEM;
	}

	if (format && !format->is_yuv) {
		switch (format->fourcc) {
		case V4L2_PIX_FMT_ARGB32:
			rgb = def_a8r8g8b8;
			break;
		case V4L2_PIX_FMT_A2R10G10B10:
			rgb = def_a2r10g10b10;
			break;
		default:
			dev_err(&pdev->dev, "unsupport pixel format\n");
			return -EINVAL;
		}

		var->red    = rgb[RED];
		var->green  = rgb[GREEN];
		var->blue   = rgb[BLUE];
		var->transp = rgb[TRANSP];
	} else {
		/* TODO: YUV format */
		;
	}

	return 0;
}

static int config_channel_pipe(struct dcss_channel_info *cinfo)
{
	int ret = 0;
	int fb_node;
	struct fb_info *fbi = cinfo->fb_info;
	struct dcss_info *info = cinfo->dev_data;
	struct platform_device *pdev = info->pdev;

	fb_node = fbi->node;

	dev_info(&cinfo->pdev->dev, "begin config pipe %d\n", fb_node);

	/* configure all the sub modules on one channel:
	 * 1. DEC400D/DTRC
	 * 2. DPR
	 * 3. SCALER
	 * 4. HDR10_INPUT
	 */
	ret = dcss_decomp_config(fb_node, info);
	if (ret) {
		dev_err(&pdev->dev, "decomp config failed\n");
		goto out;
	}

	ret = dcss_dpr_config(fb_node, info);
	if (ret) {
		dev_err(&pdev->dev, "dpr config failed\n");
		goto out;
	}

	ret = dcss_scaler_config(fb_node, info);
	if (ret) {
		dev_err(&pdev->dev, "scaler config failed\n");
		goto out;
	}

	ret = dcss_hdr10_input_config(fb_node, info);
	if (ret) {
		dev_err(&pdev->dev, "hdr10 input config failed\n");
		goto out;
	}

out:
	return ret;
}

static int dcss_set_par(struct fb_info *fbi)
{
	int ret = 0, saved_blank;
	int fb_node = fbi->node;
	struct dcss_channel_info *cinfo = fbi->par;
	struct dcss_info *info = cinfo->dev_data;
	struct platform_device *pdev = info->pdev;
	struct dcss_channels *chans = &info->chans;
	struct dcss_channel_info *chan_info;
	struct cbuffer *cb;

	if (fb_node < 0 || fb_node > 2)
		BUG_ON(1);

	chan_info = &chans->chan_info[fb_node];
	cb = &chan_info->cb;

	saved_blank = chan_info->blank;

	if (saved_blank == FB_BLANK_UNBLANK) {
		/* blank the fb if is not to
		 * stop dcss for a while
		 */
		ret = dcss_blank(FB_BLANK_NORMAL, fbi);
	}

	ret = config_channel_pipe(chan_info);
	if (ret)
		goto fail;

	/* dcss output timings can only be set for fb0 */
	if (!fb_node) {
		ret = dcss_hdr10_output_config(info);
		if (ret) {
			dev_err(&pdev->dev, "hdr10 output config failed\n");
			goto fail;
		}
	}

	if (saved_blank == FB_BLANK_UNBLANK) {
		/* unblank the fb if need */
		ret = dcss_blank(FB_BLANK_UNBLANK, fbi);
	}

	goto out;

fail:
	/* drop any ctxld_uint already
	 * been written to sb or db
	 */
	cb->sb_data_len = 0;
	cb->db_data_len = 0;
out:
	return ret;
}

static int dcss_setcolreg(unsigned regno, unsigned red, unsigned green,
		unsigned blue, unsigned transp, struct fb_info *info)
{
	return 0;
}

static int dcss_channel_blank(int blank,
		struct dcss_channel_info *cinfo)
{
	uint32_t dtg_ctrl;
	struct dcss_info *info = cinfo->dev_data;
	struct dcss_channels *chans = &info->chans;
	struct cbuffer *cb = &cinfo->cb;

	dtg_ctrl = readl(info->base + chans->dtg_addr + 0x0);

	switch (blank) {
	case FB_BLANK_UNBLANK:
		/* enable dtg */
		dtg_ctrl |= 0x1 << (2 - cinfo->channel_id);

		/* set global alpha */
		if (cinfo->channel_id == DCSS_CHAN_MAIN)
			dtg_ctrl |= (0xff << 24);
		else
			dtg_ctrl &= ~(0xff << 24);
		break;
	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		/* disable dtg */
		dtg_ctrl &= ~(0x1 << (2 - cinfo->channel_id));
		break;
	default:
		return -EINVAL;
	}

	fill_sb(cb, chans->dtg_addr + 0x0, dtg_ctrl);

	return 0;
}

static int dcss_blank(int blank, struct fb_info *fbi)
{
	int ret = 0;
	int fb_node = fbi->node;
	struct dcss_channel_info *cinfo = fbi->par;
	struct dcss_info *info = cinfo->dev_data;
	struct platform_device *pdev = info->pdev;

	if (blank == FB_BLANK_UNBLANK) {
		/* dcss output timings can only be set for fb0 */
		if (!fb_node) {
			ret = dcss_subsam_config(info);
			if (ret) {
				dev_err(&pdev->dev, "subsam config failed\n");
				goto out;
			}

			ret = dcss_dtg_config(fb_node, info);
			if (ret) {
				dev_err(&pdev->dev, "dtg config failed\n");
				goto out;
			}
		} else {
			dcss_channel_blank(blank, cinfo);
			dtg_channel_timing_config(cinfo);
		}

#if USE_CTXLD
		ret = commit_to_fifo(fb_node, info);
		if (ret) {
			dev_err(&pdev->dev, "commit config failed\n");
			goto out;
		}
#endif

#if USE_CTXLD
		if (!fb_node) {
			/* start global timing */
			if (info->dcss_state == DCSS_STATE_RESET) {
				ret = dcss_dtg_start(info);
				if (ret) {
					dev_err(&pdev->dev, "start dtg failed\n");
					goto out;
				}

				info->dcss_state = DCSS_STATE_RUNNING;
			}
		}
#endif
	} else {
		dcss_channel_blank(blank, cinfo);
#if USE_CTXLD
		ret = commit_to_fifo(fb_node, info);
		if (ret) {
			dev_err(&pdev->dev, "commit config failed\n");
			goto out;
		}
#endif
	}

out:
	return ret;
}

static int dcss_pan_display(struct fb_var_screeninfo *var,
		struct fb_info *fbi)
{
	return 0;
}

static void ctxld_irq_clear(struct dcss_info *info)
{
	uint32_t irq_status;
	struct dcss_channels *chans  = &info->chans;
	struct platform_device *pdev = info->pdev;

	irq_status = readl(info->base + chans->ctxld_addr + CTXLD_CTRL_STATUS);
	dev_dbg(&pdev->dev, "ctxld irq_status before = 0x%x\n", irq_status);

	if (irq_status & RD_ERR)
		writel(RD_ERR, info->base + chans->ctxld_addr + CTXLD_CTRL_STATUS_CLR);

	if (irq_status & DB_COMP)
		writel(DB_COMP, info->base + chans->ctxld_addr + CTXLD_CTRL_STATUS_CLR);

	if (irq_status & SB_HP_COMP)
		writel(SB_HP_COMP,
		       info->base + chans->ctxld_addr + CTXLD_CTRL_STATUS_CLR);

	if (irq_status & SB_LP_COMP)
		writel(SB_LP_COMP,
		       info->base + chans->ctxld_addr + CTXLD_CTRL_STATUS_CLR);

	if (irq_status & AHB_ERR)
		writel(AHB_ERR,
		       info->base + chans->ctxld_addr + CTXLD_CTRL_STATUS_CLR);

	irq_status = readl(info->base + chans->ctxld_addr + CTXLD_CTRL_STATUS);
}

static irqreturn_t dcss_irq_handler(int irq, void *dev_id)
{
	struct irq_desc *desc;
	uint32_t irq_status;
	struct dcss_info *info = (struct dcss_info *)dev_id;
	struct dcss_channels *chans = &info->chans;
	struct dcss_channel_info *chan;
	struct ctxld_fifo *cfifo;

	cfifo = &info->cfifo;
	desc = irq_to_desc(irq);

	switch (desc->irq_data.hwirq) {
	case IRQ_DPR_CH1:
		chan = &chans->chan_info[0];
		irq_status = readl(info->base + chan->dpr_addr + 0x40);
		writel(irq_status, info->base + chan->dpr_addr + 0x40);
		break;
	case IRQ_DPR_CH2:
		break;
	case IRQ_DPR_CH3:
		break;
	case IRQ_CTX_LD:
		ctxld_irq_clear(info);
		complete(&cfifo->complete);
		break;
	case IRQ_DEC400D_CH1:
	case IRQ_DTRC_CH2:
	case IRQ_DTRC_CH3:
		break;
	}

	return IRQ_HANDLED;
}

static int dcss_interrupts_init(struct dcss_info *info)
{
	int i, ret = 0;
	struct platform_device *pdev = info->pdev;

	for (i = 0; i < DCSS_IRQS_NUM; i++) {
		info->irqs[i] = platform_get_irq(pdev, i);
		if (info->irqs[i] < 0)
			break;

		ret = devm_request_irq(&pdev->dev, info->irqs[i],
				dcss_irq_handler, 0,
				dev_name(&pdev->dev), info);
		if (ret) {
			dev_err(&pdev->dev, "request_irq (%d) failed with error %d\n",
				info->irqs[i], ret);
			return ret;
		}
	}

	if (i == 0)
		return -ENXIO;

	info->irqs_num = i + 1;

	return 0;
}

static void __iomem *dev_iomem_init(struct platform_device *pdev,
				    unsigned int res_idx)
{
	struct resource *res;

	if (res_idx > IORESOURCE_MEM_NUM)
		return NULL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, res_idx);
	if (!res)
		return ERR_PTR(-ENODEV);

	return devm_ioremap_resource(&pdev->dev, res);
}

static int dcss_dispdrv_init(struct platform_device *pdev,
			     struct fb_info *fbi)
{
	struct dcss_channel_info *cinfo = fbi->par;
	struct dcss_info *info = cinfo->dev_data;
	struct mxc_dispdrv_setting setting;
	char disp_dev[NAME_LEN];

	memset(&setting, 0x0, sizeof(setting));
	setting.fbi = fbi;
	memcpy(disp_dev, info->disp_dev, strlen(info->disp_dev));
	disp_dev[strlen(info->disp_dev)] = '\0';

	info->dispdrv = mxc_dispdrv_gethandle(disp_dev, &setting);
	if (IS_ERR(info->dispdrv)) {
		dev_info(&pdev->dev, "no encoder driver exists\n");
		return -EPROBE_DEFER;
	}

	dev_info(&pdev->dev, "%s encoder registered success\n", disp_dev);

	return 0;
}

static int dcss_register_one_ch(uint32_t ch_id,
				struct dcss_info *info)
{
	int ret = 0;
	struct dcss_channels *chans;
	struct dcss_channel_info *cinfo;

	BUG_ON(ch_id > 2);

	chans = &info->chans;
	cinfo = &chans->chan_info[ch_id];

	cinfo->pdev = info->pdev;
	cinfo->dev_data = (void *)info;

	ret = fill_one_chan_info(ch_id, cinfo);
	if (ret) {
		dev_err(&info->pdev->dev, "register channel %d failed\n", ch_id);
		return ret;
	}

	return ret;
}

static int dcss_register_one_fb(struct dcss_channel_info *cinfo)
{
	int ret = 0;
	struct fb_info *fbi;

	ret = alloc_one_fbinfo(cinfo);
	if (ret) {
		dev_err(&cinfo->pdev->dev,
			"register fb %d failed\n", cinfo->channel_id);
		goto out;
	}

	fbi = cinfo->fb_info;
	ret = dcss_init_fbinfo(fbi);
	if (ret)
		goto out;

	init_chan_pixmap(cinfo);
	init_ch_pos(cinfo);

	if (cinfo->channel_id == 0) {
		ret = dcss_dispdrv_init(cinfo->pdev, fbi);
		if (ret == -EPROBE_DEFER) {
			dev_info(&cinfo->pdev->dev,
				 "Defer fb probe for encoder unready\n");
			goto out;
		}
	}

	ret = register_framebuffer(fbi);
	if (ret) {
		dev_err(&cinfo->pdev->dev, "failed to register fb%d\n",
			cinfo->channel_id);
		goto out;
        }

out:
	return ret;
}

static int read_dcss_properties(struct dcss_info *info)
{
	int ret = 0;
	uint32_t disp_mode;
	const char *disp_dev;
	struct platform_device *pdev = info->pdev;
	struct device_node *np = pdev->dev.of_node;

	/* read disp-mode */
	ret = of_property_read_u32(np, "disp-mode", &disp_mode);
	if (ret < 0) {
		dev_err(&pdev->dev, "invalid disp-mode provided in dtb\n");
		return -EINVAL;
	}

	info->dft_disp_mode = &imx_cea_mode[disp_mode];
	if (!info->dft_disp_mode->xres)
		return -EINVAL;

	/* read disp-dev */
	ret = of_property_read_string(np, "disp-dev", &disp_dev);
	if (!ret) {
		memcpy(info->disp_dev, disp_dev, strlen(disp_dev));
		dev_info(&pdev->dev, "%s: disp_dev = %s\n", __func__,
			 info->disp_dev);
	}

	return 0;
}

static int dcss_info_init(struct dcss_info *info)
{
	int ret = 0;
	struct platform_device *pdev = info->pdev;

	INIT_LIST_HEAD(&info->ctxld_list);
	spin_lock_init(&info->llock);

	info->dcss_state = DCSS_STATE_RESET;

	ret = read_dcss_properties(info);
	if (ret)
		return -ENODEV;

	ret = dcss_init_chans(info);

	info->base = dev_iomem_init(pdev, 0);
	if (IS_ERR(info->base)) {
		ret = PTR_ERR(info->base);
		goto out;
	}

	info->blkctl_base = dev_iomem_init(pdev, 1);
	if (IS_ERR(info->blkctl_base)) {
		ret = PTR_ERR(info->blkctl_base);
		goto out;
	}

	ret = dcss_clks_get(info);
	if (ret)
		goto out;

	ret = dcss_clks_rate_set(info);
	if (ret)
		goto out;

	ret = dcss_clks_enable(info);
	if (ret)
		goto out;

	/* alloc ctxld fifo */
	ret = ctxld_fifo_alloc(&pdev->dev, &info->cfifo, DCSS_CFIFO_SIZE);
	if (ret) {
		dev_err(&pdev->dev, "ctxld fifo alloc failed\n");
		goto out;
	}

	info->ctxld_wq = alloc_ordered_workqueue("ctxld-wq", WQ_FREEZABLE);
	if (!info->ctxld_wq) {
		dev_err(&pdev->dev, "allocate ctxld wq failed\n");
		ret = -EINVAL;
		goto free_cfifo;
	}

	platform_set_drvdata(pdev, info);

	goto out;

free_cfifo:
	ctxld_fifo_free(&pdev->dev, &info->cfifo);
out:
	return ret;
}

static int dcss_enable_encoder(struct dcss_info *info)
{
	int ret = 0;
	struct fb_info *main_fbinfo;
	struct platform_device *pdev;

	if (!info->dispdrv)
		goto out;

	pdev = info->pdev;
	main_fbinfo = get_one_fbinfo(0, &info->chans);

	if (info->dispdrv->drv->setup) {
		ret = info->dispdrv->drv->setup(info->dispdrv, main_fbinfo);
		if (ret < 0) {
			dev_err(&pdev->dev, "setup encoder failed: %d\n", ret);
			goto out;
		}
	}

	if (info->dispdrv->drv->enable) {
		ret = info->dispdrv->drv->enable(info->dispdrv, main_fbinfo);
		if (ret < 0) {
			dev_err(&pdev->dev, "enable encoder failed: %d\n", ret);
			goto out;
		}
	}

out:
	return ret;
}

static int dcss_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct dcss_info *info;
	struct fb_info *m_fbinfo;

	info = devm_kzalloc(&pdev->dev, sizeof(struct dcss_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->pdev = pdev;

	ret = dcss_info_init(info);
	if (ret)
		goto kfree_info;

	/* TODO: reset DCSS to make it clean */

	/* Clocks select: before dcss de-resets */
	if (!strcmp(info->disp_dev, "hdmi_disp"))
		/* HDMI */
		writel(0x0, info->blkctl_base + 0x10);
	else
		/* MIPI DSI */
		writel(0x101, info->blkctl_base + 0x10);

	/* Pull DCSS out of resets */
	writel(0xffffffff, info->blkctl_base + 0x0);

	/* register channel 0: graphic */
	ret = dcss_register_one_ch(0, info);
	if (ret)
		goto kfree_info;

	/* register fb 0 */
	ret = dcss_register_one_fb(&info->chans.chan_info[0]);
	if (ret)
		goto unregister_ch0;

	dcss_interrupts_init(info);

	/* enable encoder if exists */
	dcss_enable_encoder(info);

	/* register channel 1: video */
	ret = dcss_register_one_ch(1, info);
	if (ret)
		goto unregister_fb0;

	/* register fb 1 */
	ret = dcss_register_one_fb(&info->chans.chan_info[1]);
	if (ret)
		goto unregister_ch1;

	/* unblank fb0 */
	m_fbinfo = get_one_fbinfo(0, &info->chans);
	dcss_blank(FB_BLANK_UNBLANK, m_fbinfo);

	/* init fb1 */
	dcss_set_par(get_one_fbinfo(1, &info->chans));

	goto out;

unregister_ch1:
	/* TODO: add later */
	;
unregister_fb0:
	framebuffer_release(get_one_fbinfo(0, &info->chans));
unregister_ch0:
	/* TODO: add later */
	;
kfree_info:
	devm_kfree(&pdev->dev, info);
out:
	return ret;
}

static int dcss_remove(struct platform_device *pdev)
{
	return 0;
}

static void dcss_shutdown(struct platform_device *pdev)
{
}

static struct platform_driver dcss_driver = {
	.probe  = dcss_probe,
	.remove = dcss_remove,
	.shutdown = dcss_shutdown,
	.driver = {
		.name = "dcss_fb",
		.of_match_table = dcss_dt_ids,
	},
};

module_platform_driver(dcss_driver);

MODULE_DESCRIPTION("NXP DCSS framebuffer driver");
MODULE_LICENSE("GPL");
