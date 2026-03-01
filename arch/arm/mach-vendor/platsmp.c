/*
 * Copyright (c) 2016-2017 HiSilicon Technologies Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
*/

#include <linux/io.h>
#include <linux/smp.h>
#include <asm/smp_scu.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/irqchip/arm-gic.h>
#include <asm/smp_plat.h>
#include <linux/cpu.h>
#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>

#include "mach_common.h"

void __init hi35xx_smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned long base = 0;
	void __iomem *scu_base = NULL;

	if (scu_a9_has_base()) {
		base = scu_a9_get_base();
		scu_base = ioremap(base, PAGE_SIZE);
		if (!scu_base) {
			pr_err("ioremap(scu_base) failed\n");
			return;
		}

		scu_enable(scu_base);
		iounmap(scu_base);
	}
}

int hi35xx_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	phys_addr_t jumpaddr;
	jumpaddr = virt_to_phys(secondary_startup);

	hi35xx_set_entrypoint(jumpaddr);
	hi35xx_set_cpu(cpu, true);
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static void hi35xx_smp_cpu_die(unsigned int l_cpu)
{
	v7_exit_coherency_flush(louis);

	for (;;)
		cpu_do_idle();
}

#define MISC_CPU_CTRL0 0x11024100

#define A7_STANDBYWFI 4

static int hi35xx_smp_cpu_kill(unsigned int cpu)
{
	void __iomem *misc_cpu_ctrl0;
	unsigned long mpidr, p_cpu;
	misc_cpu_ctrl0 = ioremap(MISC_CPU_CTRL0, PAGE_SIZE);

	if (!misc_cpu_ctrl0) {
		printk("kill cpu error %s:%d: map cpu ctrl faild\n", __func__, __LINE__);
		return 0;
	}

	mpidr = cpu_logical_map(cpu);
	p_cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);

	// wait wfi mode
	while (true) {
		unsigned int test_bit = 1 << (A7_STANDBYWFI + p_cpu);
		if (readl(misc_cpu_ctrl0) & test_bit) {
			break;
		}
	}

	// CPU power down
	hi35xx_set_cpu(p_cpu, false);

	iounmap(misc_cpu_ctrl0);
	return 1;
}

static bool hi35xx_smp_cpu_can_disable(unsigned int cpu)
{
	return true;
}
#endif

static const struct smp_operations hi35xx_smp_ops __initconst = {
	.smp_prepare_cpus       = hi35xx_smp_prepare_cpus,
	.smp_boot_secondary     = hi35xx_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= hi35xx_smp_cpu_die,
	.cpu_kill		= hi35xx_smp_cpu_kill,
	.cpu_can_disable	= hi35xx_smp_cpu_can_disable,
#endif
};

CPU_METHOD_OF_DECLARE(hi35xx_smp, "hisilicon,hi35xx", &hi35xx_smp_ops);

void __init hi35xx_init(void)
{
#ifdef CONFIG_PM
	hi35xx_pm_init();
#endif
}


static const char *const hi35xx_compat[] __initconst = {
	"hisilicon,hi3516cv610",
	"hisilicon,hi3516cv608",
	NULL
};

DT_MACHINE_START(HI35XX, "HI35XX")
	.init_machine	= hi35xx_init,
	.dt_compat	= hi35xx_compat,
MACHINE_END
