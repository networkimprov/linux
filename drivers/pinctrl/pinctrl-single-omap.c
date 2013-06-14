/*
 * pinctrl-single-omap - omap specific wake-up irq handler
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/platform_data/pinctrl-single-omap.h>
#include <linux/pm_runtime.h>

#include "pinctrl-single.h"

#define OMAP_WAKEUP_EN		(1 << 14)
#define OMAP_WAKEUP_EVENT	(1 << 15)
#define OMAP_WAKEUP_EVENT_MASK	(OMAP_WAKEUP_EN | OMAP_WAKEUP_EVENT)

struct pcs_omap {
	unsigned int irq;
	struct device *dev;
	struct list_head wakeirqs;
	struct pcs_soc soc;
	void (*reconfigure_io_chain)(void);
	struct mutex mutex;
};

static int pcs_omap_reg_init(const struct pcs_soc *soc, struct pcs_reg *r)
{
	struct pcs_omap *pcso = container_of(soc, struct pcs_omap, soc);
	struct list_head *pos;
	struct pcs_reg *pcsoi;
	int res = 0;

	if (!(r->val & OMAP_WAKEUP_EN))
		return 0;

	if (r->irq <= 0)
		return 0;

	mutex_lock(&pcso->mutex);
	list_for_each(pos, &pcso->wakeirqs) {
		pcsoi = list_entry(pos, struct pcs_reg, node);
		if (r->reg == pcsoi->reg) {
			pcsoi->read = r->read;
			pcsoi->write = r->write;
			pcsoi->reg = r->reg;
			pcsoi->val = r->val;
			pcsoi->irq = r->irq;
			pcsoi->gpio = r->gpio;
			res++;
			goto out;
		}
	}
	pcsoi = devm_kzalloc(pcso->dev, sizeof(*r), GFP_KERNEL);
	if (!pcsoi) {
		mutex_unlock(&pcso->mutex);
		res = -ENOMEM;
		goto out;
	}
	*pcsoi = *r;
	list_add_tail(&pcsoi->node, &pcso->wakeirqs);

out:
	mutex_unlock(&pcso->mutex);

	if (res && pcso->reconfigure_io_chain)
		pcso->reconfigure_io_chain();

	return res > 0 ? 0 : res;
}

static int pcs_update_list(const struct pcs_soc *soc, struct pcs_reg *r)
{
	struct pcs_omap *pcso = container_of(soc, struct pcs_omap, soc);
	struct list_head *pos;
	int changed = 0;

	if (!r->irq)
		return 0;

	mutex_lock(&pcso->mutex);
	list_for_each(pos, &pcso->wakeirqs) {
		struct pcs_reg *pcsoi;

		pcsoi = list_entry(pos, struct pcs_reg, node);
		if ((r->reg == pcsoi->reg) &&
		    (r->val != pcsoi->val)) {
			pcsoi->val = r->val;
			changed++;
		}
	}
	mutex_unlock(&pcso->mutex);

	if (pcso->reconfigure_io_chain && changed)
		pcso->reconfigure_io_chain();

	return 0;
}

static int pcs_omap_enable(const struct pcs_soc *soc, struct pcs_reg *r)
{
	return pcs_update_list(soc, r);
}

static void pcs_omap_disable(const struct pcs_soc *soc, struct pcs_reg *r)
{
	pcs_update_list(soc, r);
}

static irqreturn_t pcs_omap_handle_irq(int irq, void *data)
{
	struct pcs_omap *pcso = data;
	struct list_head *pos;
	unsigned int wakeirq;

	list_for_each(pos, &pcso->wakeirqs) {
		struct pcs_reg *pcsoi;
		u16 val;

		pcsoi = list_entry(pos, struct pcs_reg, node);
		wakeirq = pcsoi->irq;
		val = pcsoi->read(pcsoi->reg);
		if ((val & OMAP_WAKEUP_EVENT_MASK) == OMAP_WAKEUP_EVENT_MASK)
			generic_handle_irq(wakeirq);
	}

	if (pcso->reconfigure_io_chain)
		pcso->reconfigure_io_chain();

	return IRQ_HANDLED;
}

/*
 * Note that omap2430 has 8-bit padconf registers and uses
 * the plain pinctrl-single binding.
 */
