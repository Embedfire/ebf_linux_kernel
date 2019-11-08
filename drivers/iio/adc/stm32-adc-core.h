/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is part of STM32 ADC driver
 *
 * Copyright (C) 2016, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>.
 *
 */

#ifndef __STM32_ADC_H
#define __STM32_ADC_H

/*
 * STM32 - ADC global register map
 * ________________________________________________________
 * | Offset |                 Register                    |
 * --------------------------------------------------------
 * | 0x000  |                Master ADC1                  |
 * --------------------------------------------------------
 * | 0x100  |                Slave ADC2                   |
 * --------------------------------------------------------
 * | 0x200  |                Slave ADC3                   |
 * --------------------------------------------------------
 * | 0x300  |         Master & Slave common regs          |
 * --------------------------------------------------------
 */
#define STM32_ADC_MAX_ADCS		3
#define STM32_ADC_OFFSET		0x100
#define STM32_ADCX_COMN_OFFSET		0x300

/* STM32F4 - Registers for each ADC instance */
#define STM32F4_ADC_SR			0x00
#define STM32F4_ADC_CR1			0x04
#define STM32F4_ADC_CR2			0x08
#define STM32F4_ADC_SMPR1		0x0C
#define STM32F4_ADC_SMPR2		0x10
#define STM32F4_ADC_HTR			0x24
#define STM32F4_ADC_LTR			0x28
#define STM32F4_ADC_SQR1		0x2C
#define STM32F4_ADC_SQR2		0x30
#define STM32F4_ADC_SQR3		0x34
#define STM32F4_ADC_JSQR		0x38
#define STM32F4_ADC_JDR1		0x3C
#define STM32F4_ADC_JDR2		0x40
#define STM32F4_ADC_JDR3		0x44
#define STM32F4_ADC_JDR4		0x48
#define STM32F4_ADC_DR			0x4C

/* STM32F4 - common registers for all ADC instances: 1, 2 & 3 */
#define STM32F4_ADC_CSR			(STM32_ADCX_COMN_OFFSET + 0x00)
#define STM32F4_ADC_CCR			(STM32_ADCX_COMN_OFFSET + 0x04)

/* STM32F4_ADC_SR - bit fields */
#define STM32F4_OVR			BIT(5)
#define STM32F4_STRT			BIT(4)
#define STM32F4_JSTRT			BIT(3)
#define STM32F4_JEOC			BIT(2)
#define STM32F4_EOC			BIT(1)
#define STM32F4_AWD			BIT(0)

/* STM32F4_ADC_CR1 - bit fields */
#define STM32F4_OVRIE			BIT(26)
#define STM32F4_RES_SHIFT		24
#define STM32F4_RES_MASK		GENMASK(25, 24)
#define STM32F4_AWDEN			BIT(23)
#define STM32F4_JAWDEN			BIT(22)
#define STM32F4_AWDSGL			BIT(9)
#define STM32F4_SCAN			BIT(8)
#define STM32F4_JEOCIE			BIT(7)
#define STM32F4_AWDIE			BIT(6)
#define STM32F4_EOCIE			BIT(5)
#define STM32F4_AWDCH_SHIFT		0
#define STM32F4_AWDCH_MASK		GENMASK(4, 0)

/* STM32F4_ADC_CR2 - bit fields */
#define STM32F4_SWSTART			BIT(30)
#define STM32F4_EXTEN_SHIFT		28
#define STM32F4_EXTEN_MASK		GENMASK(29, 28)
#define STM32F4_EXTSEL_SHIFT		24
#define STM32F4_EXTSEL_MASK		GENMASK(27, 24)
#define STM32F4_JSWSTART		BIT(22)
#define STM32F4_JEXTEN_SHIFT		20
#define STM32F4_JEXTEN_MASK		GENMASK(21, 20)
#define STM32F4_JEXTSEL_SHIFT		16
#define STM32F4_JEXTSEL_MASK		GENMASK(19, 16)
#define STM32F4_EOCS			BIT(10)
#define STM32F4_DDS			BIT(9)
#define STM32F4_DMA			BIT(8)
#define STM32F4_ADON			BIT(0)

