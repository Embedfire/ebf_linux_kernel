/* SPDX-License-Identifier: (GPL-2.0+ OR MIT)*/
/*
 * Copyright (C) 2017 Cadence Design Systems, Inc.
 * Copyright 2018-2020 NXP
 *
 */

#ifndef FSL_DSP_H
#define FSL_DSP_H
#include <uapi/linux/mxc_dsp.h>
#include <linux/firmware/imx/ipc.h>
#include "fsl_dsp_proxy.h"
#include "fsl_dsp_platform.h"
#include "fsl_dsp_audiomix.h"


#define FSL_DSP_COMP_NAME "fsl-dsp-component"

typedef void (*memcpy_func) (void *dest, const void *src, size_t n);
typedef void (*memset_func) (void *s, int c, size_t n);

/* ...maximal number of IPC clients per proxy */
#define XF_CFG_MAX_IPC_CLIENTS          (1 << 4)

enum {
	DSP_IMX8QXP_TYPE = 0,
	DSP_IMX8QM_TYPE,
	DSP_IMX8MP_TYPE,
};

/* ...proxy client data */
struct xf_client {
	/* ...pointer to proxy interface */
	struct xf_proxy     *proxy;

	/* ...allocated proxy client id */
	u32 id;

	/* ...pending response queue */
	struct xf_msg_queue	queue;
	/* ...response waiting queue */
	wait_queue_head_t	wait;

	/* ...virtual memory mapping */
	unsigned long	vm_start;
	/* ...counter of memory mappings (no real use of it yet - tbd) */
	atomic_t	vm_use;

	/* ...global structure pointer */
	void	*global;
	struct xf_message m;

	struct snd_compr_stream *cstream;

	struct work_struct work;
	struct completion compr_complete;

	int input_bytes;
	int consume_bytes;
	int offset;
	atomic_t buffer_cnt;
	int ping_pong_offset;
};

union xf_client_link {
	/* ...index of next client in free list */
	u32                 next;

	/* ...reference to proxy data for allocated client */
	struct xf_client    *client;
};

struct fsl_dsp {
	struct device			*dev;
	const char			*fw_name;
	const char			*audio_iface;
	void __iomem			*regs;
	void __iomem			*mu_base_virtaddr;
	struct imx_sc_ipc		*dsp_ipcHandle;
	struct imx_audiomix_dsp_data 	*audiomix;
	unsigned int			dsp_mu_id;
	int				dsp_mu_init;
	int				dsp_is_lpa;
	atomic_long_t			refcnt;
	unsigned long			paddr;
	unsigned long			dram0;
	unsigned long			dram1;
	unsigned long			iram;
	unsigned long			sram;
	void			        *sdram_vir_addr;
	unsigned long			sdram_phys_addr;
	int				sdram_reserved_size;
	void			        *dram_reserved_vir_addr;
	unsigned long			dram_reserved_phys_addr;
	int				dram_reserved_size;
	void			        *ocram_vir_addr;
	unsigned long			ocram_phys_addr;
	int				ocram_reserved_size;
	void			        *ocram_e_vir_addr;
	unsigned long			ocram_e_phys_addr;
	int				ocram_e_reserved_size;
	void				*msg_buf_virt;
	dma_addr_t			 msg_buf_phys;
	int				 msg_buf_size;
	void				*scratch_buf_virt;
	dma_addr_t			 scratch_buf_phys;
	int				 scratch_buf_size;
	void				*dsp_config_virt;
	dma_addr_t			 dsp_config_phys;
	int				 dsp_config_size;
	int				 dsp_board_type;
	unsigned int			fixup_offset;

	/* ...proxy data structures */
	struct xf_proxy proxy;

	/* ...mutex lock */
	struct mutex dsp_mutex;

	struct dsp_data dsp_data;

	/* ...global clients pool (item[0] serves as list terminator) */
	union xf_client_link xf_client_map[XF_CFG_MAX_IPC_CLIENTS];

	struct clk *esai_ipg_clk;
	struct clk *esai_mclk;
	struct clk *asrc_mem_clk;
	struct clk *asrc_ipg_clk;
	struct clk *asrck_clk[4];
	struct clk *dsp_ocrama_clk;
	struct clk *dsp_root_clk;
	struct clk *audio_root_clk;
	struct clk *audio_axi_clk;
	struct clk *debug_clk;
	struct clk *mu2_clk;
	struct clk *sdma_root_clk;
	struct clk *sai_ipg_clk;
	struct clk *sai_mclk;
	struct clk *pll8k_clk;
	struct clk *pll11k_clk;
	struct clk *uart_ipg_clk;
	struct clk *uart_per_clk;

	struct device **pd_dev;
	struct device_link **pd_dev_link;
	int    num_domains;
};

#define IRAM_OFFSET		0x10000
#define IRAM_SIZE		2048

#define DRAM0_OFFSET		0x0
#define DRAM0_SIZE		0x8000

#define DRAM1_OFFSET		0x8000
#define DRAM1_SIZE		0x8000

#define SYSRAM_OFFSET		0x18000
#define SYSRAM_SIZE		0x40000

#define SYSROM_OFFSET		0x58000
#define SYSROM_SIZE		0x30000

#define MSG_BUF_SIZE		8192
#define INPUT_BUF_SIZE		4096
#define OUTPUT_BUF_SIZE		16384
#define DSP_CONFIG_SIZE		8192

void *memcpy_dsp(void *dest, const void *src, size_t count);
void *memset_dsp(void *dest, int c, size_t count);
struct xf_client *xf_client_lookup(struct fsl_dsp *dsp_priv, u32 id);
struct xf_client *xf_client_alloc(struct fsl_dsp *dsp_priv);

int fsl_dsp_open_func(struct fsl_dsp *dsp_priv, struct xf_client *client);
int fsl_dsp_close_func(struct xf_client *client);

#endif
