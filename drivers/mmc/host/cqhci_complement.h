/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2024. All rights reserved.
 */

#ifndef MMC_CQHCI_CMPLMNT_H
#define MMC_CQHCI_CMPLMNT_H

#include "cqhci.h"

inline dma_addr_t cqe_get_trans_desc_dma(struct cqhci_host *cq_host, u8 tag)
{
	return cq_host->trans_desc_dma_base + (cq_host->mmc->max_segs * tag *
		2 * cq_host->trans_desc_len); /* 2 is double segs size */
}

inline u8 *cqe_get_trans_desc(struct cqhci_host *cq_host, u8 tag)
{
	return cq_host->trans_desc_base + (cq_host->trans_desc_len *
		cq_host->mmc->max_segs * 2 * tag); /* 2 is double segs size */
}

inline size_t cqe_cal_data_size(struct cqhci_host *cq_host)
{
	if (cq_host->quirks & CQHCI_QUIRK_TXFR_DESC_SZ_SPLIT)
		return cq_host->trans_desc_len * cq_host->mmc->max_segs *
			2 * cq_host->mmc->cqe_qdepth; /* 2 is double segs size */
	else
		return cq_host->trans_desc_len * cq_host->mmc->max_segs *
			cq_host->mmc->cqe_qdepth;
}

#endif /* MMC_CQHCI_CMPLMNT_H */

