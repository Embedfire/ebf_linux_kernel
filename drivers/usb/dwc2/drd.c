// SPDX-License-Identifier: GPL-2.0
/*
 * drd.c - DesignWare USB2 DRD Controller Dual-role support
 *
 * Copyright (C) 2019 STMicroelectronics
 *
 * Author(s): Amelie Delaunay <amelie.delaunay@st.com>
 */

#include <linux/extcon.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include "core.h"

static void dwc2_ovr_init(struct dwc2_hsotg *hsotg)
{
	unsigned long flags;
	u32 gotgctl;

	spin_lock_irqsave(&hsotg->lock, flags);

	gotgctl = dwc2_readl(hsotg, GOTGCTL);
	gotgctl |= GOTGCTL_BVALOEN | GOTGCTL_AVALOEN | GOTGCTL_VBVALOEN;
	gotgctl |= GOTGCTL_DBNCE_FLTR_BYPASS;
	gotgctl &= ~(GOTGCTL_BVALOVAL | GOTGCTL_AVALOVAL | GOTGCTL_VBVALOVAL);
	dwc2_writel(hsotg, gotgctl, GOTGCTL);

	spin_unlock_irqrestore(&hsotg->lock, flags);
}

static int dwc2_ovr_avalid(struct dwc2_hsotg *hsotg, bool valid)
{
	u32 gotgctl = dwc2_readl(hsotg, GOTGCTL);

	/* Check if A-Session is already in the right state */
	if ((valid && (gotgctl & GOTGCTL_ASESVLD)) ||
	    (!valid && !(gotgctl & GOTGCTL_ASESVLD)))
		return -EALREADY;

	if (valid)
		gotgctl |= GOTGCTL_AVALOVAL | GOTGCTL_VBVALOVAL;
	else
		gotgctl &= ~(GOTGCTL_AVALOVAL | GOTGCTL_VBVALOVAL);
	dwc2_writel(hsotg, gotgctl, GOTGCTL);

	return 0;
}

static int dwc2_ovr_bvalid(struct dwc2_hsotg *hsotg, bool valid)
{
	u32 gotgctl = dwc2_readl(hsotg, GOTGCTL);

	/* Check if B-Session is already in the right state */
	if ((valid && (gotgctl & GOTGCTL_BSESVLD)) ||
	    (!valid && !(gotgctl & GOTGCTL_BSESVLD)))
		return -EALREADY;

	if (valid)
		gotgctl |= GOTGCTL_BVALOVAL | GOTGCTL_VBVALOVAL;
	else
		gotgctl &= ~(GOTGCTL_BVALOVAL | GOTGCTL_VBVALOVAL);
	dwc2_writel(hsotg, gotgctl, GOTGCTL);

	return 0;
}

static void dwc2_drd_update(struct dwc2_hsotg *hsotg)
{
	int avalid, bvalid;
	unsigned long flags;

	avalid = extcon_get_state(hsotg->edev, EXTCON_USB_HOST);
	if (avalid < 0)
		avalid = 0;

	bvalid = extcon_get_state(hsotg->edev, EXTCON_USB);
	if (bvalid < 0)
		bvalid = 0;

	/* Skip session not in line with dr_mode */
	if ((avalid && hsotg->dr_mode == USB_DR_MODE_PERIPHERAL) ||
	    (bvalid && hsotg->dr_mode == USB_DR_MODE_HOST))
		return;

	/* Skip session if core is in test mode */
	if (!avalid && !bvalid && hsotg->test_mode) {
		dev_dbg(hsotg->dev, "Core is in test mode\n");
		return;
	}

	spin_lock_irqsave(&hsotg->lock, flags);

	if (avalid) {
		if (dwc2_ovr_avalid(hsotg, true))
			goto unlock;

		if (hsotg->dr_mode == USB_DR_MODE_OTG)
			/*
			 * This will raise a Connector ID Status Change
			 * Interrupt - connID A
			 */
			dwc2_force_mode(hsotg, true);
	} else if (bvalid) {
		if (dwc2_ovr_bvalid(hsotg, true))
			goto unlock;

		if (hsotg->dr_mode == USB_DR_MODE_OTG)
			/*
			 * This will raise a Connector ID Status Change
			 * Interrupt - connID B
			 */
			dwc2_force_mode(hsotg, false);

		/* This clear DCTL.SFTDISCON bit */
		dwc2_hsotg_core_connect(hsotg);
	} else {
		if (dwc2_ovr_avalid(hsotg, false) &&
		    dwc2_ovr_bvalid(hsotg, false))
			goto unlock;

		if (hsotg->dr_mode == USB_DR_MODE_PERIPHERAL)
			/* This set DCTL.SFTDISCON bit */
			dwc2_hsotg_core_disconnect(hsotg);

		dwc2_force_dr_mode(hsotg);
	}

	dev_dbg(hsotg->dev, "%s-session valid\n",
		avalid ? "A" : bvalid ? "B" : "No");

unlock:
	spin_unlock_irqrestore(&hsotg->lock, flags);
}

static int dwc2_drd_notifier(struct notifier_block *nb,
			     unsigned long event, void *ptr)
{
	struct dwc2_hsotg *hsotg = container_of(nb, struct dwc2_hsotg, edev_nb);

	dwc2_drd_update(hsotg);

	return NOTIFY_DONE;
}

int dwc2_drd_init(struct dwc2_hsotg *hsotg)
{
	struct extcon_dev *edev;
	int ret;

	if (of_property_read_bool(hsotg->dev->of_node, "extcon")) {
		edev = extcon_get_edev_by_phandle(hsotg->dev, 0);
		if (IS_ERR(edev)) {
			ret = PTR_ERR(edev);
			if (ret != -EPROBE_DEFER)
				dev_err(hsotg->dev,
					"couldn't get extcon device: %d\n",
					ret);
			return ret;
		}

		hsotg->edev_nb.notifier_call = dwc2_drd_notifier;
		ret = devm_extcon_register_notifier(hsotg->dev, edev,
						    EXTCON_USB,
						    &hsotg->edev_nb);
		if (ret < 0) {
			dev_err(hsotg->dev,
				"USB cable notifier register failed: %d\n",
				ret);
			return ret;
		}

		ret = devm_extcon_register_notifier(hsotg->dev, edev,
						    EXTCON_USB_HOST,
						    &hsotg->edev_nb);
		if (ret < 0) {
			dev_err(hsotg->dev,
				"USB-HOST cable notifier register failed: %d\n",
				ret);
			return ret;
		}

		hsotg->edev = edev;

		/* Enable override and initialize values */
		dwc2_ovr_init(hsotg);

		dwc2_drd_update(hsotg);
	}

	return 0;
}
