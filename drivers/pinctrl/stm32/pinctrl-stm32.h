// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Maxime Coquelin 2015
 * Copyright (C) STMicroelectronics 2017
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 */
#ifndef __PINCTRL_STM32_H
#define __PINCTRL_STM32_H

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>

#define STM32_PIN_NO(x) ((x) << 8)
#define STM32_GET_PIN_NO(x) ((x) >> 8)
#define STM32_GET_PIN_FUNC(x) ((x) & 0xff)

#define STM32_PIN_GPIO		0
#define STM32_PIN_AF(x)		((x) + 1)
#define STM32_PIN_ANALOG	(STM32_PIN_AF(15) + 1)
#define STM32_PIN_RSVD		(STM32_PIN_ANALOG + 1)
#define STM32_CONFIG_NUM	(STM32_PIN_RSVD + 1)

/*  package information */
#define STM32MP157CAA		BIT(0)
#define STM32MP157CAB		BIT(1)
#define STM32MP157CAC		BIT(2)
#define STM32MP157CAD		BIT(3)

#define STM32MP157_Z_BASE_SHIFT	400

struct stm32_desc_function {
	const char *name;
	const unsigned char num;
};

struct stm32_desc_pin {
	struct pinctrl_pin_desc pin;
	const struct stm32_desc_function functions[STM32_CONFIG_NUM];
	const unsigned int pkg;
};

#define STM32_PIN(_pin, ...)					\
	{							\
		.pin = _pin,					\
		.functions = (struct stm32_desc_function[]){	\
			__VA_ARGS__, { } },			\
	}

#define STM32_PIN_PKG(_pin, _pkg, ...)					\
	{							\
		.pin = _pin,					\
		.pkg  = _pkg,				\
		.functions = {	\
			__VA_ARGS__},			\
	}
#define STM32_FUNCTION(_num, _name)		\
	[_num] = {						\
		.num = _num,					\
		.name = _name,					\
	}

struct stm32_pinctrl_match_data {
	const struct stm32_desc_pin *pins;
	const unsigned int npins;
	const unsigned int pin_base_shift;
};

struct stm32_gpio_bank;

int stm32_pctl_probe(struct platform_device *pdev);
void stm32_pmx_get_mode(struct stm32_gpio_bank *bank,
			int pin, u32 *mode, u32 *alt);

#ifdef CONFIG_PM
void stm32_gpio_backup_value(struct stm32_gpio_bank *bank,
			     u32 offset, u32 value);
void stm32_gpio_backup_driving(struct stm32_gpio_bank *bank,
			       u32 offset, u32 drive);
void stm32_gpio_backup_speed(struct stm32_gpio_bank *bank,
			     u32 offset, u32 speed);
void stm32_gpio_backup_mode(struct stm32_gpio_bank *bank,
			    u32 offset, u32 mode, u32 alt);
void stm32_gpio_backup_bias(struct stm32_gpio_bank *bank,
			    u32 offset, u32 bias);
int stm32_pinctrl_resume(struct device *dev);
#else
static void stm32_gpio_backup_value(struct stm32_gpio_bank *bank,
				    u32 offset, u32 value)
{}
static void stm32_gpio_backup_driving(struct stm32_gpio_bank *bank,
				      u32 offset, u32 drive)
{}
static void stm32_gpio_backup_speed(struct stm32_gpio_bank *bank,
				    u32 offset, u32 speed)
{}
static void stm32_gpio_backup_mode(struct stm32_gpio_bank *bank,
				   u32 offset, u32 mode, u32 alt)
{}
static void stm32_gpio_backup_bias(struct stm32_gpio_bank *bank,
				   u32 offset, u32 bias)
{}
#endif /*  CONFIG_PM */
#endif /* __PINCTRL_STM32_H */

