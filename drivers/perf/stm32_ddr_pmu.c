// SPDX-License-Identifier: GPL-2.0
/*
 * This file is the STM32 DDR performance monitor (DDRPERFM) driver
 *
 * Copyright (C) 2019, STMicroelectronics - All Rights Reserved
 * Author: Gerald Baeza <gerald.baeza@st.com>
 */

#include <linux/clk.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/perf_event.h>
#include <linux/slab.h>
#include <linux/types.h>

#define POLL_MS		4000	/* The counter roll over after ~8s @533MHz */
#define WORD_LENGTH	4	/* Bytes */
#define BURST_LENGTH	8	/* Words */

#define DDRPERFM_CTL	0x000
#define DDRPERFM_CFG	0x004
#define DDRPERFM_STATUS 0x008
#define DDRPERFM_CCR	0x00C
#define DDRPERFM_IER	0x010
#define DDRPERFM_ISR	0x014
#define DDRPERFM_ICR	0x018
#define DDRPERFM_TCNT	0x020
#define DDRPERFM_CNT(X)	(0x030 + 8 * (X))
#define DDRPERFM_HWCFG	0x3F0
#define DDRPERFM_VER	0x3F4
#define DDRPERFM_ID	0x3F8
#define DDRPERFM_SID	0x3FC

#define CTL_START	0x00000001
#define CTL_STOP	0x00000002
#define CCR_CLEAR_ALL	0x8000000F
#define SID_MAGIC_ID	0xA3C5DD01

#define STRING "Read = %llu, Write = %llu, Read & Write = %llu (MB/s)\n"

enum {
	READ_CNT,
	WRITE_CNT,
	ACTIVATE_CNT,
	IDLE_CNT,
	TIME_CNT,
	PMU_NR_COUNTERS
};

struct stm32_ddr_pmu {
	struct pmu pmu;
	void __iomem *membase;
	struct clk *clk;
	struct clk *clk_ddr;
	unsigned long clk_ddr_rate;
	struct hrtimer hrtimer;
	ktime_t poll_period;
	spinlock_t lock; /* for shared registers access */
	struct perf_event *events[PMU_NR_COUNTERS];
	u64 events_cnt[PMU_NR_COUNTERS];
};

static inline struct stm32_ddr_pmu *pmu_to_stm32_ddr_pmu(struct pmu *p)
{
	return container_of(p, struct stm32_ddr_pmu, pmu);
}

static inline struct stm32_ddr_pmu *hrtimer_to_stm32_ddr_pmu(struct hrtimer *h)
{
	return container_of(h, struct stm32_ddr_pmu, hrtimer);
}

static u64 stm32_ddr_pmu_compute_bw(struct stm32_ddr_pmu *stm32_ddr_pmu,
				    int counter)
{
	u64 bw = stm32_ddr_pmu->events_cnt[counter], tmp;
	u64 div = stm32_ddr_pmu->events_cnt[TIME_CNT];
	u32 prediv = 1, premul = 1;

	if (bw && div) {
		/* Maximize the dividend into 64 bits */
		while ((bw < 0x8000000000000000ULL) &&
		       (premul < 0x40000000UL)) {
			bw = bw << 1;
			premul *= 2;
		}
		/* Minimize the dividor to fit in 32 bits */
		while ((div > 0xffffffffUL) && (prediv < 0x40000000UL)) {
			div = div >> 1;
			prediv *= 2;
		}
		/* Divide counter per time and multiply per DDR settings */
		do_div(bw, div);
		tmp = bw * BURST_LENGTH * WORD_LENGTH;
		tmp *= stm32_ddr_pmu->clk_ddr_rate;
		if (tmp < bw)
			goto error;
		bw = tmp;
		/* Cancel the prediv and premul factors */
		while (prediv > 1) {
			bw = bw >> 1;
			prediv /= 2;
		}
		while (premul > 1) {
			bw = bw >> 1;
			premul /= 2;
		}
		/* Convert MHz to Hz and B to MB, to finally get MB/s */
		tmp = bw * 1000000;
		if (tmp < bw)
			goto error;
		bw = tmp;
		premul = 1024 * 1024;
		while (premul > 1) {
			bw = bw >> 1;
			premul /= 2;
		}
	}
	return bw;

error:
	pr_warn("stm32-ddr-pmu: overflow detected\n");
	return 0;
}

