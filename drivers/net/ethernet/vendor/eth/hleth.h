/*
 * Copyright (c) CompanyNameMagicTag 2022. All rights reserved.
 * Description: Header file for hleth.c
 * Author: AuthorNameMagicTag
 * Create: 2022-4-20
 */

#ifndef __HLETH_H
#define __HLETH_H

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/phy.h>
#include <linux/io.h>
#include <linux/reset.h>

#define HLETH_SKB_MEMORY_STATS

#define HLETH_MIIBUS_NAME	"hlmii"
#define HLETH_DRIVER_NAME	"hleth"

/* hleth max port */
#define HLETH_MAX_PORT	2

/* invalid phy addr */
#define HLETH_INVALID_PHY_ADDR	0xff

/* hleth monitor timer, 10ms */
#define HLETH_MONITOR_TIMER	10

/* hleth hardware queue send fifo depth, increase to optimize TX performance. */
#define HLETH_HWQ_XMIT_DEPTH	12

/* set irq affinity to cpu1 when multi-processor */
#define HLETH_IRQ_AFFINITY_CPU	1

#define HLETH_MAX_QUEUE_DEPTH	64
#define HLETH_MAX_RX_HEAD_LEN	(10000)  /* max skbs for rx */
#define HLETH_MAX_RCV_LEN	1535     /* max receive length */
#define TXQ_NUM			64
#define RXQ_NUM			128

#define HLETH_NAPI_WEIGHT 32
/*  mmu should be less than 1600 Bytes
 */

#define HLETH_MAX_FRAME_SIZE	(1600)
#define SKB_SIZE		(HLETH_MAX_FRAME_SIZE)
#define hleth_invalid_rxpkg_len(len) (!((len) >= 42 && \
				      (len) <= HLETH_MAX_FRAME_SIZE))

#define HLETH_MAX_MAC_FILTER_NUM	8
#define HLETH_MAX_UNICAST_ADDRESSES	2
#define HLETH_MAX_MULTICAST_ADDRESSES	(HLETH_MAX_MAC_FILTER_NUM - \
		HLETH_MAX_UNICAST_ADDRESSES)

/* Register Definition
 */
/*------------------------- port register -----------------------------------*/
/* Mac port sel */
#define HLETH_P_MAC_PORTSEL             0x0200
#define  HLETH_P_MAC_PORTSEL_STAT       0
#define   HLETH_P_MAC_PORTSEL_STAT_MDIO 0
#define   HLETH_P_MAC_PORTSEL_STAT_CPU  1
#define  HLETH_P_MAC_PORTSEL_MII_MODE   1
#define   HLETH_P_MAC_PORTSEL_MII       ~BIT(1)
#define   HLETH_P_MAC_PORTSEL_RMII      BIT(1)
/* Mac ro status */
#define HLETH_P_MAC_RO_STAT             0x0204
/* Mac port status set */
#define HLETH_P_MAC_PORTSET             0x0208
#define  HLETH_P_MAC_PORTSET_SPD_100M   BIT(2)
#define  HLETH_P_MAC_PORTSET_LINKED     BIT(1)
#define  HLETH_P_MAC_PORTSET_DUP_FULL   BIT(0)
/* Mac status change */
#define HLETH_P_MAC_STAT_CHANGE         0x020C
/* Mac set */
#define HLETH_P_MAC_SET                 0x0210
#define  BIT_PAUSE_EN                   BIT(18)
#define  hleth_p_mac_set_len_max(n)     ((n) & 0x7FF)
#define  HLETH_P_MAC_SET_LEN_MAX_MSK    GENMASK(10, 0)

