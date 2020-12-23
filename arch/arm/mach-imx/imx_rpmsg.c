/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 *
 * derived from the omap-rpmsg implementation.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <linux/imx_rpmsg.h>
#include "common.h"

struct imx_virdev {
	struct virtio_device vdev;
	unsigned int vring[2];
	struct virtqueue *vq[2];
	int base_vq_id;
	int num_of_vqs;
	struct notifier_block nb;
};

struct imx_rpmsg_vproc {
	char *rproc_name;
	struct mutex lock;
	int vdev_nums;
#define MAX_VDEV_NUMS	5
	struct imx_virdev ivdev[MAX_VDEV_NUMS];
};

/*
 * For now, allocate 256 buffers of 512 bytes for each side. each buffer
 * will then have 16B for the msg header and 496B for the payload.
 * This will require a total space of 256KB for the buffers themselves, and
 * 3 pages for every vring (the size of the vring depends on the number of
 * buffers it supports).
 */
#define RPMSG_NUM_BUFS		(512)
#define RPMSG_BUF_SIZE		(512)
#define RPMSG_BUFS_SPACE	(RPMSG_NUM_BUFS * RPMSG_BUF_SIZE)

/*
 * The alignment between the consumer and producer parts of the vring.
 * Note: this is part of the "wire" protocol. If you change this, you need
 * to update your BIOS image as well
 */
#define RPMSG_VRING_ALIGN	(4096)

/* With 256 buffers, our vring will occupy 3 pages */
#define RPMSG_RING_SIZE	((DIV_ROUND_UP(vring_size(RPMSG_NUM_BUFS / 2, \
				RPMSG_VRING_ALIGN), PAGE_SIZE)) * PAGE_SIZE)

#define to_imx_virdev(vd) container_of(vd, struct imx_virdev, vdev)
#define to_imx_rpdev(vd, id) container_of(vd, struct imx_rpmsg_vproc, ivdev[id])

struct imx_rpmsg_vq_info {
	__u16 num;	/* number of entries in the virtio_ring */
	__u16 vq_id;	/* a globaly unique index of this virtqueue */
	void *addr;	/* address where we mapped the virtio ring */
	struct imx_rpmsg_vproc *rpdev;
};

static u64 imx_rpmsg_get_features(struct virtio_device *vdev)
{
	/* VIRTIO_RPMSG_F_NS has been made private */
	return 1 << 0;
}

static int imx_rpmsg_finalize_features(struct virtio_device *vdev)
{
	/* Give virtio_ring a chance to accept features */
	vring_transport_features(vdev);
	return 0;
}

/* kick the remote processor, and let it know which virtqueue to poke at */
static bool imx_rpmsg_notify(struct virtqueue *vq)
{
	int ret;
	unsigned int mu_rpmsg = 0;
	struct imx_rpmsg_vq_info *rpvq = vq->priv;

	mu_rpmsg = rpvq->vq_id << 16;
	mutex_lock(&rpvq->rpdev->lock);
	/* send the index of the triggered virtqueue as the mu payload */
	ret = imx_mu_rpmsg_send(mu_rpmsg);
	mutex_unlock(&rpvq->rpdev->lock);
	if (ret) {
		pr_err("ugh, imx_mu_rpmsg_send() failed: %d\n", ret);
		return false;
	}

	return true;
}

static int imx_mu_rpmsg_callback(struct notifier_block *this,
					unsigned long index, void *data)
{
	u32 mu_msg = (u32) data;
	struct imx_virdev *virdev;

	virdev = container_of(this, struct imx_virdev, nb);

	pr_debug("%s mu_msg: 0x%x\n", __func__, mu_msg);

	/* ignore vq indices which are clearly not for us */
	mu_msg = mu_msg >> 16;
	if (mu_msg < virdev->base_vq_id || mu_msg > virdev->base_vq_id + 1) {
		pr_debug("mu_msg: 0x%x is invalid\n", mu_msg);
		return NOTIFY_DONE;
	}

	mu_msg -= virdev->base_vq_id;

	/*
	 * Currently both PENDING_MSG and explicit-virtqueue-index
	 * messaging are supported.
	 * Whatever approach is taken, at this point 'mu_msg' contains
	 * the index of the vring which was just triggered.
	 */
	if (mu_msg < virdev->num_of_vqs)
		vring_interrupt(mu_msg, virdev->vq[mu_msg]);

	return NOTIFY_DONE;
}

