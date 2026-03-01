// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2011 Calxeda, Inc.
 */

#include "asm/io.h"
#include "linux/io.h"
#include "linux/securec.h"
#include <linux/dev_printk.h>
#include "linux/gfp.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include <linux/cpu_pm.h>
#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <asm/cp15.h>

#include <asm/suspend.h>
#include <linux/types.h>

struct hi35xxvxx_pm_device {
	const struct firmware *fw;
	void __iomem *sram_base;
	void __iomem *ahb_misc_base;
	struct resource *ahb_misc_res;
};

struct hi35xxvxx_pm_device *g_pm_device;

#define xstr(s) str(s)
#define str(s) #s

#define SRAM_ENTRY_ADDR 0x4021500
#define SRAM_ENTRY_OFFSET 0x1500

__attribute__((__section__(".idmap.text")))
static int suspend_in_sram(unsigned long arg)
{
	int (*sram_entry)(unsigned long);
	unsigned int clear_bits = CR_I | CR_C | CR_M | CR_W | CR_V;

	sram_entry = (typeof(sram_entry)) SRAM_ENTRY_ADDR;

	flush_cache_all();

	asm(
	"mrc	p15, 0, r0, c1, c0, 0\n\t"
	"bics	r0, %1\n\t"
	"mcr	p15, 0, r0, c1, c0, 0\n\t"
	"isb\n\t"
	"mov r0, %2\n\t"
	"blx %0\n\t"
	:: "r" (sram_entry), "r" (clear_bits), "r" (__pa_symbol(cpu_resume)));

	return 1;
}

static int hi35xx_pm_enter(suspend_state_t state)
{
	cpu_pm_enter();

	cpu_suspend(0, suspend_in_sram);

	cpu_pm_exit();

	return 0;
}

#define STR_DEFAULT_SLEEP_TIME	1000
#define STR_DEFAULT_WAKEUP_GPIO	0

#define SEC_BOOTRAM_CTRL 0x620

static int hi35xx_pm_prepare(void)
{
	writel(0x0, g_pm_device->ahb_misc_base + SEC_BOOTRAM_CTRL);

	if (memcpy_s(g_pm_device->sram_base + SRAM_ENTRY_OFFSET, g_pm_device->fw->size,
	      g_pm_device->fw->data, g_pm_device->fw->size) != EOK)
		return -1;

	return 0;
}

void hi35xx_pm_resume(void)
{
	writel(0x1, g_pm_device->ahb_misc_base + SEC_BOOTRAM_CTRL);
}

static const struct platform_suspend_ops hi35xx_pm_ops = {
	.wake = hi35xx_pm_resume,
	.enter = hi35xx_pm_enter,
	.prepare = hi35xx_pm_prepare,
	.valid = suspend_valid_only_mem,
};

void __init hi35xx_pm_init(void)
{
	suspend_set_ops(&hi35xx_pm_ops);
}

static int hi35xxvxx_pm_probe(struct platform_device *pdev)
{
	char fw_name[36] = "str_firmware.bin";
	int ret;
	struct hi35xxvxx_pm_device *pm_device;

	pm_device = kmalloc(sizeof(*pm_device), GFP_KERNEL);
	if (!pm_device) {
		dev_dbg(&pdev->dev, "hi35xxvxx_pm_device alloc error!\n");
		return -ENOMEM;
	}

	pm_device->ahb_misc_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!pm_device->ahb_misc_res) {
		kfree(pm_device);
		return -ENOENT;
	}

	pm_device->ahb_misc_base = devm_ioremap(&pdev->dev, pm_device->ahb_misc_res->start, \
					 resource_size(pm_device->ahb_misc_res));
	if (!pm_device->ahb_misc_base) {
		release_resource(pm_device->ahb_misc_res);
		kfree(pm_device);
		return -ENOMEM;
	}

	pm_device->sram_base = devm_platform_ioremap_resource(pdev, 0);
	if (!pm_device->sram_base) {
		devm_iounmap(&pdev->dev, pm_device->ahb_misc_base);
		release_resource(pm_device->ahb_misc_res);
		kfree(pm_device);
		dev_dbg(&pdev->dev, "map memory resource\n");
		return -ENXIO;
	}

	ret = request_firmware_direct(&(pm_device->fw), (const char *)fw_name, &pdev->dev);
	if (ret < 0) {
		devm_iounmap(&pdev->dev, pm_device->sram_base);
		devm_iounmap(&pdev->dev, pm_device->ahb_misc_base);
		release_resource(pm_device->ahb_misc_res);
		kfree(pm_device);
		pr_err("hi35xx get str firmware error");
		return -ENOENT;
	}

	platform_set_drvdata(pdev, pm_device);
	g_pm_device = pm_device;
	return 0;
}

static int hi35xxvxx_pm_remove(struct platform_device *pdev)
{
	struct hi35xxvxx_pm_device *pm_device = platform_get_drvdata(pdev);
	release_firmware(pm_device->fw);
	devm_iounmap(&pdev->dev, pm_device->sram_base);
	devm_iounmap(&pdev->dev, pm_device->ahb_misc_base);
	release_resource(pm_device->ahb_misc_res);
	kfree(pm_device);
	return 0;
}

static const struct of_device_id hi35xx_pm_ids[] = {
	{
		.compatible = "pm,sram",
	},
	{}
};
MODULE_DEVICE_TABLE(of, hi35xx_pm_ids);

static struct platform_driver hi35xxvxx_pm_driver = {
	.driver = {
		.name = "hi35xxvxx_pm_driver",
		.of_match_table = of_match_ptr(hi35xx_pm_ids),
	},
	.probe = hi35xxvxx_pm_probe,
	.remove = hi35xxvxx_pm_remove,
};

module_platform_driver(hi35xxvxx_pm_driver);
