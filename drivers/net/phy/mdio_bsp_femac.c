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

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/securec.h>
#define MDIO_RWCTRL		0x00
#define MDIO_RO_DATA		0x04
#define MDIO_WRITE		BIT(13)
#define MDIO_RW_FINISH		BIT(15)
#define BIT_PHY_ADDR_OFFSET	8
#define BIT_WR_DATA_OFFSET	16

#define BIT_MASK_FEPHY_ADDR	GENMASK(4, 0)
#define BIT_FEPHY_SEL		BIT(5)

#if defined(CONFIG_ARCH_HI3516CV610_FAMILY)
#define BIT_OFFSET_LD_SET   6
#define BIT_OFFSET_LDO_SET  12
#define BIT_OFFSET_R_TUNING 0
#define DEF_LD_AM              0x9
#define DEF_LDO_AM             0x3
#define DEF_R_TUNING           0x16
#else
#define BIT_OFFSET_LD_SET	25
#define BIT_OFFSET_LDO_SET	22
#define BIT_OFFSET_R_TUNING	16
#endif
#define MII_EXPMD		0x1d
#define MII_EXPMA		0x1e

#define REG_LD_AM		0x3050
#define BIT_MASK_LD_SET		GENMASK(4, 0)
#define REG_LDO_AM		0x3051
#define BIT_MASK_LDO_SET	GENMASK(2, 0)
#define REG_R_TUNING		0x3052
#define BIT_MASK_R_TUNING	GENMASK(5, 0)
#define REG_WR_DONE		0x3053
#define BIT_CFG_DONE		BIT(0)
#define BIT_CFG_ACK		BIT(1)
#define REG_DEF_ATE		0x3057
#define BIT_AUTOTRIM_DONE	BIT(0)

#define PHY_RESET_DELAYS_PROPERTY	"phy-reset-delays-us"

enum phy_reset_delays {
	PRE_DELAY,
	PULSE,
	POST_DELAY,
	DELAYS_NUM,
};

struct bsp_femac_mdio_data {
	struct clk *clk;
	struct clk *fephy_clk;
	struct reset_control *phy_rst;
	struct reset_control *fephy_rst;
	u32 phy_reset_delays[DELAYS_NUM];
	void __iomem *membase;
	void __iomem *fephy_iobase;
	void __iomem *fephy_trim_iobase;
	struct mii_bus *bus;
	u32 phy_addr;
};

static int bsp_femac_mdio_wait_ready(struct bsp_femac_mdio_data *data)
{
	u32 val;
#define DELAY_US 20
#define TIMEOUT_US 10000
	return readl_poll_timeout_atomic(data->membase + MDIO_RWCTRL,
				  val, val & MDIO_RW_FINISH, DELAY_US, TIMEOUT_US);
}

static int bsp_femac_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct bsp_femac_mdio_data *data = bus->priv;
	int ret;

	ret = bsp_femac_mdio_wait_ready(data);
	if (ret)
		return ret;

	writel(((u32)mii_id << BIT_PHY_ADDR_OFFSET) | ((u32)regnum),
		  data->membase + MDIO_RWCTRL);

	ret = bsp_femac_mdio_wait_ready(data);
	if (ret)
		return ret;

	return readl(data->membase + MDIO_RO_DATA) & 0xFFFF;
}

static int bsp_femac_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
				 u16 value)
{
	struct bsp_femac_mdio_data *data = bus->priv;
	int ret;

	ret = bsp_femac_mdio_wait_ready(data);
	if (ret)
		return ret;

	writel(MDIO_WRITE | (value << BIT_WR_DATA_OFFSET) |
	       ((u32)mii_id << BIT_PHY_ADDR_OFFSET) | ((u32)regnum),
	       data->membase + MDIO_RWCTRL);

	return bsp_femac_mdio_wait_ready(data);
}

static void bsp_femac_sleep_us(u32 time_us)
{
	u32 time_ms;

	if (!time_us)
		return;

	time_ms = DIV_ROUND_UP(time_us, 1000); /* 1000:time_us */
	if (time_ms < 20) /* 20:time cmp */
		usleep_range(time_us, time_us + 500); /* 500:time add */
	else
		msleep(time_ms);
}