/* STM32F4_ADC_CSR - bit fields */
#define STM32F4_OVR3			BIT(21)
#define STM32F4_JEOC3			BIT(18)
#define STM32F4_EOC3			BIT(17)
#define STM32F4_AWD3			BIT(16)
#define STM32F4_OVR2			BIT(13)
#define STM32F4_JEOC2			BIT(10)
#define STM32F4_EOC2			BIT(9)
#define STM32F4_AWD2			BIT(8)
#define STM32F4_OVR1			BIT(5)
#define STM32F4_JEOC1			BIT(2)
#define STM32F4_EOC1			BIT(1)
#define STM32F4_AWD1			BIT(0)
#define STM32F4_EOC_MASK1		(STM32F4_EOC1 | STM32F4_AWD1 | \
					 STM32F4_OVR1)
#define STM32F4_EOC_MASK2		(STM32F4_EOC2 | STM32F4_AWD2 | \
					 STM32F4_OVR2)
#define STM32F4_EOC_MASK3		(STM32F4_EOC3 | STM32F4_AWD3 | \
					 STM32F4_OVR3)
#define STM32F4_JEOC_MASK1		(STM32F4_JEOC1 | STM32F4_AWD1)
#define STM32F4_JEOC_MASK2		(STM32F4_JEOC2 | STM32F4_AWD2)
#define STM32F4_JEOC_MASK3		(STM32F4_JEOC3 | STM32F4_AWD3)

/* STM32F4_ADC_CCR - bit fields */
#define STM32F4_ADC_TSVREFE		BIT(23)
#define STM32F4_ADC_ADCPRE_SHIFT	16
#define STM32F4_ADC_ADCPRE_MASK		GENMASK(17, 16)

/* STM32H7 - Registers for each ADC instance */
#define STM32H7_ADC_ISR			0x00
#define STM32H7_ADC_IER			0x04
#define STM32H7_ADC_CR			0x08
#define STM32H7_ADC_CFGR		0x0C
#define STM32H7_ADC_SMPR1		0x14
#define STM32H7_ADC_SMPR2		0x18
#define STM32H7_ADC_PCSEL		0x1C
#define STM32H7_ADC_LTR1		0x20
#define STM32H7_ADC_HTR1		0x24
#define STM32H7_ADC_SQR1		0x30
#define STM32H7_ADC_SQR2		0x34
#define STM32H7_ADC_SQR3		0x38
#define STM32H7_ADC_SQR4		0x3C
#define STM32H7_ADC_DR			0x40
#define STM32H7_ADC_JSQR		0x4C
#define STM32H7_ADC_JDR1		0x80
#define STM32H7_ADC_JDR2		0x84
#define STM32H7_ADC_JDR3		0x88
#define STM32H7_ADC_JDR4		0x8C
#define STM32H7_ADC_AWD2CR		0xA0
#define STM32H7_ADC_AWD3CR		0xA4
#define STM32H7_ADC_LTR2		0xB0
#define STM32H7_ADC_HTR2		0xB4
#define STM32H7_ADC_LTR3		0xB8
#define STM32H7_ADC_HTR3		0xBC
#define STM32H7_ADC_DIFSEL		0xC0
#define STM32H7_ADC_CALFACT		0xC4
#define STM32H7_ADC_CALFACT2		0xC8

/* STM32H7 - common registers for all ADC instances */
#define STM32H7_ADC_CSR			(STM32_ADCX_COMN_OFFSET + 0x00)
#define STM32H7_ADC_CCR			(STM32_ADCX_COMN_OFFSET + 0x08)

/* STM32H7_ADC_ISR - bit fields */
#define STM32MP1_VREGREADY		BIT(12)
#define STM32H7_AWD3			BIT(9)
#define STM32H7_AWD2			BIT(8)
#define STM32H7_AWD1			BIT(7)
#define STM32H7_JEOS			BIT(6)
#define STM32H7_OVR			BIT(4)
#define STM32H7_EOC			BIT(2)
#define STM32H7_ADRDY			BIT(0)

/* STM32H7_ADC_IER - bit fields */
#define STM32H7_AWD3IE			STM32H7_AWD3
#define STM32H7_AWD2IE			STM32H7_AWD2
#define STM32H7_AWD1IE			STM32H7_AWD1
#define STM32H7_JEOSIE			STM32H7_JEOS
#define STM32H7_OVRIE			STM32H7_OVR
#define STM32H7_EOCIE			STM32H7_EOC

