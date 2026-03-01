/*
 * Copyright (c) CompanyNameMagicTag 2022. All rights reserved.
 * Description: Main driver functions.
 * Author: AuthorNameMagicTag
 * Create: 2022-4-20
 */
#include <securec.h>
#include <net/ipv6.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/clk.h>
#include <linux/circ_buf.h>
#include <linux/netdevice.h>
#include <linux/version.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>

#include "mdio.h"
#include "hleth_dbg.h"
#include "hleth.h"

/* default: softirq recv packets, disable hardirq recv */

/* default, eth enable */
static bool hleth_disable;

static void hleth_tx_timeout_task(struct work_struct *ws);

#ifdef MODULE
module_param(hleth_disable, bool, 0);
#else
static int __init hleth_noeth(char *str)
{
	hleth_disable = true;

	return 0;
}

early_param("noeth", hleth_noeth);
#endif

#include "pm.c"

static int hleth_hw_set_macaddress(const struct hleth_netdev_priv *priv,
				   const unsigned char *mac)
{
	u32 reg;

	if (priv->port == HLETH_PORT_1) {
		reg = hleth_readl(priv->glb_base, HLETH_GLB_DN_HOSTMAC_ENA);
		reg |= HLETH_GLB_DN_HOSTMAC_ENA_BIT;
		hleth_writel(priv->glb_base, reg, HLETH_GLB_DN_HOSTMAC_ENA);
	}

	reg = mac[1] | (mac[0] << 8); /* 8:left shift val */
	if (priv->port == HLETH_PORT_0)
		hleth_writel(priv->glb_base, reg, HLETH_GLB_HOSTMAC_H16);
	else
		hleth_writel(priv->glb_base, reg, HLETH_GLB_DN_HOSTMAC_H16);

	reg = mac[5] | (mac[4] << 8) | (mac[3] << 16) | (mac[2] << 24); /* 2,3,4,5,8,16,24:left shift val */
	if (priv->port == HLETH_PORT_0)
		hleth_writel(priv->glb_base, reg, HLETH_GLB_HOSTMAC_L32);
	else
		hleth_writel(priv->glb_base, reg, HLETH_GLB_DN_HOSTMAC_L32);

	return 0;
}

static void hleth_irq_enable(struct hleth_netdev_priv *priv, int irqs)
{
	u32 val;

	local_lock(priv);
	val = hleth_readl(priv->glb_base, HLETH_GLB_IRQ_ENA);
	hleth_writel(priv->glb_base, val | (u32)irqs, HLETH_GLB_IRQ_ENA);
	local_unlock(priv);
}

static void hleth_irq_disable(struct hleth_netdev_priv *priv, int irqs)
{
	u32 val;

	local_lock(priv);
	val = hleth_readl(priv->glb_base, HLETH_GLB_IRQ_ENA);
	hleth_writel(priv->glb_base, val & (u32)(~irqs), HLETH_GLB_IRQ_ENA);
	local_unlock(priv);
}

static void hleth_clear_irqstatus(struct hleth_netdev_priv *priv, int irqs)
{
	local_lock(priv);
	hleth_writel(priv->glb_base, irqs, HLETH_GLB_IRQ_RAW);
	local_unlock(priv);
}

static int hleth_port_reset(const struct hleth_netdev_priv *priv)
{
	struct hleth_platdrv_data *pdata = dev_get_drvdata(priv->dev);
	u32 rst_bit = 0;
	u32 val;

	if (pdata->hleth_real_port_cnt == 1) {
		rst_bit = HLETH_GLB_SOFT_RESET_ALL;
	} else {
		if (priv->port == HLETH_PORT_0) {
			rst_bit |= HLETH_GLB_SOFT_RESET_P0;
		} else if (priv->port == HLETH_PORT_1) {
			rst_bit |= HLETH_GLB_SOFT_RESET_P1;
		}
	}

	val = hleth_readl(priv->glb_base, HLETH_GLB_SOFT_RESET);

	val |= rst_bit;
	hleth_writel(priv->glb_base, val, HLETH_GLB_SOFT_RESET);
	usleep_range(1000, 10000); /* 1000,10000:delay zone */
	val &= ~rst_bit;
	hleth_writel(priv->glb_base, val, HLETH_GLB_SOFT_RESET);
	usleep_range(1000, 10000); /* 1000,10000:delay zone */
	val |= rst_bit;
	hleth_writel(priv->glb_base, val, HLETH_GLB_SOFT_RESET);
	usleep_range(1000, 10000); /* 1000,10000:delay zone */
	val &= ~rst_bit;
	hleth_writel(priv->glb_base, val, HLETH_GLB_SOFT_RESET);

	return 0;
}

static void hleth_set_flow_ctrl(struct hleth_netdev_priv *priv, bool enable)
{
	unsigned int pause_en;
	unsigned int tx_flow_ctrl;

	tx_flow_ctrl = hleth_readl(priv->port_base, HLETH_P_GLB_FC_LEVEL);
	tx_flow_ctrl &= ~HLETH_P_GLB_FC_DEACTIVE_THR_MASK;
	tx_flow_ctrl |= priv->tx_pause_deactive_thresh;
	tx_flow_ctrl &= ~HLETH_P_GLB_FC_ACTIVE_THR_MASK;
	tx_flow_ctrl |= priv->tx_pause_active_thresh << BITS_FC_ACTIVE_THR_OFFSET;

	pause_en = hleth_readl(priv->port_base, HLETH_P_MAC_SET);

	if (priv->tx_pause_en && enable) {
		tx_flow_ctrl |= BIT_FC_EN;
		pause_en |= BIT_PAUSE_EN;
	} else {
		tx_flow_ctrl &= ~BIT_FC_EN;
		pause_en &= ~BIT_PAUSE_EN;
	}

	hleth_writel(priv->port_base, tx_flow_ctrl, HLETH_P_GLB_FC_LEVEL);

	hleth_writel(priv->port_base, pause_en, HLETH_P_MAC_SET);
}

static void hleth_get_pauseparam(struct net_device *dev, struct ethtool_pauseparam *pause)
{
	struct hleth_netdev_priv *priv = netdev_priv(dev);

	pause->autoneg = dev->phydev->autoneg;
	pause->rx_pause = 1;
	if (priv->tx_pause_en)
		pause->tx_pause = 1;
}

static int hleth_set_pauseparam(struct net_device *dev, struct ethtool_pauseparam *pause)
{
	struct hleth_netdev_priv *priv = netdev_priv(dev);
	struct phy_device *phy = NULL;

	phy = dev->phydev;
	if (pause->tx_pause != priv->tx_pause_en) {
		priv->tx_pause_en = pause->tx_pause;
		hleth_set_flow_ctrl(priv, priv->tx_pause_en);

		linkmode_clear_bit(ETHTOOL_LINK_MODE_Pause_BIT, phy->advertising);
		if (priv->tx_pause_en)
			linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT, phy->advertising);
		if ((phy->autoneg != 0) && netif_running(dev)) {
			return phy_start_aneg(phy);
		}
	}

	return 0;
}

static void hleth_port_init(struct hleth_netdev_priv *priv)
{
	u32 val;
	unsigned long phy_intf = (priv->phy_mode == PHY_INTERFACE_MODE_MII ?
			HLETH_P_MAC_PORTSEL_MII : HLETH_P_MAC_PORTSEL_RMII);

	/* set little endian */
	val = hleth_readl(priv->glb_base, HLETH_GLB_ENDIAN_MOD);
	val |= HLETH_GLB_ENDIAN_MOD_IN;
	val |= HLETH_GLB_ENDIAN_MOD_OUT;
	hleth_writel(priv->glb_base, val, HLETH_GLB_ENDIAN_MOD);

	/* set stat ctrl to cpuset, and MII or RMII mode */
	hleth_writel(priv->port_base, phy_intf | HLETH_P_MAC_PORTSEL_STAT_CPU,
		     HLETH_P_MAC_PORTSEL);

	/* clear all interrupt status */
	hleth_clear_irqstatus(priv, ud_bit_name(HLETH_GLB_IRQ_ENA_BIT));

	/* disable interrupts */
	hleth_irq_disable(priv, ud_bit_name(HLETH_GLB_IRQ_ENA_BIT) |
			  ud_bit_name(HLETH_GLB_IRQ_ENA_IEN));

	if (has_tso_cap(priv->hw_cap)) {
		/* enable TSO debug for error handle */
		val = readl(priv->port_base + HLETH_P_TSO_DBG_EN);
		val |= BITS_TSO_DBG_EN;
		writel(val, priv->port_base + HLETH_P_TSO_DBG_EN);
	}

	/* disable vlan, enable UpEther<->CPU */
	val = hleth_readl(priv->glb_base, HLETH_GLB_FWCTRL);
	val &= ~HLETH_GLB_FWCTRL_VLAN_ENABLE;
	val |= ud_bit_name(HLETH_GLB_FWCTRL_FW2CPU_ENA);
	val &= ~(ud_bit_name(HLETH_GLB_FWCTRL_FWALL2CPU));
	hleth_writel(priv->glb_base, val, HLETH_GLB_FWCTRL);
	val = hleth_readl(priv->glb_base, HLETH_GLB_MACTCTRL);
	val |= ud_bit_name(HLETH_GLB_MACTCTRL_BROAD2CPU);
	val |= ud_bit_name(HLETH_GLB_MACTCTRL_MACT_ENA);
	hleth_writel(priv->glb_base, val, HLETH_GLB_MACTCTRL);

	/* set pre count limit */
	val = hleth_readl(priv->port_base, HLETH_P_MAC_TX_IPGCTRL);
	val &= ~HLETH_P_MAC_TX_IPGCTRL_PRE_CNT_LMT_MSK;
	val |= 0;
	hleth_writel(priv->port_base, val, HLETH_P_MAC_TX_IPGCTRL);

	/* set max receive length */
	val = hleth_readl(priv->port_base, HLETH_P_MAC_SET);
	val &= ~HLETH_P_MAC_SET_LEN_MAX_MSK;
	val |= hleth_p_mac_set_len_max(HLETH_MAX_RCV_LEN);
	hleth_writel(priv->port_base, val, HLETH_P_MAC_SET);

	/* config Rx Checksum Offload,
	 * disable TCP/UDP payload checksum bad drop
	 */
	val = hleth_readl(priv->port_base, HLETH_P_RX_COE_CTRL);
	val &= ~BIT_COE_PAYLOAD_DROP;
	hleth_writel(priv->port_base, val, HLETH_P_RX_COE_CTRL);

	hleth_set_flow_ctrl(priv, priv->tx_pause_en);
}

static void hleth_set_hwq_depth(const struct hleth_netdev_priv *priv)
{
	u32 val;

	val = hleth_readl(priv->port_base, HLETH_P_GLB_QLEN_SET);
	val &= ~HLETH_P_GLB_QLEN_SET_TXQ_DEP_MSK;
	val |= hleth_p_glb_qlen_set_txq_dep((unsigned int)(priv->depth.hw_xmitq));
	val &= ~HLETH_P_GLB_QLEN_SET_RXQ_DEP_MSK;
	val |= hleth_p_glb_qlen_set_rxq_dep((unsigned int)(HLETH_MAX_QUEUE_DEPTH -
					    priv->depth.hw_xmitq));
	hleth_writel(priv->port_base, val, HLETH_P_GLB_QLEN_SET);
}