static void stm32_ddr_pmu_event_configure(struct perf_event *event)
{
	struct stm32_ddr_pmu *stm32_ddr_pmu = pmu_to_stm32_ddr_pmu(event->pmu);
	unsigned long lock_flags, config_base = event->hw.config_base;
	u32 val;

	spin_lock_irqsave(&stm32_ddr_pmu->lock, lock_flags);
	writel_relaxed(CTL_STOP, stm32_ddr_pmu->membase + DDRPERFM_CTL);

	if (config_base < TIME_CNT) {
		val = readl_relaxed(stm32_ddr_pmu->membase + DDRPERFM_CFG);
		val |= (1 << config_base);
		writel_relaxed(val, stm32_ddr_pmu->membase + DDRPERFM_CFG);
	}
	spin_unlock_irqrestore(&stm32_ddr_pmu->lock, lock_flags);
}

static void stm32_ddr_pmu_event_read(struct perf_event *event)
{
	struct stm32_ddr_pmu *stm32_ddr_pmu = pmu_to_stm32_ddr_pmu(event->pmu);
	unsigned long flags, config_base = event->hw.config_base;
	struct hw_perf_event *hw = &event->hw;
	u64 prev_count, new_count, mask;
	u32 val, offset, bit;

	spin_lock_irqsave(&stm32_ddr_pmu->lock, flags);

	writel_relaxed(CTL_STOP, stm32_ddr_pmu->membase + DDRPERFM_CTL);

	if (config_base == TIME_CNT) {
		offset = DDRPERFM_TCNT;
		bit = 1 << 31;
	} else {
		offset = DDRPERFM_CNT(config_base);
		bit = 1 << config_base;
	}
	val = readl_relaxed(stm32_ddr_pmu->membase + DDRPERFM_STATUS);
	if (val & bit)
		pr_warn("stm32_ddr_pmu hardware overflow\n");
	val = readl_relaxed(stm32_ddr_pmu->membase + offset);
	writel_relaxed(bit, stm32_ddr_pmu->membase + DDRPERFM_CCR);
	writel_relaxed(CTL_START, stm32_ddr_pmu->membase + DDRPERFM_CTL);

	do {
		prev_count = local64_read(&hw->prev_count);
		new_count = prev_count + val;
	} while (local64_xchg(&hw->prev_count, new_count) != prev_count);

	mask = GENMASK_ULL(31, 0);
	local64_add(val & mask, &event->count);

	if (new_count < prev_count)
		pr_warn("STM32 DDR PMU counter saturated\n");

	spin_unlock_irqrestore(&stm32_ddr_pmu->lock, flags);
}