static const struct of_device_id pcs_omap_of_match[] = {
	{ .compatible = "ti,omap3-padconf", },
	{ .compatible = "ti,omap4-padconf", },
	{ .compatible = "ti,omap5-padconf", },
	{}
};
MODULE_DEVICE_TABLE(of, pcs_omap_of_match);

/* SoC glue */
static bool soc_found;
static unsigned int soc_irq;
static void (*soc_reconfigure_io_chain)(void);

/* Fill in the SoC glue */
static int pcs_omap_soc_probe(struct platform_device *pdev)
{
	struct pcs_omap_pdata *pdata = pdev->dev.platform_data;

	if (pdata) {
		soc_irq = pdata->irq;
		soc_reconfigure_io_chain = pdata->reconfigure_io_chain;
		soc_found = true;
	}

	return 0;
}

static int pcs_omap_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct pcs_omap *pcso;
	struct pcs_soc *soc;
	int ret;

	match = of_match_device(pcs_omap_of_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "no match found\n");
		return -ENODEV;
	}

	if (!soc_found) {
		dev_dbg(&pdev->dev,
			"%s deferring as SoC glue not yet registered\n",
			 __func__);
		return -EPROBE_DEFER;
	}

	pcso = devm_kzalloc(&pdev->dev, sizeof(*pcso), GFP_KERNEL);
	if (!pcso)
		return -ENOMEM;

	pcso->dev = &pdev->dev;
	mutex_init(&pcso->mutex);
	INIT_LIST_HEAD(&pcso->wakeirqs);
	pcso->irq = soc_irq;
	pcso->reconfigure_io_chain = soc_reconfigure_io_chain;
	soc = &pcso->soc;
	soc->reg_init = pcs_omap_reg_init;
	soc->enable = pcs_omap_enable;
	soc->disable = pcs_omap_disable;
	soc->flags = PCS_HAS_FUNCTION_GPIO | PCS_HAS_FUNCTION_IRQ;

	ret = pinctrl_single_probe(pdev, soc);
	if (ret) {
		dev_err(&pdev->dev, "could not probe pictrl_single driver: %i\n",
			ret);
		return ret;
	}

	ret = request_irq(soc_irq, pcs_omap_handle_irq,
			  IRQF_SHARED | IRQF_NO_SUSPEND,
			  "pinctrl-single-omap", pcso);
	if (ret) {
		dev_err(&pdev->dev, "could not get irq%i: %i\n",
			soc_irq, ret);
		return ret;
	}

	platform_set_drvdata(pdev, pcso);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int pcs_omap_remove(struct platform_device *pdev)
{
	struct pcs_omap *pcso = platform_get_drvdata(pdev);

	pinctrl_single_remove(pdev);
	free_irq(pcso->irq, NULL);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver pcs_omap_driver = {
	.probe		= pcs_omap_probe,
	.remove		= pcs_omap_remove,
	.driver		= {
		.name	= "pinctrl-single-omap",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pcs_omap_of_match),
	},
};

/* Dummy driver for registering SoC glue */
static struct platform_driver pcs_omap_soc_driver = {
	.probe = pcs_omap_soc_probe,
	.driver	= {
		.name = "pinctrl-single-omap-soc",
		.owner = THIS_MODULE,
	},
};

static int __init pcs_omap_init(void)
{
	platform_driver_register(&pcs_omap_soc_driver);
	platform_driver_register(&pcs_omap_driver);

	return 0;
}
module_init(pcs_omap_init);

static void __exit pcs_omap_exit(void)
{
	platform_driver_unregister(&pcs_omap_driver);
	platform_driver_unregister(&pcs_omap_soc_driver);
}
module_exit(pcs_omap_exit);

MODULE_ALIAS("platform: pinctrl-single-omap");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("pinctrl-single-omap driver");
MODULE_LICENSE("GPL v2");