static inline unsigned int hleth_hw_xmitq_ready(const struct hleth_netdev_priv *priv)
{
	unsigned int ret;

	if (priv == NULL)
		return 0;

	ret = hleth_readl(priv->port_base, HLETH_P_GLB_RO_QUEUE_STAT);
	ret &= HLETH_P_GLB_RO_QUEUE_STAT_XMITQ_RDY_MSK;

	return ret;
}

static void hleth_tx_sg_dma_unmap(struct hleth_netdev_priv *priv,
				struct sk_buff *skb, unsigned int pos)
{
	struct sg_desc *desc_cur;
	dma_addr_t addr;
	u32 len;
	int i;

	desc_cur = priv->sg_desc_queue.desc + pos;

	addr = desc_cur->linear_addr;
	len = desc_cur->linear_len;
	dma_unmap_single(priv->dev, addr, len, DMA_TO_DEVICE);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		addr = desc_cur->frags[i].addr;
		len = desc_cur->frags[i].size;
		dma_unmap_page(priv->dev, addr, len, DMA_TO_DEVICE);
	}
}

static void hleth_tx_dma_unmap(struct hleth_netdev_priv *priv,
				struct sk_buff *skb, unsigned int pos)
{
	dma_addr_t dma_addr;

	if (!(skb_is_gso(skb) || (skb_shinfo(skb)->nr_frags != 0))) {
		dma_addr = priv->txq.dma_phys[pos];
		dma_unmap_single(priv->dev, dma_addr, skb->len, DMA_TO_DEVICE);
	} else {
		hleth_tx_sg_dma_unmap(priv, skb, pos);
	}
}

static int hleth_xmit_release_skb(struct hleth_netdev_priv *priv)
{
	struct hleth_queue *txq = &priv->txq;
	u32 val;
	int ret = 0;
	struct sk_buff *skb = NULL;
	u32 tx_comp = 0;
	struct net_device *ndev = priv->ndev;

	local_lock(priv);

	val = hleth_readl(priv->port_base, HLETH_P_GLB_RO_QUEUE_STAT) &
			  HLETH_P_GLB_RO_QUEUE_STAT_XMITQ_CNT_INUSE_MSK;
	while (val < priv->tx_fifo_used_cnt) {
		skb = txq->skb[txq->tail];

		if (skb == NULL) {
			pr_err("hw_xmitq_cnt_inuse=%d, tx_fifo_used_cnt=%d\n",
			       val, priv->tx_fifo_used_cnt);
			ret = -1;
			goto error_exit;
		}
#ifdef HLETH_SKB_MEMORY_STATS
		atomic_dec(&priv->tx_skb_occupied);
		atomic_sub(skb->truesize, &priv->tx_skb_mem_occupied);
#endif
		hleth_tx_dma_unmap(priv, skb, txq->tail);
		dev_kfree_skb_any(skb);

		priv->tx_fifo_used_cnt--;
		tx_comp++;

		val = hleth_readl(priv->port_base, HLETH_P_GLB_RO_QUEUE_STAT) &
				HLETH_P_GLB_RO_QUEUE_STAT_XMITQ_CNT_INUSE_MSK;
		txq->skb[txq->tail] = NULL;
		txq->tail = (txq->tail + 1) % txq->num;
	}

	/*
	 * In some cases, when hw_xmitq is not ready, the netif queue stops
	 * even if txq is already empty. After this, when hw_xmitq is ready,
	 * the netif queue should be woken up.
	 */
	if ((tx_comp != 0) ||
	    ((priv->tx_fifo_used_cnt == 0) && (hleth_hw_xmitq_ready(priv) != 0) && netif_queue_stopped(ndev))) {
		netif_wake_queue(ndev);
	}

error_exit:
	local_unlock(priv);
	return ret;
}

#ifdef CONFIG_HLETH_MAX_RX_POOLS
static __maybe_unused struct sk_buff *hleth_platdev_alloc_skb(struct hleth_netdev_priv *priv)
{
	struct sk_buff *skb;
	int i;
	int ret;

	skb = priv->rx_pool.sk_pool[priv->rx_pool.next_free_skb++];

	if (priv->rx_pool.next_free_skb == CONFIG_HLETH_MAX_RX_POOLS)
		priv->rx_pool.next_free_skb = 0;

	/* current skb is used by kernel or other process,find another skb */
	if (skb_shared(skb) || (atomic_read(&(skb_shinfo(skb)->dataref)) > 1)) {
		for (i = 0; i < CONFIG_HLETH_MAX_RX_POOLS; i++) {
			skb = priv->rx_pool.sk_pool[priv->
						    rx_pool.next_free_skb++];
			if (priv->rx_pool.next_free_skb ==
			    CONFIG_HLETH_MAX_RX_POOLS)
				priv->rx_pool.next_free_skb = 0;

			if ((skb_shared(skb) == 0) &&
			    (atomic_read(&(skb_shinfo(skb)->dataref)) <= 1))
				break;
		}

		if (i == CONFIG_HLETH_MAX_RX_POOLS) {
			priv->stat.rx_pool_dry_times++;
			pr_debug("%ld: no free skb\n",
				    priv->stat.rx_pool_dry_times);
			skb = netdev_alloc_skb_ip_align(priv->ndev, SKB_SIZE);
			return skb;
		}
	}
	ret = memset_s(skb, sizeof(struct sk_buff), 0, offsetof(struct sk_buff, tail));
	if (ret != EOK) {
		pr_err("memset skb error ret=%d\n", ret);
		return NULL;
	}

	skb->data = skb->head;
	skb_reset_tail_pointer(skb);
	WARN(skb->end != (skb->tail + SKB_DATA_ALIGN(SKB_SIZE + NET_IP_ALIGN + NET_SKB_PAD)),
	     "head=%p, tail=%x, end=%x\n", skb->head, (unsigned int)skb->tail,
	     (unsigned int)skb->end);
	skb->end = skb->tail + SKB_DATA_ALIGN(SKB_SIZE + NET_IP_ALIGN + NET_SKB_PAD);

	skb_reserve(skb, NET_IP_ALIGN + NET_SKB_PAD);
	skb->len = 0;
	skb->data_len = 0;
	skb->cloned = 0;
	skb->dev = priv->ndev;
	atomic_inc(&skb->users.refs);
	return skb;
}
#endif

static int hleth_feed_hw(struct hleth_netdev_priv *priv)
{
	struct hleth_queue *rxq = &priv->rxq;
	struct sk_buff *skb = NULL;
	dma_addr_t addr;
	int cnt = 0;
	u32 rx_head_len;
	u32 pos;

	/* if skb occupied too much, then do not alloc any more. */
	rx_head_len = skb_queue_len(&priv->rx_head);
	if (rx_head_len > HLETH_MAX_RX_HEAD_LEN)
		return 0;

	local_lock(priv);

	pos = rxq->head;
	while ((unsigned int)hleth_readl(priv->port_base, HLETH_P_GLB_RO_QUEUE_STAT) &
		HLETH_P_GLB_RO_QUEUE_STAT_RECVQ_RDY_MSK) {
		if (unlikely(CIRC_SPACE(pos, rxq->tail, (unsigned int)rxq->num) == 0))
			break;
		if (unlikely(rxq->skb[pos])) {
			netdev_err(priv->ndev, "err skb[%d]=%p\n",
					pos, rxq->skb[pos]);
			break;
		}

		skb = netdev_alloc_skb_ip_align(priv->ndev, HLETH_MAX_FRAME_SIZE);
		if (skb == NULL)
			break;

		addr = dma_map_single(priv->dev, skb->data, HLETH_MAX_FRAME_SIZE,
				DMA_FROM_DEVICE);
		if (dma_mapping_error(priv->dev, addr)) {
			dev_kfree_skb_any(skb);
			break;
		}
		rxq->dma_phys[pos] = addr;
		rxq->skb[pos] = skb;

		hleth_writel(priv->port_base, addr, HLETH_P_GLB_IQ_ADDR);
		pos = (pos + 1) % rxq->num;
		cnt++;

#ifdef HLETH_SKB_MEMORY_STATS
		atomic_inc(&priv->rx_skb_occupied);
		atomic_add(skb->truesize, &priv->rx_skb_mem_occupied);
#endif
	}
	rxq->head = pos;

	local_unlock(priv);
	return cnt;
}

static int hleth_skb_rxcsum(struct hleth_netdev_priv *priv, struct sk_buff *skb,
				 u32 rx_pkt_info)
{
	struct net_device *dev = priv->ndev;
	int hdr_csum_done, hdr_csum_err;
	int payload_csum_done, payload_csum_err;

	skb->ip_summed = CHECKSUM_NONE;
	if (dev->features & NETIF_F_RXCSUM) {
		hdr_csum_done =
			(rx_pkt_info >> BITS_HEADER_DONE_OFFSET) &
			BITS_HEADER_DONE_MASK;
		payload_csum_done =
			(rx_pkt_info >> BITS_PAYLOAD_DONE_OFFSET) &
			BITS_PAYLOAD_DONE_MASK;
		hdr_csum_err =
			(rx_pkt_info >> BITS_HEADER_ERR_OFFSET) &
			BITS_HEADER_ERR_MASK;
		payload_csum_err =
			(rx_pkt_info >> BITS_PAYLOAD_ERR_OFFSET) &
			BITS_PAYLOAD_ERR_MASK;

		if ((hdr_csum_done != 0) && (payload_csum_done != 0)) {
			if (unlikely(hdr_csum_err != 0)) {
				dev->stats.rx_errors++;
				dev->stats.rx_crc_errors++;
				dev_kfree_skb_any(skb);
				return -1;
			} else if (payload_csum_err == 0) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			}
		}
	}
	return 0;
}

static int hleth_recv_budget(struct hleth_netdev_priv *priv)
{
	struct hleth_queue *rxq = &priv->rxq;
	struct sk_buff *skb = NULL;
	dma_addr_t addr;
	u32 pos;
	u32 rlen;
	int cnt = 0;

	local_lock(priv);

	pos = rxq->tail;
	while ((hleth_readl(priv->glb_base, HLETH_GLB_IRQ_RAW) &
		(ud_bit_name(HLETH_GLB_IRQ_INT_RX_RDY)))) {
		rlen = hleth_readl(priv->port_base, HLETH_P_GLB_RO_IQFRM_DES);

		/* hw set rx pkg finish */
		hleth_writel(priv->glb_base,
			     ud_bit_name(HLETH_GLB_IRQ_INT_RX_RDY),
			     HLETH_GLB_IRQ_RAW);

		skb = rxq->skb[pos];

		if (skb == NULL) {
			pr_err("chip told us to receive pkg,"
			       "but no more can be received!\n");
			break;
		}
		rxq->skb[pos] = NULL;

		addr = rxq->dma_phys[pos];
		dma_unmap_single(priv->dev, addr, HLETH_MAX_FRAME_SIZE,
			DMA_FROM_DEVICE);

		if (hleth_skb_rxcsum(priv, skb, rlen) < 0)
			goto next;
		rlen &= HLETH_P_GLB_RO_IQFRM_DES_FDIN_LEN_MSK;
		rlen -= ETH_FCS_LEN; /* remove FCS 4Bytes */
		skb_put(skb, rlen);

		skb_queue_tail(&priv->rx_head, skb);
next:
		pos = (pos + 1) % rxq->num;
		cnt++;
	}
	rxq->tail = pos;

	local_unlock(priv);

	/* fill hardware receive queue again */
	hleth_feed_hw(priv);

	return cnt;
}