static struct virtqueue *rp_find_vq(struct virtio_device *vdev,
				    unsigned index,
				    void (*callback)(struct virtqueue *vq),
				    const char *name,
				    bool ctx)
{
	struct imx_virdev *virdev = to_imx_virdev(vdev);
	struct imx_rpmsg_vproc *rpdev = to_imx_rpdev(virdev,
						     virdev->base_vq_id / 2);
	struct imx_rpmsg_vq_info *rpvq;
	struct virtqueue *vq;
	int err;

	rpvq = kmalloc(sizeof(*rpvq), GFP_KERNEL);
	if (!rpvq)
		return ERR_PTR(-ENOMEM);

	/* ioremap'ing normal memory, so we cast away sparse's complaints */
	rpvq->addr = (__force void *) ioremap_nocache(virdev->vring[index],
							RPMSG_RING_SIZE);
	if (!rpvq->addr) {
		err = -ENOMEM;
		goto free_rpvq;
	}

	memset(rpvq->addr, 0, RPMSG_RING_SIZE);

	pr_debug("vring%d: phys 0x%x, virt 0x%x\n", index, virdev->vring[index],
					(unsigned int) rpvq->addr);

	vq = vring_new_virtqueue(index, RPMSG_NUM_BUFS / 2, RPMSG_VRING_ALIGN,
			vdev, true, ctx,
			rpvq->addr,
			imx_rpmsg_notify, callback,
			name);
	if (!vq) {
		pr_err("vring_new_virtqueue failed\n");
		err = -ENOMEM;
		goto unmap_vring;
	}

	virdev->vq[index] = vq;
	vq->priv = rpvq;
	/* system-wide unique id for this virtqueue */
	rpvq->vq_id = virdev->base_vq_id + index;
	rpvq->rpdev = rpdev;
	mutex_init(&rpdev->lock);

	return vq;

unmap_vring:
	/* iounmap normal memory, so make sparse happy */
	iounmap((__force void __iomem *) rpvq->addr);
free_rpvq:
	kfree(rpvq);
	return ERR_PTR(err);
}

static void imx_rpmsg_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;
	struct imx_virdev *virdev = to_imx_virdev(vdev);
	struct imx_rpmsg_vproc *rpdev = to_imx_rpdev(virdev,
						     virdev->base_vq_id / 2);

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		struct imx_rpmsg_vq_info *rpvq = vq->priv;

		iounmap(rpvq->addr);
		vring_del_virtqueue(vq);
		kfree(rpvq);
	}

	if (&virdev->nb)
		imx_mu_rpmsg_unregister_nb((const char *)rpdev->rproc_name,
				&virdev->nb);
}

static int imx_rpmsg_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char * const names[],
		       const bool *ctx,
		       struct irq_affinity *desc)
{
	struct imx_virdev *virdev = to_imx_virdev(vdev);
	struct imx_rpmsg_vproc *rpdev = to_imx_rpdev(virdev,
						     virdev->base_vq_id / 2);
	int i, err;

