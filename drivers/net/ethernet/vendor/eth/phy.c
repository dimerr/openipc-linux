/*
 * Copyright (c) CompanyNameMagicTag 2022. All rights reserved.
 * Description: Phy settings.
 * Author: AuthorNameMagicTag
 * Create: 2022-4-20
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include "huanglong_phy.h"
#include "hleth.h"
#include "mdio.h"
#include "phy.h"

static const u32 phy_s28v200_fix_param[] = {
#include "festa.h"
};

#define CONFIG_FEPHY_TRIM
#ifdef CONFIG_FEPHY_TRIM
#define REG_LD_AM		0x3050
#define LD_AM_MASK		GENMASK(4, 0)
#define REG_LDO_AM		0x3051
#define LDO_AM_MASK		GENMASK(2, 0)
#define REG_R_TUNING		0x3052
#define R_TUNING_MASK		GENMASK(5, 0)
#define REG_WR_DONE		0x3053
#define REG_DEF_ATE		0x3057
#define DEF_LD_AM		0x0f
#define DEF_LDO_AM		0x7
#define DEF_R_TUNING		0x15

static inline int hleth_phy_expanded_read(struct mii_bus *bus, int phyaddr,
						u32 reg_addr)
{
	int ret;

	hleth_mdiobus_write(bus, phyaddr, MII_EXPMA, reg_addr);
	ret = hleth_mdiobus_read(bus, phyaddr, MII_EXPMD);

	return ret;
}

static inline int hleth_phy_expanded_write(struct mii_bus *bus, int phyaddr,
						u32 reg_addr, u16 val)
{
	int ret;

	hleth_mdiobus_write(bus, phyaddr, MII_EXPMA, reg_addr);
	ret = hleth_mdiobus_write(bus, phyaddr, MII_EXPMD, val);

	return ret;
}

static void hleth_use_default_trim(struct mii_bus *bus, int phyaddr)
{
	unsigned short v;
	int timeout = 3;

	pr_info("FEPHY: No OTP data, use default ATE parameters to auto-trim!\n");

	do {
		msleep(250); /* 250:delay */
		v = (unsigned short)hleth_phy_expanded_read(bus, phyaddr, REG_DEF_ATE);
		v &= BIT(0);
	} while ((v == 0) && (--timeout != 0));

	if (timeout == 0)
		pr_warn("FEPHY: fail to wait auto-trim finish!\n");
}

static void hleth_config_festa_phy_trim(struct mii_bus *bus, int phyaddr,
					u32 trim_params)
{
	unsigned short ld_amptlitude;
	unsigned short ldo_amptlitude;
	unsigned short r_tuning_val;
	unsigned short v;
	int timeout = 3000; /* 3000:timeout */

	if (!trim_params) {
		hleth_use_default_trim(bus, phyaddr);
		return;
	}

	ld_amptlitude = trim_params & LD_AM_MASK;
	ldo_amptlitude = (trim_params >> 8) & LDO_AM_MASK; /* 8:right shift val */
	r_tuning_val = (trim_params >> 16) & R_TUNING_MASK; /* 16:right shift val */

	v = hleth_phy_expanded_read(bus, phyaddr, REG_LD_AM);
	v = (v & ~LD_AM_MASK) | (ld_amptlitude & LD_AM_MASK);
	hleth_phy_expanded_write(bus, phyaddr, REG_LD_AM, v);

	v = hleth_phy_expanded_read(bus, phyaddr, REG_LDO_AM);
	v = (v & ~LDO_AM_MASK) | (ldo_amptlitude & LDO_AM_MASK);
	hleth_phy_expanded_write(bus, phyaddr, REG_LDO_AM, v);

	v = hleth_phy_expanded_read(bus, phyaddr, REG_R_TUNING);
	v = (v & ~R_TUNING_MASK) | (r_tuning_val & R_TUNING_MASK);
	hleth_phy_expanded_write(bus, phyaddr, REG_R_TUNING, v);

	v = hleth_phy_expanded_read(bus, phyaddr, REG_WR_DONE);
	if (v & BIT(1))
		pr_warn("FEPHY: invalid trim status.\n");
	v = v | BIT(0);
	hleth_phy_expanded_write(bus, phyaddr, REG_WR_DONE, v);

	do {
		usleep_range(100, 150); /* 100,150:delay zone */
		v = hleth_phy_expanded_read(bus, phyaddr, REG_WR_DONE);
		v &= BIT(1);
	} while ((v == 0) && (--timeout != 0));
	if (timeout == 0)
		pr_warn("FEPHY: faile to wait trim finish!\n");

	pr_info("FEPHY:addr=%d, la_am=0x%x, ldo_am=0x%x, r_tuning=0x%x\n",
		phyaddr,
		hleth_phy_expanded_read(bus, phyaddr, REG_LD_AM),
		hleth_phy_expanded_read(bus, phyaddr, REG_LDO_AM),
		hleth_phy_expanded_read(bus, phyaddr, REG_R_TUNING));
}
#endif