static void hleth_adjust_link(struct net_device *dev)
{
	int stat = 0;
	bool fc_enable = false;
	struct hleth_netdev_priv *priv = netdev_priv(dev);

	stat |= (priv->phy->link) ? HLETH_P_MAC_PORTSET_LINKED : 0;
	stat |= (priv->phy->duplex == DUPLEX_FULL) ?
		HLETH_P_MAC_PORTSET_DUP_FULL : 0;
	stat |= (priv->phy->speed == SPEED_100) ?
		HLETH_P_MAC_PORTSET_SPD_100M : 0;

	/* The following expression
	 * "(stat | priv->link_stat) & HLETH_P_MAC_PORTSET_LINKED"
	 * means we only consider three link status change as valid:
	 * 1) down -> up;
	 * 2) up -> down;
	 * 3) up -> up; (maybe the link speed and duplex changed)
	 * We will ignore the "down -> down" condition.
	 */
	if ((stat != priv->link_stat) &&
	    ((((unsigned int)stat | (unsigned int)priv->link_stat) & HLETH_P_MAC_PORTSET_LINKED) != 0)) {
		hleth_writel(priv->port_base, stat, HLETH_P_MAC_PORTSET);
		phy_print_status(priv->phy);
		priv->link_stat = stat;

		if (priv->phy->pause != 0)
			fc_enable = true;
		hleth_set_flow_ctrl(priv, fc_enable);
		if (priv->autoeee_enabled)
			hleth_autoeee_init(priv, stat);
	}
}

#ifdef CONFIG_HLETH_MAX_RX_POOLS
static int hleth_init_skb_buffers(struct hleth_netdev_priv *priv)
{
	int i;
	struct sk_buff *skb;

	for (i = 0; i < CONFIG_HLETH_MAX_RX_POOLS; i++) {
		skb = netdev_alloc_skb_ip_align(priv->ndev, SKB_SIZE);
		if (!skb)
			break;
		priv->rx_pool.sk_pool[i] = skb;
	}

	if (i < CONFIG_HLETH_MAX_RX_POOLS) {
		pr_err("no mem\n");
		for (i--; i > 0; i--)
			dev_kfree_skb_any(priv->rx_pool.sk_pool[i]);
		return -ENOMEM;
	}

	priv->rx_pool.next_free_skb = 0;
	priv->stat.rx_pool_dry_times = 0;
	return 0;
}

static void hleth_destroy_skb_buffers(struct hleth_netdev_priv *priv)
{
	int i;

	for (i = 0; i < CONFIG_HLETH_MAX_RX_POOLS; i++)
		dev_kfree_skb_any(priv->rx_pool.sk_pool[i]);

	priv->rx_pool.next_free_skb = 0;
	priv->stat.rx_pool_dry_times = 0;
}
#endif

static void hleth_net_isr_proc(struct net_device *ndev, int ints)
{
	struct hleth_netdev_priv *priv = netdev_priv(ndev);

	if ((((unsigned int)ints & ud_bit_name(HLETH_GLB_IRQ_INT_MULTI_RXRDY)) != 0) ||
	    (((unsigned int)ints & ud_bit_name(HLETH_GLB_IRQ_INT_TXQUE_RDY))!= 0)) {
		hleth_clear_irqstatus(priv, ud_bit_name(HLETH_GLB_IRQ_INT_TXQUE_RDY));
#ifdef FEMAC_RX_REFILL_IN_IRQ
		hleth_recv_budget(priv);
#else
		hleth_irq_disable(priv,
				ud_bit_name(HLETH_GLB_IRQ_INT_MULTI_RXRDY));
		hleth_irq_disable(priv,
				ud_bit_name(HLETH_GLB_IRQ_INT_TXQUE_RDY));
#endif
		napi_schedule(&priv->napi);
	}
}

static void hleth_get_tso_err_info(struct hleth_netdev_priv *priv)
{
	unsigned int reg_addr, reg_tx_info, reg_tx_err;
	unsigned int sg_index;
	struct sg_desc *sg_desc = NULL;
	int *sg_word = NULL;
	int i;

	reg_addr = readl(priv->port_base + HLETH_P_TSO_DBG_ADDR);
	reg_tx_info = readl(priv->port_base + HLETH_P_TSO_DBG_TX_INFO);
	reg_tx_err = readl(priv->port_base + HLETH_P_TSO_DBG_TX_ERR);

	WARN(1, "tx err=0x%x, tx_info=0x%x, addr=0x%x\n",
	     reg_tx_err, reg_tx_info, reg_addr);

	sg_index = (reg_addr - priv->sg_desc_queue.dma_phys) / sizeof(struct sg_desc);
	sg_desc = priv->sg_desc_queue.desc + sg_index;
	sg_word = (int *)sg_desc;
	for (i = 0; i < sizeof(struct sg_desc) / sizeof(int); i++)
		pr_err("%s,%d: sg_desc word[%d]=0x%x\n",
		       __func__, __LINE__, i, sg_word[i]);

	/* restart MAC to transmit next packet */
	hleth_irq_disable(priv, ud_bit_name(HLETH_GLB_IRQ_INT_TX_ERR));
	/*
	 * If we need allow netcard transmit packet again.
	 * we should readl TSO_DBG_STATE and enable irq.
	 */
}

static irqreturn_t hleth_net_isr(int irq, void *dev_id)
{
	unsigned int ints;
	struct net_device *dev = (struct net_device *)dev_id;
	struct hleth_netdev_priv *priv = netdev_priv(dev);

	/* mask the all interrupt */
	hleth_irq_disable(priv, HLETH_GLB_IRQ_ENA_IEN_A);
	ints = hleth_readl(priv->glb_base, HLETH_GLB_IRQ_STAT);
	if ((priv->port == HLETH_PORT_0) &&
	    likely((ints & HLETH_GLB_IRQ_ENA_BIT_U) != 0)) {
		hleth_net_isr_proc(dev, (ints & HLETH_GLB_IRQ_ENA_BIT_U));
		hleth_clear_irqstatus(priv, (ints & HLETH_GLB_IRQ_ENA_BIT_U));
		ints &= ~HLETH_GLB_IRQ_ENA_BIT_U;
	}

	if ((priv->port == HLETH_PORT_1) &&
	    likely((ints & HLETH_GLB_IRQ_ENA_BIT_D) != 0)) {
		hleth_net_isr_proc(dev, (ints & HLETH_GLB_IRQ_ENA_BIT_D));
		hleth_clear_irqstatus(priv, (ints & HLETH_GLB_IRQ_ENA_BIT_D));
		ints &= ~HLETH_GLB_IRQ_ENA_BIT_D;
	}

	if ((has_tso_cap(priv->hw_cap) != 0) && unlikely((ints & ud_bit_name(HLETH_GLB_IRQ_INT_TX_ERR)) != 0))
		hleth_get_tso_err_info(priv);

	/* unmask the all interrupt */
	hleth_irq_enable(priv, HLETH_GLB_IRQ_ENA_IEN_A);

	return IRQ_HANDLED;
}

static void hleth_monitor_func(struct timer_list *t)
{
	struct hleth_netdev_priv *priv = from_timer(priv, t, monitor);

	if (priv == NULL || !netif_running(priv->ndev)) {
		pr_debug("network driver is stopped.\n");
		return;
	}

	hleth_feed_hw(priv);
	hleth_xmit_release_skb(priv);

	priv->monitor.expires =
	    jiffies + msecs_to_jiffies(HLETH_MONITOR_TIMER);
	add_timer(&priv->monitor);
}

static int hleth_net_open(struct net_device *dev)
{
	int ret = 0;
	struct cpumask cpumask;
	struct hleth_netdev_priv *priv = netdev_priv(dev);

	ret = request_irq(dev->irq, hleth_net_isr, IRQF_SHARED,
			  dev->name, dev);
	if (ret) {
		pr_err("request_irq %d failed!\n", dev->irq);
		return ret;
	}

	/* set irq affinity */
	if ((num_online_cpus() > 1) && (cpu_online(HLETH_IRQ_AFFINITY_CPU) != 0)) {
		cpumask_clear(&cpumask);
		cpumask_set_cpu(HLETH_IRQ_AFFINITY_CPU, &cpumask);
		irq_set_affinity(dev->irq, &cpumask);
	}

	if (!is_valid_ether_addr(dev->dev_addr))
		random_ether_addr(dev->dev_addr);

	hleth_hw_set_macaddress(priv, dev->dev_addr);

	/* setup hardware */
	hleth_set_hwq_depth(priv);
	hleth_clear_irqstatus(priv, ud_bit_name(HLETH_GLB_IRQ_ENA_BIT));

	netif_carrier_off(dev);
	hleth_feed_hw(priv);

	netif_wake_queue(dev);
	napi_enable(&priv->napi);

	priv->link_stat = 0;
	if (priv->phy != NULL)
		phy_start(priv->phy);

	hleth_irq_enable(priv, ud_bit_name(HLETH_GLB_IRQ_INT_MULTI_RXRDY) |
			ud_bit_name(HLETH_GLB_IRQ_ENA_IEN) |
			HLETH_GLB_IRQ_ENA_IEN_A);
	if (has_tso_cap(priv->hw_cap))
		hleth_irq_enable(priv, ud_bit_name(HLETH_GLB_IRQ_INT_TX_ERR));

	priv->monitor.expires =
	    jiffies + msecs_to_jiffies(HLETH_MONITOR_TIMER);
	add_timer(&priv->monitor);

	return 0;
}

static void hleth_free_skb_rings(struct hleth_netdev_priv *priv);

