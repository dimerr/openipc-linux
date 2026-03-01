/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2024. All rights reserved.
 */

#include <linux/securec.h>
#include <linux/printk.h>

#include "sdhci_complement.h"
#include "sdhci.h"

int sdhci_card_info_save(struct mmc_host *mmc)
{
	struct mmc_card *card = mmc->card;
	struct sdhci_host *host = mmc_priv(mmc);
	struct card_info *c_info = &host->c_info;
	int ret;

	if (card == NULL) {
		ret = memset_s(c_info, sizeof(struct card_info), 0, sizeof(struct card_info));
		if (ret != EOK)
			pr_err("memset_s c_info failed\n");
		c_info->card_connect = CARD_DISCONNECT;
		goto out;
	}

	c_info->card_type = card->type;
	c_info->card_state = card->state;

	c_info->timing = mmc->ios.timing;
	c_info->enhanced_strobe = mmc->ios.enhanced_strobe;
	c_info->card_support_clock = mmc->ios.clock;

	c_info->sd_bus_speed = card->sd_bus_speed;

	ret = memcpy_s(c_info->ssr, sizeof(c_info->ssr), card->raw_ssr, 64); /* SSR length: 512bit / 8 = 64 byte */
	if (ret != EOK) {
		pr_err("SD Status Reg memcpy_s failed\n");
		return ret;
	}

	c_info->card_connect = CARD_CONNECT;
out:
	return 0;
}
EXPORT_SYMBOL_GPL(sdhci_card_info_save);

void sdhci_check_card_resp(struct sdhci_host *host, struct mmc_command *cmd)
{
	if (((cmd->flags & MMC_RSP_R1) == MMC_RSP_R1) &&
		((cmd->flags & MMC_CMD_MASK) != MMC_CMD_BCR)) {
		if ((cmd->resp[0] & CMD_ERRORS) && !host->is_tuning &&
			(host->error_count < S32_MAX)) {
			host->error_count++;
			cmd->mrq->cmd->error = -EACCES;
			pr_err("The status of the card is abnormal, cmd->resp[0]: %x", cmd->resp[0]);
		}
	}
}
EXPORT_SYMBOL_GPL(sdhci_check_card_resp);

void sdhci_card_clk_enable(struct sdhci_host *host)
{
	u16 clk;
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
}
EXPORT_SYMBOL_GPL(sdhci_card_clk_enable);

MODULE_LICENSE("GPL");
