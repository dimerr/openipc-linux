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

#ifndef __ETH_UTIL_H__
#define __ETH_UTIL_H__

#include "bsp_femac.h"

int bsp_femac_check_hw_capability(struct sk_buff *skb);
u32 bsp_femac_get_pkt_info(struct sk_buff *skb);
void bsp_femac_sleep_us(u32 time_us);
void bsp_femac_set_flow_ctrl(const struct bsp_femac_priv *priv);
void bsp_femac_get_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *pause);
int bsp_femac_set_pauseparam(struct net_device *dev,
			      struct ethtool_pauseparam *pause);
void bsp_femac_enable_rxcsum_drop(const struct bsp_femac_priv *priv,
				   bool drop);
int bsp_femac_set_features(struct net_device *dev, netdev_features_t features);

#endif