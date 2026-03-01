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

#include "mach_common.h"

#include <asm/smp_scu.h>
#include <linux/of_address.h>

#ifdef CONFIG_SMP

#define CPU1_RST_REQ BIT(8)
#define CPU0_RST_REQ BIT(4)
#define REG_CPU_RST 0x204C

#define REG_CPU_FRQ_CRG 0x2040
#define DBG1_SRST_REQ BIT(10)
#define DBG0_SRST_REQ BIT(4)

void hi35xx_set_cpu(unsigned int cpu, bool enable)
{
	struct device_node *np = NULL;
	unsigned int regval;
	void __iomem *crg_base;

	np = of_find_compatible_node(NULL, NULL, "vendor,hi3516cv610_clock");
	if (!np) {
		pr_err("failed to find hisilicon clock node\n");
		return;
	}

	crg_base = of_iomap(np, 0);
	if (!crg_base) {
		pr_err("failed to map address\n");
		return;
	}

	if (enable) {
		/* clear the slave cpu reset */
		regval = readl(crg_base + REG_CPU_FRQ_CRG);

		if (cpu == 0)
			regval &= ~DBG0_SRST_REQ;
		else if (cpu == 1)
			regval &= ~DBG1_SRST_REQ;
		else
			return;

		writel(regval, (crg_base + REG_CPU_FRQ_CRG));

		regval = readl(crg_base + REG_CPU_RST);

		if (cpu == 0)
			regval &= ~CPU0_RST_REQ;
		else if (cpu == 1)
			regval &= ~CPU1_RST_REQ;
		else
			return;

		writel(regval, (crg_base + REG_CPU_RST));
	} else {
		regval = readl(crg_base + REG_CPU_FRQ_CRG);

		if (cpu == 0)
			regval |= DBG0_SRST_REQ;
		else if (cpu == 1)
			regval |= DBG1_SRST_REQ;
		else
			return;

		writel(regval, (crg_base + REG_CPU_FRQ_CRG));

		regval = readl(crg_base + REG_CPU_RST);

		if (cpu == 0)
			regval |= CPU0_RST_REQ;
		else if (cpu == 1)
			regval |= CPU1_RST_REQ;
		else
			return;

		writel(regval, (crg_base + REG_CPU_RST));
	}

	iounmap(crg_base);
}

#define TEE_GEN_REG0     0x500
#define REG_START_WARM_ENTRYPOINT TEE_GEN_REG0


void hi35xx_set_entrypoint(phys_addr_t jumpaddr)
{
	struct device_node *np;
	void *sysctrl_reg_base;

	np = of_find_compatible_node(NULL, NULL, "vendor,sysctrl");
	sysctrl_reg_base = of_iomap(np, 0);

	writel(jumpaddr, sysctrl_reg_base + REG_START_WARM_ENTRYPOINT);

	iounmap(sysctrl_reg_base);
}

#endif
