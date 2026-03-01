/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */

#include <asm/io.h>
#include <linux/delay.h>

#include "../basedrv_clk.h"

#define REG_PERI_CRG3664_USB20_CTRL     0x38C8
#define REG_PERI_CRG3632_USB2_PHY0      0x38C0

#define USB2_CTRL_CRG_DEFAULT_VALUE     0x10131
#define USB2_PHY_CRG_DEFAULT_VALUE      0x33

#define USB2_CRG_SRST_REQ               (0x1 << 0)

#define USB2_PHY_CRG_APB_SREQ           (0x1 << 2)
#define USB2_PHY_CRG_TREQ               (0x1 << 1)
#define USB2_PHY_CRG_REQ                (0x1 << 0)

#define OTP_USB_SHADOW_ADDR             0x101E001C
#define USB_VBUS_SEL_MASK               (0x1 << 14)
#define PINOUT_REG_BASE                 0x10260060
#define PINOUT_USB_DEFAULT_VAL          0x1200
// VBUS_IN_PULL_UP_ENABLE_VAL is for QFN, no vbus pin, pull up internal
#define VBUS_IN_PULL_UP_ENABLE_VAL      0x1131
// VBUS_IN_PULL_UP_DISABLE_VAL is for BGA, vbus pin exist
#define VBUS_IN_PULL_UP_DISABLE_VAL     0x1031

#define DELAY_TIME_10                   10
#define DELAY_TIME_200                  200

static unsigned int basedrv_clk_readl(const void __iomem *addr)
{
	unsigned int reg = readl(addr);

	basedrv_clk_dbg("readl(0x%lx) = %#08X\n", (uintptr_t)addr, reg);
	return reg;
}

static void basedrv_clk_writel(unsigned int v, void __iomem *addr)
{
	writel(v, addr);
	basedrv_clk_dbg("writel(0x%lx) = %#08X\n", (uintptr_t)addr, v);
}

static void usb_pinout_cfg(void)
{
	void __iomem *otp_addr = NULL;
	void __iomem *pinout_addr = NULL;
	unsigned int reg;

	otp_addr = ioremap(OTP_USB_SHADOW_ADDR, 0x4);
	if (otp_addr == NULL) {
		basedrv_clk_info("ioremap OTP_USB_SHADOW_ADDR registor failed\n");
		return;
	}

	reg = basedrv_clk_readl(otp_addr) & USB_VBUS_SEL_MASK;
	pinout_addr = ioremap(PINOUT_REG_BASE, 0x4);
	if (pinout_addr == NULL) {
		basedrv_clk_info("ioremap pinout registor failed\n");
		return;
	}

	if (reg == USB_VBUS_SEL_MASK) {
		basedrv_clk_writel(VBUS_IN_PULL_UP_DISABLE_VAL, pinout_addr);
	} else {
		basedrv_clk_writel(VBUS_IN_PULL_UP_ENABLE_VAL, pinout_addr);
	}

	udelay(DELAY_TIME_200); /* delay 200 us to wait vbus stable */

	iounmap(otp_addr);
	iounmap(pinout_addr);
	otp_addr = NULL;
	pinout_addr = NULL;
}

static int xvpphy0_clk_prepare(struct clk_hw *hw)
{
	u32 val;
	struct basedrv_clk_hw *clk = to_basedrv_clk_hw(hw);
	basedrv_clk_dbg("+++");

	/* init phy step1 */
	basedrv_clk_writel(USB2_PHY_CRG_DEFAULT_VALUE,
		clk->peri_crg_base + REG_PERI_CRG3632_USB2_PHY0);

	/* init phy step2 */
	val = basedrv_clk_readl(clk->peri_crg_base + REG_PERI_CRG3632_USB2_PHY0);
	val &= ~(USB2_PHY_CRG_REQ);
	basedrv_clk_writel(val, clk->peri_crg_base + REG_PERI_CRG3632_USB2_PHY0);
	basedrv_clk_dbg("---");

	return 0;
}

static void xvpphy0_clk_unprepare(struct clk_hw *hw)
{
	struct basedrv_clk_hw *clk = to_basedrv_clk_hw(hw);
	basedrv_clk_dbg("+++");

	basedrv_clk_writel(USB2_PHY_CRG_DEFAULT_VALUE,
		clk->peri_crg_base + REG_PERI_CRG3632_USB2_PHY0);
	basedrv_clk_dbg("---");
}

