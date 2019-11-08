// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of STM32 ADC driver
 *
 * Copyright (C) 2016, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/timer/stm32-lptim-trigger.h>
#include <linux/iio/timer/stm32-timer-trigger.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq_work.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "stm32-adc-core.h"

/* BOOST bit must be set on STM32H7 when ADC clock is above 20MHz */
#define STM32H7_BOOST_CLKRATE		20000000UL

#define STM32_ADC_CH_MAX		20	/* max number of channels */
#define STM32_ADC_CH_SZ			10	/* max channel name size */
#define STM32_ADC_MAX_SQ		16	/* SQ1..SQ16 */
#define STM32_ADC_MAX_JSQ		4	/* JSQ1..JSQ4 */
#define STM32_ADC_MAX_SMP		7	/* SMPx range is [0..7] */
#define STM32_ADC_TIMEOUT_US		100000
#define STM32_ADC_TIMEOUT	(msecs_to_jiffies(STM32_ADC_TIMEOUT_US / 1000))
#define STM32_ADC_HW_STOP_DELAY_MS	100

#define STM32_DMA_BUFFER_SIZE		PAGE_SIZE

/* External trigger enable */
enum stm32_adc_exten {
	STM32_EXTEN_SWTRIG,
	STM32_EXTEN_HWTRIG_RISING_EDGE,
	STM32_EXTEN_HWTRIG_FALLING_EDGE,
	STM32_EXTEN_HWTRIG_BOTH_EDGES,
};

/**
 * stm32_adc_regs - stm32 ADC misc registers & bitfield desc
 * @reg:		register offset
 * @mask:		bitfield mask
 * @shift:		left shift
 */
struct stm32_adc_regs {
	int reg;
	int mask;
	int shift;
};

/**
 * struct stm32_adc_awd_reginfo - stm32 ADC analog watchdog regs desc
 * @reg:		awd control register offset
 * @en_bits:		ADW enable bits for regular conversions, in @reg
 * @jen_bits:		ADW enable bits for injected conversions, in @reg
 * @awdch_mask:		AWDCH bitfield mask, in @reg
 * @awdch_shift:	AWDCH shift, in @reg
 * @htr:		High threshold register offset
 * @ltr:		Low threshold register offset
 * @ier_msk:		interrupt enable bit mask in ier register
 * @isr_msk:		interrupt status bit mask in isr register
 */
struct stm32_adc_awd_reginfo {
	u32 reg;
	u32 en_bits;
	u32 jen_bits;
	u32 awdch_mask;
	u32 awdch_shift;
	u32 htr;
	u32 ltr;
	u32 ier_msk;
	u32 isr_msk;
};

/**
 * stm32_adc_regspec - stm32 registers definition, compatible dependent data
 * @dr:			data register offset
 * @jdr:		injected data registers offsets
 * @ier_eoc:		interrupt enable register & eocie bitfield
 * @ier_jeoc:		interrupt enable register & jeocie bitfield
 * @ier_ovr:		interrupt enable register & overrun bitfield
 * @isr_eoc:		interrupt status register & eoc bitfield
 * @isr_jeoc:		interrupt status register & jeoc bitfield
 * @isr_ovr:		interrupt status register & overrun bitfield
 * @sqr:		reference to sequence registers array
 * @jsqr:		reference to injected sequence registers array
 * @exten:		trigger control register & bitfield
 * @extsel:		trigger selection register & bitfield
 * @jexten:		injected trigger control register & bitfield
 * @jextsel:		injected trigger selection register & bitfield
 * @res:		resolution selection register & bitfield
 * @smpr:		smpr1 & smpr2 registers offset array
 * @smp_bits:		smpr1 & smpr2 index and bitfields
 * @write_one_to_clear:	clear isr flags by writing one to it
 * @awd_reginfo:	Analog watchdog description
 * @num_awd:		Number of Analog watchdog
 */
struct stm32_adc_regspec {
	const u32 dr;
	const u32 jdr[4];
	const struct stm32_adc_regs ier_eoc;
	const struct stm32_adc_regs ier_jeoc;
	const struct stm32_adc_regs ier_ovr;
	const struct stm32_adc_regs isr_eoc;
	const struct stm32_adc_regs isr_jeoc;
	const struct stm32_adc_regs isr_ovr;
	const struct stm32_adc_regs *sqr;
	const struct stm32_adc_regs *jsqr;
	const struct stm32_adc_regs exten;
	const struct stm32_adc_regs extsel;
	const struct stm32_adc_regs jexten;
	const struct stm32_adc_regs jextsel;
	const struct stm32_adc_regs res;
	const u32 smpr[2];
	const struct stm32_adc_regs *smp_bits;
	const bool write_one_to_clear;
	const struct stm32_adc_awd_reginfo *awd_reginfo;
	unsigned int num_awd;
};

struct stm32_adc;

/**
 * stm32_adc_cfg - stm32 compatible configuration data
 * @regs:		registers descriptions
 * @adc_info:		per instance input channels definitions
 * @trigs:		external trigger sources
 * @clk_required:	clock is required
 * @has_vregready:	vregready status flag presence
 * @prepare:		optional prepare routine (power-up, enable)
 * @start_conv:		routine to start conversions
 * @stop_conv:		routine to stop conversions
 * @unprepare:		optional unprepare routine (disable, power-down)
 * @smp_cycles:		programmable sampling time (ADC clock cycles)
 * @is_started:		routine to get adc 'started' state
 */
struct stm32_adc_cfg {
	const struct stm32_adc_regspec	*regs;
	const struct stm32_adc_info	*adc_info;
	struct stm32_adc_trig_info	*trigs;
	bool clk_required;
	bool has_vregready;
	int (*prepare)(struct stm32_adc *);
	void (*start_conv)(struct stm32_adc *, bool dma);
	void (*stop_conv)(struct stm32_adc *);
	void (*unprepare)(struct stm32_adc *);
	const unsigned int *smp_cycles;
	bool (*is_started)(struct stm32_adc *adc);
};

/**
 * struct stm32_adc_evt - Configuration data for Analog watchdog events
 * @list:		event configuration list
 * @awd_id:		assigned AWD index
 * @chan:		IIO chan spec reference for this event
 * @hthresh:		High threshold value
 * @lthresh:		Low threshold value
 * @enabled:		Event enabled state
 * @set:		Flag, event has been assigned an AWD and has been set
 */
struct stm32_adc_evt {
	struct list_head list;
	int awd_id;
	const struct iio_chan_spec *chan;
	u32 hthresh;
	u32 lthresh;
	bool enabled;
	bool set;
};

/**
 * struct stm32_adc - private data of each ADC IIO instance
 * @common:		reference to ADC block common data
 * @offset:		ADC instance register offset in ADC block
 * @id:			ADC instance id from offset
 * @cfg:		compatible configuration data
 * @completion:		end of single conversion completion
 * @buffer:		data buffer
 * @clk:		clock for this adc instance
 * @irq:		interrupt for this adc instance
 * @lock:		spinlock
 * @bufi:		data buffer index
 * @num_conv:		expected number of scan conversions
 * @res:		data resolution (e.g. RES bitfield value)
 * @trigger_polarity:	external trigger polarity (e.g. exten)
 * @dma_chan:		dma channel
 * @rx_buf:		dma rx buffer cpu address
 * @rx_dma_buf:		dma rx buffer bus address
 * @rx_buf_sz:		dma rx buffer size
 * @chan_name:		channel name array
 * @injected:		use injected channels on this adc
 * @evt_list:		list of all events configured for this ADC block
 * @awd_mask:		analog watchdog bitmask for this adc
 * @work:		irq work used to call trigger poll routine
 */
struct stm32_adc {
	struct stm32_adc_common	*common;
	u32			offset;
	u32			id;
	const struct stm32_adc_cfg	*cfg;
	struct completion	completion;
	u16			buffer[STM32_ADC_MAX_SQ];
	struct clk		*clk;
	int			irq;
	spinlock_t		lock;		/* interrupt lock */
	unsigned int		bufi;
	unsigned int		num_conv;
	u32			res;
	u32			trigger_polarity;
	struct dma_chan		*dma_chan;
	u8			*rx_buf;
	dma_addr_t		rx_dma_buf;
	unsigned int		rx_buf_sz;
	char			chan_name[STM32_ADC_CH_MAX][STM32_ADC_CH_SZ];
	bool			injected;
	struct list_head	evt_list;
	u32			awd_mask;
	struct irq_work		work;
};

struct stm32_adc_diff_channel {
	u32 vinp;
	u32 vinn;
};

/**
 * struct stm32_adc_info - stm32 ADC, per instance config data
 * @max_channels:	Number of channels
 * @resolutions:	available resolutions
 * @num_res:		number of available resolutions
 */
struct stm32_adc_info {
	int max_channels;
	const unsigned int *resolutions;
	const unsigned int num_res;
};

static const unsigned int stm32f4_adc_resolutions[] = {
	/* sorted values so the index matches RES[1:0] in STM32F4_ADC_CR1 */
	12, 10, 8, 6,
};

/* stm32f4 can have up to 19 channels (incl. 16 external sources) */
static const struct stm32_adc_info stm32f4_adc_info = {
	.max_channels = 19,
	.resolutions = stm32f4_adc_resolutions,
	.num_res = ARRAY_SIZE(stm32f4_adc_resolutions),
};

static const unsigned int stm32h7_adc_resolutions[] = {
	/* sorted values so the index matches RES[2:0] in STM32H7_ADC_CFGR */
	16, 14, 12, 10, 8,
};

/* stm32h7 can have up to 20 channels */
static const struct stm32_adc_info stm32h7_adc_info = {
	.max_channels = STM32_ADC_CH_MAX,
	.resolutions = stm32h7_adc_resolutions,
	.num_res = ARRAY_SIZE(stm32h7_adc_resolutions),
};

/**
 * stm32f4_sq - describe regular sequence registers
 * - L: sequence len (register & bit field)
 * - SQ1..SQ16: sequence entries (register & bit field)
 */
static const struct stm32_adc_regs stm32f4_sq[STM32_ADC_MAX_SQ + 1] = {
	/* L: len bit field description to be kept as first element */
	{ STM32F4_ADC_SQR1, GENMASK(23, 20), 20 },
	/* SQ1..SQ16 registers & bit fields (reg, mask, shift) */
	{ STM32F4_ADC_SQR3, GENMASK(4, 0), 0 },
	{ STM32F4_ADC_SQR3, GENMASK(9, 5), 5 },
	{ STM32F4_ADC_SQR3, GENMASK(14, 10), 10 },
	{ STM32F4_ADC_SQR3, GENMASK(19, 15), 15 },
	{ STM32F4_ADC_SQR3, GENMASK(24, 20), 20 },
	{ STM32F4_ADC_SQR3, GENMASK(29, 25), 25 },
	{ STM32F4_ADC_SQR2, GENMASK(4, 0), 0 },
	{ STM32F4_ADC_SQR2, GENMASK(9, 5), 5 },
	{ STM32F4_ADC_SQR2, GENMASK(14, 10), 10 },
	{ STM32F4_ADC_SQR2, GENMASK(19, 15), 15 },
	{ STM32F4_ADC_SQR2, GENMASK(24, 20), 20 },
	{ STM32F4_ADC_SQR2, GENMASK(29, 25), 25 },
	{ STM32F4_ADC_SQR1, GENMASK(4, 0), 0 },
	{ STM32F4_ADC_SQR1, GENMASK(9, 5), 5 },
	{ STM32F4_ADC_SQR1, GENMASK(14, 10), 10 },
	{ STM32F4_ADC_SQR1, GENMASK(19, 15), 15 },
};

