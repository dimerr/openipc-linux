/*
 * Copyright (c) CompanyNameMagicTag 2020-2021. All rights reserved.
 */
#ifndef HLETH_DBG_H
#define HLETH_DBG_H

#include "hleth.h"

struct regdef_s {
	char *fmt;
	u32 offset;
};
int hleth_dbg_init(void __iomem *base, struct platform_device *pdev);
int hleth_dbg_deinit(struct platform_device *pdev);
int multicast_dump_netdev_flags(u32 flags, struct hleth_platdrv_data *pdata);
void multicast_dump_macaddr(u32 nr, char *macaddr, struct hleth_platdrv_data *pdata);

#endif
