/*
 * Copyright (c) CompanyNameMagicTag 2022. All rights reserved.
 * Description: Header file for phy.c
 * Author: AuthorNameMagicTag
 * Create: 2022-4-20
 */

#ifndef __HLETH_FEPHY_FIX_H
#define __HLETH_FEPHY_FIX_H

#define HLETH_PHY_ADDR_BIT			23
#define MASK_PHY_ADDR				0x1F

#define MII_EXPMD			0x1D
#define MII_EXPMA			0x1E

/* the following two copied from phy_quirk()
 * in "./drivers/net/ethernet/hleth-sf/net.c"
 */
#define PHY_ID_KSZ8051MNL		0x00221550
#define PHY_ID_KSZ8081RNB		0x00221560
#define DEFAULT_PHY_MASK		0xfffffff0

#endif