#define HLETH_P_MAC_RX_IPGCTRL          0x0214
#define HLETH_P_MAC_TX_IPGCTRL          0x0218
#define  HLETH_P_MAC_TX_IPGCTRL_PRE_CNT_LMT_SHIFT 23
#define  HLETH_P_MAC_TX_IPGCTRL_PRE_CNT_LMT_MSK   GENMASK(25, 23)
/* queue length set */
#define HLETH_P_GLB_QLEN_SET            0x0344
#define  HLETH_P_GLB_QLEN_SET_TXQ_DEP_MSK GENMASK(5, 0)
#define  hleth_p_glb_qlen_set_txq_dep(n)  ((n) << 0)
#define  HLETH_P_GLB_QLEN_SET_RXQ_DEP_MSK GENMASK(13, 8)
#define  hleth_p_glb_qlen_set_rxq_dep(n)  ((n) << 8)

#define HLETH_P_GLB_FC_LEVEL            0x0348
#define BITS_FC_ACTIVE_THR_OFFSET       8
#define HLETH_P_GLB_FC_DEACTIVE_THR_MASK GENMASK(5, 0)
#define HLETH_P_GLB_FC_ACTIVE_THR_MASK  GENMASK(13, 8)
#define BIT_FC_EN                       BIT(14)