static int hleth_net_close(struct net_device *dev)
{
	struct hleth_netdev_priv *priv = netdev_priv(dev);
	struct sk_buff *skb = NULL;

	hleth_irq_disable(priv, ud_bit_name(HLETH_GLB_IRQ_INT_MULTI_RXRDY));
	napi_disable(&priv->napi);
	netif_stop_queue(dev);
	if (priv->phy != NULL)
		phy_stop(priv->phy);

	del_timer_sync(&priv->monitor);

	/* reset and init port */
	hleth_port_reset(priv);

	while ((skb = skb_dequeue(&priv->rx_head)) != NULL) {
#ifdef HLETH_SKB_MEMORY_STATS
		atomic_dec(&priv->rx_skb_occupied);
		atomic_sub(skb->truesize, &priv->rx_skb_mem_occupied);
#endif
		kfree_skb(skb);
	}

	hleth_free_skb_rings(priv);

	free_irq(dev->irq, dev);
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static void hleth_net_timeout(struct net_device *dev, unsigned int txqueue)
#else
static void hleth_net_timeout(struct net_device *dev)
#endif
{
	struct hleth_netdev_priv *priv = netdev_priv(dev);

	pr_err("tx timeout\n");
	pr_info("# Net device %s, GLB_ADDRQ_STAT: 0x%x\n",
		priv->ndev->name, hleth_readl(priv->glb_base, HLETH_P_GLB_RO_QUEUE_STAT));
	schedule_work(&priv->tx_timeout_task);
}

static void hleth_do_udp_checksum(struct sk_buff *skb)
{
	int offset;
	__wsum csum;
	__sum16 udp_csum;

	offset = skb_checksum_start_offset(skb);
	WARN_ON(offset >= skb_headlen(skb));
	csum = skb_checksum(skb, offset, skb->len - offset, 0);

	offset += skb->csum_offset;
	WARN_ON(offset + sizeof(__sum16) > skb_headlen(skb));

	udp_csum = csum_fold(csum);
	if (udp_csum == 0)
		udp_csum = CSUM_MANGLED_0;

	*(__sum16 *)(skb->data + offset) = udp_csum;

	skb->ip_summed = CHECKSUM_NONE;
}

static __be16 hleth_get_l3_proto(struct sk_buff *skb)
{
	__be16 l3_proto;

	l3_proto = skb->protocol;
	if (skb->protocol == htons(ETH_P_8021Q))
		l3_proto = vlan_get_protocol(skb);

	return l3_proto;
}

static inline bool hleth_skb_is_ipv6(struct sk_buff *skb)
{
	return (hleth_get_l3_proto(skb) == htons(ETH_P_IPV6));
}

static int hleth_check_hw_capability_for_ipv6(struct sk_buff *skb)
{
	unsigned int l4_proto;

	l4_proto = ipv6_hdr(skb)->nexthdr;
	if ((l4_proto != IPPROTO_TCP) && (l4_proto != IPPROTO_UDP)) {
		/*
		 * when IPv6 next header is not tcp or udp,
		 * it means that IPv6 next header is extension header.
		 * Hardware can't deal with this case,
		 * so do checksumming by software or do GSO by software.
		 */
		if (skb_is_gso(skb))
			return -ENOTSUPP;

		if ((skb->ip_summed == CHECKSUM_PARTIAL) && (skb_checksum_help(skb) != 0))
			return -EINVAL;
	}

	return 0;
}

static int hleth_check_hw_capability(struct sk_buff *skb)
{
	/*
	 * if tcp_mtu_probe() use (2 * tp->mss_cache) as probe_size,
	 * the linear data length will be larger than 2048,
	 * the MAC can't handle it, so let the software do it.
	 */
	if (skb_is_gso(skb) && (skb_headlen(skb) > 2048)) /* max is 2048 */
		return -ENOTSUPP;

	if (hleth_skb_is_ipv6(skb))
		return hleth_check_hw_capability_for_ipv6(skb);

	return 0;
}

static unsigned int hleth_get_pkt_info_gso(struct sk_buff *skb,
		bool txcsum, unsigned int max_mss, unsigned int l4_proto)
{
	u32 pkt_info = 0;
	bool do_txcsum = txcsum;

	/*
	 * Although netcard support UFO feature, it can't deal with
	 * UDP header checksum.
	 * So the driver will do UDP header checksum and netcard will just
	 * fragment the packet.
	 */
	if (do_txcsum && skb_is_gso(skb) && (l4_proto == IPPROTO_UDP)) {
		hleth_do_udp_checksum(skb);
		do_txcsum = false;
	}

	if (do_txcsum)
		pkt_info |= BIT_FLAG_TXCSUM;

	if (skb_is_gso(skb)) {
		pkt_info |= (BIT_FLAG_SG | BIT_FLAG_TSO);
	} else if (skb_shinfo(skb)->nr_frags) {
		pkt_info |= BIT_FLAG_SG;
	}

	pkt_info |= (skb_shinfo(skb)->nr_frags << BIT_OFFSET_NFRAGS_NUM);
	pkt_info |= (skb_is_gso(skb) ? ((skb_shinfo(skb)->gso_size > max_mss) ?
					max_mss : skb_shinfo(skb)->gso_size) :
					(skb->len + ETH_FCS_LEN));
	return pkt_info;
}

static u32 hleth_get_pkt_info(struct sk_buff *skb)
{
	__be16 l3_proto;
	unsigned int l4_proto = IPPROTO_MAX;
	bool do_txcsum = false;
	int max_data_len;
	unsigned int max_mss = ETH_DATA_LEN;
	u32 pkt_info = 0;

	if (skb == NULL)
		return 0;

	max_data_len = skb->len - ETH_HLEN;

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		do_txcsum = true;

	l3_proto = skb->protocol;
	if (skb->protocol == htons(ETH_P_8021Q)) {
		l3_proto = vlan_get_protocol(skb);
		max_data_len -= VLAN_HLEN;
		pkt_info |= BIT_FLAG_VLAN;
	}

	if (l3_proto == htons(ETH_P_IP)) {
		struct iphdr *iph = ip_hdr(skb);

		if ((max_data_len >= GSO_MAX_SIZE) &&
			(ntohs(iph->tot_len) <= (iph->ihl << 2))) /* trans 2 bytes */
			iph->tot_len = htons(GSO_MAX_SIZE - 1);

		max_mss -= iph->ihl * WORD_TO_BYTE;
		pkt_info |= (iph->ihl << BIT_OFFSET_IP_HEADER_LEN);
		l4_proto = iph->protocol;
	} else if (l3_proto == htons(ETH_P_IPV6)) {
		max_mss -= IPV6_HDR_LEN * WORD_TO_BYTE;
		pkt_info |= BIT_FLAG_IPV6;
		pkt_info |= (IPV6_HDR_LEN << BIT_OFFSET_IP_HEADER_LEN);
		l4_proto = ipv6_hdr(skb)->nexthdr;
	} else {
		do_txcsum = false;
	}

	if (l4_proto == IPPROTO_TCP) {
		max_mss -= tcp_hdr(skb)->doff * WORD_TO_BYTE;
		pkt_info |= (tcp_hdr(skb)->doff << BIT_OFFSET_PROT_HEADER_LEN);
	} else if (l4_proto == IPPROTO_UDP) {
		if (l3_proto == htons(ETH_P_IPV6))
			max_mss -= sizeof(struct frag_hdr);
		pkt_info |= (BIT_FLAG_UDP |
			     (UDP_HDR_LEN << BIT_OFFSET_PROT_HEADER_LEN));
	} else {
		do_txcsum = false;
	}

	pkt_info |= hleth_get_pkt_info_gso(skb, do_txcsum, max_mss, l4_proto);

	return pkt_info;
}

static netdev_tx_t hleth_net_hard_start_xmit(struct sk_buff *skb, struct net_device *dev);
static netdev_tx_t hleth_sw_gso(struct sk_buff *skb,
				     struct net_device *dev)
{
	struct sk_buff *segs = NULL;
	struct sk_buff *curr_skb = NULL;
	netdev_features_t features = dev->features;

	features &= ~(NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		      NETIF_F_TSO | NETIF_F_TSO6);
	segs = skb_gso_segment(skb, features);
	if (IS_ERR_OR_NULL(segs)) {
		goto drop;
	}

	do {
		curr_skb = segs;
		segs = segs->next;
		curr_skb->next = NULL;
		if (hleth_net_hard_start_xmit(curr_skb, dev)) {
			dev_kfree_skb(curr_skb);
			while (segs != NULL) {
				curr_skb = segs;
				segs = segs->next;
				curr_skb->next = NULL;
				dev_kfree_skb_any(curr_skb);
			}
			goto drop;
		}
	} while (segs != NULL);

	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;

drop:
	dev_kfree_skb_any(skb);
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static int hleth_fill_sg_desc(const struct hleth_netdev_priv *priv,
				   const struct sk_buff *skb, unsigned int pos)
{
	struct sg_desc *desc_cur;
	dma_addr_t addr;
	int ret;
	int i;

	desc_cur = priv->sg_desc_queue.desc + pos;

	desc_cur->total_len = skb->len;
	addr = dma_map_single(priv->dev, skb->data, skb_headlen(skb),
			      DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(priv->dev, addr) != 0))
		return -EINVAL;

	desc_cur->linear_addr = addr;
	desc_cur->linear_len = skb_headlen(skb);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		unsigned int len = skb_frag_size(frag);

		addr = skb_frag_dma_map(priv->dev, frag, 0, len, DMA_TO_DEVICE);
		ret = dma_mapping_error(priv->dev, addr);
		if (unlikely(ret != 0))
			return -EINVAL;

		desc_cur->frags[i].addr = addr;
		desc_cur->frags[i].size = len;
	}

	return 0;
}

static netdev_tx_t hleth_xmit_gso(struct net_device *dev, struct sk_buff *skb,
				dma_addr_t *addr, u32 *val)
{
	int ret;
	struct hleth_netdev_priv *priv = netdev_priv(dev);
	struct hleth_queue *txq = &priv->txq;

	ret = hleth_check_hw_capability(skb);
	if (unlikely(ret != 0)) {
		if (ret == -ENOTSUPP)
			return hleth_sw_gso(skb, dev);
		return ret;
	}

	*val = hleth_get_pkt_info(skb);

	if (!(skb_is_gso(skb) || (skb_shinfo(skb)->nr_frags != 0))) {
		*addr = dma_map_single(priv->dev, skb->data, skb->len, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->dev, *addr)) {
			netdev_err(priv->ndev, "DMA mapping error when sending.");
			return -EINVAL;
		}
	} else {
		ret = hleth_fill_sg_desc(priv, skb, txq->head);
		if (unlikely(ret < 0)) {
			return ret;
		}

		*addr = priv->sg_desc_queue.dma_phys + txq->head * sizeof(struct sg_desc);

		/* Ensure desc info writen to memory before config hardware */
		wmb();
	}

	return 0;
}

static netdev_tx_t hleth_net_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hleth_netdev_priv *priv = netdev_priv(dev);
	struct hleth_queue *txq = &priv->txq;
	dma_addr_t addr;
	u32 val;
	int ret;

	if ((hleth_hw_xmitq_ready(priv) == 0) ||
	    unlikely(CIRC_SPACE(txq->head, txq->tail, (unsigned int)txq->num) == 0)) {
		netif_stop_queue(dev);
		hleth_irq_enable(priv, ud_bit_name(HLETH_GLB_IRQ_INT_TXQUE_RDY));
		return NETDEV_TX_BUSY;
	}

	if (has_tso_cap(priv->hw_cap)) {
		ret = hleth_xmit_gso(dev, skb, &addr, &val);
		if (unlikely(ret < 0)) {
			priv->stats.tx_dropped++;
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
	} else {
		addr = dma_map_single(priv->dev, skb->data, skb->len, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->dev, addr)) {
			netdev_err(priv->ndev, "DMA mapping error when sending.");
			priv->stats.tx_errors++;
			priv->stats.tx_dropped++;
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
		val = hleth_readl(priv->port_base, HLETH_P_GLB_EQFRM_LEN);
		val &= ~HLETH_P_GLB_EQFRM_TXINQ_LEN_MSK;
		val |= skb->len + ETH_FCS_LEN;
	}

	local_lock(priv);

	/* we must use "skb->len" before sending packet to hardware,
	 * because once we send packet to hardware,
	 * "hleth_xmit_release_skb" in softirq may free this skb.
	 * This bug is reported by KASAN: use-after-free.
	 */
	priv->stats.tx_packets++;
	priv->stats.tx_bytes += skb->len;
#ifdef HLETH_SKB_MEMORY_STATS
	atomic_inc(&priv->tx_skb_occupied);
	atomic_add(skb->truesize, &priv->tx_skb_mem_occupied);
#endif

	txq->dma_phys[txq->head] = addr;
	txq->skb[txq->head] = skb;

	/* for recalc CRC, 4 bytes more is needed */
	hleth_writel(priv->port_base, addr, HLETH_P_GLB_EQ_ADDR);
	hleth_writel(priv->port_base, val, HLETH_P_GLB_EQFRM_LEN);

	txq->head = (txq->head + 1) % txq->num;
	priv->tx_fifo_used_cnt++;

	netif_trans_update(dev);

	local_unlock(priv);

	return NETDEV_TX_OK;
}