static void bsp_femac_phy_reset(const struct bsp_femac_mdio_data *data)
{
	/* To make sure PHY hardware reset success,
	 * we must keep PHY in deassert state first and
	 * then complete the hardware reset operation
	 */
	reset_control_deassert(data->phy_rst);
	bsp_femac_sleep_us(data->phy_reset_delays[PRE_DELAY]);

	reset_control_assert(data->phy_rst);
	/* delay some time to ensure reset ok,
	 * this depends on PHY hardware feature
	 */
	bsp_femac_sleep_us(data->phy_reset_delays[PULSE]);
	reset_control_deassert(data->phy_rst);
	/* delay some time to ensure later MDIO access */
	bsp_femac_sleep_us(data->phy_reset_delays[POST_DELAY]);
}

static void bsp_femac_get_phy_addr(struct bsp_femac_mdio_data *data,
				    struct device_node *np)
{
	struct device_node *child = NULL;
	int addr;

	child = of_get_next_available_child(np, NULL);
	if (!child) {
		pr_err("%s: No valid PHY device node!\n", __func__);
		return;
	}

	addr = of_mdio_parse_addr(&data->bus->dev, child);
	if (addr < 0) {
		pr_err("%s: get PHY address failed!\n", __func__);
		return;
	}

	data->phy_addr = addr;
}

static inline bool bsp_femac_use_fephy(struct bsp_femac_mdio_data *data)
{
	return true;
}

static void bsp_femac_fephy_reset(struct bsp_femac_mdio_data *data)
{
	u32 val;

	/* disable MDCK clock to make sure FEPHY reset success */
	clk_disable_unprepare(data->clk);

	val = readl(data->fephy_iobase);
	val &= ~BIT_MASK_FEPHY_ADDR;
	val |= data->phy_addr;
	writel(val, data->fephy_iobase);

	clk_prepare_enable(data->fephy_clk);
	udelay(10); /* 10:delay */

	reset_control_assert(data->fephy_rst);
	udelay(10); /* 10:delay */
	reset_control_deassert(data->fephy_rst);
	/* delay at least 15ms for MDIO operation */
	msleep(20); /* 20:delay */

	clk_prepare_enable(data->clk);
	/* delay 5ms after enable MDCK to make sure FEPHY trim safe */
	mdelay(5); /* 5:delay */
}

static int fephy_expanded_read(struct mii_bus *bus, int phy_addr,
				      u32 reg_addr)
{
	int ret;

	bsp_femac_mdio_write(bus, phy_addr, MII_EXPMA, reg_addr);
	ret = bsp_femac_mdio_read(bus, phy_addr, MII_EXPMD);

	return ret;
}

static int fephy_expanded_write(struct mii_bus *bus, int phy_addr,
				       u32 reg_addr, u16 val)
{
	int ret;

	bsp_femac_mdio_write(bus, phy_addr, MII_EXPMA, reg_addr);
	ret = bsp_femac_mdio_write(bus, phy_addr, MII_EXPMD, val);

	return ret;
}

void bsp_femac_fephy_use_default_trim(struct bsp_femac_mdio_data *data)
{
	unsigned short val;
	int timeout = 3;

	pr_info("No OTP data, festa PHY use default ATE parameters!\n");

	do {
		msleep(250); /* 250:delay */
		val = fephy_expanded_read(data->bus, data->phy_addr,
					  REG_DEF_ATE);
		val &= BIT_AUTOTRIM_DONE;
	} while (!val && --timeout);

	if (!timeout)
		pr_err("festa PHY wait autotrim done timeout!\n");

	mdelay(5); /* 5:delay */
}

