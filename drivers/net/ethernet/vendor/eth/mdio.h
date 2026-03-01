/*
 * Copyright (c) CompanyNameMagicTag 2022. All rights reserved.
 * Description: Header file for mdio.c
 * Author: AuthorNameMagicTag
 * Create: 2022-4-20
 */
#ifndef __SOCT_ETH_MDIO_H__
#define __SOCT_ETH_MDIO_H__

#include "hleth.h"

#define HLETH_MDIO_FRQDIV               0

#define HLETH_MDIO_RWCTRL               0x1100
#define HLETH_MDIO_RO_DATA              0x1104
#define HLETH_U_MDIO_PHYADDR            0x0108
#define HLETH_D_MDIO_PHYADDR            0x2108
#define HLETH_U_MDIO_RO_STAT            0x010C
#define HLETH_D_MDIO_RO_STAT            0x210C
#define HLETH_U_MDIO_ANEG_CTRL          0x0110
#define HLETH_D_MDIO_ANEG_CTRL          0x2110
#define HLETH_U_MDIO_IRQENA             0x0114
#define HLETH_D_MDIO_IRQENA             0x2114

#define mdio_mk_rwctl(cpu_data_in, finish, rw, phy_exaddr, frq_div, phy_regnum) \
		(((cpu_data_in) << 16) | \
		  (((finish) & 0x01) << 15) | \
		  (((rw) & 0x01) << 13) | \
		  (((phy_exaddr) & 0x1F) << 8) | \
		  (((frq_div) & 0x7) << 5) | \
		  ((phy_regnum) & 0x1F))

/* hardware set bit'15 of MDIO_REG(0) if mdio ready */
#define mdio_test_ready(priv) (hleth_readl((priv)->glb_base, \
			       HLETH_MDIO_RWCTRL) & (1 << 15))

#define mdio_start_phyread(priv, phy_addr, regnum) \
	hleth_writel((priv)->glb_base, \
		     mdio_mk_rwctl(0, 0, 0, phy_addr, HLETH_MDIO_FRQDIV, \
				   regnum), \
		     HLETH_MDIO_RWCTRL)

#define mdio_get_phyread_val(priv) (hleth_readl((priv)->glb_base, \
				    HLETH_MDIO_RO_DATA) & 0xFFFF)

#define mdio_phywrite(priv, phy_addr, regnum, val) \
	hleth_writel((priv)->glb_base, \
		     mdio_mk_rwctl(val, 0, 1, phy_addr, HLETH_MDIO_FRQDIV, \
				   regnum), \
		     HLETH_MDIO_RWCTRL)

/* write mdio registers reset value */
#define mdio_reg_reset(priv) do { \
	hleth_writel((priv)->glb_base, 0x00008000, HLETH_MDIO_RWCTRL); \
	hleth_writel((priv)->glb_base, 0x00000001, HLETH_U_MDIO_PHYADDR); \
	hleth_writel((priv)->glb_base, 0x00000001, HLETH_D_MDIO_PHYADDR); \
	hleth_writel((priv)->glb_base, 0x04631EA9, HLETH_U_MDIO_ANEG_CTRL); \
	hleth_writel((priv)->glb_base, 0x04631EA9, HLETH_D_MDIO_ANEG_CTRL); \
	hleth_writel((priv)->glb_base, 0x00000000, HLETH_U_MDIO_IRQENA); \
	hleth_writel((priv)->glb_base, 0x00000000, HLETH_D_MDIO_IRQENA); \
} while (0)

int hleth_mdiobus_driver_init(struct platform_device *pdev,
			      struct hleth_netdev_priv *priv);
void hleth_mdiobus_driver_exit(struct hleth_netdev_priv *priv);

int hleth_mdiobus_read(struct mii_bus *bus, int phy_addr, int regnum);
int hleth_mdiobus_write(struct mii_bus *bus, int phy_addr, int regnum,
			u16 val);
int hleth_mdiobus_write_nodelay(struct mii_bus *bus, int phy_addr, int regnum,
				u16 val);
#endif

/* vim: set ts=8 sw=8 tw=78: */
