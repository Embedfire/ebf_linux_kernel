// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019~2020 NXP
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include "clk.h"
#include "clk-scu.h"
#include "clk-imx-acm-utils.h"

#include <dt-bindings/clock/imx8-clock.h>

struct imx8dxl_acm_priv {
	struct clk_imx_acm_pm_domains dev_pm;
	void __iomem *reg;
	u32 regs[0x20];
};

static const char *aud_clk_sels[] = {
	"aud_rec_clk0_lpcg_clk",
	"aud_rec_clk1_lpcg_clk",
	"ext_aud_mclk0",
	"ext_aud_mclk1",
	"dummy",
	"dummy",
	"dummy",
	"dummy",
	"spdif0_rx",
	"sai0_rx_bclk",
	"sai0_tx_bclk",
	"sai1_rx_bclk",
	"sai1_tx_bclk",
	"sai2_rx_bclk",
	"sai3_rx_bclk",
};

static const char *mclk_out_sels[] = {
	"aud_rec_clk0_lpcg_clk",
	"aud_rec_clk1_lpcg_clk",
	"dummy",
	"dummy",
	"spdif0_rx",
	"dummy",
	"dummy",
	"dummy",
};

static const char *sai_mclk_sels[] = {
	"aud_pll_div_clk0_lpcg_clk",
	"aud_pll_div_clk1_lpcg_clk",
	"acm_aud_clk0_sel",
	"acm_aud_clk1_sel",
};

static const char *spdif_mclk_sels[] = {
	"aud_pll_div_clk0_lpcg_clk",
	"aud_pll_div_clk1_lpcg_clk",
	"acm_aud_clk0_sel",
	"acm_aud_clk1_sel",
};

static const char *mqs_mclk_sels[] = {
	"aud_pll_div_clk0_lpcg_clk",
	"aud_pll_div_clk1_lpcg_clk",
	"acm_aud_clk0_sel",
	"acm_aud_clk1_sel",
};

static int imx8dxl_acm_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct clk_onecell_data *clk_data;
	struct imx8dxl_acm_priv *priv;
	struct resource *res;
	struct clk **clks;
	void __iomem *base;
	int ret;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->reg = base;

	platform_set_drvdata(pdev, priv);

	clk_data = devm_kzalloc(&pdev->dev, sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->clks = devm_kcalloc(&pdev->dev, IMX_ADMA_ACM_CLK_END,
					sizeof(*clk_data->clks), GFP_KERNEL);
	if (!clk_data->clks)
		return -ENOMEM;

	clk_data->clk_num = IMX_ADMA_ACM_CLK_END;

	clks = clk_data->clks;

	ret = clk_imx_acm_attach_pm_domains(&pdev->dev, &priv->dev_pm);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	clks[IMX_ADMA_EXT_AUD_MCLK0]     = imx_clk_fixed("ext_aud_mclk0", 0);
	clks[IMX_ADMA_EXT_AUD_MCLK1]     = imx_clk_fixed("ext_aud_mclk1", 0);
	clks[IMX_ADMA_SPDIF0_RX]         = imx_clk_fixed("spdif0_rx", 0);
	clks[IMX_ADMA_SAI0_RX_BCLK]      = imx_clk_fixed("sai0_rx_bclk", 0);
	clks[IMX_ADMA_SAI0_TX_BCLK]      = imx_clk_fixed("sai0_tx_bclk", 0);
	clks[IMX_ADMA_SAI1_RX_BCLK]      = imx_clk_fixed("sai1_rx_bclk", 0);
	clks[IMX_ADMA_SAI1_TX_BCLK]      = imx_clk_fixed("sai1_tx_bclk", 0);
	clks[IMX_ADMA_SAI2_RX_BCLK]      = imx_clk_fixed("sai2_rx_bclk", 0);
	clks[IMX_ADMA_SAI3_RX_BCLK]      = imx_clk_fixed("sai3_rx_bclk", 0);

	clks[IMX_ADMA_ACM_AUD_CLK0_SEL] = imx_dev_clk_mux(dev, "acm_aud_clk0_sel", base+0x000000, 0, 5, aud_clk_sels, ARRAY_SIZE(aud_clk_sels));
	clks[IMX_ADMA_ACM_AUD_CLK1_SEL]	= imx_dev_clk_mux(dev, "acm_aud_clk1_sel", base+0x010000, 0, 5, aud_clk_sels, ARRAY_SIZE(aud_clk_sels));

	clks[IMX_ADMA_ACM_MCLKOUT0_SEL]	= imx_dev_clk_mux(dev, "acm_mclkout0_sel", base+0x020000, 0, 3, mclk_out_sels, ARRAY_SIZE(mclk_out_sels));
	clks[IMX_ADMA_ACM_MCLKOUT1_SEL]	= imx_dev_clk_mux(dev, "acm_mclkout1_sel", base+0x030000, 0, 3, mclk_out_sels, ARRAY_SIZE(mclk_out_sels));

	clks[IMX_ADMA_ACM_SAI0_MCLK_SEL] = imx_dev_clk_mux(dev, "acm_sai0_mclk_sel", base+0x0E0000, 0, 2, sai_mclk_sels, ARRAY_SIZE(sai_mclk_sels));
	clks[IMX_ADMA_ACM_SAI1_MCLK_SEL] = imx_dev_clk_mux(dev, "acm_sai1_mclk_sel", base+0x0F0000, 0, 2, sai_mclk_sels, ARRAY_SIZE(sai_mclk_sels));
	clks[IMX_ADMA_ACM_SAI2_MCLK_SEL] = imx_dev_clk_mux(dev, "acm_sai2_mclk_sel", base+0x100000, 0, 2, sai_mclk_sels, ARRAY_SIZE(sai_mclk_sels));
	clks[IMX_ADMA_ACM_SAI3_MCLK_SEL] = imx_dev_clk_mux(dev, "acm_sai3_mclk_sel", base+0x110000, 0, 2, sai_mclk_sels, ARRAY_SIZE(sai_mclk_sels));

	clks[IMX_ADMA_ACM_SPDIF0_TX_CLK_SEL] = imx_dev_clk_mux(dev, "acm_spdif0_mclk_sel", base+0x1A0000, 0, 2, spdif_mclk_sels, ARRAY_SIZE(spdif_mclk_sels));
	clks[IMX_ADMA_ACM_MQS_TX_CLK_SEL] = imx_dev_clk_mux(dev, "acm_mqs_mclk_sel", base+0x1C0000, 0, 2, mqs_mclk_sels, ARRAY_SIZE(mqs_mclk_sels));

	for (i = 0; i < clk_data->clk_num; i++) {
		if (IS_ERR(clks[i]))
			pr_warn("i.MX clk %u: register failed with %ld\n",
				i, PTR_ERR(clks[i]));
	}

	ret = of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);

	pm_runtime_put_sync(&pdev->dev);

	return ret;
}

