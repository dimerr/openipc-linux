/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

#include <asm/proc-fns.h>

static void __iomem *base;
static void __iomem *wdg_base;
static void __iomem *gic_base;
static u32 reboot_offset;

#define REG_BASE_WATCH_DOG 0x11030000
#define WATCH_DOG_LOAD_VAL 0x30
#define WATCH_DOG_CONTROL_OFFSET 0x8
#define WATCH_DOG_ENABLE 0x3
#define WATCH_DOG_UNLOCK_VAL    0x1ACCE551
#define WATCH_DOG_LOCK 0xC00

#define GICD_DISABLE 0x0
#define GIC_DIST_CTRL 0x000
#define GICD_BASE 0x12401000

static int bsp_restart_handler(struct notifier_block *this, unsigned long mode,
			       void *cmd)
{
	// ignore wdg irq
	writel_relaxed(GICD_DISABLE, gic_base + GIC_DIST_CTRL);

	writel_relaxed(WATCH_DOG_UNLOCK_VAL, wdg_base + WATCH_DOG_LOCK);

	writel_relaxed(WATCH_DOG_LOAD_VAL, wdg_base);
	writel_relaxed(WATCH_DOG_ENABLE, wdg_base + WATCH_DOG_CONTROL_OFFSET);

	while (1)
		cpu_do_idle();

	return NOTIFY_DONE;
}

static struct notifier_block bsp_restart_nb = {
	.notifier_call = bsp_restart_handler,
	.priority = 128,
};

#define HI3516CV610_CRG_BASE 0x11010000
#define SIZE_4K 0x4000

static int bsp_reboot_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int err;

	base = of_iomap(np, 0);
	if (!base) {
		WARN(1, "failed to map base address");
		return -ENODEV;
	}

	wdg_base = ioremap(REG_BASE_WATCH_DOG, SIZE_4K);
	if (!wdg_base) {
		iounmap(base);
		WARN(1, "failed to map crg base address");
		return -ENODEV;
	}

	gic_base = ioremap(GICD_BASE, SIZE_4K);
	if (!gic_base) {
		iounmap(base);
		iounmap(wdg_base);
		WARN(1, "failed to map crg base address");
		return -ENODEV;
	}

	if (of_property_read_u32(np, "reboot-offset", &reboot_offset) < 0) {
		pr_err("failed to find reboot-offset property\n");
		iounmap(base);
		iounmap(wdg_base);
		iounmap(gic_base);
		return -EINVAL;
	}

	err = register_restart_handler(&bsp_restart_nb);
	if (err) {
		dev_err(&pdev->dev,
			"cannot register restart handler (err=%d)\n", err);
		iounmap(base);
		iounmap(wdg_base);
		iounmap(gic_base);
	}

	return err;
}

static const struct of_device_id bsp_reboot_of_match[] = {
	{ .compatible = "vendor,sysctrl" },
	{}
};

static struct platform_driver bsp_reboot_driver = {
	.probe = bsp_reboot_probe,
	.driver = {
		.name = "bsp-reboot",
		.of_match_table = bsp_reboot_of_match,
	},
};
module_platform_driver(bsp_reboot_driver);
