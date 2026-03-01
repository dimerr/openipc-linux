/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */
#include <linux/regmap.h>

#include "sdhci.h"

/* Host controller CRG */
#define EMMC_CRG_CLK_OFS            0x35c0
#define SDIO0_CRG_CLK_OFS           0x35c0
#define SDIO1_CRG_CLK_OFS           0x36c0
#define CRG_SRST_REQ                (BIT(16) | BIT(17) | BIT(18))
#define CRG_CLK_EN_MASK             (BIT(0) | BIT(1))
#define CRG_CLK_BIT_OFS             24
#define CRG_CLK_SEL_MASK            (0x7 << CRG_CLK_BIT_OFS)

/* Host dll reset register */
#define EMMC_DLL_RST_OFS            0x35c4
#define SDIO0_DLL_RST_OFS           0x35c4
#define SDIO1_DLL_RST_OFS           0x36c4
#define CRG_DLL_SRST_REQ            BIT(1)

/* Host dll phase register */
#define EMMC_DRV_DLL_OFS            0x35c8
#define SDIO0_DRV_DLL_OFS           0x35c8
#define SDIO1_DRV_DLL_OFS           0x36c8
#define CRG_DRV_PHASE_SHIFT         15
#define CRG_DRV_PHASE_MASK          (0x1F << CRG_DRV_PHASE_SHIFT)

/* Host dll state register */
#define EMMC_DLL_STA_OFS            0x35d8
#define SDIO0_DLL_STA_OFS           0x35d8
#define SDIO1_DLL_STA_OFS           0x36d8
#define CRG_P4_DLL_LOCKED           BIT(9)
#define CRG_DS_DLL_READY            BIT(10)
#define CRG_SAM_DLL_READY           BIT(12)

/* Host drv cap config */
#define DRV_STR_SHIFT               4
#define DRV_STR_MASK_GPIO           (0xF << DRV_STR_SHIFT)
#define SR_STR_SHIFT                10
#define SR_STR_MASK_GPIO            (0x1 << SR_STR_SHIFT)
#define NEBULA_VOLT_SW_BVT          1

/* EMMC IO register offset */
#define EMMC_CLK_GPIO_OFS           0x34
#define EMMC_CMD_GPIO_OFS           0x4c
#define EMMC_RSTN_GPIO_OFS          0x28
#define EMMC_DQS_GPIO_OFS           0x48
#define EMMC_D0_GPIO_OFS            0x2c
#define EMMC_D1_GPIO_OFS            0x38
#define EMMC_D2_GPIO_OFS            0x30
#define EMMC_D3_GPIO_OFS            0x50
#define EMMC_D4_GPIO_OFS            0x44
#define EMMC_D5_GPIO_OFS            0x54
#define EMMC_D6_GPIO_OFS            0x3c
#define EMMC_D7_GPIO_OFS            0x40

/* EMMC QFN IO register offset */
#define EMMC_CLK_QFN_GPIO_OFS           0x34
#define EMMC_CMD_QFN_GPIO_OFS           0x38
#define EMMC_RSTN_QFN_GPIO_OFS          0x28
#define EMMC_D0_QFN_GPIO_OFS            0x30
#define EMMC_D1_QFN_GPIO_OFS            0x2c
#define EMMC_D2_QFN_GPIO_OFS            0x40
#define EMMC_D3_QFN_GPIO_OFS            0x3c

#define EMMC_BUS_WIDTH_PHY_ADDR     0x11020018
#define EMMC_QUICK_BOOT_PHY_ADDR    0x11020138
#define EMMC_QUICK_BOOT_PARAM1_OFS  0x14

/* SDIO0 IO register offset */
#define SDIO0_DETECT_OFS            0x28
#define SDIO0_PWEN_OFS              0x44
#define SDIO0_CLK_OFS               0x34
#define SDIO0_CMD_OFS               0x38
#define SDIO0_D0_OFS                0x30
#define SDIO0_D1_OFS                0x2c
#define SDIO0_D2_OFS                0x40
#define SDIO0_D3_OFS                0x3c

/* QFN or BGA mode */
#define EMMC_OTP_STAT               0x101E001C
#define EMMC_INPUT_SEL              BIT(17)

/* SDIO1 IO register offset */
#define SDIO1_DETECT_OFS            0x50
#define SDIO1_PWEN_OFS              0x54
#define SDIO1_CLK_OFS               0x44
#define SDIO1_CMD_OFS               0x40
#define SDIO1_D0_OFS                0x48
#define SDIO1_D1_OFS                0x4c
#define SDIO1_D2_OFS                0x38
#define SDIO1_D3_OFS                0x3c

#include "sdhci_nebula.h"
#include "platform_priv.h"
#include "platform_timing.h"

static const nebula_crg_mask g_crg_mask = NEBULA_CRG_MASK_DESC;