static void bsp_femac_fephy_trim(struct bsp_femac_mdio_data *data)
{
	struct mii_bus *bus = data->bus;
	u32 phy_addr = data->phy_addr;
	int timeout = 3000;
	u32 val, ld_set, ldo_set, r_tuning;

	/* FEPHY get OTP trim data from special reg not fephy control reg1 */
#if defined(CONFIG_ARCH_HI3516CV610_FAMILY)
	val = readl(data->fephy_trim_iobase);
#else
	val = readl(data->fephy_iobase);
#endif
	ld_set = (val >> BIT_OFFSET_LD_SET) & BIT_MASK_LD_SET;
	ldo_set = (val >> BIT_OFFSET_LDO_SET) & BIT_MASK_LDO_SET;
	r_tuning = (val >> BIT_OFFSET_R_TUNING) & BIT_MASK_R_TUNING;
#if defined(CONFIG_ARCH_HI3516CV610_FAMILY)
	if (!ld_set && !ldo_set && !r_tuning) {
		ld_set = DEF_LD_AM;
		ldo_set = DEF_LDO_AM;
		r_tuning = DEF_R_TUNING;
	}
#endif

	if (!ld_set && !ldo_set && !r_tuning) {
		bsp_femac_fephy_use_default_trim(data);
		return;
	}

	val = fephy_expanded_read(bus, phy_addr, REG_LD_AM);
	val = (val & ~BIT_MASK_LD_SET) | (ld_set & BIT_MASK_LD_SET);
	fephy_expanded_write(bus, phy_addr, REG_LD_AM, val);

	val = fephy_expanded_read(bus, phy_addr, REG_LDO_AM);
	val = (val & ~BIT_MASK_LDO_SET) | (ldo_set & BIT_MASK_LDO_SET);
	fephy_expanded_write(bus, phy_addr, REG_LDO_AM, val);

	val = fephy_expanded_read(bus, phy_addr, REG_R_TUNING);
	val = (val & ~BIT_MASK_R_TUNING) | (r_tuning & BIT_MASK_R_TUNING);
	fephy_expanded_write(bus, phy_addr, REG_R_TUNING, val);

	val = fephy_expanded_read(bus, phy_addr, REG_WR_DONE);
	if (val & BIT_CFG_ACK)
		pr_err("festa PHY 0x3053 bit CFG_ACK value: 1\n");
	val = val | BIT_CFG_DONE;
	fephy_expanded_write(bus, phy_addr, REG_WR_DONE, val);

	do {
		usleep_range(100, 150); /* 100,150:delay */
		val = fephy_expanded_read(bus, phy_addr, REG_WR_DONE);
		val &= BIT_CFG_ACK;
	} while (!val && --timeout);
	if (!timeout)
		pr_err("festa PHY 0x3053 wait bit CFG_ACK timeout!\n");

	mdelay(5); /* 5:delay */

	pr_info("FEPHY:addr=%d, la_am=0x%x, ldo_am=0x%x, r_tuning=0x%x\n", phy_addr,
		fephy_expanded_read(bus, phy_addr, REG_LD_AM),
		fephy_expanded_read(bus, phy_addr, REG_LDO_AM),
		fephy_expanded_read(bus, phy_addr, REG_R_TUNING));
}

static void bsp_femac_fephy_reset_and_trim(struct bsp_femac_mdio_data *data)
{
	bsp_femac_fephy_reset(data);
	bsp_femac_fephy_trim(data);
}

static void bsp_femac_fephy_set_phy_addr(struct bsp_femac_mdio_data *data)
{
	u32 val;

	if (!data->fephy_iobase)
		return;

	val = readl(data->fephy_iobase);
	val &= ~BIT_MASK_FEPHY_ADDR;
	val |= (data->phy_addr + 1);
	writel(val, data->fephy_iobase);
}
static int bsp_femac_ioresource_remap(struct bsp_femac_mdio_data *data, struct platform_device *pdev)
{
	int ret;
	struct resource *res = NULL;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->membase)) {
		ret = PTR_ERR(data->membase);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		data->fephy_iobase = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(data->fephy_iobase)) {
			ret = PTR_ERR(data->fephy_iobase);
			return ret;
		}
	} else {
		data->fephy_iobase = NULL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2); /* 2:index */
	if (res) {
		data->fephy_trim_iobase = devm_ioremap_resource(&pdev->dev,
								res);
		if (IS_ERR(data->fephy_trim_iobase)) {
			ret = PTR_ERR(data->fephy_trim_iobase);
			return ret;
		}
	} else {
		data->fephy_trim_iobase = NULL;
	}

	data->clk = devm_clk_get(&pdev->dev, "mdio");
	if (IS_ERR(data->clk)) {
		ret = PTR_ERR(data->clk);
		return ret;
	}

	data->fephy_clk = devm_clk_get(&pdev->dev, "phy");
	if (IS_ERR(data->fephy_clk))
		data->fephy_clk = NULL;

	ret = clk_prepare_enable(data->clk);
	if (ret)
		return ret;
	return 0;
}