static void stm32_ddr_pmu_event_start(struct perf_event *event, int flags)
{
	struct stm32_ddr_pmu *stm32_ddr_pmu = pmu_to_stm32_ddr_pmu(event->pmu);
	struct hw_perf_event *hw = &event->hw;
	unsigned long lock_flags;

	if (WARN_ON_ONCE(!(hw->state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(hw->state & PERF_HES_UPTODATE));

	stm32_ddr_pmu_event_configure(event);

	/* Clear all counters to synchronize them, then start */
	spin_lock_irqsave(&stm32_ddr_pmu->lock, lock_flags);
	writel_relaxed(CCR_CLEAR_ALL, stm32_ddr_pmu->membase + DDRPERFM_CCR);
	writel_relaxed(CTL_START, stm32_ddr_pmu->membase + DDRPERFM_CTL);
	spin_unlock_irqrestore(&stm32_ddr_pmu->lock, lock_flags);

	hw->state = 0;
}

static void stm32_ddr_pmu_event_stop(struct perf_event *event, int flags)
{
	struct stm32_ddr_pmu *stm32_ddr_pmu = pmu_to_stm32_ddr_pmu(event->pmu);
	unsigned long lock_flags, config_base = event->hw.config_base;
	struct hw_perf_event *hw = &event->hw;
	u32 val, bit;

	if (WARN_ON_ONCE(hw->state & PERF_HES_STOPPED))
		return;

	spin_lock_irqsave(&stm32_ddr_pmu->lock, lock_flags);
	writel_relaxed(CTL_STOP, stm32_ddr_pmu->membase + DDRPERFM_CTL);
	if (config_base == TIME_CNT)
		bit = 1 << 31;
	else
		bit = 1 << config_base;
	writel_relaxed(bit, stm32_ddr_pmu->membase + DDRPERFM_CCR);
	if (config_base < TIME_CNT) {
		val = readl_relaxed(stm32_ddr_pmu->membase + DDRPERFM_CFG);
		val &= ~bit;
		writel_relaxed(val, stm32_ddr_pmu->membase + DDRPERFM_CFG);
	}
	spin_unlock_irqrestore(&stm32_ddr_pmu->lock, lock_flags);

	hw->state |= PERF_HES_STOPPED;

	if (flags & PERF_EF_UPDATE) {
		stm32_ddr_pmu_event_read(event);
		hw->state |= PERF_HES_UPTODATE;
	}
}

static int stm32_ddr_pmu_event_add(struct perf_event *event, int flags)
{
	struct stm32_ddr_pmu *stm32_ddr_pmu = pmu_to_stm32_ddr_pmu(event->pmu);
	unsigned long config_base = event->hw.config_base;
	struct hw_perf_event *hw = &event->hw;

	stm32_ddr_pmu->events_cnt[config_base] = 0;
	stm32_ddr_pmu->events[config_base] = event;

	clk_enable(stm32_ddr_pmu->clk);
	hrtimer_start(&stm32_ddr_pmu->hrtimer, stm32_ddr_pmu->poll_period,
		      HRTIMER_MODE_REL);

	stm32_ddr_pmu_event_configure(event);

	hw->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	if (flags & PERF_EF_START)
		stm32_ddr_pmu_event_start(event, 0);

	return 0;
}

static void stm32_ddr_pmu_event_del(struct perf_event *event, int flags)
{
	struct stm32_ddr_pmu *stm32_ddr_pmu = pmu_to_stm32_ddr_pmu(event->pmu);
	unsigned long config_base = event->hw.config_base;
	bool stop = true;
	int i;

	stm32_ddr_pmu_event_stop(event, PERF_EF_UPDATE);

	stm32_ddr_pmu->events_cnt[config_base] += local64_read(&event->count);
	stm32_ddr_pmu->events[config_base] = NULL;

	for (i = 0; i < PMU_NR_COUNTERS; i++)
		if (stm32_ddr_pmu->events[i])
			stop = false;
	if (stop)
		hrtimer_cancel(&stm32_ddr_pmu->hrtimer);

	clk_disable(stm32_ddr_pmu->clk);
}

static int stm32_ddr_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hw = &event->hw;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (is_sampling_event(event))
		return -EINVAL;

	if (event->attach_state & PERF_ATTACH_TASK)
		return -EINVAL;

	if (event->attr.exclude_user   ||
	    event->attr.exclude_kernel ||
	    event->attr.exclude_hv     ||
	    event->attr.exclude_idle   ||
	    event->attr.exclude_host   ||
	    event->attr.exclude_guest)
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	hw->config_base = event->attr.config;

	return 0;
}

static enum hrtimer_restart stm32_ddr_pmu_poll(struct hrtimer *hrtimer)
{
	struct stm32_ddr_pmu *stm32_ddr_pmu = hrtimer_to_stm32_ddr_pmu(hrtimer);
	int i;

	for (i = 0; i < PMU_NR_COUNTERS; i++)
		if (stm32_ddr_pmu->events[i])
			stm32_ddr_pmu_event_read(stm32_ddr_pmu->events[i]);

	hrtimer_forward_now(hrtimer, stm32_ddr_pmu->poll_period);

	return HRTIMER_RESTART;
}

static ssize_t stm32_ddr_pmu_sysfs_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);

	return sprintf(buf, "config=0x%lx\n", (unsigned long)eattr->var);
}