static void hleth_net_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct hleth_netdev_priv *priv = netdev_priv(dev);
	int ret = -1;

	ret = memcpy_s(stats, sizeof(struct rtnl_link_stats64), &priv->stats, sizeof(*stats));
	if (ret != EOK) {
		pr_err("%s : %d, memcpy stats error ret=%d\n", __func__, __LINE__, ret);
		return;
	}

	return;
}

static int hleth_net_set_mac_address(struct net_device *dev, void *p)
{
	struct hleth_netdev_priv *priv = netdev_priv(dev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_commit_mac_addr_change(dev, p);
	dev->addr_assign_type &= ~NET_ADDR_RANDOM;

	hleth_hw_set_macaddress(priv, dev->dev_addr);

	return 0;
}

static inline void hleth_enable_mac_addr_filter(const struct hleth_netdev_priv *priv,
						unsigned int reg_n, int enable)
{
	u32 val;

	if (priv == NULL)
		return;

	val = hleth_readl(priv->glb_base, glb_mac_h16(priv->port, reg_n));
	if (enable)
		val |= ud_bit_name(HLETH_GLB_MACFLT_ENA);
	else
		val &= ~(ud_bit_name(HLETH_GLB_MACFLT_ENA));
	hleth_writel(priv->glb_base, val, glb_mac_h16(priv->port, reg_n));
}

static void hleth_set_mac_addr(const struct hleth_netdev_priv *priv, u8 addr[6],
			       unsigned int high, unsigned int low)
{
	u32 val;
	u32 data;

	val = hleth_readl(priv->glb_base, high);
	val |= ud_bit_name(HLETH_GLB_MACFLT_ENA);
	hleth_writel(priv->glb_base, val, high);

	val &= ~HLETH_GLB_MACFLT_H16;
	val |= ((addr[0] << 8) | addr[1]);/* 8:right shift val */
	hleth_writel(priv->glb_base, val, high);
	/* 2,3,4,5,8,16,24:right shift val */
	data = (addr[2] << 24) | (addr[3] << 16) | (addr[4] << 8) | addr[5];
	hleth_writel(priv->glb_base, data, low);

	val |= ud_bit_name(HLETH_GLB_MACFLT_FW2CPU);
	hleth_writel(priv->glb_base, val, high);
}

static inline void hleth_set_mac_addr_filter(const struct hleth_netdev_priv *priv,
					     unsigned char *addr,
					     unsigned int reg_n)
{
	if (priv == NULL || addr == NULL)
		return;
	hleth_set_mac_addr(priv, addr, glb_mac_h16(priv->port, reg_n),
			   glb_mac_l32(priv->port, reg_n));
}

static void hleth_net_set_rx_mode(struct net_device *dev)
{
	u32 val;
	struct hleth_netdev_priv *priv = netdev_priv(dev);

	local_lock(priv);

	val = hleth_readl(priv->glb_base, HLETH_GLB_FWCTRL);
	if (dev->flags & IFF_PROMISC) {
		val |= ((priv->port == HLETH_PORT_0) ?
			HLETH_GLB_FWCTRL_FWALL2CPU_U :
			HLETH_GLB_FWCTRL_FWALL2CPU_D);
		hleth_writel(priv->glb_base, val, HLETH_GLB_FWCTRL);
	} else {
		val &= ~((priv->port == HLETH_PORT_0) ?
			HLETH_GLB_FWCTRL_FWALL2CPU_U :
			HLETH_GLB_FWCTRL_FWALL2CPU_D);
		hleth_writel(priv->glb_base, val, HLETH_GLB_FWCTRL);

		val = hleth_readl(priv->glb_base, HLETH_GLB_MACTCTRL);
		if ((netdev_mc_count(dev) > HLETH_MAX_MULTICAST_ADDRESSES) ||
		    ((dev->flags & IFF_ALLMULTI) != 0)) {
			val |= ud_bit_name(HLETH_GLB_MACTCTRL_MULTI2CPU);
		} else {
			int reg = HLETH_MAX_UNICAST_ADDRESSES;
			int i = 0;
			struct netdev_hw_addr *ha = NULL;

			for (i = reg; i < HLETH_MAX_MAC_FILTER_NUM; i++)
				hleth_enable_mac_addr_filter(priv, i, 0);

			netdev_for_each_mc_addr(ha, dev) {
				hleth_set_mac_addr_filter(priv, ha->addr, reg);
				reg++;
			}

			val &= ~(ud_bit_name(HLETH_GLB_MACTCTRL_MULTI2CPU));
		}

		/* Handle multiple unicast addresses (perfect filtering) */
		if (netdev_uc_count(dev) > HLETH_MAX_UNICAST_ADDRESSES) {
			val |= ud_bit_name(HLETH_GLB_MACTCTRL_UNI2CPU);
		} else {
			int reg = 0;
			int i;
			struct netdev_hw_addr *ha = NULL;

			for (i = reg; i < HLETH_MAX_UNICAST_ADDRESSES; i++)
				hleth_enable_mac_addr_filter(priv, i, 0);

			netdev_for_each_uc_addr(ha, dev) {
				hleth_set_mac_addr_filter(priv, ha->addr, reg);
				reg++;
			}

			val &= ~(ud_bit_name(HLETH_GLB_MACTCTRL_UNI2CPU));
		}
		hleth_writel(priv->glb_base, val, HLETH_GLB_MACTCTRL);
	}

	local_unlock(priv);
}

static int hleth_set_wol_on_phy(struct net_device *net_dev,
				struct ethtool_wolinfo *wol)
{
	struct hleth_netdev_priv *priv = netdev_priv(net_dev);

	int ret;

	if (net_dev->phydev == NULL)
		return -EOPNOTSUPP;

	ret = phy_ethtool_set_wol(net_dev->phydev, wol);
	if (ret == 0) {
		priv->pm_state_set = true;
		device_set_wakeup_enable(priv->dev, true);
		pr_info("hleth: set phy wol success!\n");
	} else {
		pr_err("hleth: set phy wol failed, err=%d\n", ret);
	}

	return ret;
}

static int hleth_set_wol_on_mac(struct net_device *net_dev,
				struct hleth_pm_config *config)
{
	int ret;

	if ((ret = hleth_pmt_config(net_dev, config)) == 0)
		pr_info("hleth: set mac wol success!\n");
	else
		pr_err("hleth: set mac wol failed, err=%d\n", ret);

	return ret;
}

static int hleth_set_wol(struct net_device *net_dev,
			 struct hleth_pm_config *config,
			 struct ethtool_wolinfo *wol)
{
	int ret;

	if (net_dev == NULL)
		return -EINVAL;

	ret = hleth_set_wol_on_phy(net_dev, wol);
	if (ret != 0)
		pr_info("hleth: phy wol unsupport, try mac wol\n");

	ret = hleth_set_wol_on_mac(net_dev, config);

	return ret;
}

static int hleth_ioctl_set_wol(struct net_device *net_dev,
				 struct hleth_pm_config *config)
{
	struct ethtool_wolinfo wol_info = {0};

	wol_info.cmd = ETHTOOL_SWOL;
	if (config->uc_pkts_enable)
		wol_info.wolopts |= WAKE_UCAST;
	if (config->magic_pkts_enable)
		wol_info.wolopts |= WAKE_MAGIC;

	return hleth_set_wol(net_dev, config, &wol_info);
}

static int hleth_get_wol(struct net_device *net_dev,
			 struct hleth_pm_config *config,
			 struct ethtool_wolinfo *wol)
{
	if (net_dev == NULL)
		return -EINVAL;

	if (net_dev->phydev != NULL) {
		phy_ethtool_get_wol(net_dev->phydev, wol);
		config->uc_pkts_enable = ((wol->wolopts & WAKE_UCAST) != 0) ? 1 : 0;
		config->magic_pkts_enable = ((wol->wolopts & WAKE_MAGIC) != 0) ? 1 : 0;
	}

	hleth_pmt_get_config(net_dev, config);
	if (config->uc_pkts_enable == 1)
		wol->wolopts |= WAKE_UCAST;
	if (config->magic_pkts_enable == 1)
		wol->wolopts |= WAKE_MAGIC;

	return 0;
}

static int hleth_ioctl_get_wol(struct net_device *net_dev,
			 struct hleth_pm_config *config)
{
	struct ethtool_wolinfo wol_info = {0};

	return hleth_get_wol(net_dev, config, &wol_info);
}

static int hleth_net_ioctl(struct net_device *net_dev,
			   struct ifreq *ifreq, int cmd)
{
	struct hleth_netdev_priv *priv = netdev_priv(net_dev);
	struct hleth_pm_config pm_config;
	int ret;

	switch (cmd) {
	case SIOCSETPM:
		if (copy_from_user(&pm_config, ifreq->ifr_data,
				   sizeof(pm_config)) != 0)
			return -EFAULT;

		return hleth_ioctl_set_wol(net_dev, &pm_config);

	case SIOCGETPM:
		if (memset_s(&pm_config, sizeof(pm_config), 0,
				   sizeof(pm_config) != 0))
			return -EFAULT;

		ret = hleth_ioctl_get_wol(net_dev, &pm_config);
		if (ret == 0) {
			if (copy_to_user(ifreq->ifr_data, &pm_config,
					   sizeof(pm_config)) != 0)
				return -EFAULT;
		}

		return ret;

	default:
		if (!netif_running(net_dev))
			return -EINVAL;

		if (priv->phy == NULL)
			return -EINVAL;

		return phy_mii_ioctl(priv->phy, ifreq, cmd);
	}

	return 0;
}

static void hleth_ethtools_get_drvinfo(struct net_device *net_dev,
				       struct ethtool_drvinfo *info)
{
	int ret;

	ret = strcpy_s(info->driver, sizeof(info->driver), "hleth driver");
	if (ret != EOK) {
		pr_err("strcpy driver error ret=%d", ret);
		return;
	}
	ret = strcpy_s(info->version, sizeof(info->version), "v300");
	if (ret != EOK) {
		pr_err("strcpy driver error ret=%d", ret);
		return;
	}
	ret = strcpy_s(info->bus_info, ETHTOOL_BUSINFO_LEN, "platform");
	if (ret != EOK) {
		pr_err("strcpy driver error ret=%d", ret);
		return;
	}
}

static u32 hleth_ethtools_get_link(struct net_device *net_dev)
{
	struct hleth_netdev_priv *priv = netdev_priv(net_dev);

	return ((priv->phy->link != 0) ? HLETH_P_MAC_PORTSET_LINKED : 0);
}

static void hleth_ethtool_get_wol(struct net_device *dev,
				  struct ethtool_wolinfo *wol)
{
	struct hleth_pm_config mac_pm_config = {0};

	(void)hleth_get_wol(dev, &mac_pm_config, wol);
}

static int hleth_ethtool_set_wol(struct net_device *dev,
				 struct ethtool_wolinfo *wol)
{
	struct hleth_netdev_priv *priv = netdev_priv(dev);
	struct hleth_pm_config mac_pm_config = {0};

	mac_pm_config.index = BIT(priv->port);
	if (wol->wolopts & WAKE_UCAST)
		mac_pm_config.uc_pkts_enable = 1;

	if (wol->wolopts & WAKE_MAGIC)
		mac_pm_config.magic_pkts_enable = 1;

	return hleth_set_wol(dev, &mac_pm_config, wol);
}

static int hleth_change_mtu(struct net_device *dev, int new_mtu)
{
	dev->mtu = (unsigned int)new_mtu;
	return 0;
}

static struct ethtool_ops hleth_ethtools_ops = {
	.get_drvinfo = hleth_ethtools_get_drvinfo,
	.get_link = hleth_ethtools_get_link,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
	.get_wol = hleth_ethtool_get_wol,
	.set_wol = hleth_ethtool_set_wol,
	.set_pauseparam = hleth_set_pauseparam,
	.get_pauseparam = hleth_get_pauseparam,
};

static const struct net_device_ops hleth_netdev_ops = {
	.ndo_open = hleth_net_open,
	.ndo_stop = hleth_net_close,
	.ndo_start_xmit = hleth_net_hard_start_xmit,
	.ndo_tx_timeout = hleth_net_timeout,
	.ndo_do_ioctl = hleth_net_ioctl,
	.ndo_set_mac_address = hleth_net_set_mac_address,
	.ndo_set_rx_mode	= hleth_net_set_rx_mode,
	.ndo_change_mtu		= hleth_change_mtu,
	.ndo_get_stats64 = hleth_net_get_stats,
};

static void hleth_clean_rx(struct hleth_netdev_priv *priv, int *workdone, int budget)
{
	int nr_recv = 0;
	struct sk_buff *skb = NULL;
	struct net_device *dev = NULL;

	if (priv == NULL || priv->ndev == NULL)
		return;

	dev = priv->ndev;

	hleth_recv_budget(priv);

	while ((skb = skb_dequeue(&priv->rx_head)) != NULL) {
#ifdef HLETH_SKB_MEMORY_STATS
		atomic_dec(&priv->rx_skb_occupied);
		atomic_sub(skb->truesize, &priv->rx_skb_mem_occupied);
#endif

		skb->protocol = eth_type_trans(skb, dev);

		if (hleth_invalid_rxpkg_len(skb->len)) {
			pr_err("pkg len error");
			priv->stats.rx_errors++;
			priv->stats.rx_length_errors++;
			dev_kfree_skb_any(skb);
			continue;
		}

		priv->stats.rx_packets++;
		priv->stats.rx_bytes += skb->len;
		napi_gro_receive(&priv->napi, skb);

		nr_recv++;
		if (nr_recv >= budget)
			break;
	}

	if (workdone != NULL)
		*workdone = nr_recv;
}

static int hleth_poll(struct napi_struct *napi, int budget)
{
	struct hleth_netdev_priv *priv = NULL;
	int work_done = 0;
	priv = container_of(napi, struct hleth_netdev_priv, napi);

	hleth_xmit_release_skb(priv);
	hleth_clean_rx(priv, &work_done, budget);

	if (work_done < budget) {
		napi_complete(napi);
		hleth_irq_enable(priv, ud_bit_name(HLETH_GLB_IRQ_INT_MULTI_RXRDY));
	}
	return work_done;
}

static int hleth_init_queue(struct device *dev,
		struct hleth_queue *queue,
		unsigned int num)
{
	queue->skb = devm_kcalloc(dev, num, sizeof(struct sk_buff *),
			GFP_KERNEL);
	if (queue->skb == NULL)
		return -ENOMEM;

	queue->dma_phys = devm_kcalloc(dev, num, sizeof(dma_addr_t),
			GFP_KERNEL);
	if (queue->dma_phys == NULL)
		return -ENOMEM;

	queue->num = num;
	queue->head = 0;
	queue->tail = 0;

	return 0;
}

static int hleth_init_tx_sg_desc_queue(struct hleth_netdev_priv *priv)
{
	priv->sg_desc_queue.desc = (struct sg_desc *)dma_alloc_coherent(priv->dev,
			     TXQ_NUM * sizeof(struct sg_desc), &priv->sg_desc_queue.dma_phys, GFP_KERNEL);
	if (!priv->sg_desc_queue.desc)
		return -ENOMEM;

	return 0;
}

static void hleth_destroy_tx_sg_desc_queue(struct hleth_netdev_priv *priv)
{
	if (priv->sg_desc_queue.desc)
		dma_free_coherent(priv->dev, TXQ_NUM * sizeof(struct sg_desc),
				  priv->sg_desc_queue.desc, priv->sg_desc_queue.dma_phys);
	priv->sg_desc_queue.desc = NULL;
}

static int hleth_init_tx_and_rx_queues(struct hleth_netdev_priv *priv)
{
	int ret;

	ret = hleth_init_queue(priv->dev, &priv->txq, TXQ_NUM);
	if (ret)
		return ret;

	ret = hleth_init_queue(priv->dev, &priv->rxq, RXQ_NUM);
	if (ret)
		return ret;

	if (has_tso_cap(priv->hw_cap)) {
		ret = hleth_init_tx_sg_desc_queue(priv);
		if (ret)
			return ret;
	}

	priv->tx_fifo_used_cnt = 0;

	return 0;
}

static void hleth_destroy_tx_and_rx_queues(struct hleth_netdev_priv *priv)
{
	if (has_tso_cap(priv->hw_cap)) {
		hleth_destroy_tx_sg_desc_queue(priv);
	}
}

static void hleth_free_skb_rings(struct hleth_netdev_priv *priv)
{
	struct hleth_queue *txq = &priv->txq;
	struct hleth_queue *rxq = &priv->rxq;
	struct sk_buff *skb = NULL;
	dma_addr_t dma_addr;
	u32 pos;

	pos = rxq->tail;
	while (pos != rxq->head) {
		skb = rxq->skb[pos];
		if (unlikely(skb == NULL)) {
			netdev_err(priv->ndev, "NULL rx skb. pos=%d, head=%d\n",
					pos, rxq->head);
			continue;
		}

		dma_addr = rxq->dma_phys[pos];
		dma_unmap_single(priv->dev, dma_addr, HLETH_MAX_FRAME_SIZE,
				DMA_FROM_DEVICE);
#ifdef HLETH_SKB_MEMORY_STATS
		atomic_dec(&priv->rx_skb_occupied);
		atomic_sub(skb->truesize, &priv->rx_skb_mem_occupied);
#endif
		dev_kfree_skb_any(skb);
		rxq->skb[pos] = NULL;
		pos = (pos + 1) % rxq->num;
	}
	rxq->tail = pos;

	pos = txq->tail;
	while (pos != txq->head) {
		skb = txq->skb[pos];
		if (unlikely(skb == NULL)) {
			netdev_err(priv->ndev, "NULL tx skb. pos=%d, head=%d\n",
					pos, txq->head);
			continue;
		}
		dma_addr = txq->dma_phys[pos];
		dma_unmap_single(priv->dev, dma_addr, skb->len, DMA_TO_DEVICE);
#ifdef HLETH_SKB_MEMORY_STATS
		atomic_dec(&priv->tx_skb_occupied);
		atomic_sub(skb->truesize, &priv->tx_skb_mem_occupied);
#endif
		dev_kfree_skb_any(skb);
		txq->skb[pos] = NULL;
		pos = (pos + 1) % txq->num;
	}
	txq->tail = pos;
	priv->tx_fifo_used_cnt = 0;
}

static int hleth_netdev_init(struct platform_device *pdev, struct net_device **netdev,
			struct hleth_netdev_priv *priv, const struct hleth_netdev_priv *com_priv, int port)
{
	int ret = -1;
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);
	struct net_device *ndev;

	ndev = alloc_etherdev(sizeof(*priv));
	if (ndev == NULL) {
		pr_err("alloc_etherdev fail!\n");
		ret = -ENOMEM;
		return ret;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	ndev->irq = com_priv->irq;

	ndev->watchdog_timeo = 3 * HZ; /* 3:hz val */
	ndev->netdev_ops = &hleth_netdev_ops;
	ndev->ethtool_ops = &hleth_ethtools_ops;
	ndev->priv_flags |= IFF_UNICAST_FLT;

	if (!IS_ERR_OR_NULL(pdata->hleth_phy_param[port].macaddr))
		ether_addr_copy(ndev->dev_addr, pdata->hleth_phy_param[port].macaddr);
	else
		eth_hw_addr_random(ndev);

	*netdev = ndev;
	return 0;
}

static void hleth_verify_flow_ctrl_args(struct hleth_netdev_priv *priv)
{
	if (priv->tx_pause_active_thresh < FC_ACTIVE_MIN ||
		priv->tx_pause_active_thresh > FC_ACTIVE_MAX)
		priv->tx_pause_active_thresh = FC_ACTIVE_DEFAULT;

	if (priv->tx_pause_deactive_thresh < FC_DEACTIVE_MIN ||
		priv->tx_pause_deactive_thresh > FC_DEACTIVE_MAX)
		priv->tx_pause_deactive_thresh = FC_DEACTIVE_DEFAULT;

	if (priv->tx_pause_active_thresh >= priv->tx_pause_deactive_thresh) {
		priv->tx_pause_active_thresh = FC_ACTIVE_DEFAULT;
		priv->tx_pause_deactive_thresh = FC_DEACTIVE_DEFAULT;
	}
}

static int hleth_netdev_priv_init(struct platform_device *pdev, struct net_device *netdev,
			struct hleth_netdev_priv *priv, const struct hleth_netdev_priv *com_priv, int port)
{
	int ret = -1;
	struct device *dev = &pdev->dev;
	ret = memset_s(priv, sizeof(*priv), 0, sizeof(*priv));
	if (ret != EOK) {
		pr_err("memset priv error ret=%d\n", ret);
		return -1;
	}
	ret = memcpy_s(priv, sizeof(*priv), com_priv, sizeof(*priv));
	if (ret != EOK) {
		pr_err("memcpy priv error ret=%d\n", ret);
		return -1;
	}

	local_lock_init(priv);

	priv->port = port;

	if (port == HLETH_PORT_0)
		priv->port_base = priv->glb_base;
	else
		priv->port_base = priv->glb_base + 0x2000;

	priv->dev = dev;
	priv->ndev = netdev;

	if (has_tso_cap(priv->hw_cap))
		netdev->hw_features |= NETIF_F_SG | NETIF_F_IP_CSUM |
			NETIF_F_IPV6_CSUM | NETIF_F_TSO | NETIF_F_TSO6;
	if (has_rxcsum_cap(priv->hw_cap))
		netdev->hw_features |= NETIF_F_RXCSUM;
	netdev->features |= netdev->hw_features;
	netdev->vlan_features |= netdev->features;

	timer_setup(&priv->monitor, hleth_monitor_func, 0);
	priv->monitor.expires = jiffies + msecs_to_jiffies(HLETH_MONITOR_TIMER);

	/* wol need */
	device_set_wakeup_capable(priv->dev, 1);
	device_set_wakeup_enable(priv->dev, 1);

	priv->tx_pause_en = false;
	priv->tx_pause_active_thresh = TX_FLOW_CTRL_ACTIVE_THRESHOLD;
	priv->tx_pause_deactive_thresh = TX_FLOW_CTRL_DEACTIVE_THRESHOLD;
	hleth_verify_flow_ctrl_args(priv);

	priv->pm_state_set = false;
	return 0;
}

static int hleth_priv_rxpool_init(struct hleth_netdev_priv **priv)
{
	__maybe_unused int ret = -1;
	if ((*priv)->autoeee_enabled)
		hleth_autoeee_init(*priv, 0);

	skb_queue_head_init(&(*priv)->rx_head);

#ifdef HLETH_SKB_MEMORY_STATS
	atomic_set(&(*priv)->tx_skb_occupied, 0);
	atomic_set(&(*priv)->tx_skb_mem_occupied, 0);
	atomic_set(&(*priv)->rx_skb_occupied, 0);
	atomic_set(&(*priv)->rx_skb_mem_occupied, 0);
#endif

#ifdef CONFIG_HLETH_MAX_RX_POOLS
	ret = hleth_init_skb_buffers(*priv);
	if (ret) {
		pr_err("hleth_init_skb_buffers failed!\n");
		return ret;
	}
#endif
	return 0;
}

static int hleth_register_netdev(struct platform_device *pdev, struct net_device *netdev,
								 struct hleth_netdev_priv *priv)
{
	int ret;

	ret = hleth_init_tx_and_rx_queues(priv);
	if (ret)
		return ret;

	netif_napi_add(netdev, &priv->napi, hleth_poll, HLETH_NAPI_WEIGHT);

	priv->pdev = pdev;
	INIT_WORK(&priv->tx_timeout_task, hleth_tx_timeout_task);
	ret = register_netdev(netdev);
	if (ret) {
		pr_err("register_netdev %s failed!\n", netdev->name);
		hleth_destroy_tx_and_rx_queues(priv);
		return ret;
	}
	return 0;
}

static int hleth_platdev_probe_port(struct platform_device *pdev,
				    struct hleth_netdev_priv *com_priv, int port)
{
	int ret = -1;
	struct net_device *netdev = NULL;
	struct hleth_netdev_priv *priv = NULL;
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);

	if ((port != HLETH_PORT_0) && (port != HLETH_PORT_1)) {
		ret = -ENODEV;
		goto _error_exit;
	}

	ret = hleth_netdev_init(pdev, &netdev, priv, com_priv, port);
	if (ret)
		goto _error_exit;

	/* init hleth_global somethings... */
	pdata->hleth_devs_save[port] = netdev;

	/* init hleth_local_driver */
	priv = netdev_priv(netdev);
	if (hleth_netdev_priv_init(pdev, netdev, priv, com_priv, port))
		goto _error_mem;

	/* reset and init port */
	hleth_port_init(priv);

	priv->depth.hw_xmitq = HLETH_HWQ_XMIT_DEPTH;

	priv->phy = of_phy_connect(netdev, priv->phy_node, hleth_adjust_link, 0, priv->phy_mode);
	if (priv->phy == NULL || IS_ERR(priv->phy)) {
		pr_info("connect to port[%d] PHY failed!\n", port);
		priv->phy = NULL;
		ret = -1;
		goto _error_phy_connect;
	}

	linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT, priv->phy->supported);
	pr_info("attached port %d PHY %d to driver %s\n", port, priv->phy->mdio.addr, priv->phy->drv->name);

	if (hleth_priv_rxpool_init(&priv))
		goto _error_init_skb_buffers;

	if (hleth_register_netdev(pdev, netdev, priv))
		goto _error_register_netdev;

	phy_suspend(priv->phy);

	return ret;