/* EMMC fixed timing parameter */
static nebula_timing g_timing_gpio_emmc[] = {
	[MMC_TIMING_LEGACY] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		.timing[IO_TYPE_RST] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		fixed_drv_samp_phase(PHASE_LVL_16, PHASE_LVL_0),
	},
	[MMC_TIMING_MMC_HS] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_12, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		.timing[IO_TYPE_RST] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		fixed_drv_samp_phase(PHASE_LVL_20, PHASE_LVL_4),
	},
	[MMC_TIMING_MMC_HS200] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_9, SR_LVL_0),
		.timing[IO_TYPE_RST] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_9, SR_LVL_0),
		fixed_drv_phase_only(PHASE_LVL_20),
	},
	[MMC_TIMING_MMC_HS400] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_0, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_0, SR_LVL_0),
		.timing[IO_TYPE_RST] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_DQS] = fixed_gpio_drv(TM_LVL_0, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_0, SR_LVL_0),
		fixed_drv_phase_only(PHASE_LVL_8),
	},
};

static nebula_timing g_qfn_timing_gpio_emmc[] = {
	[MMC_TIMING_LEGACY] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		.timing[IO_TYPE_RST] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		fixed_drv_samp_phase(PHASE_LVL_16, PHASE_LVL_0),
	},
	[MMC_TIMING_MMC_HS] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		.timing[IO_TYPE_RST] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		fixed_drv_samp_phase(PHASE_LVL_16, PHASE_LVL_4),
	},
	[MMC_TIMING_MMC_HS200] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_0, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_7, SR_LVL_0),
		.timing[IO_TYPE_RST] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_7, SR_LVL_0),
		fixed_drv_phase_only(PHASE_LVL_20),
	},
};

/* EMMC info struct */
static nebula_info g_gpio_emmc_info = \
	nebula_emmc_info_desc(g_timing_gpio_emmc);

static nebula_info g_qfn_gpio_emmc_info = \
	nebula_emmc_info_desc(g_qfn_timing_gpio_emmc);

static u32 g_qfn_emmc_io_offset[IO_TYPE_DMAX] = {
	EMMC_CLK_QFN_GPIO_OFS, EMMC_CMD_QFN_GPIO_OFS, \
	EMMC_RSTN_QFN_GPIO_OFS, 0, \
	EMMC_D0_QFN_GPIO_OFS, EMMC_D1_QFN_GPIO_OFS, \
	EMMC_D2_QFN_GPIO_OFS, EMMC_D3_QFN_GPIO_OFS,
};

/* SDIO0 fixed timing parameter */
static nebula_timing g_timing_gpio_sdio0[] = {
	[MMC_TIMING_LEGACY] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_12, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		fixed_drv_samp_phase(PHASE_LVL_16, PHASE_LVL_0),
	},
	[MMC_TIMING_SD_HS] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_10, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_14, SR_LVL_0),
		fixed_drv_samp_phase(PHASE_LVL_20, PHASE_LVL_4),
	},
};

/* SDIO0 info struct */
static const nebula_info g_gpio_sdio0_info = \
	nebula_sdio0_info_desc(g_timing_gpio_sdio0);

/* SDIO1 fixed timing parameter */
static nebula_timing g_timing_gpio_sdio1[] = {
	[MMC_TIMING_LEGACY] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		fixed_drv_samp_phase(PHASE_LVL_16, PHASE_LVL_0),
	},
	[MMC_TIMING_SD_HS] = {
		.data_valid = true,
		.timing[IO_TYPE_CLK] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_CMD] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		.timing[IO_TYPE_DATA] = fixed_gpio_drv(TM_LVL_3, SR_LVL_0),
		fixed_drv_samp_phase(PHASE_LVL_24, PHASE_LVL_4),
	},
};

/* SDIO1 info struct */
static const nebula_info g_gpio_sdio1_info = \
	nebula_sdio1_info_desc(g_timing_gpio_sdio1);

static bool priv_is_bga_package(struct sdhci_host *host)
{
	void __iomem *sys_stat_reg;
	static unsigned int sys_stat;
	static bool sys_stat_init = false;

	if (sys_stat_init)
		return ((sys_stat & EMMC_INPUT_SEL) == EMMC_INPUT_SEL);

	sys_stat_reg = ioremap(EMMC_OTP_STAT, sizeof(u32));
	if (sys_stat_reg == NULL) {
		pr_err("%s: mmc otp ioremap error.\n", mmc_hostname(host->mmc));
		return -ENOMEM;
	}

	sys_stat = readl(sys_stat_reg);
	iounmap(sys_stat_reg);

	sys_stat_init = true;

	return ((sys_stat & EMMC_INPUT_SEL) == EMMC_INPUT_SEL);
}

int plat_host_pre_init(struct platform_device *pdev, struct sdhci_host *host)
{
	struct sdhci_nebula *nebula = nebula_priv(host);

	nebula->mask = &g_crg_mask;
	if (nebula->devid == MMC_DEV_TYPE_MMC_0) {
		if (priv_is_bga_package(host)) {
			nebula->info = &g_gpio_emmc_info;
		} else {
			nebula->info = &g_qfn_gpio_emmc_info;
		}
	} else if (nebula->devid == MMC_DEV_TYPE_SDIO_0) {
		nebula->info = &g_gpio_sdio0_info;
	} else if (nebula->devid == MMC_DEV_TYPE_SDIO_1) {
		nebula->info = &g_gpio_sdio1_info;
	} else {
		pr_err("error: invalid device\n");
		return -EINVAL;
	}

	return ERET_SUCCESS;
}