/* STM32F4 external trigger sources for all instances */
static struct stm32_adc_trig_info stm32f4_adc_trigs[] = {
	{ TIM1_CH1, STM32_EXT0, 0, TRG_REGULAR },
	{ TIM1_CH2, STM32_EXT1, 0, TRG_REGULAR },
	{ TIM1_CH3, STM32_EXT2, 0, TRG_REGULAR },
	{ TIM2_CH2, STM32_EXT3, 0, TRG_REGULAR },
	{ TIM2_CH3, STM32_EXT4, 0, TRG_REGULAR },
	{ TIM2_CH4, STM32_EXT5, 0, TRG_REGULAR },
	{ TIM2_TRGO, STM32_EXT6, STM32_EXT3, TRG_BOTH },
	{ TIM3_CH1, STM32_EXT7, 0, TRG_REGULAR },
	{ TIM3_TRGO, STM32_EXT8, 0, TRG_REGULAR },
	{ TIM4_CH4, STM32_EXT9, 0, TRG_REGULAR },
	{ TIM5_CH1, STM32_EXT10, 0, TRG_REGULAR },
	{ TIM5_CH2, STM32_EXT11, 0, TRG_REGULAR },
	{ TIM5_CH3, STM32_EXT12, 0, TRG_REGULAR },
	{ TIM8_CH1, STM32_EXT13, 0, TRG_REGULAR },
	{ TIM8_TRGO, STM32_EXT14, 0, TRG_REGULAR },
	{ TIM1_CH4, 0, STM32_EXT0, TRG_INJECTED },
	{ TIM1_TRGO, 0, STM32_EXT1, TRG_INJECTED },
	{ TIM2_CH1, 0, STM32_EXT2, TRG_INJECTED },
	{ TIM3_CH2, 0, STM32_EXT4, TRG_INJECTED },
	{ TIM3_CH4, 0, STM32_EXT5, TRG_INJECTED },
	{ TIM4_CH1, 0, STM32_EXT6, TRG_INJECTED },
	{ TIM4_CH2, 0, STM32_EXT7, TRG_INJECTED  },
	{ TIM4_CH3, 0, STM32_EXT8, TRG_INJECTED },
	{ TIM4_TRGO, 0, STM32_EXT9, TRG_INJECTED },
	{ TIM5_CH4, 0, STM32_EXT10, TRG_INJECTED },
	{ TIM5_TRGO, 0, STM32_EXT11, TRG_INJECTED },
	{ TIM8_CH2, 0, STM32_EXT12, TRG_INJECTED },
	{ TIM8_CH3, 0, STM32_EXT13, TRG_INJECTED },
	{ TIM8_CH4, 0, STM32_EXT14, TRG_INJECTED },
	{}, /* sentinel */
};

/**
 * stm32f4_jsq - describe injected sequence register:
 * - JL: injected sequence len
 * - JSQ4..SQ1: sequence entries
 * When JL == 3, ADC converts JSQ1, JSQ2, JSQ3, JSQ4
 * When JL == 2, ADC converts JSQ2, JSQ3, JSQ4
 * When JL == 1, ADC converts JSQ3, JSQ4
 * When JL == 0, ADC converts JSQ4
 */
static const struct stm32_adc_regs stm32f4_jsq[STM32_ADC_MAX_JSQ + 1] = {
	/* JL: len bit field description to be kept as first element */
	{ STM32F4_ADC_JSQR, GENMASK(21, 20), 20 },
	/* JSQ4..JSQ1 registers & bit fields (reg, mask, shift) */
	{ STM32F4_ADC_JSQR, GENMASK(19, 15), 15 },
	{ STM32F4_ADC_JSQR, GENMASK(14, 10), 10 },
	{ STM32F4_ADC_JSQR, GENMASK(9, 5), 5 },
	{ STM32F4_ADC_JSQR, GENMASK(4, 0), 0 },
};

/**
 * stm32f4_smp_bits[] - describe sampling time register index & bit fields
 * Sorted so it can be indexed by channel number.
 */
static const struct stm32_adc_regs stm32f4_smp_bits[] = {
	/* STM32F4_ADC_SMPR2: smpr[] index, mask, shift for SMP0 to SMP9 */
	{ 1, GENMASK(2, 0), 0 },
	{ 1, GENMASK(5, 3), 3 },
	{ 1, GENMASK(8, 6), 6 },
	{ 1, GENMASK(11, 9), 9 },
	{ 1, GENMASK(14, 12), 12 },
	{ 1, GENMASK(17, 15), 15 },
	{ 1, GENMASK(20, 18), 18 },
	{ 1, GENMASK(23, 21), 21 },
	{ 1, GENMASK(26, 24), 24 },
	{ 1, GENMASK(29, 27), 27 },
	/* STM32F4_ADC_SMPR1, smpr[] index, mask, shift for SMP10 to SMP18 */
	{ 0, GENMASK(2, 0), 0 },
	{ 0, GENMASK(5, 3), 3 },
	{ 0, GENMASK(8, 6), 6 },
	{ 0, GENMASK(11, 9), 9 },
	{ 0, GENMASK(14, 12), 12 },
	{ 0, GENMASK(17, 15), 15 },
	{ 0, GENMASK(20, 18), 18 },
	{ 0, GENMASK(23, 21), 21 },
	{ 0, GENMASK(26, 24), 24 },
};

/* STM32F4 programmable sampling time (ADC clock cycles) */
static const unsigned int stm32f4_adc_smp_cycles[STM32_ADC_MAX_SMP + 1] = {
	3, 15, 28, 56, 84, 112, 144, 480,
};

static const struct stm32_adc_awd_reginfo stm32f4_awd_reginfo = {
	.reg = STM32F4_ADC_CR1,
	.en_bits = STM32F4_AWDSGL | STM32F4_AWDEN,
	.jen_bits = STM32F4_AWDSGL | STM32F4_JAWDEN,
	.awdch_mask = STM32F4_AWDCH_MASK,
	.awdch_shift = STM32F4_AWDCH_SHIFT,
	.htr = STM32F4_ADC_HTR,
	.ltr = STM32F4_ADC_LTR,
	.ier_msk = STM32F4_AWDIE,
	.isr_msk = STM32F4_AWD,
};

static const struct stm32_adc_regspec stm32f4_adc_regspec = {
	.dr = STM32F4_ADC_DR,
	.jdr = {
		STM32F4_ADC_JDR1,
		STM32F4_ADC_JDR2,
		STM32F4_ADC_JDR3,
		STM32F4_ADC_JDR4,
	},
	.ier_eoc = { STM32F4_ADC_CR1, STM32F4_EOCIE },
	.ier_jeoc = { STM32F4_ADC_CR1, STM32F4_JEOCIE },
	.ier_ovr = { STM32F4_ADC_CR1, STM32F4_OVRIE },
	.isr_eoc = { STM32F4_ADC_SR, STM32F4_EOC },
	.isr_jeoc = { STM32F4_ADC_SR, STM32F4_JEOC },
	.isr_ovr = { STM32F4_ADC_SR, STM32F4_OVR },
	.sqr = stm32f4_sq,
	.jsqr = stm32f4_jsq,
	.exten = { STM32F4_ADC_CR2, STM32F4_EXTEN_MASK, STM32F4_EXTEN_SHIFT },
	.extsel = { STM32F4_ADC_CR2, STM32F4_EXTSEL_MASK,
		    STM32F4_EXTSEL_SHIFT },
	.jexten = { STM32F4_ADC_CR2, STM32F4_JEXTEN_MASK,
		    STM32F4_JEXTEN_SHIFT },
	.jextsel = { STM32F4_ADC_CR2, STM32F4_JEXTSEL_MASK,
		     STM32F4_JEXTSEL_SHIFT },
	.res = { STM32F4_ADC_CR1, STM32F4_RES_MASK, STM32F4_RES_SHIFT },
	.smpr = { STM32F4_ADC_SMPR1, STM32F4_ADC_SMPR2 },
	.smp_bits = stm32f4_smp_bits,
	.awd_reginfo = &stm32f4_awd_reginfo,
	.num_awd = 1,
};

static const struct stm32_adc_regs stm32h7_sq[STM32_ADC_MAX_SQ + 1] = {
	/* L: len bit field description to be kept as first element */
	{ STM32H7_ADC_SQR1, GENMASK(3, 0), 0 },
	/* SQ1..SQ16 registers & bit fields (reg, mask, shift) */
	{ STM32H7_ADC_SQR1, GENMASK(10, 6), 6 },
	{ STM32H7_ADC_SQR1, GENMASK(16, 12), 12 },
	{ STM32H7_ADC_SQR1, GENMASK(22, 18), 18 },
	{ STM32H7_ADC_SQR1, GENMASK(28, 24), 24 },
	{ STM32H7_ADC_SQR2, GENMASK(4, 0), 0 },
	{ STM32H7_ADC_SQR2, GENMASK(10, 6), 6 },
	{ STM32H7_ADC_SQR2, GENMASK(16, 12), 12 },
	{ STM32H7_ADC_SQR2, GENMASK(22, 18), 18 },
	{ STM32H7_ADC_SQR2, GENMASK(28, 24), 24 },
	{ STM32H7_ADC_SQR3, GENMASK(4, 0), 0 },
	{ STM32H7_ADC_SQR3, GENMASK(10, 6), 6 },
	{ STM32H7_ADC_SQR3, GENMASK(16, 12), 12 },
	{ STM32H7_ADC_SQR3, GENMASK(22, 18), 18 },
	{ STM32H7_ADC_SQR3, GENMASK(28, 24), 24 },
	{ STM32H7_ADC_SQR4, GENMASK(4, 0), 0 },
	{ STM32H7_ADC_SQR4, GENMASK(10, 6), 6 },
};

static const struct stm32_adc_regs stm32h7_jsq[STM32_ADC_MAX_JSQ + 1] = {
	/* JL: len bit field description to be kept as first element */
	{ STM32H7_ADC_JSQR, GENMASK(1, 0), 0 },
	/* JSQ1..JSQ4 registers & bit fields (reg, mask, shift) */
	{ STM32H7_ADC_JSQR, GENMASK(13, 9), 9 },
	{ STM32H7_ADC_JSQR, GENMASK(19, 15), 15 },
	{ STM32H7_ADC_JSQR, GENMASK(25, 21), 21 },
	{ STM32H7_ADC_JSQR, GENMASK(31, 27), 27 },
};

/* STM32H7 external trigger sources for all instances */
static struct stm32_adc_trig_info stm32h7_adc_trigs[] = {
	{ TIM1_CH1, STM32_EXT0, 0, TRG_REGULAR },
	{ TIM1_CH2, STM32_EXT1, 0, TRG_REGULAR },
	{ TIM1_CH3, STM32_EXT2, 0, TRG_REGULAR },
	{ TIM2_CH2, STM32_EXT3, 0, TRG_REGULAR },
	{ TIM3_TRGO, STM32_EXT4, STM32_EXT12, TRG_BOTH },
	{ TIM4_CH4, STM32_EXT5, 0, TRG_REGULAR },
	{ TIM8_TRGO, STM32_EXT7, STM32_EXT9, TRG_BOTH },
	{ TIM8_TRGO2, STM32_EXT8, STM32_EXT10, TRG_BOTH },
	{ TIM1_TRGO, STM32_EXT9, STM32_EXT0, TRG_BOTH },
	{ TIM1_TRGO2, STM32_EXT10, STM32_EXT8, TRG_BOTH },
	{ TIM2_TRGO, STM32_EXT11, STM32_EXT2, TRG_BOTH },
	{ TIM4_TRGO, STM32_EXT12, STM32_EXT5, TRG_BOTH },
	{ TIM6_TRGO, STM32_EXT13, STM32_EXT14, TRG_BOTH },
	{ TIM15_TRGO, STM32_EXT14, STM32_EXT15, TRG_BOTH },
	{ TIM3_CH4, STM32_EXT15, STM32_EXT4, TRG_BOTH },
	{ LPTIM1_OUT, STM32_EXT18, STM32_EXT18, TRG_BOTH },
	{ LPTIM2_OUT, STM32_EXT19, STM32_EXT19, TRG_BOTH },
	{ LPTIM3_OUT, STM32_EXT20, STM32_EXT20, TRG_BOTH },
	{ TIM1_CH4, 0, STM32_EXT1, TRG_INJECTED },
	{ TIM2_CH1, 0, STM32_EXT3, TRG_INJECTED },
	{ TIM8_CH4, 0, STM32_EXT7, TRG_INJECTED },
	{ TIM3_CH3, 0, STM32_EXT11, TRG_INJECTED },
	{ TIM3_CH1, 0, STM32_EXT13, TRG_INJECTED },
	{},
};

/**
 * stm32h7_smp_bits - describe sampling time register index & bit fields
 * Sorted so it can be indexed by channel number.
 */