/* Rx frame start addr */
#define HLETH_P_GLB_RXFRM_SADDR         0x0350
/* Rx (read only) Queue-ID and LEN */
#define HLETH_P_GLB_RO_IQFRM_DES        0x0354
#define  HLETH_P_GLB_RO_IQFRM_DES_FDIN_LEN_MSK GENMASK(11, 0)
#define BITS_PAYLOAD_ERR_OFFSET    28
#define BITS_PAYLOAD_ERR_MASK      0x1
#define BITS_HEADER_ERR_OFFSET     29
#define BITS_HEADER_ERR_MASK       0x1
#define BITS_PAYLOAD_DONE_OFFSET   30
#define BITS_PAYLOAD_DONE_MASK     0x1
#define BITS_HEADER_DONE_OFFSET    31
#define BITS_HEADER_DONE_MASK      0x1
/* Rx ADDR */
#define HLETH_P_GLB_IQ_ADDR             0x0358
/* Tx ADDR and LEN */
#define HLETH_P_GLB_EQ_ADDR             0x0360
#define HLETH_P_GLB_EQFRM_LEN           0x0364
#define  HLETH_P_GLB_EQFRM_TXINQ_LEN_MSK GENMASK(10, 0)
/* Rx/Tx Queue ID */
#define HLETH_P_GLB_RO_QUEUE_ID         0x0368
/* Rx/Tx Queue staus  */
#define HLETH_P_GLB_RO_QUEUE_STAT       0x036C
/* check this bit to see if we can add a Tx package */
#define  HLETH_P_GLB_RO_QUEUE_STAT_XMITQ_RDY_MSK BIT(24)
/* check this bit to see if we can add a Rx addr */
#define  HLETH_P_GLB_RO_QUEUE_STAT_RECVQ_RDY_MSK BIT(25)
/* counts in queue, include currently sending */
#define  HLETH_P_GLB_RO_QUEUE_STAT_XMITQ_CNT_INUSE_MSK GENMASK(5, 0)
/* Rx Checksum Offload Control */
#define HLETH_P_RX_COE_CTRL             0x0380
#define BIT_COE_IPV6_UDP_ZERO_DROP      BIT(13)
#define BIT_COE_PAYLOAD_DROP            BIT(14)
#define BIT_COE_IPHDR_DROP              BIT(15)
#define COE_ERR_DROP (BIT_COE_IPHDR_DROP | BIT_COE_PAYLOAD_DROP | BIT_COE_IPV6_UDP_ZERO_DROP)
#define HLETH_P_TSO_DBG_EN              0x03A4
#define BITS_TSO_DBG_EN                 BIT(31)
#define HLETH_P_TSO_DBG_STATE           0x03A8
#define HLETH_P_TSO_DBG_ADDR            0x03AC
#define HLETH_P_TSO_DBG_TX_INFO         0x03B0
#define HLETH_P_TSO_DBG_TX_ERR          0x03B4
/*------------------------- global register --------------------------------*/
/* host mac address  */
#define HLETH_GLB_HOSTMAC_L32           0x1300
#define HLETH_GLB_HOSTMAC_H16           0x1304
/* soft reset */
#define HLETH_GLB_SOFT_RESET            0x1308
#define  HLETH_GLB_SOFT_RESET_ALL       BIT(0)
#define  HLETH_GLB_SOFT_RESET_P0        BIT(2)
#define  HLETH_GLB_SOFT_RESET_P1        BIT(3)
/* forward contrl */
#define HLETH_GLB_FWCTRL                0x1310
#define  HLETH_GLB_FWCTRL_VLAN_ENABLE   BIT(0)
#define  HLETH_GLB_FWCTRL_FW2CPU_ENA_U  BIT(5)
#define  HLETH_GLB_FWCTRL_FW2CPU_ENA_D  BIT(9)
#define  HLETH_GLB_FWCTRL_FWALL2CPU_U   BIT(7)
#define  HLETH_GLB_FWCTRL_FWALL2CPU_D   BIT(11)
#define  HLETH_GLB_FWCTRL_FW2OTHPORT_ENA_U   BIT(4)
#define  HLETH_GLB_FWCTRL_FW2OTHPORT_ENA_D   BIT(8)
#define  HLETH_GLB_FWCTRL_FW2OTHPORT_FORCE_U BIT(6)
#define  HLETH_GLB_FWCTRL_FW2OTHPORT_FORCE_D BIT(10)
/* Mac filter table control */
#define HLETH_GLB_MACTCTRL              0x1314
#define  HLETH_GLB_MACTCTRL_MACT_ENA_U  BIT(7)
#define  HLETH_GLB_MACTCTRL_MACT_ENA_D  BIT(15)
#define  HLETH_GLB_MACTCTRL_BROAD2CPU_U BIT(5)
#define  HLETH_GLB_MACTCTRL_BROAD2CPU_D BIT(13)
#define  HLETH_GLB_MACTCTRL_BROAD2OTHPORT_U  BIT(4)
#define  HLETH_GLB_MACTCTRL_BROAD2OTHPORT_D  BIT(12)
#define  HLETH_GLB_MACTCTRL_MULTI2CPU_U      BIT(3)
#define  HLETH_GLB_MACTCTRL_MULTI2CPU_D      BIT(11)
#define  HLETH_GLB_MACTCTRL_MULTI2OTHPORT_U  BIT(2)
#define  HLETH_GLB_MACTCTRL_MULTI2OTHPORT_D  BIT(10)
#define  HLETH_GLB_MACTCTRL_UNI2CPU_U        BIT(1)
#define  HLETH_GLB_MACTCTRL_UNI2CPU_D        BIT(9)
#define  HLETH_GLB_MACTCTRL_UNI2OTHPORT_U    BIT(0)
#define  HLETH_GLB_MACTCTRL_UNI2OTHPORT_D    BIT(8)
/* Host mac address */
#define HLETH_GLB_DN_HOSTMAC_L32        0x1340
#define HLETH_GLB_DN_HOSTMAC_H16        0x1344
#define HLETH_GLB_DN_HOSTMAC_ENA        0x1348
#define  HLETH_GLB_DN_HOSTMAC_ENA_BIT   BIT(0)
/* Mac filter */
#define HLETH_GLB_MAC_L32_BASE          0x1400
#define HLETH_GLB_MAC_H16_BASE          0x1404
#define HLETH_GLB_MAC_L32_BASE_D        (0x1400 + 16 * 0x8)
#define HLETH_GLB_MAC_H16_BASE_D        (0x1404 + 16 * 0x8)
#define  HLETH_GLB_MACFLT_H16          GENMASK(15, 0)
#define  HLETH_GLB_MACFLT_FW2CPU_U      BIT(21)
#define  HLETH_GLB_MACFLT_FW2CPU_D      BIT(19)
#define  HLETH_GLB_MACFLT_FW2PORT_U     BIT(20)
#define  HLETH_GLB_MACFLT_FW2PORT_D     BIT(18)
#define  HLETH_GLB_MACFLT_ENA_U         BIT(17)
#define  HLETH_GLB_MACFLT_ENA_D         BIT(16)
/* ENDIAN */
#define HLETH_GLB_ENDIAN_MOD            0x1318
#define  HLETH_GLB_ENDIAN_MOD_IN        BIT(1)
#define  HLETH_GLB_ENDIAN_MOD_OUT       BIT(0)
/* IRQs */
#define HLETH_GLB_IRQ_STAT              0x1330
#define HLETH_GLB_IRQ_ENA               0x1334
#define  HLETH_GLB_IRQ_ENA_IEN_A        BIT(19)
#define  HLETH_GLB_IRQ_ENA_IEN_U        BIT(18)
#define  HLETH_GLB_IRQ_ENA_IEN_D        BIT(17)
#define  HLETH_GLB_IRQ_ENA_BIT_U        GENMASK(7, 0)
#define  HLETH_GLB_IRQ_ENA_BIT_D        GENMASK(27, 20)
#define HLETH_GLB_IRQ_RAW               0x1338
#define  HLETH_GLB_IRQ_INT_TX_ERR_U     BIT(8)
#define  HLETH_GLB_IRQ_INT_TX_ERR_D     BIT(28)
#define  HLETH_GLB_IRQ_INT_MULTI_RXRDY_U BIT(7)
#define  HLETH_GLB_IRQ_INT_MULTI_RXRDY_D BIT(27)
#define  HLETH_GLB_IRQ_INT_TXQUE_RDY_U  BIT(6)
#define  HLETH_GLB_IRQ_INT_TXQUE_RDY_D  BIT(26)
#define  HLETH_GLB_IRQ_INT_RX_RDY_U     BIT(0)
#define  HLETH_GLB_IRQ_INT_RX_RDY_D     BIT(20)