void plat_extra_init(struct sdhci_host *host)
{
	u32 ctrl;
	struct sdhci_nebula *nebula = nebula_priv(host);

	ctrl = sdhci_readl(host, SDHCI_AXI_MBIU_CTRL);
	ctrl &= ~SDHCI_UNDEFL_INCR_EN;
	sdhci_writel(host, ctrl, SDHCI_AXI_MBIU_CTRL);

	if (nebula->devid == MMC_DEV_TYPE_MMC_0) {
		plat_set_emmc_type(host);
	}
}

static void priv_plat_mux_init(struct sdhci_host *host)
{
	u32 i, pin_mux_val;
	u32 bus_width = 1;
	struct sdhci_nebula *nebula = nebula_priv(host);
	const nebula_info *info = nebula->info;

	if (nebula->priv_quirk & NEBULA_QUIRK_FPGA)
		return;

	if (host->mmc->caps & MMC_CAP_8_BIT_DATA) {
		bus_width = (1 << MMC_BUS_WIDTH_8);
	} else if (host->mmc->caps & MMC_CAP_4_BIT_DATA) {
		bus_width = (1 << MMC_BUS_WIDTH_4);
	}

	if (nebula->devid == MMC_DEV_TYPE_MMC_0) {
		regmap_write(nebula->iocfg_regmap, info->io_offset[IO_TYPE_CLK], \
			0x12B1); /* 0x12B1: pinmux value */
		regmap_write(nebula->iocfg_regmap, info->io_offset[IO_TYPE_CMD], \
			0x11C1); /* 0x11C1: pinmux value */
		pin_mux_val = 0x11C1; /* 0x11C1: pinmux value */
		if (priv_is_bga_package(host)) {
			regmap_write(nebula->iocfg_regmap, info->io_offset[IO_TYPE_CMD], \
				0x11C2); /* 0x11C2: pinmux value */
			pin_mux_val = 0x11C2; /* 0x11C2: pinmux value */
		}
		for (i = IO_TYPE_D0; i < (IO_TYPE_D0 + bus_width); i++)
			regmap_write(nebula->iocfg_regmap, info->io_offset[i], pin_mux_val);
		regmap_write(nebula->iocfg_regmap, info->io_offset[IO_TYPE_RST], \
			0x1132); /* 0x1132: pinmux value */
		if (host->mmc->caps & MMC_CAP_8_BIT_DATA)
			regmap_write(nebula->iocfg_regmap, info->io_offset[IO_TYPE_DQS], \
				0x12C2); /* 0x12C2: pinmux value */
	} else if (nebula->devid == MMC_DEV_TYPE_SDIO_0 || \
		nebula->devid == MMC_DEV_TYPE_SDIO_1) {
		if ((host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION) == 0)
			regmap_write(nebula->iocfg_regmap, info->io_offset[IO_TYPE_DET], \
				0x1131); /* 0x1131: pinmux value */
		regmap_write(nebula->iocfg_regmap, info->io_offset[IO_TYPE_PWE], \
			(nebula->devid == MMC_DEV_TYPE_SDIO_0) ? 0x11F1 : 0x1131);
		regmap_write(nebula->iocfg_regmap, info->io_offset[IO_TYPE_CLK], \
			0x12F1); /* 0x12F1: pinmux value */
		regmap_write(nebula->iocfg_regmap, info->io_offset[IO_TYPE_CMD], \
			0x11F1); /* 0x11F1: pinmux value */
		for (i = IO_TYPE_D0; i < (IO_TYPE_D0 + bus_width); i++)
			regmap_write(nebula->iocfg_regmap, info->io_offset[i], \
				0x11F1); /* 0x11F1: pinmux value */
	}
}

static void plat_set_qfn_parameters(struct sdhci_host *host)
{
	int idx;

	if (priv_is_bga_package(host))
		return;

	for (idx = 0; idx < IO_TYPE_DMAX; idx++)
		g_qfn_gpio_emmc_info.io_offset[idx] = g_qfn_emmc_io_offset[idx];

	/* only 4bits width */
	host->mmc->caps &= ~MMC_CAP_8_BIT_DATA;
}

void plat_caps_quirks_init(struct sdhci_host *host)
{
	struct sdhci_nebula *nebula = nebula_priv(host);

	plat_comm_caps_quirks_init(host);

	plat_set_mmc_bus_width(host);

	if (nebula->devid == MMC_DEV_TYPE_MMC_0)
		plat_set_qfn_parameters(host);

	if ((nebula->priv_cap & NEBULA_CAP_QUICK_BOOT) == 0)
		priv_plat_mux_init(host);
}
