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

#ifndef __ETH_PHY_FIX_H__
#define __ETH_PHY_FIX_H__

#define BSP_PHY_ID_FESTAV272     0x20669901
#define BSP_PHY_ID_FESTAV115     0x20669903
#define BSP_PHY_ID_FESTAV202     0x20669906
#define BSP_PHY_MASK             0xffffffff

#define MII_EXPMD 0x1d
#define MII_EXPMA 0x1e

void phy_register_fixups(void);
void phy_unregister_fixups(void);

#endif