#define HW_CAP_TSO             BIT(0)
#define HW_CAP_RXCSUM          BIT(1)
#define has_tso_cap(hw_cap)    ((hw_cap) & HW_CAP_TSO)
#define has_rxcsum_cap(hw_cap) ((hw_cap) & HW_CAP_RXCSUM)

/* UDP header len is 2 word */
#define UDP_HDR_LEN 2
/* IPv6 header len is 10 word */
#define IPV6_HDR_LEN 10
#define WORD_TO_BYTE 4

#define BIT_OFFSET_NFRAGS_NUM      11
#define BIT_OFFSET_PROT_HEADER_LEN 16
#define BIT_OFFSET_IP_HEADER_LEN   20
#define BIT_FLAG_SG                BIT(26)
#define BIT_FLAG_TXCSUM            BIT(27)
#define BIT_FLAG_UDP               BIT(28)
#define BIT_FLAG_IPV6              BIT(29)
#define BIT_FLAG_VLAN              BIT(30)
#define BIT_FLAG_TSO               BIT(31)
/*
 * The threshold for activing tx flow ctrl.
 * When the left amount of receive queue descriptors is below this threshold,
 * hardware will send pause frame immediately.
 * We advise this value is set between 1 and 10.
 * Too bigger is not a good choice.
 * This value must be smaller than tx flow ctrl deactive threshold.
 */
#define TX_FLOW_CTRL_ACTIVE_THRESHOLD 3
/*
 * The threshold for deactiving tx flow ctrl.
 * When the left amount of receive queue descriptors is
 * above or equal with this threshold,
 * hardware will exit flow control state.
 * We advise this value is set between 1 and 10.
 * Too bigger is not a good choice.
 * This value must be larger than tx flow ctrl active threshold.
 */
#define TX_FLOW_CTRL_DEACTIVE_THRESHOLD 5
#define FC_ACTIVE_MIN                   1
#define FC_ACTIVE_DEFAULT               3
#define FC_ACTIVE_MAX                   31
#define FC_DEACTIVE_MIN                 1
#define FC_DEACTIVE_DEFAULT             5
#define FC_DEACTIVE_MAX                 31
/* ***********************************************************
*
* Only for internal used!
*
* ***********************************************************
*/

/* read/write IO */
#define hleth_readl(base, ofs) \
	readl((base) + (ofs))
