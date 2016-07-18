/*
 *  Copyright (C) 2010, Paul Cercueil <paul@crapouillou.net>
 *  JZ4740 Watchdog driver
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/reboot.h>

#define JZ_REG_WDT_TIMER_DATA     0x0
#define JZ_REG_WDT_COUNTER_ENABLE 0x4
#define JZ_REG_WDT_TIMER_COUNTER  0x8

#define DEFAULT_HEARTBEAT 5
#define MAX_HEARTBEAT     2048

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned int heartbeat = DEFAULT_HEARTBEAT;
module_param(heartbeat, uint, 0);
MODULE_PARM_DESC(heartbeat,
		"Watchdog heartbeat period in seconds from 1 to "
		__MODULE_STRING(MAX_HEARTBEAT) ", default "
		__MODULE_STRING(DEFAULT_HEARTBEAT));

struct jz4740_wdt_drvdata {
	struct watchdog_device wdt;
	void __iomem *base;
	struct clk *clk;
	struct notifier_block restart_nb;
};

static int jz4740_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct jz4740_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	writew(0x0, drvdata->base + JZ_REG_WDT_TIMER_COUNTER);
	return 0;
}

static int jz4740_wdt_set_timeout(struct watchdog_device *wdt_dev,
				    unsigned int new_timeout)
{
	struct jz4740_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	unsigned long clk_rate, timeout_value;

	clk_rate = clk_get_rate(drvdata->clk);
	timeout_value = clk_rate * new_timeout;

	writeb(0x0, drvdata->base + JZ_REG_WDT_COUNTER_ENABLE);

	if (timeout_value > 0xffff) {
		clk_rate = clk_round_rate(drvdata->clk, 0xffff / new_timeout);
		clk_set_rate(drvdata->clk, clk_rate);
		timeout_value = clk_rate * new_timeout;
		BUG_ON(timeout_value > 0xffff);
	}

	writew((u16)timeout_value, drvdata->base + JZ_REG_WDT_TIMER_DATA);
	writew(0x0, drvdata->base + JZ_REG_WDT_TIMER_COUNTER);
	writeb(0x1, drvdata->base + JZ_REG_WDT_COUNTER_ENABLE);

	wdt_dev->timeout = new_timeout;
	return 0;
}

static int jz4740_wdt_start(struct watchdog_device *wdt_dev)
{
	struct jz4740_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	clk_prepare_enable(drvdata->clk);
	jz4740_wdt_set_timeout(wdt_dev, wdt_dev->timeout);

	return 0;
}

static int jz4740_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct jz4740_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	writeb(0x0, drvdata->base + JZ_REG_WDT_COUNTER_ENABLE);
	clk_disable_unprepare(drvdata->clk);

	return 0;
}

static int jz4740_wdt_restart_handler(struct notifier_block *nb,
		unsigned long mode, void *cmd)
{
	struct jz4740_wdt_drvdata *drvdata = container_of(nb,
			struct jz4740_wdt_drvdata, restart_nb);

	drvdata->wdt.timeout = 0;
	jz4740_wdt_start(&drvdata->wdt);
	return NOTIFY_DONE;
}

static const struct watchdog_info jz4740_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "jz4740 Watchdog",
};

static const struct watchdog_ops jz4740_wdt_ops = {
	.owner = THIS_MODULE,
	.start = jz4740_wdt_start,
	.stop = jz4740_wdt_stop,
	.ping = jz4740_wdt_ping,
	.set_timeout = jz4740_wdt_set_timeout,
};

#ifdef CONFIG_OF
static const struct of_device_id jz4740_wdt_of_matches[] = {
	{ .compatible = "ingenic,jz4740-watchdog", },
	{ .compatible = "ingenic,jz4770-watchdog", },
	{ .compatible = "ingenic,jz4780-watchdog", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jz4740_wdt_of_matches)
#endif

static int jz4740_wdt_probe(struct platform_device *pdev)
{
	struct jz4740_wdt_drvdata *drvdata;
	struct watchdog_device *jz4740_wdt;
	struct resource	*res;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct jz4740_wdt_drvdata),
			       GFP_KERNEL);
	if (!drvdata) {
		dev_err(&pdev->dev, "Unable to alloacate watchdog device\n");
		return -ENOMEM;
	}

	if (heartbeat < 1 || heartbeat > MAX_HEARTBEAT)
		heartbeat = DEFAULT_HEARTBEAT;

	jz4740_wdt = &drvdata->wdt;
	jz4740_wdt->info = &jz4740_wdt_info;
	jz4740_wdt->ops = &jz4740_wdt_ops;
	jz4740_wdt->timeout = heartbeat;
	jz4740_wdt->min_timeout = 1;
	jz4740_wdt->max_timeout = MAX_HEARTBEAT;
	jz4740_wdt->parent = &pdev->dev;
	watchdog_set_nowayout(jz4740_wdt, nowayout);
	watchdog_set_drvdata(jz4740_wdt, drvdata);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drvdata->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	drvdata->clk = devm_clk_get(&pdev->dev, "wdt");
	if (IS_ERR(drvdata->clk)) {
		dev_err(&pdev->dev, "cannot find WDT clock\n");
		return PTR_ERR(drvdata->clk);
	}

	drvdata->restart_nb.notifier_call = jz4740_wdt_restart_handler;
	ret = register_restart_handler(&drvdata->restart_nb);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot register restart handler\n");
		return ret;
	}

	ret = watchdog_register_device(&drvdata->wdt);
	if (ret < 0) {
		unregister_restart_handler(&drvdata->restart_nb);
		return ret;
	}

	platform_set_drvdata(pdev, drvdata);

	return 0;
}

static int jz4740_wdt_remove(struct platform_device *pdev)
{
	struct jz4740_wdt_drvdata *drvdata = platform_get_drvdata(pdev);

	jz4740_wdt_stop(&drvdata->wdt);
	watchdog_unregister_device(&drvdata->wdt);
	unregister_restart_handler(&drvdata->restart_nb);

	return 0;
}

static struct platform_driver jz4740_wdt_driver = {
	.probe = jz4740_wdt_probe,
	.remove = jz4740_wdt_remove,
	.driver = {
		.name = "jz4740-wdt",
		.of_match_table = of_match_ptr(jz4740_wdt_of_matches),
	},
};

module_platform_driver(jz4740_wdt_driver);

MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_DESCRIPTION("jz4740 Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jz4740-wdt");