static const struct stm32_adc_regs stm32h7_smp_bits[] = {
	/* STM32H7_ADC_SMPR1, smpr[] index, mask, shift for SMP0 to SMP9 */
	{ 0, GENMASK(2, 0), 0 },
	{ 0, GENMASK(5, 3), 3 },
	{ 0, GENMASK(8, 6), 6 },
	{ 0, GENMASK(11, 9), 9 },
	{ 0, GENMASK(14, 12), 12 },
	{ 0, GENMASK(17, 15), 15 },
	{ 0, GENMASK(20, 18), 18 },
	{ 0, GENMASK(23, 21), 21 },
	{ 0, GENMASK(26, 24), 24 },
	{ 0, GENMASK(29, 27), 27 },
	/* STM32H7_ADC_SMPR2, smpr[] index, mask, shift for SMP10 to SMP19 */
	{ 1, GENMASK(2, 0), 0 },
	{ 1, GENMASK(5, 3), 3 },
	{ 1, GENMASK(8, 6), 6 },
	{ 1, GENMASK(11, 9), 9 },
	{ 1, GENMASK(14, 12), 12 },
	{ 1, GENMASK(17, 15), 15 },
	{ 1, GENMASK(20, 18), 18 },
	{ 1, GENMASK(23, 21), 21 },
	{ 1, GENMASK(26, 24), 24 },
	{ 1, GENMASK(29, 27), 27 },
};

/* STM32H7 programmable sampling time (ADC clock cycles, rounded down) */
static const unsigned int stm32h7_adc_smp_cycles[STM32_ADC_MAX_SMP + 1] = {
	1, 2, 8, 16, 32, 64, 387, 810,
};

/**
 * stm32h7_awd_reginfo[] - Analog watchdog description.
 *
 * two watchdog types are found in stm32h7 ADC:
 * - AWD1 has en_bits, and can select either a single or all channel(s)
 * - AWD2 & AWD3 are enabled by channel mask (in AWDxCR)
 * Remaining is similar (high/low threshold regs, ier/isr regs & mask)
 */
static const struct stm32_adc_awd_reginfo stm32h7_awd_reginfo[] = {
	{
		/* AWD1: has en_bits, configure it to guard one channel */
		.reg = STM32H7_ADC_CFGR,
		.en_bits = STM32H7_AWD1SGL | STM32H7_AWD1EN,
		.jen_bits = STM32H7_AWD1SGL | STM32H7_JAWD1EN,
		.awdch_mask = STM32H7_AWD1CH_MASK,
		.awdch_shift = STM32H7_AWD1CH_SHIFT,
		.htr = STM32H7_ADC_HTR1,
		.ltr = STM32H7_ADC_LTR1,
		.ier_msk = STM32H7_AWD1IE,
		.isr_msk = STM32H7_AWD1,
	}, {
		/* AWD2 uses channel mask in AWD2CR register */
		.reg = STM32H7_ADC_AWD2CR,
		.htr = STM32H7_ADC_HTR2,
		.ltr = STM32H7_ADC_LTR2,
		.ier_msk = STM32H7_AWD2IE,
		.isr_msk = STM32H7_AWD2,
	}, {
		/* AWD3 uses channel mask in AWD3CR register */
		.reg = STM32H7_ADC_AWD3CR,
		.htr = STM32H7_ADC_HTR3,
		.ltr = STM32H7_ADC_LTR3,
		.ier_msk = STM32H7_AWD3IE,
		.isr_msk = STM32H7_AWD3,
	},
};

static const struct stm32_adc_regspec stm32h7_adc_regspec = {
	.dr = STM32H7_ADC_DR,
	.jdr = {
		STM32H7_ADC_JDR1,
		STM32H7_ADC_JDR2,
		STM32H7_ADC_JDR3,
		STM32H7_ADC_JDR4,
	},
	.ier_eoc = { STM32H7_ADC_IER, STM32H7_EOCIE },
	.ier_jeoc = { STM32H7_ADC_IER, STM32H7_JEOSIE },
	.ier_ovr = { STM32H7_ADC_IER, STM32H7_OVRIE },
	.isr_eoc = { STM32H7_ADC_ISR, STM32H7_EOC },
	.isr_jeoc = { STM32H7_ADC_ISR, STM32H7_JEOS },
	.isr_ovr = { STM32H7_ADC_ISR, STM32H7_OVR },
	.sqr = stm32h7_sq,
	.jsqr = stm32h7_jsq,
	.exten = { STM32H7_ADC_CFGR, STM32H7_EXTEN_MASK, STM32H7_EXTEN_SHIFT },
	.extsel = { STM32H7_ADC_CFGR, STM32H7_EXTSEL_MASK,
		    STM32H7_EXTSEL_SHIFT },
	.jexten = { STM32H7_ADC_JSQR, STM32H7_JEXTEN_MASK,
		    STM32H7_JEXTEN_SHIFT },
	.jextsel = { STM32H7_ADC_JSQR, STM32H7_JEXTSEL_MASK,
		     STM32H7_JEXTSEL_SHIFT },
	.res = { STM32H7_ADC_CFGR, STM32H7_RES_MASK, STM32H7_RES_SHIFT },
	.smpr = { STM32H7_ADC_SMPR1, STM32H7_ADC_SMPR2 },
	.smp_bits = stm32h7_smp_bits,
	.write_one_to_clear = true,
	.awd_reginfo = stm32h7_awd_reginfo,
	.num_awd = ARRAY_SIZE(stm32h7_awd_reginfo),
};

/**
 * STM32 ADC registers access routines
 * @adc: stm32 adc instance
 * @reg: reg offset in adc instance
 *
 * Note: All instances share same base, with 0x0, 0x100 or 0x200 offset resp.
 * for adc1, adc2 and adc3.
 */
static u32 stm32_adc_readl(struct stm32_adc *adc, u32 reg)
{
	return readl_relaxed(adc->common->base + adc->offset + reg);
}

#define stm32_adc_readl_addr(addr)	stm32_adc_readl(adc, addr)

#define stm32_adc_readl_poll_timeout(reg, val, cond, sleep_us, timeout_us) \
	readx_poll_timeout(stm32_adc_readl_addr, reg, val, \
			   cond, sleep_us, timeout_us)

static u16 stm32_adc_readw(struct stm32_adc *adc, u32 reg)
{
	return readw_relaxed(adc->common->base + adc->offset + reg);
}

static void stm32_adc_writel(struct stm32_adc *adc, u32 reg, u32 val)
{
	writel_relaxed(val, adc->common->base + adc->offset + reg);
}

static void stm32_adc_set_bits(struct stm32_adc *adc, u32 reg, u32 bits)
{
	unsigned long flags;

	spin_lock_irqsave(&adc->lock, flags);
	stm32_adc_writel(adc, reg, stm32_adc_readl(adc, reg) | bits);
	spin_unlock_irqrestore(&adc->lock, flags);
}

static void stm32_adc_clr_bits(struct stm32_adc *adc, u32 reg, u32 bits)
{
	unsigned long flags;

	spin_lock_irqsave(&adc->lock, flags);
	stm32_adc_writel(adc, reg, stm32_adc_readl(adc, reg) & ~bits);
	spin_unlock_irqrestore(&adc->lock, flags);
}

/**
 * stm32_adc_conv_irq_enable() - Enable end of conversion interrupt
 * @adc: stm32 adc instance
 */
static void stm32_adc_conv_irq_enable(struct stm32_adc *adc)
{
	if (adc->injected)
		stm32_adc_set_bits(adc, adc->cfg->regs->ier_jeoc.reg,
				   adc->cfg->regs->ier_jeoc.mask);
	else
		stm32_adc_set_bits(adc, adc->cfg->regs->ier_eoc.reg,
				   adc->cfg->regs->ier_eoc.mask);
};

/**
 * stm32_adc_conv_irq_disable() - Disable end of conversion interrupt
 * @adc: stm32 adc instance
 */
static void stm32_adc_conv_irq_disable(struct stm32_adc *adc)
{
	if (adc->injected)
		stm32_adc_clr_bits(adc, adc->cfg->regs->ier_jeoc.reg,
				   adc->cfg->regs->ier_jeoc.mask);
	else
		stm32_adc_clr_bits(adc, adc->cfg->regs->ier_eoc.reg,
				   adc->cfg->regs->ier_eoc.mask);
}

static void stm32_adc_ovr_irq_enable(struct stm32_adc *adc)
{
	if (adc->injected)
		return;

	stm32_adc_set_bits(adc, adc->cfg->regs->ier_ovr.reg,
			   adc->cfg->regs->ier_ovr.mask);
}

static void stm32_adc_ovr_irq_disable(struct stm32_adc *adc)
{
	if (adc->injected)
		return;

	stm32_adc_clr_bits(adc, adc->cfg->regs->ier_ovr.reg,
			   adc->cfg->regs->ier_ovr.mask);
}

static void stm32_adc_set_res(struct stm32_adc *adc)
{
	const struct stm32_adc_regs *res = &adc->cfg->regs->res;
	u32 val;

	val = stm32_adc_readl(adc, res->reg);
	val = (val & ~res->mask) | (adc->res << res->shift);
	stm32_adc_writel(adc, res->reg, val);
}

static int stm32_adc_hw_stop(struct device *dev)
{
	struct stm32_adc *adc = dev_get_drvdata(dev);

	if (adc->cfg->unprepare)
		adc->cfg->unprepare(adc);

	if (adc->clk)
		clk_disable_unprepare(adc->clk);

	return 0;
}

static int stm32_adc_hw_start(struct device *dev)
{
	struct stm32_adc *adc = dev_get_drvdata(dev);
	int ret;

	if (adc->clk) {
		ret = clk_prepare_enable(adc->clk);
		if (ret)
			return ret;
	}

	stm32_adc_set_res(adc);

	if (adc->cfg->prepare) {
		ret = adc->cfg->prepare(adc);
		if (ret)
			goto err_clk_dis;
	}

	return 0;

err_clk_dis:
	if (adc->clk)
		clk_disable_unprepare(adc->clk);

	return ret;
}

static bool stm32f4_adc_is_started(struct stm32_adc *adc)
{
	u32 val = stm32_adc_readl(adc, STM32F4_ADC_SR);

	if (adc->injected)
		return !!(val & STM32F4_JSTRT);
	else
		return !!(val & STM32F4_STRT);
}

/**
 * stm32f4_adc_start_conv() - Start conversions for regular channels.
 * @adc: stm32 adc instance
 * @dma: use dma to transfer conversion result
 *
 * Start conversions for regular channels.
 * Also take care of normal or DMA mode. Circular DMA may be used for regular
 * conversions, in IIO buffer modes. Otherwise, use ADC interrupt with direct
 * DR read instead (e.g. read_raw, or triggered buffer mode without DMA).
 */
static void stm32f4_adc_start_conv(struct stm32_adc *adc, bool dma)
{
	u32 trig_msk, start_msk;

	stm32_adc_set_bits(adc, STM32F4_ADC_CR1, STM32F4_SCAN);

	if (!adc->injected && dma)
		stm32_adc_set_bits(adc, STM32F4_ADC_CR2,
				   STM32F4_DMA | STM32F4_DDS);

	if (!(stm32_adc_readl(adc, STM32F4_ADC_CR2) & STM32F4_ADON)) {
		stm32_adc_set_bits(adc, STM32F4_ADC_CR2,
				   STM32F4_EOCS | STM32F4_ADON);

		/* Wait for Power-up time (tSTAB from datasheet) */
		usleep_range(2, 3);
	}

	if (adc->injected) {
		trig_msk = STM32F4_JEXTEN_MASK;
		start_msk = STM32F4_JSWSTART;
	} else {
		trig_msk = STM32F4_EXTEN_MASK;
		start_msk = STM32F4_SWSTART;
	}

	/* Software start ? (e.g. trigger detection disabled ?) */
	if (!(stm32_adc_readl(adc, STM32F4_ADC_CR2) & trig_msk))
		stm32_adc_set_bits(adc, STM32F4_ADC_CR2, start_msk);
}