_error_register_netdev:
	priv->pdev = NULL;
	cancel_work_sync(&priv->tx_timeout_task);
#ifdef CONFIG_HLETH_MAX_RX_POOLS
	hleth_destroy_skb_buffers(priv);

#endif
_error_init_skb_buffers:
	phy_disconnect(priv->phy);
	priv->phy = NULL;

_error_phy_connect:
	local_lock_exit();

_error_mem:
	pdata->hleth_devs_save[port] = NULL;
	free_netdev(netdev);

_error_exit:
	return ret;
}

static int hleth_platdev_remove_port(struct platform_device *pdev, int port)
{
	struct net_device *ndev;
	struct hleth_netdev_priv *priv = NULL;
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);

	ndev = pdata->hleth_devs_save[port];

	if (ndev == NULL)
		goto _ndev_exit;

	priv = netdev_priv(ndev);
	cancel_work_sync(&priv->tx_timeout_task);

	unregister_netdev(ndev);
#ifdef CONFIG_HLETH_MAX_RX_POOLS
	hleth_destroy_skb_buffers(priv);
#endif
	hleth_destroy_tx_and_rx_queues(priv);
	phy_disconnect(priv->phy);
	priv->phy = NULL;

	iounmap((void *)priv->glb_base);

	local_lock_exit();

	pdata->hleth_devs_save[port] = NULL;
	free_netdev(ndev);