#define REG_CRG_BASE	0x11010000UL
#define OFST_CRG_FEPHY	0x37CC
#define SIZE_16KB	0x40000UL

static void bsp_femac_mdio_preinit(void)
{
	void *reg_base = ioremap(REG_CRG_BASE, SIZE_16KB);
	if (reg_base == NULL)
		return;
	writel(0x6, reg_base + OFST_CRG_FEPHY);
	iounmap(reg_base);
}

static int bsp_femac_mdio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mii_bus *bus = NULL;
	struct bsp_femac_mdio_data *data = NULL;

	int ret, str_len;

	bsp_femac_mdio_preinit();

	bus = mdiobus_alloc_size(sizeof(*data));
	if (!bus)
		return -ENOMEM;
	str_len = strlen(pdev->name);
	bus->name = "bsp_femac_mii_bus";
	bus->read = &bsp_femac_mdio_read;
	bus->write = &bsp_femac_mdio_write;
	if (snprintf_s(bus->id, MII_BUS_ID_SIZE, str_len, "%s", pdev->name) < 0)
		printk("snprintf_s failed! func:%s, line: %d\n", __func__, __LINE__);
	bus->parent = &pdev->dev;

	data = bus->priv;
	data->bus = bus;
	ret = bsp_femac_ioresource_remap(data, pdev);
	if (ret != 0)
		goto err_out_free_mdiobus;

	data->phy_rst = devm_reset_control_get(&pdev->dev, "external-phy");
	if (IS_ERR(data->phy_rst)) {
		data->phy_rst = NULL;
	} else {
		ret = of_property_read_u32_array(np, PHY_RESET_DELAYS_PROPERTY,
						 data->phy_reset_delays, DELAYS_NUM);
		if (ret)
			goto err_out_disable_clk;
		bsp_femac_phy_reset(data);
	}

	data->fephy_rst = devm_reset_control_get(&pdev->dev, "internal-phy");
	if (IS_ERR(data->fephy_rst))
		data->fephy_rst = NULL;

	bsp_femac_get_phy_addr(data, np);
	if (bsp_femac_use_fephy(data))
		bsp_femac_fephy_reset_and_trim(data);
	else
		bsp_femac_fephy_set_phy_addr(data);

	ret = of_mdiobus_register(bus, np);
	if (ret)
		goto err_out_disable_clk;

	platform_set_drvdata(pdev, bus);

	return 0;

err_out_disable_clk:
	clk_disable_unprepare(data->fephy_clk);
	clk_disable_unprepare(data->clk);
err_out_free_mdiobus:
	mdiobus_free(bus);
	return ret;
}

static int bsp_femac_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);
	struct bsp_femac_mdio_data *data = bus->priv;

	mdiobus_unregister(bus);
	clk_disable_unprepare(data->clk);
	mdiobus_free(bus);

	return 0;
}

static const struct of_device_id bsp_femac_mdio_dt_ids[] = {
	{ .compatible = "vendor,femac-mdio" },
	{ }
};
MODULE_DEVICE_TABLE(of, bsp_femac_mdio_dt_ids);

static struct platform_driver bsp_femac_mdio_driver = {
	.probe = bsp_femac_mdio_probe,
	.remove = bsp_femac_mdio_remove,
	.driver = {
		.name = "bsp-femac-mdio",
		.of_match_table = bsp_femac_mdio_dt_ids,
	},
};

module_platform_driver(bsp_femac_mdio_driver);

MODULE_DESCRIPTION("Fast Ethernet MAC MDIO interface driver");
MODULE_LICENSE("GPL v2");