static void stm32f4_adc_stop_conv(struct stm32_adc *adc)
{
	u32 val;

	if (adc->injected) {
		stm32_adc_clr_bits(adc, STM32F4_ADC_CR2, STM32F4_JEXTEN_MASK);
		stm32_adc_clr_bits(adc, STM32F4_ADC_SR, STM32F4_JSTRT);
	} else {
		stm32_adc_clr_bits(adc, STM32F4_ADC_CR2, STM32F4_EXTEN_MASK);
		stm32_adc_clr_bits(adc, STM32F4_ADC_SR, STM32F4_STRT);
	}

	/* Disable adc when all triggered conversion have been disabled */
	val = stm32_adc_readl(adc, STM32F4_ADC_CR2);
	val &= STM32F4_EXTEN_MASK | STM32F4_JEXTEN_MASK;
	if (!val) {
		stm32_adc_clr_bits(adc, STM32F4_ADC_CR1, STM32F4_SCAN);
		stm32_adc_clr_bits(adc, STM32F4_ADC_CR2, STM32F4_ADON);
	}

	if (!adc->injected)
		stm32_adc_clr_bits(adc, STM32F4_ADC_CR2,
				   STM32F4_DMA | STM32F4_DDS);
}

static bool stm32h7_adc_is_enabled(struct stm32_adc *adc)
{
	return !!(stm32_adc_readl(adc, STM32H7_ADC_CR) & STM32H7_ADEN);
}

static bool stm32h7_adc_any_ongoing_conv(struct stm32_adc *adc)
{
	u32 val = stm32_adc_readl(adc, STM32H7_ADC_CR);

	return !!(val & (STM32H7_ADSTART | STM32H7_JADSTART));
}

static bool stm32h7_adc_is_started(struct stm32_adc *adc)
{
	u32 val = stm32_adc_readl(adc, STM32H7_ADC_CR);

	if (adc->injected)
		return !!(val & STM32H7_JADSTART);
	else
		return !!(val & STM32H7_ADSTART);
}

static void stm32h7_adc_start_conv(struct stm32_adc *adc, bool dma)
{
	enum stm32h7_adc_dmngt dmngt;
	unsigned long flags;
	u32 val;

	if (adc->injected) {
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_JADSTART);
		return;
	}

	if (dma)
		dmngt = STM32H7_DMNGT_DMA_CIRC;
	else
		dmngt = STM32H7_DMNGT_DR_ONLY;

	spin_lock_irqsave(&adc->lock, flags);
	val = stm32_adc_readl(adc, STM32H7_ADC_CFGR);
	val = (val & ~STM32H7_DMNGT_MASK) | (dmngt << STM32H7_DMNGT_SHIFT);
	stm32_adc_writel(adc, STM32H7_ADC_CFGR, val);
	spin_unlock_irqrestore(&adc->lock, flags);

	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADSTART);
}

static void stm32h7_adc_stop_conv(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int ret;
	u32 val;

	if (adc->injected) {
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_JADSTP);
		ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
						   !(val & (STM32H7_JADSTART)),
						   100, STM32_ADC_TIMEOUT_US);
		if (ret)
			dev_warn(&indio_dev->dev, "stop failed\n");
		return;
	}

	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADSTP);

	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
					   !(val & (STM32H7_ADSTART)),
					   100, STM32_ADC_TIMEOUT_US);
	if (ret)
		dev_warn(&indio_dev->dev, "stop failed\n");

	stm32_adc_clr_bits(adc, STM32H7_ADC_CFGR, STM32H7_DMNGT_MASK);
}

static int stm32h7_adc_exit_pwr_down(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int ret;
	u32 val;

	/* Is ADC already up ? */
	if (stm32_adc_readl(adc, STM32H7_ADC_CR) & STM32H7_ADVREGEN)
		return 0;

	/* Exit deep power down, then enable ADC voltage regulator */
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_DEEPPWD);
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADVREGEN);

	if (adc->common->rate > STM32H7_BOOST_CLKRATE)
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_BOOST);

	/* Wait for startup time */
	if (!adc->cfg->has_vregready) {
		usleep_range(10, 20);
		return 0;
	}

	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_ISR, val,
					   val & STM32MP1_VREGREADY, 100,
					   STM32_ADC_TIMEOUT_US);
	if (ret) {
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_DEEPPWD);
		dev_err(&indio_dev->dev, "Failed to exit power down\n");
	}

	return ret;
}

static void stm32h7_adc_enter_pwr_down(struct stm32_adc *adc)
{
	/* Check there is no regular or injected on-going conversions */
	if (stm32h7_adc_any_ongoing_conv(adc))
		return;

	stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_BOOST);

	/* Setting DEEPPWD disables ADC vreg and clears ADVREGEN */
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_DEEPPWD);
}

static int stm32h7_adc_enable(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int ret;
	u32 val;

	if (stm32h7_adc_is_enabled(adc))
		return 0;

	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADEN);

	/* Poll for ADRDY to be set (after adc startup time) */
	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_ISR, val,
					   val & STM32H7_ADRDY,
					   100, STM32_ADC_TIMEOUT_US);
	if (ret) {
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADDIS);
		dev_err(&indio_dev->dev, "Failed to enable ADC\n");
	} else {
		/* Clear ADRDY by writing one */
		stm32_adc_set_bits(adc, STM32H7_ADC_ISR, STM32H7_ADRDY);
	}

	return ret;
}

static void stm32h7_adc_disable(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int ret;
	u32 val;

	/* Check there is no regular or injected on-going conversions */
	if (stm32h7_adc_any_ongoing_conv(adc))
		return;

	/* Disable ADC and wait until it's effectively disabled */
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADDIS);
	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
					   !(val & STM32H7_ADEN), 100,
					   STM32_ADC_TIMEOUT_US);
	if (ret)
		dev_warn(&indio_dev->dev, "Failed to disable\n");
}

/**
 * stm32h7_adc_read_selfcalib() - read calibration shadow regs, save result
 * @adc: stm32 adc instance
 * Note: Must be called once ADC is enabled, so LINCALRDYW[1..6] are writable
 */
static int stm32h7_adc_read_selfcalib(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct stm32_adc_calib *cal = &adc->common->cal[adc->id];
	int i, ret;
	u32 lincalrdyw_mask, val;

	/* Read linearity calibration */
	lincalrdyw_mask = STM32H7_LINCALRDYW6;
	for (i = STM32H7_LINCALFACT_NUM - 1; i >= 0; i--) {
		/* Clear STM32H7_LINCALRDYW[6..1]: transfer calib to CALFACT2 */
		stm32_adc_clr_bits(adc, STM32H7_ADC_CR, lincalrdyw_mask);

		/* Poll: wait calib data to be ready in CALFACT2 register */
		ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
						   !(val & lincalrdyw_mask),
						   100, STM32_ADC_TIMEOUT_US);
		if (ret) {
			dev_err(&indio_dev->dev, "Failed to read calfact\n");
			return ret;
		}

		val = stm32_adc_readl(adc, STM32H7_ADC_CALFACT2);
		cal->lincalfact[i] = (val & STM32H7_LINCALFACT_MASK);
		cal->lincalfact[i] >>= STM32H7_LINCALFACT_SHIFT;

		lincalrdyw_mask >>= 1;
	}

	/* Read offset calibration */
	val = stm32_adc_readl(adc, STM32H7_ADC_CALFACT);
	cal->calfact_s = (val & STM32H7_CALFACT_S_MASK);
	cal->calfact_s >>= STM32H7_CALFACT_S_SHIFT;
	cal->calfact_d = (val & STM32H7_CALFACT_D_MASK);
	cal->calfact_d >>= STM32H7_CALFACT_D_SHIFT;
	cal->calibrated = true;

	return 0;
}

/**
 * stm32h7_adc_restore_selfcalib() - Restore saved self-calibration result
 * @adc: stm32 adc instance
 * Note: ADC must be enabled, with no on-going conversions.
 */
static int stm32h7_adc_restore_selfcalib(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct stm32_adc_calib *cal = &adc->common->cal[adc->id];
	int i, ret;
	u32 lincalrdyw_mask, val;

	/* Check there is no regular or injected on-going conversions */
	if (stm32h7_adc_any_ongoing_conv(adc))
		return 0;

	val = (cal->calfact_s << STM32H7_CALFACT_S_SHIFT) |
		(cal->calfact_d << STM32H7_CALFACT_D_SHIFT);
	stm32_adc_writel(adc, STM32H7_ADC_CALFACT, val);

	lincalrdyw_mask = STM32H7_LINCALRDYW6;
	for (i = STM32H7_LINCALFACT_NUM - 1; i >= 0; i--) {
		/*
		 * Write saved calibration data to shadow registers:
		 * Write CALFACT2, and set LINCALRDYW[6..1] bit to trigger
		 * data write. Then poll to wait for complete transfer.
		 */
		val = cal->lincalfact[i] << STM32H7_LINCALFACT_SHIFT;
		stm32_adc_writel(adc, STM32H7_ADC_CALFACT2, val);
		stm32_adc_set_bits(adc, STM32H7_ADC_CR, lincalrdyw_mask);
		ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
						   val & lincalrdyw_mask,
						   100, STM32_ADC_TIMEOUT_US);
		if (ret) {
			dev_err(&indio_dev->dev, "Failed to write calfact\n");
			return ret;
		}

		/*
		 * Read back calibration data, has two effects:
		 * - It ensures bits LINCALRDYW[6..1] are kept cleared
		 *   for next time calibration needs to be restored.
		 * - BTW, bit clear triggers a read, then check data has been
		 *   correctly written.
		 */
		stm32_adc_clr_bits(adc, STM32H7_ADC_CR, lincalrdyw_mask);
		ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
						   !(val & lincalrdyw_mask),
						   100, STM32_ADC_TIMEOUT_US);
		if (ret) {
			dev_err(&indio_dev->dev, "Failed to read calfact\n");
			return ret;
		}
		val = stm32_adc_readl(adc, STM32H7_ADC_CALFACT2);
		if (val != cal->lincalfact[i] << STM32H7_LINCALFACT_SHIFT) {
			dev_err(&indio_dev->dev, "calfact not consistent\n");
			return -EIO;
		}

		lincalrdyw_mask >>= 1;
	}

	return 0;
}

/**
 * Fixed timeout value for ADC calibration.
 * worst cases:
 * - low clock frequency
 * - maximum prescalers
 * Calibration requires:
 * - 131,072 ADC clock cycle for the linear calibration
 * - 20 ADC clock cycle for the offset calibration
 *
 * Set to 100ms for now
 */
#define STM32H7_ADC_CALIB_TIMEOUT_US		100000

/**
 * stm32h7_adc_selfcalib() - Procedure to calibrate ADC
 * @adc: stm32 adc instance
 * Note: Must be called once ADC is out of power down.
 */
static int stm32h7_adc_selfcalib(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct stm32_adc_calib *cal = &adc->common->cal[adc->id];
	int ret;
	u32 val;

	if (cal->calibrated)
		return cal->calibrated;

	/*
	 * Select calibration mode:
	 * - Offset calibration for single ended inputs
	 * - No linearity calibration (do it later, before reading it)
	 */
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_ADCALDIF);
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR, STM32H7_ADCALLIN);

	/* Start calibration, then wait for completion */
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADCAL);
	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
					   !(val & STM32H7_ADCAL), 100,
					   STM32H7_ADC_CALIB_TIMEOUT_US);
	if (ret) {
		dev_err(&indio_dev->dev, "calibration failed\n");
		goto out;
	}

	/*
	 * Select calibration mode, then start calibration:
	 * - Offset calibration for differential input
	 * - Linearity calibration (needs to be done only once for single/diff)
	 *   will run simultaneously with offset calibration.
	 */
	stm32_adc_set_bits(adc, STM32H7_ADC_CR,
			   STM32H7_ADCALDIF | STM32H7_ADCALLIN);
	stm32_adc_set_bits(adc, STM32H7_ADC_CR, STM32H7_ADCAL);
	ret = stm32_adc_readl_poll_timeout(STM32H7_ADC_CR, val,
					   !(val & STM32H7_ADCAL), 100,
					   STM32H7_ADC_CALIB_TIMEOUT_US);
	if (ret) {
		dev_err(&indio_dev->dev, "calibration failed\n");
		goto out;
	}

out:
	stm32_adc_clr_bits(adc, STM32H7_ADC_CR,
			   STM32H7_ADCALDIF | STM32H7_ADCALLIN);

	return ret;
}

/**
 * stm32h7_adc_prepare() - Leave power down mode to enable ADC.
 * @adc: stm32 adc instance
 * Leave power down mode.
 * Configure channels as single ended or differential before enabling ADC.
 * Enable ADC.
 * Restore calibration data.
 * Pre-select channels that may be used in PCSEL (required by input MUX / IO):
 * - Only one input is selected for single ended (e.g. 'vinp')
 * - Two inputs are selected for differential channels (e.g. 'vinp' & 'vinn')
 */