_ndev_exit:
	return 0;
}

#define DEFAULT_LD_AM          0xe
#define DEFAULT_LDO_AM         0x3
#define DEFAULT_R_TUNING       0x16
#define BIT_OFFSET_LD_SET	6
#define BIT_OFFSET_LDO_SET	12
#define BIT_OFFSET_R_TUNING	0
#define LD_AM_MASK		GENMASK(4, 0)
#define LDO_AM_MASK		GENMASK(2, 0)
#define R_TUNING_MASK		GENMASK(5, 0)
static void hleth_of_get_phy_trim_params(struct hleth_platdrv_data *pdata, int port_index, struct device_node *phy_node)
{
	struct device_node *chiptrim_node = NULL;
	u32 phy_trim_val = 0;
	u8 ld_am, ldo_am, r_tuning;
	int value;
	int ret;
	struct hleth_phy_param_s *phy_param = &pdata->hleth_phy_param[port_index];

	/* currently only one internal PHY */
	if (port_index == HLETH_PORT_1)
		return;

	ld_am = DEFAULT_LD_AM;
	ldo_am = DEFAULT_LDO_AM;
	r_tuning = DEFAULT_R_TUNING;

	chiptrim_node = of_find_node_by_path("/soc/chiptrim");
	if (chiptrim_node != NULL) {
		ret = of_property_read_u32(chiptrim_node, "chiptrim_fephy", &phy_trim_val);
		if (ret) {
			pr_err("%s,%d: chiptrim0 property not found\n",
				__func__, __LINE__);
			return;
		}
	}

	if (phy_trim_val) {
		ld_am = (phy_trim_val >> 24) & LD_AM_MASK; /* 24:right shift val */
		ldo_am = (phy_trim_val >> 16) & LDO_AM_MASK; /* 16:right shift val */
		r_tuning = (phy_trim_val >> 8) & R_TUNING_MASK; /* 8:right shift val */
	}

	if (phy_param->fephy_trim != NULL) {
		phy_trim_val = readl(phy_param->fephy_trim);
		ld_am = (phy_trim_val >> BIT_OFFSET_LD_SET) & LD_AM_MASK;
		ldo_am = (phy_trim_val >> BIT_OFFSET_LDO_SET) & LDO_AM_MASK;
		r_tuning = (phy_trim_val >> BIT_OFFSET_R_TUNING) & R_TUNING_MASK;
	}

	ret = of_property_read_s32(phy_node, "ld_trim_compensation", &value);
	if (ret == 0) {
		pr_info("Get ld_trim_compensation %d from dts.\n", value);

		value += (int)ld_am;
		ld_am = (value <= 0) ? 0 : ((value >= (int)LD_AM_MASK) ? LD_AM_MASK : (u8)value & LD_AM_MASK);
	}

	phy_param->trim_params =
		(r_tuning << 16) | (ldo_am << 8) | ld_am; /* 8,16:right shift val */
}

static int hleth_of_get_phy_addr(struct device_node *node, struct hleth_phy_param_s *param)
{
	int data;

	if (of_property_read_u32(node, "reg", &data))
		return -EINVAL;

	if ((data < 0) || (data >= PHY_MAX_ADDR)) {
		if (node->full_name != NULL)
			pr_info("%s has invalid PHY address\n", node->full_name);
		data = HLETH_INVALID_PHY_ADDR;
	}

	param->phy_addr = data;
	if (data != HLETH_INVALID_PHY_ADDR)
		param->isvalid = true;

	return 0;
}

#define MAX_NAME_SIZE 32
static int hleth_of_get_phy_resource(struct platform_device *pdev, int port)
{
	struct resource *res;
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);
	struct hleth_phy_param_s *phy_param = &pdata->hleth_phy_param[port];
	struct device *dev = &pdev->dev;
	int ret;
	char name_buf[MAX_NAME_SIZE] = {0};

	ret = snprintf_s(name_buf, MAX_NAME_SIZE, MAX_NAME_SIZE - 1, "reset_phy%d", port);
	if (ret < 0)
		return ret;
	phy_param->phy_rst = devm_reset_control_get_optional(dev, name_buf);
	if (IS_ERR(phy_param->phy_rst))
		return PTR_ERR(phy_param->phy_rst);

	ret = snprintf_s(name_buf, MAX_NAME_SIZE, MAX_NAME_SIZE - 1, "phy_clk%d", port);
	if (ret < 0)
		return ret;
	phy_param->phy_clk = devm_clk_get(dev, name_buf);
	if (IS_ERR(phy_param->phy_clk))
		phy_param->phy_clk = NULL;

	ret = snprintf_s(name_buf, MAX_NAME_SIZE, MAX_NAME_SIZE - 1, "fephy_sysctrl%d", port);
	if (ret < 0)
		return ret;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name_buf);
	if (res != NULL) {
		phy_param->fephy_sysctrl = devm_ioremap_resource(dev, res);
		if (IS_ERR(phy_param->fephy_sysctrl))
			phy_param->fephy_sysctrl = NULL;
	}

	ret = snprintf_s(name_buf, MAX_NAME_SIZE, MAX_NAME_SIZE - 1, "fephy_trim%d", port);
	if (ret < 0)
		return ret;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name_buf);
	if (res != NULL) {
		phy_param->fephy_trim = devm_ioremap_resource(dev, res);
		if (IS_ERR(phy_param->fephy_trim))
			phy_param->fephy_trim = NULL;
	}

	return 0;
}

