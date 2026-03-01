/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2024. All rights reserved.
 */

#ifndef MMC_CORE_CMPLMNT_H
#define MMC_CORE_CMPLMNT_H

#include <linux/mmc/host.h>

inline void mmc_set_card_status_uninit(struct mmc_host *host)
{
	host->card_status = MMC_CARD_UNINIT;
}
inline void mmc_set_card_status_init(struct mmc_host *host)
{
	host->card_status = MMC_CARD_INIT;
}
inline void mmc_set_card_status_init_fail(struct mmc_host *host)
{
	host->card_status = MMC_CARD_INIT_FAIL;
}
inline void mmc_card_info_save(struct mmc_host *host)
{
	if (host->ops->card_info_save)
		host->ops->card_info_save(host);
}

#endif /* MMC_CORE_CMPLMNT_H */

