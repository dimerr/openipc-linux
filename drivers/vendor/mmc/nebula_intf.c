/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */

#include <linux/version.h>
#include <linux/types.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include "core.h"
#include "sdhci_nebula.h"
#include "nebula_intf.h"

/*
 * This api is for wifi driver rescan the sdio device
 */
int bsp_sdio_rescan(int slot)
{
	struct mmc_host *mmc = NULL;

	if ((slot >= MCI_SLOT_NUM) || (slot < 0)) {
		pr_err("invalid mmc slot, please check the argument\n");
		return -EINVAL;
	}

	mmc = g_mci_host[slot];
	if (mmc == NULL) {
		pr_err("invalid mmc, please check the argument\n");
		return -EINVAL;
	}

	mmc->rescan_entered = 0;
	mmc_detect_change(mmc, 0);
	return 0;
}
EXPORT_SYMBOL_GPL(bsp_sdio_rescan);

int hl_drv_sdio_rescan(int index)
{
	struct mmc_host *mmc = NULL;
	int i;

	if ((index < 0) || (index >= MCI_SLOT_NUM)) {
		pr_err("invalid mmc_host index for sdio %d\n", index);
		return -EINVAL;
	}

	for (i = MMC_DEV_TYPE_SDIO_0; i <= MMC_DEV_TYPE_SDIO_1; i++) {
		mmc = g_mmc_host[i];

		if ((mmc == NULL) || (mmc->card != NULL)) {
			continue;
		}
		printk("Trigger sdio%d scanning card successfully\n", i);
		mmc_detect_change(mmc, 0);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hl_drv_sdio_rescan);

int sdhci_nebula_sdio_rescan(int index)
{
	int ret = 0;
	struct mmc_host *mmc = NULL;

	if ((index < 0) || (index >= MCI_SLOT_NUM)) {
		pr_err("invalid mmc_host index for sdio %d\n", index);
		return -EINVAL;
	}

	mmc = g_mmc_host[index];
	if (mmc == NULL) {
		pr_err("sdio %d not init\n", index);
		return -EINVAL;
	}

	pr_info("Trigger sdio%d rescan\n", index);
	if (mmc->card == NULL) {
		mmc->rescan_entered = 0;
		mmc_detect_change(mmc, 0);
	} else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
		mmc_claim_host(mmc);
		ret = mmc_hw_reset(mmc);
		mmc_release_host(mmc);
#else
		ret = mmc_hw_reset(mmc);
#endif
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sdhci_nebula_sdio_rescan);