static int stm32h7_adc_prepare(struct stm32_adc *adc)
{
	u32 *difsel = &adc->common->difsel[adc->id];
	u32 *pcsel = &adc->common->pcsel[adc->id];
	int calib, ret;

	/* protect race between regular/injected prepare, unprepare */
	mutex_lock(&adc->common->inj[adc->id]);
	adc->common->prepcnt[adc->id]++;
	if (adc->common->prepcnt[adc->id] > 1) {
		mutex_unlock(&adc->common->inj[adc->id]);
		return 0;
	}

	ret = stm32h7_adc_exit_pwr_down(adc);
	if (ret)
		goto unlock;

	ret = stm32h7_adc_selfcalib(adc);
	if (ret < 0)
		goto pwr_dwn;
	calib = ret;

	stm32_adc_writel(adc, STM32H7_ADC_DIFSEL, *difsel);

	ret = stm32h7_adc_enable(adc);
	if (ret)
		goto pwr_dwn;

	/* Either restore or read calibration result for future reference */
	if (calib)
		ret = stm32h7_adc_restore_selfcalib(adc);
	else
		ret = stm32h7_adc_read_selfcalib(adc);
	if (ret)
		goto disable;

	stm32_adc_writel(adc, STM32H7_ADC_PCSEL, *pcsel);
	mutex_unlock(&adc->common->inj[adc->id]);

	return 0;

disable:
	stm32h7_adc_disable(adc);
pwr_dwn:
	stm32h7_adc_enter_pwr_down(adc);
unlock:
	adc->common->prepcnt[adc->id]--;
	mutex_unlock(&adc->common->inj[adc->id]);

	return ret;
}

static void stm32h7_adc_unprepare(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);

	mutex_lock(&adc->common->inj[adc->id]);
	adc->common->prepcnt[adc->id]--;
	if (adc->common->prepcnt[adc->id] > 0) {
		mutex_unlock(&adc->common->inj[adc->id]);
		return;
	}

	if (adc->common->prepcnt[adc->id] < 0)
		dev_err(&indio_dev->dev, "Unbalanced (un)prepare\n");
	stm32h7_adc_disable(adc);
	stm32h7_adc_enter_pwr_down(adc);
	mutex_unlock(&adc->common->inj[adc->id]);
}

/**
 * stm32_adc_find_unused_awd() - Find an unused analog watchdog
 * @adc: stm32 adc instance
 *
 * Loop for all AWD to find a free AWD.
 * Returns free AWD index or busy error.
 */
static int stm32_adc_find_unused_awd(struct stm32_adc *adc)
{
	const struct stm32_adc_awd_reginfo *awd_reginfo =
		adc->cfg->regs->awd_reginfo;
	u32 val, mask;
	int i;

	/* find unused AWD, either use en bits or channel mask */
	for (i = 0; i < adc->cfg->regs->num_awd; i++) {
		val = stm32_adc_readl(adc, awd_reginfo[i].reg);
		mask = awd_reginfo[i].en_bits | awd_reginfo[i].jen_bits;
		if (mask && !(val & mask))
			break;
		if (!mask && !val)
			break;
	}

	if (i >= adc->cfg->regs->num_awd)
		return -EBUSY;

	return i;
}

/**
 * stm32_adc_awd_clear() - Disable analog watchdog for one adc
 * @adc: stm32 adc instance
 *
 * Mask awd interrupts, disable awd.
 */
static void stm32_adc_awd_clear(struct stm32_adc *adc)
{
	int i;
	u32 en_bits, ier = adc->cfg->regs->ier_eoc.reg;
	struct stm32_adc_evt *evt;
	const struct stm32_adc_awd_reginfo *awd_reginfo =
		adc->cfg->regs->awd_reginfo;

	list_for_each_entry(evt, &adc->evt_list, list) {
		if (!evt->set)
			continue;

		i = evt->awd_id;

		/* Disable AWD interrupt */
		stm32_adc_clr_bits(adc, ier, awd_reginfo[i].ier_msk);

		/* Disable AWD: either use en bits and channel num, or mask */
		en_bits = awd_reginfo[i].en_bits | awd_reginfo[i].jen_bits;
		if (en_bits)
			stm32_adc_clr_bits(adc, awd_reginfo[i].reg, en_bits);
		else
			stm32_adc_writel(adc, awd_reginfo[i].reg, 0);

		adc->awd_mask &= ~awd_reginfo[i].isr_msk;
		evt->set = false;
	}
}

/**
 * stm32_adc_awd_set() - Set analog watchdog
 * @adc: stm32 adc instance
 *
 * Set analog watchdog registers based on pre-built event list.
 *
 * Two watchdog types can be found in stm32 ADC:
 * - 1st type can be used either on all channels, or on one channel. Choice
 *   is made to assing it to one channel only. It is enabled with enable bits
 *   and channel number.
 * - 2nd type uses channel mask (choice to assign it to one channel only).
 * In both case, set high & low threshold. Also unmask interrupt.
 */
static int stm32_adc_awd_set(struct stm32_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int i;
	struct stm32_adc_evt *evt;
	const struct stm32_adc_awd_reginfo *awd_reginfo =
		adc->cfg->regs->awd_reginfo;
	u32 val, ier = adc->cfg->regs->ier_eoc.reg;

	list_for_each_entry(evt, &adc->evt_list, list) {
		if (!evt->enabled)
			continue;

		i = stm32_adc_find_unused_awd(adc);
		if (i < 0) {
			stm32_adc_awd_clear(adc);
			return i;
		}

		evt->awd_id = i;
		evt->set = true;
		dev_dbg(&indio_dev->dev, "%s chan%d htr:%d ltr:%d\n",
			__func__, evt->chan->channel, evt->hthresh,
			evt->lthresh);

		stm32_adc_writel(adc, awd_reginfo[i].htr, evt->hthresh);
		stm32_adc_writel(adc, awd_reginfo[i].ltr, evt->lthresh);

		/* Enable AWD: either use en bits and channel num, or mask */
		if (awd_reginfo[i].en_bits | awd_reginfo[i].jen_bits) {
			u32 mask = awd_reginfo[i].awdch_mask;
			u32 shift = awd_reginfo[i].awdch_shift;

			val = stm32_adc_readl(adc, awd_reginfo[i].reg);
			val &= ~mask;
			val |= (evt->chan->channel << shift) & mask;

			if (adc->injected)
				val |= awd_reginfo[i].jen_bits;
			else
				val |= awd_reginfo[i].en_bits;
			stm32_adc_writel(adc, awd_reginfo[i].reg, val);
		} else {
			stm32_adc_writel(adc, awd_reginfo[i].reg,
					 BIT(evt->chan->channel));
		}

		/* Enable AWD interrupt */
		adc->awd_mask |= awd_reginfo[i].isr_msk;
		stm32_adc_set_bits(adc, ier, awd_reginfo[i].ier_msk);
	}

	return 0;
}

/**
 * stm32_adc_conf_scan_seq() - Build channels scan sequence
 * @indio_dev: IIO device
 * @scan_mask: channels to be converted
 *
 * Conversion sequence :
 * Apply sampling time settings for all channels.
 * Configure ADC scan sequence based on selected channels in scan_mask.
 * Add channels to (J)SQR registers, from scan_mask LSB to MSB, then
 * program sequence len.
 */
static int stm32_adc_conf_scan_seq(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	u32 *smpr_val = adc->common->smpr_val[adc->id];
	const struct stm32_adc_regs *sqr;
	const struct iio_chan_spec *chan;
	u32 val, bit;
	int sq_max, i = 0;

	if (adc->injected) {
		sqr = adc->cfg->regs->jsqr;
		sq_max = STM32_ADC_MAX_JSQ;
	} else {
		sqr = adc->cfg->regs->sqr;
		sq_max = STM32_ADC_MAX_SQ;
	}

	/* Apply sampling time settings */
	stm32_adc_writel(adc, adc->cfg->regs->smpr[0], smpr_val[0]);
	stm32_adc_writel(adc, adc->cfg->regs->smpr[1], smpr_val[1]);

	for_each_set_bit(bit, scan_mask, indio_dev->masklength) {
		chan = indio_dev->channels + bit;
		/*
		 * Assign one channel per SQ entry in regular
		 * sequence, starting with SQ1.
		 */
		i++;
		if (i > sq_max)
			return -EINVAL;

		dev_dbg(&indio_dev->dev, "%s chan %d to %s%d\n",
			__func__, chan->channel, adc->injected ? "JSQ" : "SQ",
			i);

		val = stm32_adc_readl(adc, sqr[i].reg);
		val &= ~sqr[i].mask;
		val |= chan->channel << sqr[i].shift;
		stm32_adc_writel(adc, sqr[i].reg, val);
	}

	if (!i)
		return -EINVAL;

	/* Sequence len */
	val = stm32_adc_readl(adc, sqr[0].reg);
	val &= ~sqr[0].mask;
	val |= ((i - 1) << sqr[0].shift);
	stm32_adc_writel(adc, sqr[0].reg, val);

	return 0;
}

/**
 * stm32_adc_get_trig_extsel() - Get external trigger selection
 * @trig: trigger
 *
 * Returns trigger extsel value, if trig matches, -EINVAL otherwise.
 */
static int stm32_adc_get_trig_extsel(struct iio_dev *indio_dev,
				     struct iio_trigger *trig)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct stm32_adc_trig_info *trinfo;
	struct iio_trigger *tr;
	int i;

	/* lookup triggers registered by stm32 timer trigger driver */
	for (i = 0; adc->cfg->trigs[i].name; i++) {
		trinfo = &adc->cfg->trigs[i];
		/**
		 * Checking both stm32 timer trigger type and trig name
		 * should be safe against arbitrary trigger names.
		 */
		if ((is_stm32_timer_trigger(trig) ||
		     is_stm32_lptim_trigger(trig)) &&
		    !strcmp(trinfo->name, trig->name)) {
			if (adc->injected && (trinfo->flags & TRG_INJECTED))
				return trinfo->jextsel;

			if (!adc->injected && (trinfo->flags & TRG_REGULAR))
				return trinfo->extsel;
		}
	}

	/* loop for triggers registered by stm32-adc-core */
	list_for_each_entry(tr, &adc->common->extrig_list, alloc_list) {
		if (tr == trig) {
			trinfo = iio_trigger_get_drvdata(trig);
			if (adc->injected && (trinfo->flags & TRG_INJECTED))
				return trinfo->jextsel;
			if (!adc->injected && (trinfo->flags & TRG_REGULAR))
				return trinfo->extsel;
		}
	}

	return -EINVAL;
}

/**
 * stm32_adc_set_trig() - Set a regular trigger
 * @indio_dev: IIO device
 * @trig: IIO trigger
 *
 * Set trigger source/polarity (e.g. SW, or HW with polarity) :
 * - if HW trigger disabled (e.g. trig == NULL, conversion launched by sw)
 * - if HW trigger enabled, set source & polarity
 */
static int stm32_adc_set_trig(struct iio_dev *indio_dev,
			      struct iio_trigger *trig)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	u32 val, extsel = 0, exten = STM32_EXTEN_SWTRIG, reg, mask,
	    exten_shift, extsel_shift;
	unsigned long flags;
	int ret;

	if (adc->injected) {
		reg = adc->cfg->regs->jexten.reg;
		mask = adc->cfg->regs->jexten.mask |
			adc->cfg->regs->jextsel.mask;
		exten_shift = adc->cfg->regs->jexten.shift;
		extsel_shift = adc->cfg->regs->jextsel.shift;
	} else {
		reg = adc->cfg->regs->exten.reg;
		mask = adc->cfg->regs->exten.mask | adc->cfg->regs->extsel.mask;
		exten_shift = adc->cfg->regs->exten.shift;
		extsel_shift = adc->cfg->regs->extsel.shift;
	}

	if (trig) {
		ret = stm32_adc_get_trig_extsel(indio_dev, trig);
		if (ret < 0)
			return ret;

		/* set trigger source and polarity (default to rising edge) */
		extsel = ret;
		exten = adc->trigger_polarity + STM32_EXTEN_HWTRIG_RISING_EDGE;
	}

	spin_lock_irqsave(&adc->lock, flags);
	val = stm32_adc_readl(adc, reg) & ~mask;
	val |= (exten << exten_shift) | (extsel << extsel_shift);
	stm32_adc_writel(adc,  reg, val);
	spin_unlock_irqrestore(&adc->lock, flags);

	return 0;
}