#ifdef CONFIG_FEPHY_TRIM
void hleth_fix_festa_phy_trim(struct mii_bus *bus, struct hleth_platdrv_data *pdata)
{
	struct hleth_phy_param_s *phy_param = NULL;
	int i;
	int phyaddr;

	if (bus == NULL || pdata == NULL)
		return;

	for (i = 0; i < HLETH_MAX_PORT; i++) {
		phy_param = &pdata->hleth_phy_param[i];

		if (phy_param == NULL)
			continue;

		if (!phy_param->isvalid || !phy_param->isinternal)
			continue;

		phyaddr = phy_param->phy_addr;
		hleth_config_festa_phy_trim(bus, phyaddr,
				phy_param->trim_params);
		mdelay(5); /* 5:delay */
	}
}
#else
void hleth_fix_festa_phy_trim(struct mii_bus *bus, struct hleth_platdrv_data *pdata)
{
	msleep(300); /* 300:delay */
}
#endif

static int phy_expanded_write_bulk(struct phy_device *phy_dev,
				   const u32 reg_and_val[],
				   int count)
{
	int i, v, ret = 0;
	u32 reg_addr;
	u16 val;

	v = phy_read(phy_dev, MII_BMCR);
	v = (unsigned int)v | BMCR_PDOWN;
	phy_write(phy_dev, MII_BMCR, v);

	for (i = 0; i < (2 * count); i += 2) { /* 2:operated value */
		if ((i % 50) == 0) /* 50:operated value */
			schedule();

		reg_addr = reg_and_val[i];
		val = (u16)reg_and_val[i + 1];
		hleth_mdiobus_write_nodelay(phy_dev->mdio.bus,
					    phy_dev->mdio.addr,
					    MII_EXPMA, reg_addr);
		ret = hleth_mdiobus_write_nodelay(phy_dev->mdio.bus,
						  phy_dev->mdio.addr,
						  MII_EXPMD, val);
	}

	v = phy_read(phy_dev, MII_BMCR);
	v =(unsigned int)v & (~BMCR_PDOWN);
	phy_write(phy_dev, MII_BMCR, v);

	return ret;
}

static int hl_fephy_s28v200_fix(struct phy_device *phy_dev)
{
	int count;

	count = ARRAY_SIZE(phy_s28v200_fix_param);
	if (count % 2) /* 2:operated value */
		pr_warn("internal FEPHY fix register count is not right.\n");
	count /= 2; /* 2:operated value */

	phy_expanded_write_bulk(phy_dev, phy_s28v200_fix_param, count);

	return 0;
}

static int ksz8051mnl_phy_fix(struct phy_device *phy_dev)
{
	u32 v;

	v = phy_read(phy_dev, 0x1F);
	v |= (1 << 7);       /* 7:set phy RMII 50MHz clk; */
	phy_write(phy_dev, 0x1F, v);

	v = phy_read(phy_dev, 0x16);
	v |= (1 << 1);       /* set phy RMII override; */
	phy_write(phy_dev, 0x16, v);

	return 0;
}

static int ksz8081rnb_phy_fix(struct phy_device *phy_dev)
{
	u32 v;

	v = phy_read(phy_dev, 0x1F);
	v |= (1 << 7);       /* 7:set phy RMII 50MHz clk; */
	phy_write(phy_dev, 0x1F, v);

	return 0;
}

void hleth_phy_register_fixups(void)
{
	phy_register_fixup_for_uid(HUANGLONG_PHY_ID_FESTA_S28V200,
				   HUANGLONG_PHY_ID_MASK,
				   hl_fephy_s28v200_fix);
	phy_register_fixup_for_uid(PHY_ID_KSZ8051MNL,
				   DEFAULT_PHY_MASK, ksz8051mnl_phy_fix);
	phy_register_fixup_for_uid(PHY_ID_KSZ8081RNB,
				   DEFAULT_PHY_MASK, ksz8081rnb_phy_fix);
}