	/* we maintain two virtqueues per remote processor (for RX and TX) */
	if (nvqs != 2)
		return -EINVAL;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = rp_find_vq(vdev, i, callbacks[i], names[i],
				ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	virdev->num_of_vqs = nvqs;

	virdev->nb.notifier_call = imx_mu_rpmsg_callback;
	imx_mu_rpmsg_register_nb((const char *)rpdev->rproc_name, &virdev->nb);

	return 0;

error:
	imx_rpmsg_del_vqs(vdev);
	return err;
}

static void imx_rpmsg_reset(struct virtio_device *vdev)
{
	dev_dbg(&vdev->dev, "reset !\n");
}

static u8 imx_rpmsg_get_status(struct virtio_device *vdev)
{
	return 0;
}

static void imx_rpmsg_set_status(struct virtio_device *vdev, u8 status)
{
	dev_dbg(&vdev->dev, "%s new status: %d\n", __func__, status);
}

static void imx_rpmsg_vproc_release(struct device *dev)
{
	/* this handler is provided so driver core doesn't yell at us */
}

static struct virtio_config_ops imx_rpmsg_config_ops = {
	.get_features	= imx_rpmsg_get_features,
	.finalize_features = imx_rpmsg_finalize_features,
	.find_vqs	= imx_rpmsg_find_vqs,
	.del_vqs	= imx_rpmsg_del_vqs,
	.reset		= imx_rpmsg_reset,
	.set_status	= imx_rpmsg_set_status,
	.get_status	= imx_rpmsg_get_status,
};

static struct imx_rpmsg_vproc imx_rpmsg_vprocs[] = {
	{
		.rproc_name	= "m4",
	},
};

static const struct of_device_id imx_rpmsg_dt_ids[] = {
	{ .compatible = "fsl,imx6sx-rpmsg", },
	{ .compatible = "fsl,imx7d-rpmsg", },
	{ .compatible = "fsl,imx7ulp-rpmsg", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_rpmsg_dt_ids);

static int set_vring_phy_buf(struct platform_device *pdev,
		       struct imx_rpmsg_vproc *rpdev, int vdev_nums)
{
	struct resource *res;
	resource_size_t size;
	unsigned int start, end;
	int i, ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		size = resource_size(res);
		start = res->start;
		end = res->start + size;
		for (i = 0; i < vdev_nums; i++) {
			rpdev->ivdev[i].vring[0] = start;
			rpdev->ivdev[i].vring[1] = start +
						   0x8000;
			start += 0x10000;
			if (start > end) {
				pr_err("Too small memory size %x!\n", size);
				ret = -EINVAL;
				break;
			}
		}
	} else {
		return -ENOMEM;
	}

	return ret;
}

static int imx_rpmsg_probe(struct platform_device *pdev)
{
	int i, j, ret = 0;
	struct device_node *np = pdev->dev.of_node;

	for (i = 0; i < ARRAY_SIZE(imx_rpmsg_vprocs); i++) {
		struct imx_rpmsg_vproc *rpdev = &imx_rpmsg_vprocs[i];

		ret = of_property_read_u32_index(np, "vdev-nums", i,
			&rpdev->vdev_nums);
		if (ret)
			rpdev->vdev_nums = 1;
		if (rpdev->vdev_nums > MAX_VDEV_NUMS) {
			pr_err("vdev-nums exceed the max %d\n", MAX_VDEV_NUMS);
			return -EINVAL;
		}

		if (!strcmp(rpdev->rproc_name, "m4")) {
			ret = set_vring_phy_buf(pdev, rpdev,
						rpdev->vdev_nums);
			if (ret) {
				pr_err("No vring buffer.\n");
				return -ENOMEM;
			}
		} else {
			pr_err("No remote m4 processor.\n");
			return -ENODEV;
		}

		for (j = 0; j < rpdev->vdev_nums; j++) {
			pr_debug("%s rpdev%d vdev%d: vring0 0x%x, vring1 0x%x\n",
				 __func__, i, rpdev->vdev_nums,
				 rpdev->ivdev[j].vring[0],
				 rpdev->ivdev[j].vring[1]);
			rpdev->ivdev[j].vdev.id.device = VIRTIO_ID_RPMSG;
			rpdev->ivdev[j].vdev.config = &imx_rpmsg_config_ops;
			rpdev->ivdev[j].vdev.dev.parent = &pdev->dev;
			rpdev->ivdev[j].vdev.dev.release = imx_rpmsg_vproc_release;
			rpdev->ivdev[j].base_vq_id = j * 2;

			ret = register_virtio_device(&rpdev->ivdev[j].vdev);
			if (ret) {
				pr_err("%s failed to register rpdev: %d\n",
						__func__, ret);
				return ret;
			}

		}
	}

	return ret;
}

static int imx_rpmsg_remove(struct platform_device *pdev)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(imx_rpmsg_vprocs); i++) {
		struct imx_rpmsg_vproc *rpdev = &imx_rpmsg_vprocs[i];

		for (j = 0; j < rpdev->vdev_nums; j++)
			unregister_virtio_device(&rpdev->ivdev[j].vdev);
	}
	return 0;
}

static void imx_rpmsg_shutdown(struct platform_device *pdev)
{
	pm_shutdown_notify_m4();
	pr_info("shutdown imx rpmsg.\n");
}

static struct platform_driver imx_rpmsg_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "imx-rpmsg",
		   .of_match_table = imx_rpmsg_dt_ids,
		   },
	.probe = imx_rpmsg_probe,
	.remove = imx_rpmsg_remove,
	.shutdown = imx_rpmsg_shutdown,
};

static int __init imx_rpmsg_init(void)
{
	int ret;

	ret = platform_driver_register(&imx_rpmsg_driver);
	if (ret)
		pr_err("Unable to initialize rpmsg driver\n");
	else
		pr_info("imx rpmsg driver is registered.\n");

	return ret;
}

static void __exit imx_rpmsg_exit(void)
{
	pr_info("imx rpmsg driver is unregistered.\n");
	platform_driver_unregister(&imx_rpmsg_driver);
}

module_exit(imx_rpmsg_exit);
module_init(imx_rpmsg_init);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("iMX remote processor messaging virtio device");
MODULE_LICENSE("GPL v2");