#define hleth_writel(base, v, ofs) \
	writel(v, ((base) + (ofs)))

/* port */
enum hleth_port_e {
	HLETH_PORT_0 = 0,
	HLETH_PORT_1,
	HLETH_PORT_NUM,
};

struct hleth_queue {
	struct sk_buff **skb;
	dma_addr_t *dma_phys;
	int num;
	unsigned int head;
	unsigned int tail;
};

struct frags_info {
	/* Word(2*i+2) */
	u32 addr;
	/* Word(2*i+3) */
	u32 size : 16;
	u32 reserved : 16;
};

struct sg_desc {
	/* Word0 */
	u32 total_len : 17;
	u32 reserv : 15;
	/* Word1 */
	u32 ipv6_id;
	/* Word2 */
	u32 linear_addr;
	/* Word3 */
	u32 linear_len : 16;
	u32 reserv3 : 16;
	/* MAX_SKB_FRAGS is 30 */
	struct frags_info frags[30];
};

struct hleth_sg_desc {
	struct sg_desc *desc;
	dma_addr_t dma_phys;
};

struct hleth_netdev_priv {
	void __iomem *glb_base;     /* virtual io global addr */
	void __iomem *port_base;    /* virtual to port addr:
				     * port0-0; port1-0x2000
				     */
	struct reset_control *mac_reset;
	int port;                   /* 0 => up port, 1 => down port */
	int irq;

	u32 hw_cap;
	struct device *dev;
	struct net_device *ndev;
	struct rtnl_link_stats64 stats;
	struct phy_device *phy;
	struct device_node *phy_node;
	struct platform_device *pdev;
	phy_interface_t	phy_mode;

	struct mii_bus *mii_bus;

	struct sk_buff_head rx_head;   /* received pkgs */
	struct hleth_queue rxq;
	struct hleth_queue txq;
	struct hleth_sg_desc sg_desc_queue;
	u32 tx_fifo_used_cnt;

	struct timer_list monitor;

	struct {
		int hw_xmitq;
	} depth;

#ifdef CONFIG_HLETH_MAX_RX_POOLS
	struct {
		unsigned long rx_pool_dry_times;
	} stat;

	struct rx_skb_pool {
		struct sk_buff *sk_pool[CONFIG_HLETH_MAX_RX_POOLS];/* skb pool */
		int next_free_skb;	/* next free skb */
	} rx_pool;
#endif

	struct napi_struct napi;

	int link_stat;
	int (*eee_init)(struct phy_device *phy_dev);
	struct work_struct tx_timeout_task;

	spinlock_t lock; /* lock for reg rw */
	unsigned long lockflags;

	spinlock_t mdio_lock; /* lock for mdio reg */
	unsigned long mdio_lockflags;

	struct clk *clk;
	bool mac_wol_enabled;
	bool pm_state_set;
	bool autoeee_enabled;

	/* 802.3x flow control */
	bool tx_pause_en;
	u32 tx_pause_active_thresh;
	u32 tx_pause_deactive_thresh;
#ifdef HLETH_SKB_MEMORY_STATS
	atomic_t tx_skb_occupied;
	atomic_t tx_skb_mem_occupied;
	atomic_t rx_skb_occupied;
	atomic_t rx_skb_mem_occupied;
#endif
};

/* phy parameter */
struct hleth_phy_param_s {
	bool isvalid;     /* valid or not */
	bool isinternal;  /* internal phy or external phy */
	int phy_addr;
	u32 trim_params;
	phy_interface_t phy_mode;
	const char *macaddr;

	struct clk *phy_clk;
	struct reset_control *phy_rst;
	/* gpio reset pin if has */
	void __iomem *gpio_base;
	void __iomem *fephy_sysctrl;
	void __iomem *fephy_trim;
	int fephy_phyaddr_bit;
	u32 gpio_bit;
};

#define MAX_MULTICAST_FILTER    8
#define MAX_MAC_LIMIT ETH_ALEN * MAX_MULTICAST_FILTER

struct ethstats {
	void __iomem *base;
	void __iomem *macbase[2];