static ssize_t bandwidth_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct stm32_ddr_pmu *stm32_ddr_pmu = dev_get_drvdata(dev);
	u64 r_bw, w_bw;
	int ret;

	if (stm32_ddr_pmu->events_cnt[TIME_CNT]) {
		r_bw = stm32_ddr_pmu_compute_bw(stm32_ddr_pmu, READ_CNT);
		w_bw = stm32_ddr_pmu_compute_bw(stm32_ddr_pmu, WRITE_CNT);

		ret = snprintf(buf, PAGE_SIZE, STRING,
			       r_bw, w_bw, (r_bw + w_bw));
	} else {
		ret = snprintf(buf, PAGE_SIZE, "No data available\n");
	}

	return ret;
}

#define STM32_DDR_PMU_ATTR(_name, _func, _config)			\
	(&((struct dev_ext_attribute[]) {				\
		{ __ATTR(_name, 0444, _func, NULL), (void *)_config }   \
	})[0].attr.attr)

#define STM32_DDR_PMU_EVENT_ATTR(_name, _config)		\
	STM32_DDR_PMU_ATTR(_name, stm32_ddr_pmu_sysfs_show,	\
			   (unsigned long)_config)

static struct attribute *stm32_ddr_pmu_event_attrs[] = {
	STM32_DDR_PMU_EVENT_ATTR(read_cnt, READ_CNT),
	STM32_DDR_PMU_EVENT_ATTR(write_cnt, WRITE_CNT),
	STM32_DDR_PMU_EVENT_ATTR(activate_cnt, ACTIVATE_CNT),
	STM32_DDR_PMU_EVENT_ATTR(idle_cnt, IDLE_CNT),
	STM32_DDR_PMU_EVENT_ATTR(time_cnt, TIME_CNT),
	NULL
};

static DEVICE_ATTR_RO(bandwidth);
static struct attribute *stm32_ddr_pmu_bandwidth_attrs[] = {
	&dev_attr_bandwidth.attr,
	NULL,
};

static struct attribute_group stm32_ddr_pmu_event_attrs_group = {
	.name = "events",
	.attrs = stm32_ddr_pmu_event_attrs,
};

static struct attribute_group stm32_ddr_pmu_bandwidth_attrs_group = {
	.attrs = stm32_ddr_pmu_bandwidth_attrs,
};

static const struct attribute_group *stm32_ddr_pmu_attr_groups[] = {
	&stm32_ddr_pmu_event_attrs_group,
	&stm32_ddr_pmu_bandwidth_attrs_group,
	NULL,
};