static int imx8dxl_acm_clk_remove(struct platform_device *pdev)
{
	struct imx8dxl_acm_priv *priv = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);

	clk_imx_acm_detach_pm_domains(&pdev->dev, &priv->dev_pm);

	return 0;
}

static const struct of_device_id imx8dxl_acm_match[] = {
	{ .compatible = "nxp,imx8dxl-acm", },
	{ /* sentinel */ }
};

static int __maybe_unused imx8dxl_acm_runtime_suspend(struct device *dev)
{
	struct imx8dxl_acm_priv *priv = dev_get_drvdata(dev);

	priv->regs[0]  = readl_relaxed(priv->reg + 0x000000);
	priv->regs[1]  = readl_relaxed(priv->reg + 0x010000);
	priv->regs[2]  = readl_relaxed(priv->reg + 0x020000);
	priv->regs[3]  = readl_relaxed(priv->reg + 0x030000);
	priv->regs[14] = readl_relaxed(priv->reg + 0x0E0000);
	priv->regs[15] = readl_relaxed(priv->reg + 0x0F0000);
	priv->regs[16] = readl_relaxed(priv->reg + 0x100000);
	priv->regs[17] = readl_relaxed(priv->reg + 0x110000);
	priv->regs[26] = readl_relaxed(priv->reg + 0x1A0000);
	priv->regs[28] = readl_relaxed(priv->reg + 0x1C0000);

	return 0;
}

static int __maybe_unused imx8dxl_acm_runtime_resume(struct device *dev)
{
	struct imx8dxl_acm_priv *priv = dev_get_drvdata(dev);

	writel_relaxed(priv->regs[0],  priv->reg + 0x000000);
	writel_relaxed(priv->regs[1],  priv->reg + 0x010000);
	writel_relaxed(priv->regs[2],  priv->reg + 0x020000);
	writel_relaxed(priv->regs[3],  priv->reg + 0x030000);
	writel_relaxed(priv->regs[14], priv->reg + 0x0E0000);
	writel_relaxed(priv->regs[15], priv->reg + 0x0F0000);
	writel_relaxed(priv->regs[16], priv->reg + 0x100000);
	writel_relaxed(priv->regs[17], priv->reg + 0x110000);
	writel_relaxed(priv->regs[26], priv->reg + 0x1A0000);
	writel_relaxed(priv->regs[28], priv->reg + 0x1C0000);

	return 0;
}

const struct dev_pm_ops imx8dxl_acm_pm_ops = {
	SET_RUNTIME_PM_OPS(imx8dxl_acm_runtime_suspend,
			   imx8dxl_acm_runtime_resume, NULL)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				      pm_runtime_force_resume)
};

static struct platform_driver imx8dxl_acm_clk_driver = {
	.driver = {
		.name = "imx8dxl-acm",
		.of_match_table = imx8dxl_acm_match,
		.pm = &imx8dxl_acm_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe = imx8dxl_acm_clk_probe,
	.remove = imx8dxl_acm_clk_remove,
};

static int __init imx8dxl_acm_init(void)
{
	return platform_driver_register(&imx8dxl_acm_clk_driver);
}
fs_initcall(imx8dxl_acm_init);
MODULE_LICENSE("GPL v2");