	char prbuf[SZ_4K];
	u32 sz_prbuf;

	struct dentry *dentry;
};

struct mcdump {
	void __iomem *base;
	u32 net_flags;
	u32 mc_rcv;
	u32 mc_drop;
	u32 mac_nr;
	spinlock_t lock;
	char mac[MAX_MAC_LIMIT];
	char prbuf[SZ_1K];
	u32 sz_prbuf;

	struct dentry *dentry;
};

#ifdef HLETH_SKB_MEMORY_STATS
struct eth_mem_stats {
	char prbuf[SZ_1K];
	u32 sz_prbuf;

	struct dentry *dentry;
};
#endif

struct hleth_platdrv_data {
	struct hleth_phy_param_s hleth_phy_param[HLETH_MAX_PORT];
	struct net_device *hleth_devs_save[HLETH_MAX_PORT];
	struct hleth_netdev_priv hleth_priv;
	int hleth_real_port_cnt;

	/* debugfs info */
	struct dentry *root;
	struct ethstats ethstats;
	struct mcdump mcdump;
#ifdef HLETH_SKB_MEMORY_STATS
	struct eth_mem_stats eth_mem_stats;
#endif
};

#undef local_lock_init
#undef local_lock
#undef local_unlock

#define local_lock_init(priv)	spin_lock_init(&(priv)->lock)
#define local_lock_exit(priv)
#define local_lock(priv)	spin_lock_irqsave(&(priv)->lock, \
							(priv)->lockflags)
#define local_unlock(priv)	spin_unlock_irqrestore(&(priv)->lock, \
							(priv)->lockflags)

#define hleth_mdio_lock_init(priv) spin_lock_init(&(priv)->mdio_lock)
#define hleth_mdio_lock_exit(priv)
#define hleth_mdio_lock(priv)      spin_lock_irqsave(&(priv)->mdio_lock, \
						     (priv)->mdio_lockflags)
#define hleth_mdio_unlock(priv)    spin_unlock_irqrestore(&(priv)->mdio_lock, \
							 (priv)->mdio_lockflags)

#define ud_bit_name(name)       ((priv->port == HLETH_PORT_0) ? \
				 name##_U : name##_D)

#define glb_mac_h16(port, reg)	((((port) == HLETH_PORT_0) ? \
				 HLETH_GLB_MAC_H16_BASE : \
				 HLETH_GLB_MAC_H16_BASE_D) + ((reg) * 0x8))
#define glb_mac_l32(port, reg)	((((port) == HLETH_PORT_0) ? \
				 HLETH_GLB_MAC_L32_BASE : \
				 HLETH_GLB_MAC_L32_BASE_D) + ((reg) * 0x8))

#define SIOCGETMODE	(SIOCDEVPRIVATE)	/* get work mode */
#define SIOCSETMODE	(SIOCDEVPRIVATE + 1)	/* set work mode */
#define SIOCGETFWD	(SIOCDEVPRIVATE + 2)	/* get forcing forward config */
#define SIOCSETFWD	(SIOCDEVPRIVATE + 3)	/* set forcing forward config */
#define SIOCSETPM	(SIOCDEVPRIVATE + 4)	/* set pmt wake up config */
#define SIOCSETSUSPEND	(SIOCDEVPRIVATE + 5)	/* call dev->suspend */
#define SIOCSETRESUME	(SIOCDEVPRIVATE + 6)	/* call dev->resume */
#define SIOCGETPM	(SIOCDEVPRIVATE + 7)	/* call dev->resume */

void hleth_autoeee_init(struct hleth_netdev_priv *priv, int link_stat);
void hleth_phy_register_fixups(void);
void hleth_phy_unregister_fixups(void);
void hleth_phy_reset(struct hleth_platdrv_data *pdata);
void hleth_phy_clk_disable(struct hleth_platdrv_data *pdata);
void hleth_fix_festa_phy_trim(struct mii_bus *bus, struct hleth_platdrv_data *pdata);
#endif

/* vim: set ts=8 sw=8 tw=78: */
