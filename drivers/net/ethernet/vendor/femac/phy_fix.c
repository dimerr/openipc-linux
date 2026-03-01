/*
 *
 * Copyright (c) 2012-2021 Shenshu Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/phy.h>
#include "phy_fix.h"

static const u32 phy_v272_fix_param[] = {
#include "festa_v272_2723.h"
};

static const u32 phy_v115_fix_param[] = {
#include "festa_s28v115_2c02.h"
};

static const u32 phy_v202_fix_param[] = {
#include "festa_s28v202_2e01.h"
};

static int phy_expanded_write_bulk(struct phy_device *phy_dev,
				   const u32 reg_and_val[], int count)
{
	int i, v;
	u32 reg_addr;
	u16 val;

	v = phy_read(phy_dev, MII_BMCR);
	v = (u32)v | BMCR_PDOWN;
	phy_write(phy_dev, MII_BMCR, v);

	for (i = 0; i < count; i += 2) { /* Process 2 data at a time. */
		reg_addr = reg_and_val[i];
		val = (u16)reg_and_val[i + 1];
		phy_write(phy_dev, MII_EXPMA, reg_addr);
		phy_write(phy_dev, MII_EXPMD, val);
	}

	v = phy_read(phy_dev, MII_BMCR);
	v = (u32)v & (~BMCR_PDOWN);
	phy_write(phy_dev, MII_BMCR, v);

	return 0;
}

static int bsp_fephy_v272_fix(struct phy_device *phy_dev)
{
	int count;

	count = ARRAY_SIZE(phy_v272_fix_param);
	if (count % 2) /* must be an even number, mod 2 */
		pr_warn("internal FEPHY fix register count is not right.\n");
	phy_expanded_write_bulk(phy_dev, phy_v272_fix_param, count);

	return 0;
}

static int bsp_fephy_v115_fix(struct phy_device *phy_dev)
{
	int count;

	count = ARRAY_SIZE(phy_v115_fix_param);
	if (count % 2) /* must be an even number, mod 2 */
		pr_warn("internal FEPHY fix register count is not right.\n");
	phy_expanded_write_bulk(phy_dev, phy_v115_fix_param, count);

	return 0;
}

static int bsp_fephy_v202_fix(struct phy_device *phy_dev)
{
	int count;

	count = ARRAY_SIZE(phy_v202_fix_param);
	if (count % 2) /* must be an even number, mod 2 */
		pr_warn("internal FEPHY fix register count is not right.\n");
	phy_expanded_write_bulk(phy_dev, phy_v202_fix_param, count);

	return 0;
}

void phy_register_fixups(void)
{
	phy_register_fixup_for_uid(BSP_PHY_ID_FESTAV272,
				   BSP_PHY_MASK,
				   bsp_fephy_v272_fix);

	phy_register_fixup_for_uid(BSP_PHY_ID_FESTAV115,
				   BSP_PHY_MASK,
				   bsp_fephy_v115_fix);

	phy_register_fixup_for_uid(BSP_PHY_ID_FESTAV202,
				   BSP_PHY_MASK,
				   bsp_fephy_v202_fix);
}

void phy_unregister_fixups(void)
{
	phy_unregister_fixup_for_uid(BSP_PHY_ID_FESTAV272,
				     BSP_PHY_MASK);

	phy_unregister_fixup_for_uid(BSP_PHY_ID_FESTAV115,
				     BSP_PHY_MASK);

	phy_unregister_fixup_for_uid(BSP_PHY_ID_FESTAV202,
				     BSP_PHY_MASK);
}