void hleth_phy_unregister_fixups(void)
{
	phy_unregister_fixup_for_uid(HUANGLONG_PHY_ID_FESTA_S28V200,
				     HUANGLONG_PHY_ID_MASK);
	phy_unregister_fixup_for_uid(PHY_ID_KSZ8051MNL, DEFAULT_PHY_MASK);
	phy_unregister_fixup_for_uid(PHY_ID_KSZ8081RNB, DEFAULT_PHY_MASK);
}

static void hleth_internal_phy_clk_disable(struct hleth_phy_param_s *phy_param)
{
	clk_disable_unprepare(phy_param->phy_clk);
}

static void hleth_internal_phy_reset(struct hleth_phy_param_s *phy_param)
{
	unsigned int val;
	int bit_offset;

	/* FEPHY enable clock */
	clk_prepare_enable(phy_param->phy_clk);

	/* set FEPHY address */
	if (phy_param->fephy_phyaddr_bit < 0)
		phy_param->fephy_phyaddr_bit = HLETH_PHY_ADDR_BIT;
	bit_offset = phy_param->fephy_phyaddr_bit;
	if (phy_param->fephy_sysctrl != NULL) {
		val = readl(phy_param->fephy_sysctrl);
		val &= ~((MASK_PHY_ADDR) << bit_offset);
		val |= (((unsigned int)phy_param->phy_addr & MASK_PHY_ADDR) << bit_offset);
		writel(val, phy_param->fephy_sysctrl);
	}

	/* FEPHY set reset */
	reset_control_assert(phy_param->phy_rst);
	usleep_range(10, 1000); /* 10,1000:delay zone */
	/* FEPHY cancel reset */
	reset_control_deassert(phy_param->phy_rst);

	msleep(20); /* 20:delay at least 15ms for MDIO operation */
}

static void hleth_gpio_reset(void __iomem *gpio_base, u32 gpio_bit)
{
	u32 v;

#define RESET_DATA      (1)

	if (gpio_base == NULL)
		return;

	gpio_base = (void *)ioremap((uintptr_t)gpio_base, 0x1000);

	/* config gpio[x] dir to output */
	v = readb(gpio_base + 0x400);
	v |= (1 << gpio_bit);
	writeb(v, gpio_base + 0x400);

	/* output 1--0--1 */
	writeb(RESET_DATA << gpio_bit, gpio_base + (4 << gpio_bit));
	msleep(20);
	writeb((!RESET_DATA) << gpio_bit, gpio_base + (4 << gpio_bit));
	msleep(20);
	writeb(RESET_DATA << gpio_bit, gpio_base + (4 << gpio_bit));
	msleep(20);

	iounmap(gpio_base);
}

static void hleth_external_phy_reset(struct hleth_phy_param_s *phy_param)
{
	/************************************************/
	reset_control_deassert(phy_param->phy_rst);

	msleep(20); /* 20:delay */

	reset_control_assert(phy_param->phy_rst);

	msleep(20); /* 20:delay */
	reset_control_deassert(phy_param->phy_rst);

	/************************************************/
	/* reset external phy with gpio */
	hleth_gpio_reset(phy_param->gpio_base, phy_param->gpio_bit);

	/************************************************/

	/* add some delay in case mdio cann't access now! */
	msleep(30); /* 30:delay */
}

void hleth_phy_reset(struct hleth_platdrv_data *pdata)
{
	int i;
	struct hleth_phy_param_s *phy_param = NULL;

	if (pdata == NULL)
		return;

	for (i = 0; i < HLETH_MAX_PORT; i++) {
		phy_param = &pdata->hleth_phy_param[i];

		if (!phy_param->isvalid)
			continue;

		if (phy_param->isinternal)
			hleth_internal_phy_reset(phy_param);
		else
			hleth_external_phy_reset(phy_param);
	}
}

void hleth_phy_clk_disable(struct hleth_platdrv_data *pdata)
{
	struct hleth_phy_param_s *phy_param = NULL;
	int i;

	if (pdata == NULL)
		return;

	for (i = 0; i < HLETH_MAX_PORT; i++) {
		phy_param = &pdata->hleth_phy_param[i];

		if (!phy_param->isvalid)
			continue;

		if (phy_param->isinternal)
			hleth_internal_phy_clk_disable(phy_param);
	}
}
