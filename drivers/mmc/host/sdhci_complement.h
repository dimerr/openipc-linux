/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2024. All rights reserved.
 */

#ifndef MMC_SDHCI_COMPLEMENT_H
#define MMC_SDHCI_COMPLEMENT_H

#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/types.h>

#define CMD_ERRORS                          \
	(R1_OUT_OF_RANGE |  /* Command argument out of range */ \
	R1_ADDRESS_ERROR | /* Misaligned address */        \
	R1_BLOCK_LEN_ERROR |   /* Transferred block length incorrect */\
	R1_WP_VIOLATION |  /* Tried to write to protected block */ \
	R1_CC_ERROR |      /* Card controller error */     \
	R1_ERROR)      /* General/unknown error */
 
struct card_info {
	unsigned int     card_type;
	unsigned char    timing;
	bool             enhanced_strobe;
	unsigned char    card_connect;
#define CARD_CONNECT    1
#define CARD_DISCONNECT 0
	unsigned int     card_support_clock; /* clock rate */
	unsigned int     card_state;         /* (our) card state */
	unsigned int     sd_bus_speed;
	unsigned int     ssr[16];
};

struct mmc_host;
struct sdhci_host;
struct mmc_command;
int sdhci_card_info_save(struct mmc_host *mmc);
void sdhci_check_card_resp(struct sdhci_host *host, struct mmc_command *cmd);
void sdhci_card_clk_enable(struct sdhci_host *host);

#endif /* MMC_SDHCI_COMPLEMENT_H */