/* STM32H7_ADC_CR - bit fields */
#define STM32H7_ADCAL			BIT(31)
#define STM32H7_ADCALDIF		BIT(30)
#define STM32H7_DEEPPWD			BIT(29)
#define STM32H7_ADVREGEN		BIT(28)
#define STM32H7_LINCALRDYW6		BIT(27)
#define STM32H7_LINCALRDYW5		BIT(26)
#define STM32H7_LINCALRDYW4		BIT(25)
#define STM32H7_LINCALRDYW3		BIT(24)
#define STM32H7_LINCALRDYW2		BIT(23)
#define STM32H7_LINCALRDYW1		BIT(22)
#define STM32H7_ADCALLIN		BIT(16)
#define STM32H7_BOOST			BIT(8)
#define STM32H7_JADSTP			BIT(5)
#define STM32H7_ADSTP			BIT(4)
#define STM32H7_JADSTART		BIT(3)
#define STM32H7_ADSTART			BIT(2)
#define STM32H7_ADDIS			BIT(1)
#define STM32H7_ADEN			BIT(0)

/* STM32H7_ADC_CFGR bit fields */
#define STM32H7_AWD1CH_SHIFT		26
#define STM32H7_AWD1CH_MASK		GENMASK(30, 26)
#define STM32H7_JAWD1EN			BIT(24)
#define STM32H7_AWD1EN			BIT(23)
#define STM32H7_AWD1SGL			BIT(22)
#define STM32H7_EXTEN_SHIFT		10
#define STM32H7_EXTEN_MASK		GENMASK(11, 10)
#define STM32H7_EXTSEL_SHIFT		5
#define STM32H7_EXTSEL_MASK		GENMASK(9, 5)
#define STM32H7_RES_SHIFT		2
#define STM32H7_RES_MASK		GENMASK(4, 2)
#define STM32H7_DMNGT_SHIFT		0
#define STM32H7_DMNGT_MASK		GENMASK(1, 0)

enum stm32h7_adc_dmngt {
	STM32H7_DMNGT_DR_ONLY,		/* Regular data in DR only */
	STM32H7_DMNGT_DMA_ONESHOT,	/* DMA one shot mode */
	STM32H7_DMNGT_DFSDM,		/* DFSDM mode */
	STM32H7_DMNGT_DMA_CIRC,		/* DMA circular mode */
};

/* STM32H7_ADC_JSQR - bit fields */
#define STM32H7_JEXTEN_SHIFT		7
#define STM32H7_JEXTEN_MASK		GENMASK(8, 7)
#define STM32H7_JEXTSEL_SHIFT		2
#define STM32H7_JEXTSEL_MASK		GENMASK(6, 2)

/* STM32H7_ADC_CALFACT - bit fields */
#define STM32H7_CALFACT_D_SHIFT		16
#define STM32H7_CALFACT_D_MASK		GENMASK(26, 16)
#define STM32H7_CALFACT_S_SHIFT		0
#define STM32H7_CALFACT_S_MASK		GENMASK(10, 0)

/* STM32H7_ADC_CALFACT2 - bit fields */
#define STM32H7_LINCALFACT_SHIFT	0
#define STM32H7_LINCALFACT_MASK		GENMASK(29, 0)

/* STM32H7_ADC_CSR - bit fields */
#define STM32H7_AWD3_SLV		BIT(25)
#define STM32H7_AWD2_SLV		BIT(24)
#define STM32H7_AWD1_SLV		BIT(23)
#define STM32H7_JEOS_SLV		BIT(22)
#define STM32H7_OVR_SLV			BIT(20)
#define STM32H7_EOC_SLV			BIT(18)
#define STM32H7_AWD3_MST		BIT(9)
#define STM32H7_AWD2_MST		BIT(8)
#define STM32H7_AWD1_MST		BIT(7)
#define STM32H7_JEOS_MST		BIT(6)
#define STM32H7_OVR_MST			BIT(4)
#define STM32H7_EOC_MST			BIT(2)
#define STM32H7_EOC_MASK1		(STM32H7_EOC_MST | STM32H7_AWD1_MST | \
					 STM32H7_AWD2_MST | STM32H7_AWD3_MST | \
					 STM32H7_OVR_MST)