static int stm32_ddr_pmu_device_probe(struct platform_device *pdev)
{
	struct stm32_ddr_pmu *stm32_ddr_pmu;
	struct resource *res;
	int i, ret;
	u32 val;

	stm32_ddr_pmu = devm_kzalloc(&pdev->dev, sizeof(struct stm32_ddr_pmu),
				     GFP_KERNEL);
	if (!stm32_ddr_pmu)
		return -ENOMEM;
	platform_set_drvdata(pdev, stm32_ddr_pmu);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	stm32_ddr_pmu->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(stm32_ddr_pmu->membase)) {
		pr_warn("Unable to get STM32 DDR PMU membase\n");
		return PTR_ERR(stm32_ddr_pmu->membase);
	}

	stm32_ddr_pmu->clk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(stm32_ddr_pmu->clk)) {
		pr_warn("Unable to get STM32 DDR PMU clock\n");
		return PTR_ERR(stm32_ddr_pmu->clk);
	}

	ret = clk_prepare_enable(stm32_ddr_pmu->clk);
	if (ret) {
		pr_warn("Unable to prepare STM32 DDR PMU clock\n");
		return ret;
	}

	stm32_ddr_pmu->clk_ddr = devm_clk_get(&pdev->dev, "ddr");
	if (IS_ERR(stm32_ddr_pmu->clk_ddr)) {
		pr_warn("Unable to get STM32 DDR clock\n");
		return PTR_ERR(stm32_ddr_pmu->clk_ddr);
	}
	stm32_ddr_pmu->clk_ddr_rate = clk_get_rate(stm32_ddr_pmu->clk_ddr);
	stm32_ddr_pmu->clk_ddr_rate /= 1000000;

	stm32_ddr_pmu->poll_period = ms_to_ktime(POLL_MS);
	hrtimer_init(&stm32_ddr_pmu->hrtimer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	stm32_ddr_pmu->hrtimer.function = stm32_ddr_pmu_poll;
	spin_lock_init(&stm32_ddr_pmu->lock);

	for (i = 0; i < PMU_NR_COUNTERS; i++) {
		stm32_ddr_pmu->events[i] = NULL;
		stm32_ddr_pmu->events_cnt[i] = 0;
	}

	val = readl_relaxed(stm32_ddr_pmu->membase + DDRPERFM_SID);
	if (val != SID_MAGIC_ID)
		return -EINVAL;

	stm32_ddr_pmu->pmu = (struct pmu) {
		.task_ctx_nr = perf_invalid_context,
		.start = stm32_ddr_pmu_event_start,
		.stop = stm32_ddr_pmu_event_stop,
		.add = stm32_ddr_pmu_event_add,
		.del = stm32_ddr_pmu_event_del,
		.event_init = stm32_ddr_pmu_event_init,
		.attr_groups = stm32_ddr_pmu_attr_groups,
	};
	ret = perf_pmu_register(&stm32_ddr_pmu->pmu, "ddrperfm", -1);
	if (ret) {
		pr_warn("Unable to register STM32 DDR PMU\n");
		return ret;
	}

	pr_info("stm32-ddr-pmu: probed (ID=0x%08x VER=0x%08x), DDR@%luMHz\n",
		readl_relaxed(stm32_ddr_pmu->membase + DDRPERFM_ID),
		readl_relaxed(stm32_ddr_pmu->membase + DDRPERFM_VER),
		stm32_ddr_pmu->clk_ddr_rate);

	clk_disable(stm32_ddr_pmu->clk);

	return 0;
}

static int stm32_ddr_pmu_device_remove(struct platform_device *pdev)
{
	struct stm32_ddr_pmu *stm32_ddr_pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&stm32_ddr_pmu->pmu);

	return 0;
}

static const struct of_device_id stm32_ddr_pmu_of_match[] = {
	{ .compatible = "st,stm32-ddr-pmu" },
	{ },
};

static struct platform_driver stm32_ddr_pmu_driver = {
	.driver = {
		.name	= "stm32-ddr-pmu",
		.of_match_table = of_match_ptr(stm32_ddr_pmu_of_match),
	},
	.probe = stm32_ddr_pmu_device_probe,
	.remove = stm32_ddr_pmu_device_remove,
};

module_platform_driver(stm32_ddr_pmu_driver);

MODULE_DESCRIPTION("Perf driver for STM32 DDR performance monitor");
MODULE_AUTHOR("Gerald Baeza <gerald.baeza@st.com>");
MODULE_LICENSE("GPL v2");
