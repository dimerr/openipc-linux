/*
 * Copyright (c) CompanyNameMagicTag 2022. All rights reserved.
 * Description: Mdio settings.
 * Author: AuthorNameMagicTag
 * Create: 2022-4-20
 */

#include <securec.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/of_mdio.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include "hleth.h"
#include "mdio.h"

/* MDIO Bus Interface */

static void hleth_mdio_init(struct hleth_netdev_priv *priv)
{
	hleth_mdio_lock_init(priv);
	mdio_reg_reset(priv);
}

static void hleth_mdio_exit(struct hleth_netdev_priv *priv)
{
	hleth_mdio_lock_exit(priv);
}

static int hleth_wait_mdio_ready(const struct hleth_netdev_priv *priv)
{
	int timeout_us = 1000;

	while (--timeout_us && !mdio_test_ready(priv))
		udelay(1);

	return timeout_us;
}

int hleth_mdiobus_read(struct mii_bus *bus, int phy_addr, int regnum)
{
	int ret = 0;
	struct hleth_netdev_priv *priv = NULL;

	if (bus == NULL)
		return -ETIMEDOUT;

	priv = bus->priv;
	if (priv == NULL)
		return -ETIMEDOUT;

	hleth_mdio_lock(priv);

	if (hleth_wait_mdio_ready(priv) == 0) {
		pr_err("%s,%d:mdio busy\n", __func__, __LINE__);
		ret = -ETIMEDOUT;
		goto error_exit;
	}

	mdio_start_phyread(priv, phy_addr, regnum);

	if (hleth_wait_mdio_ready(priv)) {
		ret = mdio_get_phyread_val(priv);
	} else {
		pr_err("%s,%d:mdio busy\n", __func__, __LINE__);
		ret = -ETIMEDOUT;
	}

error_exit:

	hleth_mdio_unlock(priv);

	pr_debug("phy_addr = %d, regnum = %d, ret = 0x%04x\n", phy_addr,
		    regnum, ret);

	return ret;
}

int hleth_mdiobus_write(struct mii_bus *bus, int phy_addr, int regnum,
			       u16 val)
{
	int ret = 0;
	struct hleth_netdev_priv *priv = NULL;

	if (bus == NULL)
		return -ETIMEDOUT;

	priv = bus->priv;
	if (priv == NULL)
		return -ETIMEDOUT;

	pr_debug("phy_addr = %d, regnum = %d\n", phy_addr, regnum);

	hleth_mdio_lock(priv);

	if (hleth_wait_mdio_ready(priv) == 0) {
		pr_err("%s,%d:mdio busy\n", __func__, __LINE__);
		ret = -ETIMEDOUT;
		goto error_exit;
	}

	mdio_phywrite(priv, phy_addr, regnum, val);

	udelay(500); /* 500:delay */
	if (hleth_wait_mdio_ready(priv) == 0) {
		pr_err("%s,%d:mdio busy\n", __func__, __LINE__);
		ret = -ETIMEDOUT;
	}

error_exit:
	hleth_mdio_unlock(priv);

	return ret;
}

int hleth_mdiobus_write_nodelay(struct mii_bus *bus, int phy_addr, int regnum,
				u16 val)
{
	struct hleth_netdev_priv *priv = NULL;
	int ret = 0;

	if (bus == NULL)
		return -ETIMEDOUT;

	priv = bus->priv;
	if (priv == NULL)
		return -ETIMEDOUT;

	hleth_mdio_lock(priv);

	if (hleth_wait_mdio_ready(priv) == 0) {
		ret = -ETIMEDOUT;
		pr_err("%s,%d:mdio busy\n", __func__, __LINE__);
		goto error_exit;
	}

	mdio_phywrite(priv, phy_addr, regnum, val);

	if (hleth_wait_mdio_ready(priv) == 0) {
		ret = -ETIMEDOUT;
		pr_err("%s,%d:mdio busy\n", __func__, __LINE__);
	}

error_exit:
	hleth_mdio_unlock(priv);

	return ret;
}

static int hleth_mdiobus_reset(struct mii_bus *bus)
{
	struct hleth_netdev_priv *priv = bus->priv;

	mdio_reg_reset(priv);
	return 0;
}

int hleth_mdiobus_driver_init(struct platform_device *pdev,
			      struct hleth_netdev_priv *priv)
{
	int ret = 0;
	struct mii_bus *bus = NULL;
	struct device *dev = NULL;
	struct device_node *node = NULL;
	struct hleth_platdrv_data *pdata = NULL;

	if (pdev == NULL || priv == NULL)
		return -ENOMEM;

	dev = &pdev->dev;
	node = dev->of_node;
	pdata = platform_get_drvdata(pdev);
	hleth_mdio_init(priv);

	/* register MII bus */
	bus = mdiobus_alloc();
	if (bus == NULL) {
		pr_err("get ioresource failed!\n");
		ret = -ENOMEM;
		goto _error_exit;
	}

	bus->name = HLETH_MIIBUS_NAME;

	ret = snprintf_s(bus->id, MII_BUS_ID_SIZE, MII_BUS_ID_SIZE - 1, "%s", pdev->name);
	if (ret < 0) {
		pr_err("failed to snprintf bus id, ret = %d\n", ret);
		goto _error_free_mdiobus;
	}
	bus->read = hleth_mdiobus_read;
	bus->write = hleth_mdiobus_write;
	bus->reset = hleth_mdiobus_reset;
	bus->priv = priv;
	priv->mii_bus = bus;
	bus->parent = &pdev->dev;	/* for Power Management */

	hleth_fix_festa_phy_trim(bus, pdata);

	ret = of_mdiobus_register(bus, node);
	if (ret) {
		pr_err("failed to register MDIO bus\n");
		goto _error_free_mdiobus;
	}

	return 0;

_error_free_mdiobus:
	mdiobus_free(bus);

_error_exit:
	return ret;
}

void hleth_mdiobus_driver_exit(struct hleth_netdev_priv *priv)
{
	struct mii_bus *bus = priv->mii_bus;

	mdiobus_unregister(bus);
	mdiobus_free(bus);
	hleth_mdio_exit(priv);
}

/* vim: set ts=8 sw=8 tw=78: */
