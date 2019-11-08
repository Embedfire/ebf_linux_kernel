/*
 * Copyright (C) STMicroelectronics SA 2017
 * Author: Gabriel Fernandez <gabriel.fernandez@st.com>
 *
 * License terms: GPL V2.0.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/arm-smccc.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/*
 * Registers
 */
#define REG_PWR_CR3 0x0C

/*
 * SYSTEM_PARAMETER
 */
#define REG_1_1_EN  BIT(30)
#define REG_1_8_EN  BIT(28)
#define USB_3_3_EN  BIT(24)

#define STM32_SMC_PWR		0x82001001
#define STM32_WRITE		0x1
#define STM32_SMC_REG_SET	0x2
#define STM32_SMC_REG_CLEAR	0x3

#define SMC(class, op, address, val)\
	({\
	struct arm_smccc_res res;\
	arm_smccc_smc(class, op, address, val,\
			0, 0, 0, 0, &res);\
	})

static int stm32_pwr_secure_regulator_enable(struct regulator_dev *rdev)
{
	SMC(STM32_SMC_PWR, STM32_SMC_REG_SET, rdev->desc->enable_reg,
	    rdev->desc->enable_mask);

	return 0;
}

static int stm32_pwr_secure_regulator_disable(struct regulator_dev *rdev)
{
	SMC(STM32_SMC_PWR, STM32_SMC_REG_CLEAR, rdev->desc->enable_reg,
	    rdev->desc->enable_mask);

	return 0;
}

static const struct regulator_ops stm32_pwr_reg_ops = {
	.list_voltage	= regulator_list_voltage_linear,
	.enable		= regulator_enable_regmap,
	.disable	= regulator_disable_regmap,
	.is_enabled	= regulator_is_enabled_regmap,
};

static const struct regulator_ops stm32_pwr_reg_secure_ops = {
	.list_voltage	= regulator_list_voltage_linear,
	.enable		= stm32_pwr_secure_regulator_enable,
	.disable	= stm32_pwr_secure_regulator_disable,
	.is_enabled	= regulator_is_enabled_regmap,
};

static const struct regulator_desc stm32_pwr_reg11 = {
	.name = "REG11",
	.of_match = of_match_ptr("reg11"),
	.n_voltages = 1,
	.type = REGULATOR_VOLTAGE,
	.min_uV = 1100000,
	.fixed_uV = 1100000,
	.ops = &stm32_pwr_reg_ops,
	.enable_reg = REG_PWR_CR3,
	.enable_mask = REG_1_1_EN,
	.owner = THIS_MODULE,
	.supply_name = "vdd",
};

static const struct regulator_desc stm32_pwr_reg18 = {
	.name = "REG18",
	.of_match = of_match_ptr("reg18"),
	.n_voltages = 1,
	.type = REGULATOR_VOLTAGE,
	.min_uV = 1800000,
	.fixed_uV = 1800000,
	.ops = &stm32_pwr_reg_ops,
	.enable_reg = REG_PWR_CR3,
	.enable_mask = REG_1_8_EN,
	.owner = THIS_MODULE,
	.supply_name = "vdd",
};

static const struct regulator_desc stm32_pwr_usb33 = {
	.name = "USB33",
	.of_match = of_match_ptr("usb33"),
	.n_voltages = 1,
	.type = REGULATOR_VOLTAGE,
	.min_uV = 3300000,
	.fixed_uV = 3300000,
	.ops = &stm32_pwr_reg_ops,
	.enable_reg = REG_PWR_CR3,
	.enable_mask = USB_3_3_EN,
	.owner = THIS_MODULE,
	.supply_name = "vdd_3v3_usbfs",
};

static struct of_regulator_match stm32_pwr_reg_matches[] = {
	{ .name = "reg11", .driver_data = (void *)&stm32_pwr_reg11 },
	{ .name = "reg18", .driver_data = (void *)&stm32_pwr_reg18 },
	{ .name = "usb33", .driver_data = (void *)&stm32_pwr_usb33 },
};

#define STM32PWR_REG_NUM_REGS ARRAY_SIZE(stm32_pwr_reg_matches)

static int is_stm32_soc_secured(struct platform_device *pdev, int *val)
{
	struct device_node *np = pdev->dev.of_node;
	struct regmap *syscon;
	u32 reg, mask;
	int tzc_val = 0;
	int err;

	syscon = syscon_regmap_lookup_by_phandle(np, "st,tzcr");
	if (IS_ERR(syscon)) {
		dev_err(&pdev->dev, "tzcr syscon required !\n");
		return PTR_ERR(syscon);
	}

	err = of_property_read_u32_index(np, "st,tzcr", 1, &reg);
	if (err) {
		dev_err(&pdev->dev, "tzcr offset required !\n");
		return err;
	}

	err = of_property_read_u32_index(np, "st,tzcr", 2, &mask);
	if (err) {
		dev_err(&pdev->dev, "tzcr mask required !\n");
		return err;
	}

	err = regmap_read(syscon, reg, &tzc_val);
	if (err) {
		dev_err(&pdev->dev, "failed to read tzcr status !\n");
		return err;
	}

	*val = tzc_val & mask;

	return 0;
}

static int stm32_power_regulator_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct regulator_dev *rdev;
	struct regulator_config config = { };
	struct regmap *regmap;
	struct regulator_desc *desc;
	int i, ret = 0;
	int tzen = 0;

	of_regulator_match(&pdev->dev, np, stm32_pwr_reg_matches,
			   STM32PWR_REG_NUM_REGS);

	regmap = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	config.regmap = regmap;
	config.dev = &pdev->dev;

	ret = is_stm32_soc_secured(pdev, &tzen);
	if (ret)
		return ret;

	for (i = 0; i < STM32PWR_REG_NUM_REGS; i++) {
		struct of_regulator_match *match = &stm32_pwr_reg_matches[i];

		if (!match->init_data ||
		    !match->of_node)
			continue;

		config.init_data = match->init_data;
		config.driver_data = match->driver_data;
		config.of_node = match->of_node;

		if  (tzen) {
			desc = match->driver_data;
			desc->ops = &stm32_pwr_reg_secure_ops;
		}

		rdev = devm_regulator_register(&pdev->dev,
					       match->driver_data,
					       &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(&pdev->dev,
				"Failed to register regulator: %d\n", ret);
			break;
		}
	}
	return ret;
}

static const struct of_device_id stm32_pwr_reg_of_match[] = {
	{ .compatible = "st,stm32mp1,pwr-reg", },
	{},
};

static struct platform_driver stm32_pwr_reg_driver = {
	.probe = stm32_power_regulator_probe,
	.driver = {
		.name = "stm32-pwr-regulator",
		.of_match_table = of_match_ptr(stm32_pwr_reg_of_match),
	},
};

static int __init stm32_pwr_regulator_init(void)
{
	return platform_driver_register(&stm32_pwr_reg_driver);
}
subsys_initcall(stm32_pwr_regulator_init);