static int stm32_adc_set_trig_pol(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  unsigned int type)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

	adc->trigger_polarity = type;

	return 0;
}

static int stm32_adc_get_trig_pol(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan)
{
	struct stm32_adc *adc = iio_priv(indio_dev);

	return adc->trigger_polarity;
}

static const char * const stm32_trig_pol_items[] = {
	"rising-edge", "falling-edge", "both-edges",
};

static const struct iio_enum stm32_adc_trig_pol = {
	.items = stm32_trig_pol_items,
	.num_items = ARRAY_SIZE(stm32_trig_pol_items),
	.get = stm32_adc_get_trig_pol,
	.set = stm32_adc_set_trig_pol,
};

/**
 * stm32_adc_single_conv() - Performs a single conversion
 * @indio_dev: IIO device
 * @chan: IIO channel
 * @res: conversion result
 *
 * The function performs a single conversion on a given channel:
 * - Apply sampling time settings
 * - Program sequencer with one channel (e.g. in SQ1 with len = 1)
 * - Use SW trigger
 * - Start conversion, then wait for interrupt completion.
 */
static int stm32_adc_single_conv(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 int *res)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	const struct stm32_adc_regspec *regs = adc->cfg->regs;
	u32 *smpr_val = adc->common->smpr_val[adc->id];
	const struct stm32_adc_regs *sqr;
	long timeout;
	u32 val;
	int ret;

	reinit_completion(&adc->completion);

	adc->num_conv = 1;
	adc->bufi = 0;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	/* Apply sampling time settings */
	stm32_adc_writel(adc, regs->smpr[0], smpr_val[0]);
	stm32_adc_writel(adc, regs->smpr[1], smpr_val[1]);

	if (adc->injected)
		sqr = regs->jsqr;
	else
		sqr = regs->sqr;

	/* Program chan number in regular sequence (SQ1) */
	val = stm32_adc_readl(adc, sqr[1].reg) & ~sqr[1].mask;
	val |= chan->channel << sqr[1].shift;
	stm32_adc_writel(adc, sqr[1].reg, val);

	/* Set regular sequence len (0 for 1 conversion) */
	stm32_adc_clr_bits(adc, sqr[0].reg, sqr[0].mask);

	/* Trigger detection disabled (conversion can be launched in SW) */
	if (adc->injected)
		stm32_adc_clr_bits(adc, regs->jexten.reg, regs->jexten.mask);
	else
		stm32_adc_clr_bits(adc, regs->exten.reg, regs->exten.mask);

	stm32_adc_conv_irq_enable(adc);

	adc->cfg->start_conv(adc, false);

	timeout = wait_for_completion_interruptible_timeout(
					&adc->completion, STM32_ADC_TIMEOUT);
	if (timeout == 0) {
		ret = -ETIMEDOUT;
	} else if (timeout < 0) {
		ret = timeout;
	} else {
		*res = adc->buffer[0];
		ret = IIO_VAL_INT;
	}

	adc->cfg->stop_conv(adc);

	stm32_adc_conv_irq_disable(adc);

	pm_runtime_mark_last_busy(dev->parent);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static int stm32_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		if (chan->type == IIO_VOLTAGE)
			ret = stm32_adc_single_conv(indio_dev, chan, val);
		else
			ret = -EINVAL;
		iio_device_release_direct_mode(indio_dev);
		return ret;

	case IIO_CHAN_INFO_SCALE:
		if (chan->differential) {
			*val = adc->common->vref_mv * 2;
			*val2 = chan->scan_type.realbits;
		} else {
			*val = adc->common->vref_mv;
			*val2 = chan->scan_type.realbits;
		}
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_OFFSET:
		if (chan->differential)
			/* ADC_full_scale / 2 */
			*val = -((1 << chan->scan_type.realbits) / 2);
		else
			*val = 0;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static irqreturn_t stm32_adc_threaded_isr(int irq, void *data)
{
	struct stm32_adc *adc = data;
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct stm32_adc_evt *evt;
	const struct stm32_adc_regspec *regs = adc->cfg->regs;
	const struct stm32_adc_awd_reginfo *awd_reginfo = regs->awd_reginfo;
	u32 ier = regs->ier_eoc.reg, isr = regs->isr_eoc.reg;
	u32 status = stm32_adc_readl(adc, isr);
	irqreturn_t ret = IRQ_NONE;

	/* Handle analog watchdog events */
	list_for_each_entry(evt, &adc->evt_list, list) {
		if (!evt->set || !(status & awd_reginfo[evt->awd_id].isr_msk))
			continue;

		/* We don't know whether it is a upper or lower threshold. */
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(evt->chan->type,
						    evt->chan->channel,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       iio_get_time_ns(indio_dev));

		/* clear analog watchdog flag */
		if (regs->write_one_to_clear)
			stm32_adc_set_bits(adc, isr,
					   awd_reginfo[evt->awd_id].isr_msk);
		else
			stm32_adc_clr_bits(adc, isr,
					   awd_reginfo[evt->awd_id].isr_msk);

		/* re-enable current awd interrupt */
		stm32_adc_set_bits(adc, ier, awd_reginfo[evt->awd_id].ier_msk);

		ret = IRQ_HANDLED;
	}

	return ret;
}

static irqreturn_t stm32_adc_isr(int irq, void *data)
{
	struct stm32_adc *adc = data;
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	const struct stm32_adc_regspec *regs = adc->cfg->regs;
	const struct stm32_adc_awd_reginfo *awd_reginfo = regs->awd_reginfo;
	u32 status = stm32_adc_readl(adc, regs->isr_eoc.reg);
	u32 ier = adc->cfg->regs->ier_eoc.reg;
	irqreturn_t ret = IRQ_NONE;
	int i;

	if (!adc->injected && (status & regs->isr_ovr.mask)) {
		/*
		 * Overrun occured on regular conversions. Can't recover easily
		 * especially in scan mode: data for wrong channel may be read.
		 * Then, unconditionally disable interrupts to stop processing
		 * data, and lazily print error message (once).
		 */
		stm32_adc_ovr_irq_disable(adc);
		stm32_adc_conv_irq_disable(adc);
		dev_err(&indio_dev->dev, "Overrun interrupt, stopping.\n");
		return IRQ_HANDLED;
	}

	if (!adc->injected && (status & regs->isr_eoc.mask)) {
		/* Reading DR also clears EOC status flag */
		adc->buffer[adc->bufi] = stm32_adc_readw(adc, regs->dr);
		if (iio_buffer_enabled(indio_dev)) {
			adc->bufi++;
			if (adc->bufi >= adc->num_conv) {
				stm32_adc_conv_irq_disable(adc);
				iio_trigger_poll(indio_dev->trig);
			}
		} else {
			complete(&adc->completion);
		}
		ret = IRQ_HANDLED;
	}

	if (adc->injected && (status & regs->isr_jeoc.mask)) {
		int i;

		if (regs->write_one_to_clear)
			stm32_adc_writel(adc, regs->isr_jeoc.reg,
					 regs->isr_jeoc.mask);
		else
			stm32_adc_writel(adc, regs->isr_jeoc.reg,
					 ~regs->isr_jeoc.mask);

		for (i = 0; i < adc->num_conv; i++) {
			adc->buffer[i] = stm32_adc_readw(adc, regs->jdr[i]);
			adc->bufi++;
		}

		if (iio_buffer_enabled(indio_dev)) {
			stm32_adc_conv_irq_disable(adc);
			iio_trigger_poll(indio_dev->trig);
		} else {
			complete(&adc->completion);
		}
		ret = IRQ_HANDLED;
	}

	/* only check AWD assigned to this ADC (e.g. regular or injected) */
	status &= adc->awd_mask;
	if (status) {
		for (i = 0; i < adc->cfg->regs->num_awd; i++) {
			/* mask current awd interrupt */
			if (status & awd_reginfo[i].isr_msk)
				stm32_adc_clr_bits(adc, ier,
						   awd_reginfo[i].ier_msk);
		}

		/* AWD has detected an event, need to wake IRQ thread */
		ret = IRQ_WAKE_THREAD;
	}

	return ret;
}

/**
 * stm32_adc_validate_trigger() - validate trigger for stm32 adc
 * @indio_dev: IIO device
 * @trig: new trigger
 *
 * Returns: 0 if trig matches one of the triggers registered by stm32 adc
 * driver, -EINVAL otherwise.
 */
static int stm32_adc_validate_trigger(struct iio_dev *indio_dev,
				      struct iio_trigger *trig)
{
	return stm32_adc_get_trig_extsel(indio_dev, trig) < 0 ? -EINVAL : 0;
}

static int stm32_adc_set_watermark(struct iio_dev *indio_dev, unsigned int val)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	unsigned int watermark = STM32_DMA_BUFFER_SIZE / 2;
	unsigned int rx_buf_sz = STM32_DMA_BUFFER_SIZE;

	/*
	 * dma cyclic transfers are used, buffer is split into two periods.
	 * There should be :
	 * - always one buffer (period) dma is working on
	 * - one buffer (period) driver can push with iio_trigger_poll().
	 */
	watermark = min(watermark, val * (unsigned)(sizeof(u16)));
	adc->rx_buf_sz = min(rx_buf_sz, watermark * 2 * adc->num_conv);

	return 0;
}

static int stm32_adc_update_scan_mode(struct iio_dev *indio_dev,
				      const unsigned long *scan_mask)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	adc->num_conv = bitmap_weight(scan_mask, indio_dev->masklength);

	ret = stm32_adc_conf_scan_seq(indio_dev, scan_mask);
	pm_runtime_mark_last_busy(dev->parent);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

/*
 * stm32 awd monitors specified channel(s) are within window range.
 * Define events here as high/low thresholds, with a common enable for
 * both directions. There is no way to know from interrupt flags, which
 * direction an event occurred. It's up to upper layers then to check
 * value.
 */
static const struct iio_event_spec stm32_adc_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static int stm32_adc_read_event_config(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct stm32_adc_evt *evt;

	list_for_each_entry(evt, &adc->evt_list, list)
		if (evt->chan == chan)
			return evt->enabled;

	return 0;
}

static int stm32_adc_write_event_config(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					enum iio_event_type type,
					enum iio_event_direction dir,
					int state)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct stm32_adc_evt *evt;
	bool found = false;
	int i = 0;

	/* AWD can only be configured before starting conversions */
	if (adc->cfg->is_started(adc))
		return -EBUSY;

	list_for_each_entry(evt, &adc->evt_list, list) {
		if (evt->chan == chan) {
			found = true;
			evt->enabled = !!state;
		}

		/* number of enabled AWD for this adc instance */
		if (evt->enabled)
			i++;

		/* unique event per AWD: don't exceed number of AWD */
		if (i > adc->cfg->regs->num_awd)
			goto err_busy;
	}

	/* In case no threshold have been configured, can't enable evt */
	if (!found)
		return -EINVAL;

	return 0;

err_busy:
	dev_err(&indio_dev->dev, "Number of awd exceeded\n");

	list_for_each_entry(evt, &adc->evt_list, list)
		if (evt->chan == chan)
			evt->enabled = false;

	return -EBUSY;
}

static int stm32_adc_read_thresh(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 enum iio_event_info info, int *val,
				 int *val2)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct stm32_adc_evt *evt;

	*val = 0;

	list_for_each_entry(evt, &adc->evt_list, list) {
		if (evt->chan == chan) {
			if (dir == IIO_EV_DIR_RISING)
				*val = evt->hthresh;
			else
				*val = evt->lthresh;
			break;
		}
	}

	return IIO_VAL_INT;
}

static int stm32_adc_write_thresh(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir,
				  enum iio_event_info info, int val,
				  int val2)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct stm32_adc_evt *evt;
	unsigned long flags;

	if (adc->cfg->is_started(adc))
		return -EBUSY;

	/* Look for existing evt for this channel */
	list_for_each_entry(evt, &adc->evt_list, list)
		if (evt->chan == chan)
			goto found;

	/* Allocate new event: up to num_channels evts */
	evt = devm_kzalloc(&indio_dev->dev, sizeof(*evt), GFP_KERNEL);
	if (!evt)
		return -ENOMEM;

	evt->chan = chan;

	spin_lock_irqsave(&adc->lock, flags);
	list_add_tail(&evt->list, &adc->evt_list);
	spin_unlock_irqrestore(&adc->lock, flags);