static int hleth_of_get_param(struct platform_device *pdev, struct hleth_platdrv_data *pdata)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *child = NULL;
	int idx = 0;
	int ret;

	for_each_available_child_of_node(node, child) {
		/* get phy-addr */
		ret = hleth_of_get_phy_addr(child, &pdata->hleth_phy_param[idx]);
		if (ret != 0)
			return ret;

		pdata->hleth_phy_param[idx].fephy_phyaddr_bit = -1;
		of_property_read_u32(child, "phyaddr-bit-offset",
				&pdata->hleth_phy_param[idx].fephy_phyaddr_bit);

		/* get phy_mode */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
		of_get_phy_mode(child, &(pdata->hleth_phy_param[idx].phy_mode));
#else
		pdata->hleth_phy_param[idx].phy_mode = of_get_phy_mode(child);
#endif

		/* get mac */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
		of_get_mac_address(child, (u8 *)(pdata->hleth_phy_param[idx].macaddr));
#else
		pdata->hleth_phy_param[idx].macaddr = of_get_mac_address(child);
#endif
		/* get gpio_base and bit */
		of_property_read_u32(child, "phy-gpio-base",
				     (u32 *)(&pdata->hleth_phy_param[idx].gpio_base));
		of_property_read_u32(child, "phy-gpio-bit",
				     &pdata->hleth_phy_param[idx].gpio_bit);

		/* get internal flag */
		pdata->hleth_phy_param[idx].isinternal =
			of_property_read_bool(child, "internal-phy");

		ret = hleth_of_get_phy_resource(pdev, idx);
		if (ret != 0)
			return ret;

		hleth_of_get_phy_trim_params(pdata, idx, child);

		if (++idx >= HLETH_MAX_PORT)
			break;
	}

	return 0;
}

static int hleth_plat_driver_probe_res(struct platform_device *pdev, struct hleth_netdev_priv *priv,
									   struct hleth_platdrv_data *pdata)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct resource *res = NULL;
	int ret;

	if (of_device_is_compatible(node, "vendor,femac-v2"))
		priv->hw_cap |= HW_CAP_TSO | HW_CAP_RXCSUM;

	ret = hleth_of_get_param(pdev, pdata);
	if (ret != 0) {
		if (ret != -EPROBE_DEFER)
			pr_err("failed to get phy param, error: %d.\n", ret);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->glb_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->glb_base))
		return PTR_ERR(priv->glb_base);

	priv->clk = devm_clk_get(dev, "hleth_clk");
	if (IS_ERR(priv->clk)) {
		pr_err("failed to get clk\n");
		return PTR_ERR(priv->clk);
	}

	priv->mac_reset = devm_reset_control_get_optional(dev, "mac_reset");
	if (IS_ERR(priv->mac_reset))
		return PTR_ERR(priv->mac_reset);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		pr_err("no IRQ defined!\n");
		return ret;
	}
	priv->irq = ret;
	return 0;
}

static void hleth_phy_detect(struct platform_device *pdev, struct hleth_platdrv_data *pdata)
{
	int port = -1;
	struct hleth_netdev_priv *priv = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *child = NULL;
	priv = &pdata->hleth_priv;
	for_each_available_child_of_node(node, child) {
		if (++port >= HLETH_MAX_PORT)
			break;

		if (!pdata->hleth_phy_param[port].isvalid)
			continue;

		priv->phy_node = of_parse_phandle(node, "phy-handle", port);
		if (priv->phy_node == NULL) {
			pr_err("not find phy-handle [%d]\n", port);
			continue;
		}

		priv->phy_mode = pdata->hleth_phy_param[port].phy_mode;
		priv->autoeee_enabled = of_property_read_bool(child, "autoeee");

		if (hleth_platdev_probe_port(pdev, priv, port) == 0)
			pdata->hleth_real_port_cnt++;
	}
}


static void hleth_core_reset(struct hleth_netdev_priv *priv)
{
	reset_control_assert(priv->mac_reset);
	reset_control_deassert(priv->mac_reset);
}

static int hleth_plat_driver_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct hleth_netdev_priv *priv = NULL;
	struct hleth_platdrv_data *pdata;

	pdata = devm_kzalloc(dev, sizeof(struct hleth_platdrv_data), GFP_KERNEL);
	if (pdata == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, pdata);
	priv = &pdata->hleth_priv;
	ret = hleth_plat_driver_probe_res(pdev, priv, pdata);
	if (ret != 0) {
		goto exit;
	}

	/* first disable ETH clock, then reset PHY to load PHY address */
	hleth_phy_reset(pdata);

	hleth_core_reset(priv);
	ret = clk_prepare_enable(priv->clk);
	if (ret < 0) {
		pr_err("failed to enable clk %d\n", ret);
		goto exit;
	}
	/* After MDCK clock giving, wait 5ms before MDIO access */
	mdelay(5);

	if (hleth_mdiobus_driver_init(pdev, priv)) {
		pr_err("mdio bus init error!\n");
		ret = -ENODEV;
		goto exit_clk_disable;
	}

	/* phy param */
	hleth_phy_register_fixups();

	hleth_phy_detect(pdev, pdata);

	if (!pdata->hleth_devs_save[HLETH_PORT_0] && !pdata->hleth_devs_save[HLETH_PORT_1]) {
		pr_err("no dev probed!\n");
		ret = -ENODEV;
		goto exit_mdiobus;
	}

	return ret;

exit_mdiobus:
	hleth_mdiobus_driver_exit(priv);

exit_clk_disable:
	clk_disable_unprepare(priv->clk);

exit:

	return ret;
}

static int hleth_plat_driver_remove(struct platform_device *pdev)
{
	int i;
	int ret;
	struct net_device *ndev = NULL;
	struct hleth_netdev_priv *priv = NULL;
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);

	if (pdata->hleth_devs_save[HLETH_PORT_0])
		ndev = pdata->hleth_devs_save[HLETH_PORT_0];
	else if (pdata->hleth_devs_save[HLETH_PORT_1])
		ndev = pdata->hleth_devs_save[HLETH_PORT_1];

	priv = netdev_priv(ndev);

	for (i = 0; i < HLETH_MAX_PORT; i++)
		hleth_platdev_remove_port(pdev, i);

	hleth_mdiobus_driver_exit(priv);

	clk_disable_unprepare(priv->clk);
	hleth_phy_clk_disable(pdata);

	ret = memset_s(pdata->hleth_devs_save, sizeof(pdata->hleth_devs_save), 0, sizeof(pdata->hleth_devs_save));
	if (ret != EOK) {
		pr_err("memset hleth_devs_save error ret=%d\n", ret);
		return ret;
	}
	hleth_phy_unregister_fixups();

	return ret;
}

static int hleth_plat_driver_suspend_port(struct platform_device *pdev,
					  pm_message_t state, int port)
{
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);
	struct net_device *ndev = pdata->hleth_devs_save[port];

	if (ndev != NULL) {
		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			hleth_net_close(ndev);
		}
	}

	return 0;
}

static int hleth_plat_driver_suspend(struct platform_device *pdev,
			      pm_message_t state)
{
	int i;
	bool power_off = true;
	struct hleth_netdev_priv *priv = NULL;
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);

	for (i = 0; i < HLETH_MAX_PORT; i++)
		hleth_plat_driver_suspend_port(pdev, state, i);

	if (hleth_pmt_enter(pdev))
		power_off = false;

	if (power_off) {
		for (i = 0; i < HLETH_MAX_PORT; i++) {
			if (pdata->hleth_devs_save[i]) {
				priv = netdev_priv(pdata->hleth_devs_save[i]);
				genphy_suspend(priv->phy);/* power down phy */
			}
		}

		/* need some time before phy suspend finished. */
		usleep_range(1000, 10000); /* 1000,10000:delay zone */

		if (priv != NULL && priv->clk != NULL)
			clk_disable_unprepare(priv->clk);

		hleth_phy_clk_disable(pdata);
	}

	return 0;
}

static int hleth_plat_driver_resume_port(struct platform_device *pdev, int port)
{
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);
	struct net_device *ndev = pdata->hleth_devs_save[port];
	struct hleth_netdev_priv *priv = netdev_priv(ndev);

	if (ndev != NULL) {
		if (ndev->phydev != NULL)
		    phy_init_hw(ndev->phydev);
		if (netif_running(ndev)) {
			hleth_port_init(priv);
			hleth_net_open(ndev);
			netif_device_attach(ndev);
			hleth_net_set_rx_mode(ndev);
		}
	}

	return 0;
}

static bool hleth_mac_wol_enabled(struct platform_device *pdev)
{
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);
	struct hleth_netdev_priv *priv = NULL;
	bool mac_wol_enabled = false;
	int i;

	for (i = 0; i < HLETH_MAX_PORT; i++) {
		if (!pdata->hleth_devs_save[i])
			continue;

		priv = netdev_priv(pdata->hleth_devs_save[i]);
		if (priv->mac_wol_enabled) {
			mac_wol_enabled = true;
			break;
		}
	}

	return mac_wol_enabled;
}

static int hleth_plat_driver_resume(struct platform_device *pdev)
{
	int i;
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);
	struct hleth_netdev_priv *priv = &pdata->hleth_priv;

	/* first disable ETH clock, then reset PHY to load PHY address */
	if (hleth_mac_wol_enabled(pdev))
		clk_disable_unprepare(priv->clk);
	hleth_phy_reset(pdata);

	hleth_core_reset(priv);

	/* enable clk */
	clk_prepare_enable(priv->clk);
	/* After MDCK clock giving, wait 5ms before MDIO access */
	mdelay(5);
	hleth_fix_festa_phy_trim(priv->mii_bus, pdata);

	for (i = 0; i < HLETH_MAX_PORT; i++)
		hleth_plat_driver_resume_port(pdev, i);

	hleth_pmt_exit(pdev);
	return 0;
}

static void hleth_tx_timeout_task(struct work_struct *ws)
{
	struct hleth_netdev_priv *priv = NULL;
	pm_message_t state_timeout;

	memset_s(&state_timeout, sizeof(state_timeout), 0, sizeof(state_timeout));
	priv = container_of(ws, struct hleth_netdev_priv, tx_timeout_task);

	if (netif_queue_stopped(priv->ndev))
		netif_wake_queue(priv->ndev);

#ifdef CONFIG_RESET_FEMAC_AFTER_TX_TIMEOUT
	state_timeout.event = PM_EVENT_INVALID;
	hleth_plat_driver_suspend(priv->pdev, state_timeout);
	hleth_plat_driver_resume(priv->pdev);
#endif
}

static const struct of_device_id hleth_of_match[] = {
	{.compatible = "huanglong,plat_eth"},
	{.compatible = "huanglong,t9-eth"},
	{.compatible = "vendor,femac-v2"},
	{},
};

MODULE_DEVICE_TABLE(of, hleth_of_match);

static struct platform_driver hleth_platform_driver = {
	.probe = hleth_plat_driver_probe,
	.remove = hleth_plat_driver_remove,
	.suspend = hleth_plat_driver_suspend,
	.resume = hleth_plat_driver_resume,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = HLETH_DRIVER_NAME,
		   .bus = &platform_bus_type,
		   .of_match_table = of_match_ptr(hleth_of_match),
		   },
};

static int hleth_mod_init(void)
{
	int ret = 0;

	if (hleth_disable)
		return 0;

	ret = platform_driver_register(&hleth_platform_driver);
	if (ret)
		pr_err("register platform driver failed!\n");

	return ret;
}

static void hleth_mod_exit(void)
{
	if (hleth_disable)
		return;

	platform_driver_unregister(&hleth_platform_driver);
}

module_init(hleth_mod_init);
module_exit(hleth_mod_exit);

MODULE_DESCRIPTION("Huanglong ETH driver whith MDIO support");
MODULE_LICENSE("GPL");

/* vim: set ts=8 sw=8 tw=78: */