#define STM32H7_EOC_MASK2		(STM32H7_EOC_SLV | STM32H7_AWD1_SLV | \
					 STM32H7_AWD2_SLV | STM32H7_AWD3_SLV | \
					 STM32H7_OVR_SLV)
#define STM32H7_JEOC_MASK1		(STM32H7_JEOS_MST | STM32H7_AWD1_MST | \
					 STM32H7_AWD2_MST | STM32H7_AWD3_MST)
#define STM32H7_JEOC_MASK2		(STM32H7_JEOS_SLV | STM32H7_AWD1_SLV | \
					 STM32H7_AWD2_SLV | STM32H7_AWD3_SLV)

/* STM32H7_ADC_CCR - bit fields */
#define STM32H7_VSENSEEN		BIT(23)
#define STM32H7_PRESC_SHIFT		18
#define STM32H7_PRESC_MASK		GENMASK(21, 18)
#define STM32H7_CKMODE_SHIFT		16
#define STM32H7_CKMODE_MASK		GENMASK(17, 16)

/* Number of linear calibration shadow registers / LINCALRDYW control bits */
#define STM32H7_LINCALFACT_NUM		6

/**
 * struct stm32_adc_calib - optional adc calibration data
 * @calfact_s: Calibration offset for single ended channels
 * @calfact_d: Calibration offset in differential
 * @lincalfact: Linearity calibration factor
 * @calibrated: Indicates calibration status
 */
struct stm32_adc_calib {
	u32			calfact_s;
	u32			calfact_d;
	u32			lincalfact[STM32H7_LINCALFACT_NUM];
	bool			calibrated;
};

/**
 * struct stm32_adc_common - stm32 ADC driver common data (for all instances)
 * @base:		control registers base cpu addr
 * @phys_base:		control registers base physical addr
 * @rate:		clock rate used for analog circuitry
 * @vref_mv:		vref voltage (mv)
 * @extrig_list:	External trigger list registered by adc core
 *
 * Reserved variables for child devices, shared between regular/injected:
 * @difsel		bitmask array to set single-ended/differential channel
 * @pcsel		bitmask array to preselect channels on some devices
 * @smpr_val:		sampling time settings (e.g. smpr1 / smpr2)
 * @prepcnt:		counter to manage prepare() calls concurrency
 * @inj:		mutex to protect regular/injected concurrency
 * @cal:		optional calibration data on some devices
 */
struct stm32_adc_common {
	void __iomem			*base;
	phys_addr_t			phys_base;
	unsigned long			rate;
	int				vref_mv;
	struct list_head		extrig_list;
	u32				difsel[STM32_ADC_MAX_ADCS];
	u32				pcsel[STM32_ADC_MAX_ADCS];
	u32				smpr_val[STM32_ADC_MAX_ADCS][2];
	int				prepcnt[STM32_ADC_MAX_ADCS];
	struct mutex			inj[STM32_ADC_MAX_ADCS]; /* injected */
	struct stm32_adc_calib		cal[STM32_ADC_MAX_ADCS];
};

/* extsel - trigger mux selection value */
enum stm32_adc_extsel {
	STM32_EXT0,
	STM32_EXT1,
	STM32_EXT2,
	STM32_EXT3,
	STM32_EXT4,
	STM32_EXT5,
	STM32_EXT6,
	STM32_EXT7,
	STM32_EXT8,
	STM32_EXT9,
	STM32_EXT10,
	STM32_EXT11,
	STM32_EXT12,
	STM32_EXT13,
	STM32_EXT14,
	STM32_EXT15,
	STM32_EXT16,
	STM32_EXT17,
	STM32_EXT18,
	STM32_EXT19,
	STM32_EXT20,
};

/* trigger information flags */
#define TRG_REGULAR	BIT(0)
#define TRG_INJECTED	BIT(1)
#define TRG_BOTH	(TRG_REGULAR | TRG_INJECTED)

/**
 * struct stm32_adc_trig_info - ADC trigger info
 * @name:		name of the trigger, corresponding to its source
 * @extsel:		regular trigger selection
 * @jextsel:		injected trigger selection
 * @flags:		trigger flags: e.g. for regular, injected or both
 */
struct stm32_adc_trig_info {
	const char *name;
	u32 extsel;
	u32 jextsel;
	u32 flags;
};

#endif