found:
	if (dir == IIO_EV_DIR_RISING)
		evt->hthresh = val;
	else
		evt->lthresh = val;

	return 0;
}

static int stm32_adc_of_xlate(struct iio_dev *indio_dev,
			      const struct of_phandle_args *iiospec)
{
	int i;

	for (i = 0; i < indio_dev->num_channels; i++)
		if (indio_dev->channels[i].channel == iiospec->args[0])
			return i;

	return -EINVAL;
}

/**
 * stm32_adc_debugfs_reg_access - read or write register value
 *
 * To read a value from an ADC register:
 *   echo [ADC reg offset] > direct_reg_access
 *   cat direct_reg_access
 *
 * To write a value in a ADC register:
 *   echo [ADC_reg_offset] [value] > direct_reg_access
 */
static int stm32_adc_debugfs_reg_access(struct iio_dev *indio_dev,
					unsigned reg, unsigned writeval,
					unsigned *readval)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	if (!readval)
		stm32_adc_writel(adc, reg, writeval);
	else
		*readval = stm32_adc_readl(adc, reg);

	pm_runtime_mark_last_busy(dev->parent);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;
}

static const struct iio_info stm32_adc_iio_info = {
	.read_raw = stm32_adc_read_raw,
	.validate_trigger = stm32_adc_validate_trigger,
	.hwfifo_set_watermark = stm32_adc_set_watermark,
	.update_scan_mode = stm32_adc_update_scan_mode,
	.read_event_config = &stm32_adc_read_event_config,
	.write_event_config = &stm32_adc_write_event_config,
	.read_event_value = stm32_adc_read_thresh,
	.write_event_value = stm32_adc_write_thresh,
	.debugfs_reg_access = stm32_adc_debugfs_reg_access,
	.of_xlate = stm32_adc_of_xlate,
};

static unsigned int stm32_adc_dma_residue(struct stm32_adc *adc)
{
	struct dma_tx_state state;
	enum dma_status status;

	status = dmaengine_tx_status(adc->dma_chan,
				     adc->dma_chan->cookie,
				     &state);
	if (status == DMA_IN_PROGRESS) {
		/* Residue is size in bytes from end of buffer */
		unsigned int i = adc->rx_buf_sz - state.residue;
		unsigned int size;

		/* Return available bytes */
		if (i >= adc->bufi)
			size = i - adc->bufi;
		else
			size = adc->rx_buf_sz + i - adc->bufi;

		return size;
	}

	return 0;
}

static void stm32_adc_dma_irq_work(struct irq_work *work)
{
	struct stm32_adc *adc = container_of(work, struct stm32_adc, work);
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);

	/*
	 * iio_trigger_poll calls generic_handle_irq(). So, it requires hard
	 * irq context, and cannot be called directly from dma callback,
	 * dma cb has to schedule this work instead.
	 */
	iio_trigger_poll(indio_dev->trig);
}

static void stm32_adc_dma_buffer_done(void *data)
{
	struct iio_dev *indio_dev = data;
	struct stm32_adc *adc = iio_priv(indio_dev);

	/*
	 * Invoques iio_trigger_poll() from hard irq context: We can't
	 * call iio_trigger_poll() nor iio_trigger_poll_chained()
	 * directly from DMA callback (under tasklet e.g. softirq).
	 * They require respectively HW IRQ and threaded IRQ context
	 * as it might sleep.
	 */
	irq_work_queue(&adc->work);
}

static int stm32_adc_dma_start(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
	int ret;

	if (!adc->dma_chan)
		return 0;

	dev_dbg(&indio_dev->dev, "%s size=%d watermark=%d\n", __func__,
		adc->rx_buf_sz, adc->rx_buf_sz / 2);

	/* Prepare a DMA cyclic transaction */
	desc = dmaengine_prep_dma_cyclic(adc->dma_chan,
					 adc->rx_dma_buf,
					 adc->rx_buf_sz, adc->rx_buf_sz / 2,
					 DMA_DEV_TO_MEM,
					 DMA_PREP_INTERRUPT);
	if (!desc)
		return -EBUSY;

	desc->callback = stm32_adc_dma_buffer_done;
	desc->callback_param = indio_dev;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dmaengine_terminate_sync(adc->dma_chan);
		return ret;
	}

	/* Issue pending DMA requests */
	dma_async_issue_pending(adc->dma_chan);

	return 0;
}

static int __stm32_adc_buffer_postenable(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	ret = stm32_adc_awd_set(adc);
	if (ret) {
		dev_err(&indio_dev->dev, "Failed to configure awd\n");
		goto err_pm_put;
	}

	ret = stm32_adc_set_trig(indio_dev, indio_dev->trig);
	if (ret) {
		dev_err(&indio_dev->dev, "Can't set trigger\n");
		goto err_clr_awd;
	}

	ret = stm32_adc_dma_start(indio_dev);
	if (ret) {
		dev_err(&indio_dev->dev, "Can't start dma\n");
		goto err_clr_trig;
	}

	/* Reset adc buffer index */
	adc->bufi = 0;

	stm32_adc_ovr_irq_enable(adc);

	if (!adc->dma_chan)
		stm32_adc_conv_irq_enable(adc);

	adc->cfg->start_conv(adc, !!adc->dma_chan);

	return 0;

err_clr_trig:
	stm32_adc_set_trig(indio_dev, NULL);
err_clr_awd:
	stm32_adc_awd_clear(adc);
err_pm_put:
	pm_runtime_mark_last_busy(dev->parent);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static int stm32_adc_buffer_postenable(struct iio_dev *indio_dev)
{
	int ret;

	ret = iio_triggered_buffer_postenable(indio_dev);
	if (ret < 0)
		return ret;

	ret = __stm32_adc_buffer_postenable(indio_dev);
	if (ret < 0)
		iio_triggered_buffer_predisable(indio_dev);

	return ret;
}

static void __stm32_adc_buffer_predisable(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;

	adc->cfg->stop_conv(adc);
	if (!adc->dma_chan)
		stm32_adc_conv_irq_disable(adc);

	stm32_adc_ovr_irq_disable(adc);

	if (adc->dma_chan) {
		dmaengine_terminate_sync(adc->dma_chan);
		irq_work_sync(&adc->work);
	}

	if (stm32_adc_set_trig(indio_dev, NULL))
		dev_err(&indio_dev->dev, "Can't clear trigger\n");

	stm32_adc_awd_clear(adc);

	pm_runtime_mark_last_busy(dev->parent);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

static int stm32_adc_buffer_predisable(struct iio_dev *indio_dev)
{
	int ret;

	__stm32_adc_buffer_predisable(indio_dev);

	ret = iio_triggered_buffer_predisable(indio_dev);
	if (ret < 0)
		dev_err(&indio_dev->dev, "predisable failed\n");

	return ret;
}

static const struct iio_buffer_setup_ops stm32_adc_buffer_setup_ops = {
	.postenable = &stm32_adc_buffer_postenable,
	.predisable = &stm32_adc_buffer_predisable,
};

static irqreturn_t stm32_adc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct stm32_adc *adc = iio_priv(indio_dev);

	dev_dbg(&indio_dev->dev, "%s bufi=%d\n", __func__, adc->bufi);

	if (!adc->dma_chan) {
		/* reset buffer index */
		adc->bufi = 0;
		iio_push_to_buffers_with_timestamp(indio_dev, adc->buffer,
						   pf->timestamp);
	} else {
		int residue = stm32_adc_dma_residue(adc);

		while (residue >= indio_dev->scan_bytes) {
			u16 *buffer = (u16 *)&adc->rx_buf[adc->bufi];

			iio_push_to_buffers_with_timestamp(indio_dev, buffer,
							   pf->timestamp);
			residue -= indio_dev->scan_bytes;
			adc->bufi += indio_dev->scan_bytes;
			if (adc->bufi >= adc->rx_buf_sz)
				adc->bufi = 0;
		}
	}

	iio_trigger_notify_done(indio_dev->trig);

	/* re-enable eoc irq */
	if (!adc->dma_chan)
		stm32_adc_conv_irq_enable(adc);

	return IRQ_HANDLED;
}

static const struct iio_chan_spec_ext_info stm32_adc_ext_info[] = {
	IIO_ENUM("trigger_polarity", IIO_SHARED_BY_ALL, &stm32_adc_trig_pol),
	{
		.name = "trigger_polarity_available",
		.shared = IIO_SHARED_BY_ALL,
		.read = iio_enum_available_read,
		.private = (uintptr_t)&stm32_adc_trig_pol,
	},
	{},
};

static int stm32_adc_of_get_resolution(struct iio_dev *indio_dev)
{
	struct device_node *node = indio_dev->dev.of_node;
	struct stm32_adc *adc = iio_priv(indio_dev);
	unsigned int i;
	u32 res;

	if (of_property_read_u32(node, "assigned-resolution-bits", &res))
		res = adc->cfg->adc_info->resolutions[0];

	for (i = 0; i < adc->cfg->adc_info->num_res; i++)
		if (res == adc->cfg->adc_info->resolutions[i])
			break;
	if (i >= adc->cfg->adc_info->num_res) {
		dev_err(&indio_dev->dev, "Bad resolution: %u bits\n", res);
		return -EINVAL;
	}

	dev_dbg(&indio_dev->dev, "Using %u bits resolution\n", res);
	adc->res = i;

	return 0;
}

static void stm32_adc_smpr_init(struct stm32_adc *adc, int channel, u32 smp_ns)
{
	const struct stm32_adc_regs *smpr = &adc->cfg->regs->smp_bits[channel];
	u32 *smpr_val = adc->common->smpr_val[adc->id];
	u32 period_ns, shift = smpr->shift, mask = smpr->mask;
	unsigned int smp, r = smpr->reg;

	/* Determine sampling time (ADC clock cycles) */
	period_ns = NSEC_PER_SEC / adc->common->rate;
	for (smp = 0; smp <= STM32_ADC_MAX_SMP; smp++)
		if ((period_ns * adc->cfg->smp_cycles[smp]) >= smp_ns)
			break;
	if (smp > STM32_ADC_MAX_SMP)
		smp = STM32_ADC_MAX_SMP;

	/* pre-build sampling time registers (e.g. smpr1, smpr2) */
	smpr_val[r] = (smpr_val[r] & ~mask) | (smp << shift);
}

static void stm32_adc_chan_init_one(struct iio_dev *indio_dev,
				    struct iio_chan_spec *chan, u32 vinp,
				    u32 vinn, int scan_index, bool differential)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	char *name = adc->chan_name[vinp];
	u32 *difsel = &adc->common->difsel[adc->id];
	u32 *pcsel = &adc->common->pcsel[adc->id];

	chan->type = IIO_VOLTAGE;
	chan->channel = vinp;
	if (differential) {
		chan->differential = 1;
		chan->channel2 = vinn;
		snprintf(name, STM32_ADC_CH_SZ, "in%d-in%d", vinp, vinn);
	} else {
		snprintf(name, STM32_ADC_CH_SZ, "in%d", vinp);
	}
	chan->datasheet_name = name;
	chan->scan_index = scan_index;
	chan->indexed = 1;
	chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
	chan->info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
					 BIT(IIO_CHAN_INFO_OFFSET);
	chan->scan_type.sign = 'u';
	chan->scan_type.realbits = adc->cfg->adc_info->resolutions[adc->res];
	chan->scan_type.storagebits = 16;
	chan->ext_info = stm32_adc_ext_info;
	chan->event_spec = stm32_adc_events;
	chan->num_event_specs = ARRAY_SIZE(stm32_adc_events);

	/* pre-build selected channels mask */
	*pcsel |= BIT(chan->channel);
	if (differential) {
		/* pre-build diff channels mask */
		*difsel |= BIT(chan->channel);
		/* Also add negative input to pre-selected channels */
		*pcsel |= BIT(chan->channel2);
	}
}