static int xvpphy0_clk_enable(struct clk_hw *hw)
{
	u32 val;
	struct basedrv_clk_hw *clk = to_basedrv_clk_hw(hw);
	basedrv_clk_dbg("+++");

	/* init phy step6 */
	val = basedrv_clk_readl(clk->peri_crg_base + REG_PERI_CRG3632_USB2_PHY0);
	val &= ~(USB2_PHY_CRG_TREQ);
	basedrv_clk_writel(val, clk->peri_crg_base + REG_PERI_CRG3632_USB2_PHY0);
	basedrv_clk_dbg("---");

	return 0;
}

static void xvpphy0_clk_disable(struct clk_hw *hw)
{
	u32 val;
	struct basedrv_clk_hw *clk = to_basedrv_clk_hw(hw);
	basedrv_clk_dbg("+++");

	/* unregister step3 */
	val = basedrv_clk_readl(clk->peri_crg_base + REG_PERI_CRG3632_USB2_PHY0);
	val |= USB2_PHY_CRG_APB_SREQ;
	basedrv_clk_writel(val, clk->peri_crg_base + REG_PERI_CRG3632_USB2_PHY0);
	udelay(DELAY_TIME_10);

	/* unregister step4 */
	basedrv_clk_writel(USB2_PHY_CRG_DEFAULT_VALUE,
		clk->peri_crg_base + REG_PERI_CRG3632_USB2_PHY0);
	basedrv_clk_dbg("---");
}

static int usb20_drd_clk_prepare(struct clk_hw *hw)
{
	struct basedrv_clk_hw *clk = to_basedrv_clk_hw(hw);
	basedrv_clk_dbg("+++");

	/* init ctrl step0 */
	usb_pinout_cfg();

	/* init ctrl step1 */
	basedrv_clk_writel(USB2_CTRL_CRG_DEFAULT_VALUE,
		clk->peri_crg_base + REG_PERI_CRG3664_USB20_CTRL);
	udelay(DELAY_TIME_200); // delay 200 us

	basedrv_clk_dbg("---");

	return 0;
}

static void usb20_drd_clk_unprepare(struct clk_hw *hw)
{
	struct basedrv_clk_hw *clk = to_basedrv_clk_hw(hw);
	basedrv_clk_dbg("+++");

	basedrv_clk_writel(USB2_CTRL_CRG_DEFAULT_VALUE,
		clk->peri_crg_base + REG_PERI_CRG3664_USB20_CTRL);
	basedrv_clk_dbg("---");
}

static int usb20_drd_clk_enable(struct clk_hw *hw)
{
	u32 val;
	struct basedrv_clk_hw *clk = to_basedrv_clk_hw(hw);
	basedrv_clk_dbg("+++");

	/* init ctrl step2 */
	val = basedrv_clk_readl(clk->peri_crg_base + REG_PERI_CRG3664_USB20_CTRL);
	val &= ~USB2_CRG_SRST_REQ;
	basedrv_clk_writel(val, clk->peri_crg_base + REG_PERI_CRG3664_USB20_CTRL);
	basedrv_clk_dbg("---");

	return 0;
}

static void usb20_drd_clk_disable(struct clk_hw *hw)
{
	void __iomem *addr = NULL;
	struct basedrv_clk_hw *clk = to_basedrv_clk_hw(hw);
	basedrv_clk_dbg("+++");

	/* unregister step 1 write default to Vbus pin_out */
	addr = ioremap(PINOUT_REG_BASE, 0x4);
	if (addr == NULL) {
		basedrv_clk_info("ioremap pinout registor failed\n");
		return;
	}
	basedrv_clk_writel(PINOUT_USB_DEFAULT_VAL, addr);

	/* unregister step 2 write default value to DRD ctrl */
	basedrv_clk_writel(USB2_CTRL_CRG_DEFAULT_VALUE,
		clk->peri_crg_base + REG_PERI_CRG3664_USB20_CTRL);

	iounmap(addr);
	basedrv_clk_dbg("---");
}

struct clk_ops g_clk_ops_xvpphy0 = {
	.prepare = xvpphy0_clk_prepare,
	.unprepare = xvpphy0_clk_unprepare,
	.enable = xvpphy0_clk_enable,
	.disable = xvpphy0_clk_disable,
};

struct clk_ops g_clk_ops_usb20_drd = {
	.prepare = usb20_drd_clk_prepare,
	.unprepare = usb20_drd_clk_unprepare,
	.enable = usb20_drd_clk_enable,
	.disable = usb20_drd_clk_disable,
};