static int stm32_adc_chan_of_init(struct iio_dev *indio_dev)
{
	struct device_node *node = indio_dev->dev.of_node;
	struct stm32_adc *adc = iio_priv(indio_dev);
	const struct stm32_adc_info *adc_info = adc->cfg->adc_info;
	struct stm32_adc_diff_channel diff[STM32_ADC_CH_MAX];
	struct property *prop;
	const __be32 *cur;
	struct iio_chan_spec *channels;
	int scan_index = 0, num_channels = 0, num_diff = 0, ret, i;
	u32 val, smp = 0;

	if (of_property_read_bool(node, "st,injected")) {
		dev_dbg(&indio_dev->dev, "Configured to use injected\n");
		adc->injected = true;
	}

	ret = of_property_count_u32_elems(node, "st,adc-channels");
	if (ret > adc_info->max_channels) {
		dev_err(&indio_dev->dev, "Bad st,adc-channels?\n");
		return -EINVAL;
	} else if (ret > 0) {
		num_channels += ret;
	}

	ret = of_property_count_elems_of_size(node, "st,adc-diff-channels",
					      sizeof(*diff));
	if (ret > adc_info->max_channels) {
		dev_err(&indio_dev->dev, "Bad st,adc-diff-channels?\n");
		return -EINVAL;
	} else if (ret > 0) {
		int size = ret * sizeof(*diff) / sizeof(u32);

		num_diff = ret;
		num_channels += ret;
		ret = of_property_read_u32_array(node, "st,adc-diff-channels",
						 (u32 *)diff, size);
		if (ret)
			return ret;
	}

	if (!num_channels) {
		dev_err(&indio_dev->dev, "No channels configured\n");
		return -ENODATA;
	}

	/* Optional sample time is provided either for each, or all channels */
	ret = of_property_count_u32_elems(node, "st,min-sample-time-nsecs");
	if (ret > 1 && ret != num_channels) {
		dev_err(&indio_dev->dev, "Invalid st,min-sample-time-nsecs\n");
		return -EINVAL;
	}

	channels = devm_kcalloc(&indio_dev->dev, num_channels,
				sizeof(struct iio_chan_spec), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	of_property_for_each_u32(node, "st,adc-channels", prop, cur, val) {
		if (val >= adc_info->max_channels) {
			dev_err(&indio_dev->dev, "Invalid channel %d\n", val);
			return -EINVAL;
		}

		/* Channel can't be configured both as single-ended & diff */
		for (i = 0; i < num_diff; i++) {
			if (val == diff[i].vinp) {
				dev_err(&indio_dev->dev,
					"channel %d miss-configured\n",	val);
				return -EINVAL;
			}
		}
		stm32_adc_chan_init_one(indio_dev, &channels[scan_index], val,
					0, scan_index, false);
		scan_index++;
	}

	for (i = 0; i < num_diff; i++) {
		if (diff[i].vinp >= adc_info->max_channels ||
		    diff[i].vinn >= adc_info->max_channels) {
			dev_err(&indio_dev->dev, "Invalid channel in%d-in%d\n",
				diff[i].vinp, diff[i].vinn);
			return -EINVAL;
		}
		stm32_adc_chan_init_one(indio_dev, &channels[scan_index],
					diff[i].vinp, diff[i].vinn, scan_index,
					true);
		scan_index++;
	}

	for (i = 0; i < scan_index; i++) {
		/*
		 * Using of_property_read_u32_index(), smp value will only be
		 * modified if valid u32 value can be decoded. This allows to
		 * get either no value, 1 shared value for all indexes, or one
		 * value per channel.
		 */
		of_property_read_u32_index(node, "st,min-sample-time-nsecs",
					   i, &smp);
		/* Prepare sampling time settings */
		stm32_adc_smpr_init(adc, channels[i].channel, smp);
	}

	indio_dev->num_channels = scan_index;
	indio_dev->channels = channels;

	return 0;
}

static int stm32_adc_dma_request(struct iio_dev *indio_dev)
{
	struct stm32_adc *adc = iio_priv(indio_dev);
	struct dma_slave_config config;
	int ret;

	adc->dma_chan = dma_request_slave_channel(&indio_dev->dev, "rx");
	if (!adc->dma_chan)
		return 0;

	adc->rx_buf = dma_alloc_coherent(adc->dma_chan->device->dev,
					 STM32_DMA_BUFFER_SIZE,
					 &adc->rx_dma_buf, GFP_KERNEL);
	if (!adc->rx_buf) {
		ret = -ENOMEM;
		goto err_release;
	}

	/* Configure DMA channel to read data register */
	memset(&config, 0, sizeof(config));
	config.src_addr = (dma_addr_t)adc->common->phys_base;
	config.src_addr += adc->offset + adc->cfg->regs->dr;
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;

	ret = dmaengine_slave_config(adc->dma_chan, &config);
	if (ret)
		goto err_free;

	init_irq_work(&adc->work, stm32_adc_dma_irq_work);

	return 0;

err_free:
	dma_free_coherent(adc->dma_chan->device->dev, STM32_DMA_BUFFER_SIZE,
			  adc->rx_buf, adc->rx_dma_buf);
err_release:
	dma_release_channel(adc->dma_chan);

	return ret;
}

static int stm32_adc_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct device *dev = &pdev->dev;
	struct stm32_adc *adc;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->common = dev_get_drvdata(pdev->dev.parent);
	spin_lock_init(&adc->lock);
	init_completion(&adc->completion);
	adc->cfg = (const struct stm32_adc_cfg *)
		of_match_device(dev->driver->of_match_table, dev)->data;
	INIT_LIST_HEAD(&adc->evt_list);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &stm32_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_HARDWARE_TRIGGERED;

	platform_set_drvdata(pdev, adc);

	ret = of_property_read_u32(pdev->dev.of_node, "reg", &adc->offset);
	if (ret != 0 || adc->offset >= STM32_ADCX_COMN_OFFSET) {
		dev_err(&pdev->dev, "missing reg property\n");
		return -EINVAL;
	}
	adc->id = adc->offset / STM32_ADC_OFFSET;

	of_property_read_u32(pdev->dev.of_node, "st,trigger-polarity",
			     &adc->trigger_polarity);
	if (adc->trigger_polarity >= ARRAY_SIZE(stm32_trig_pol_items)) {
		dev_err(&pdev->dev, "Invalid st,trigger-polarity property\n");
		return -EINVAL;
	}

	adc->irq = platform_get_irq(pdev, 0);
	if (adc->irq < 0) {
		dev_err(&pdev->dev, "failed to get irq\n");
		return adc->irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, adc->irq, stm32_adc_isr,
					stm32_adc_threaded_isr,
					0, pdev->name, adc);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		return ret;
	}

	adc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(adc->clk)) {
		ret = PTR_ERR(adc->clk);
		if (ret == -ENOENT && !adc->cfg->clk_required) {
			adc->clk = NULL;
		} else {
			dev_err(&pdev->dev, "Can't get clock\n");
			return ret;
		}
	}

	ret = stm32_adc_of_get_resolution(indio_dev);
	if (ret < 0)
		return ret;

	ret = stm32_adc_chan_of_init(indio_dev);
	if (ret < 0)
		return ret;

	ret = stm32_adc_dma_request(indio_dev);
	if (ret < 0)
		return ret;

	ret = iio_triggered_buffer_setup(indio_dev,
					 &iio_pollfunc_store_time,
					 &stm32_adc_trigger_handler,
					 &stm32_adc_buffer_setup_ops);
	if (ret) {
		dev_err(&pdev->dev, "buffer setup failed\n");
		goto err_dma_disable;
	}

	/* Get stm32-adc-core PM online */
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_set_autosuspend_delay(dev, STM32_ADC_HW_STOP_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	ret = stm32_adc_hw_start(dev);
	if (ret)
		goto err_buffer_cleanup;

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "iio dev register failed\n");
		goto err_hw_stop;
	}

	pm_runtime_mark_last_busy(dev->parent);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_hw_stop:
	stm32_adc_hw_stop(dev);

err_buffer_cleanup:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);
	iio_triggered_buffer_cleanup(indio_dev);

err_dma_disable:
	if (adc->dma_chan) {
		dma_free_coherent(adc->dma_chan->device->dev,
				  STM32_DMA_BUFFER_SIZE,
				  adc->rx_buf, adc->rx_dma_buf);
		dma_release_channel(adc->dma_chan);
	}

	return ret;
}

static int stm32_adc_remove(struct platform_device *pdev)
{
	struct stm32_adc *adc = platform_get_drvdata(pdev);
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);

	pm_runtime_get_sync(&pdev->dev);
	iio_device_unregister(indio_dev);
	stm32_adc_hw_stop(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	iio_triggered_buffer_cleanup(indio_dev);
	if (adc->dma_chan) {
		dma_free_coherent(adc->dma_chan->device->dev,
				  STM32_DMA_BUFFER_SIZE,
				  adc->rx_buf, adc->rx_dma_buf);
		dma_release_channel(adc->dma_chan);
	}

	return 0;
}

#if defined(CONFIG_PM_SLEEP)
static int stm32_adc_suspend(struct device *dev)
{
	struct stm32_adc *adc = dev_get_drvdata(dev);
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);

	if (iio_buffer_enabled(indio_dev))
		__stm32_adc_buffer_predisable(indio_dev);

	return pm_runtime_force_suspend(dev);
}

static int stm32_adc_resume(struct device *dev)
{
	struct stm32_adc *adc = dev_get_drvdata(dev);
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	if (!iio_buffer_enabled(indio_dev))
		return 0;

	ret = stm32_adc_update_scan_mode(indio_dev,
					 indio_dev->active_scan_mask);
	if (ret < 0)
		return ret;

	return __stm32_adc_buffer_postenable(indio_dev);
}
#endif

#if defined(CONFIG_PM)
static int stm32_adc_runtime_suspend(struct device *dev)
{
	return stm32_adc_hw_stop(dev);
}

static int stm32_adc_runtime_resume(struct device *dev)
{
	return stm32_adc_hw_start(dev);
}
#endif

static const struct dev_pm_ops stm32_adc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stm32_adc_suspend, stm32_adc_resume)
	SET_RUNTIME_PM_OPS(stm32_adc_runtime_suspend, stm32_adc_runtime_resume,
			   NULL)
};

static const struct stm32_adc_cfg stm32f4_adc_cfg = {
	.regs = &stm32f4_adc_regspec,
	.adc_info = &stm32f4_adc_info,
	.trigs = stm32f4_adc_trigs,
	.clk_required = true,
	.start_conv = stm32f4_adc_start_conv,
	.stop_conv = stm32f4_adc_stop_conv,
	.smp_cycles = stm32f4_adc_smp_cycles,
	.is_started = stm32f4_adc_is_started,
};

static const struct stm32_adc_cfg stm32h7_adc_cfg = {
	.regs = &stm32h7_adc_regspec,
	.adc_info = &stm32h7_adc_info,
	.trigs = stm32h7_adc_trigs,
	.start_conv = stm32h7_adc_start_conv,
	.stop_conv = stm32h7_adc_stop_conv,
	.prepare = stm32h7_adc_prepare,
	.unprepare = stm32h7_adc_unprepare,
	.smp_cycles = stm32h7_adc_smp_cycles,
	.is_started = stm32h7_adc_is_started,
};

static const struct stm32_adc_cfg stm32mp1_adc_cfg = {
	.regs = &stm32h7_adc_regspec,
	.adc_info = &stm32h7_adc_info,
	.trigs = stm32h7_adc_trigs,
	.has_vregready = true,
	.start_conv = stm32h7_adc_start_conv,
	.stop_conv = stm32h7_adc_stop_conv,
	.prepare = stm32h7_adc_prepare,
	.unprepare = stm32h7_adc_unprepare,
	.smp_cycles = stm32h7_adc_smp_cycles,
	.is_started = stm32h7_adc_is_started,
};

static const struct of_device_id stm32_adc_of_match[] = {
	{ .compatible = "st,stm32f4-adc", .data = (void *)&stm32f4_adc_cfg },
	{ .compatible = "st,stm32h7-adc", .data = (void *)&stm32h7_adc_cfg },
	{ .compatible = "st,stm32mp1-adc", .data = (void *)&stm32mp1_adc_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_adc_of_match);

static struct platform_driver stm32_adc_driver = {
	.probe = stm32_adc_probe,
	.remove = stm32_adc_remove,
	.driver = {
		.name = "stm32-adc",
		.of_match_table = stm32_adc_of_match,
		.pm = &stm32_adc_pm_ops,
	},
};
module_platform_driver(stm32_adc_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 ADC IIO driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:stm32-adc");
