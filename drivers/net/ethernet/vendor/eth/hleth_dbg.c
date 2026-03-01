/*
 * Copyright (c) CompanyNameMagicTag 2020-2021. All rights reserved.
 */
#include <asm/uaccess.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <linux/ctype.h>
#include <linux/if_ether.h>
#include <linux/securec.h>

#ifndef CONFIG_ARM64
#include <mach/hardware.h>
#endif

#define MAC_FMT "	MAC%d: %02x:%02x:%02x:%02x:%02x:%02x\n"

struct cmd_val_t {
	const char *key;
	int sz_key;
	char *buf;
	int sz_buf;
};

static int dump_eth_stats(int index, void __iomem *macbase, char *buf, int sz_buf)
{
	int ix;
	int count;
	char *ptr = buf;

	struct regdef {
		char *fmt;
		u32 offset;
	} regdef[] = {
		{"  Rx:%u Bytes\n", 0x608},
		{"    total packets:%u ", 0x610},
		{"broadcast:%u ", 0x614},
		{"multicast:%u ", 0x618},
		{"unicast:%u ", 0x61C},
		{"to me:%u\n", 0x60C},
		{"    error packets:%u ", 0x620},
		{"crc/alignment:%u ", 0x624},
		{"invalid size:%u ", 0x628},
		{"nibble error:%u\n", 0x62C},
		{"    pause frame:%u, ", 0x630},
		{"overflow: %u ", 0x634},
		{"mac filterd: %u\n", 0x64c},
		{"  Tx:%u Bytes\n", 0x790},
		{"    total packets:%u ", 0x780},
		{"broadcast:%u ", 0x784},
		{"multicast:%u ", 0x788},
		{"unicast:%u\n", 0x78C},
	};

	count = snprintf_s(ptr, sz_buf, sz_buf - 1, "eth%d:\n", index);
	if (count < 0) {
		pr_err("snprintf eth%d error, count=%d\n", index, count);
		return count;
	}
	sz_buf -= count;
	ptr += count;

	for (ix = 0; ix < ARRAY_SIZE(regdef); ix++) {
		count = snprintf_s(ptr, sz_buf, sz_buf - 1, regdef[ix].fmt, readl(macbase + regdef[ix].offset));
		if (count < 0) {
			pr_err("snprintf macbase error, ix=%d,count=%d\n", ix, count);
			return count;
		}
		sz_buf -= count;
		ptr += count;
	}

	return ptr - buf;
}

#ifdef HLETH_SKB_MEMORY_STATS
static int dump_eth_mem_stats(struct hleth_platdrv_data *pdata, char *buf, int sz_buf)
{
	int ix, count;
	char *ptr = buf;
	struct net_device *ndev = NULL;
	struct hleth_netdev_priv *priv = NULL;

	for (ix = 0; ix < pdata->hleth_real_port_cnt; ix++) {
		ndev = pdata->hleth_devs_save[ix];
		priv = netdev_priv(ndev);

		count = snprintf_s(ptr, sz_buf, sz_buf - 1, "%s:\n", ndev->name);
		if (count < 0) {
			pr_err("snprintf ndev name error, count=%d\n", count);
			return count;
		}
		sz_buf -= count;
		ptr += count;

		count = snprintf_s(ptr, sz_buf, sz_buf - 1, "tx skb occupied: %d\n",
				atomic_read(&priv->tx_skb_occupied));
		if (count < 0) {
			pr_err("snprintf tx skb occupied error, count=%d\n", count);
			return count;
		}
		sz_buf -= count;
		ptr += count;

		count = snprintf_s(ptr, sz_buf, sz_buf - 1, "tx skb memory occupied: %d Bytes\n",
				atomic_read(&priv->tx_skb_mem_occupied));
		if (count < 0) {
			pr_err("snprintf tx skb memory occupied error, count=%d\n", count);
			return count;
		}
		sz_buf -= count;
		ptr += count;

		count = snprintf_s(ptr, sz_buf, sz_buf - 1, "rx skb occupied: %d\n",
				atomic_read(&priv->rx_skb_occupied));
		if (count < 0) {
			pr_err("snprintf rx skb occupied error, count=%d\n", count);
			return count;
		}
		sz_buf -= count;
		ptr += count;

		count = snprintf_s(ptr, sz_buf, sz_buf - 1, "rx skb memory occupied: %d Bytes\n",
				atomic_read(&priv->rx_skb_mem_occupied));
		if (count < 0) {
			pr_err("snprintf rx skb memory occupied error, count=%d\n", count);
			return count;
		}
		sz_buf -= count;
		ptr += count;
	}

	return ptr - buf;
}
#endif

static ssize_t fo_dump_ethstats_read(struct file *filp, char __user *ubuf,
			size_t sz_ubuf, loff_t *ppos)
{
	struct ethstats *stats = filp->private_data;

	return simple_read_from_buffer(ubuf, sz_ubuf, ppos, stats->prbuf,
		stats->sz_prbuf);
}

static int fo_dump_ethstats_open(struct inode *inode, struct file *file)
{
	struct ethstats *data;
	int count = 0, sz_buf;
	char *ptr;
	struct hleth_platdrv_data *pdata = inode->i_private;

	data = &pdata->ethstats;

	file->private_data = (void *)data;

	ptr = data->prbuf;
	sz_buf = sizeof(data->prbuf);

	count = dump_eth_stats(0, data->macbase[0], ptr, sz_buf);

	data->sz_prbuf = count;

	return nonseekable_open(inode, file);
}

#ifdef HLETH_SKB_MEMORY_STATS
static ssize_t fo_dump_eth_mem_stats_read(struct file *filp, char __user *ubuf,
			size_t sz_ubuf, loff_t *ppos)
{
	struct eth_mem_stats *mem_stats = filp->private_data;

	return simple_read_from_buffer(ubuf, sz_ubuf, ppos, mem_stats->prbuf,
		mem_stats->sz_prbuf);
}

static int fo_dump_eth_mem_stats_open(struct inode *inode, struct file *file)
{
	struct eth_mem_stats *data;
	int count = 0, sz_buf;
	char *ptr;
	struct hleth_platdrv_data *pdata = inode->i_private;

	data = &pdata->eth_mem_stats;

	file->private_data = (void *)data;

	ptr = data->prbuf;
	sz_buf = sizeof(data->prbuf);

	count = dump_eth_mem_stats(pdata, ptr, sz_buf);

	data->sz_prbuf = count;

	return nonseekable_open(inode, file);
}
#endif

int multicast_dump_netdev_flags(u32 flags, struct hleth_platdrv_data *pdata)
{
	u32 old = pdata->mcdump.net_flags;
	spin_lock(&pdata->mcdump.lock);
	pdata->mcdump.net_flags = flags;
	spin_unlock(&pdata->mcdump.lock);
	return old;
}

void multicast_dump_macaddr(u32 nr, char *macaddr, struct hleth_platdrv_data *pdata)
{
	char *ptr = NULL;
	int ret;
	if (nr > MAX_MULTICAST_FILTER)
		return;

	ptr = pdata->mcdump.mac + nr * ETH_ALEN;
	spin_lock(&pdata->mcdump.lock);
	ret = memcpy_s(ptr, ETH_ALEN, macaddr, ETH_ALEN);
	if (ret != EOK) {
		pr_err("memcpy macaddr error, ret=%d\n", ret);
		spin_unlock(&pdata->mcdump.lock);
		return;
	}
	pdata->mcdump.mac_nr = nr + 1;
	spin_unlock(&pdata->mcdump.lock);
}
static int dump_mc_format_by_net_flags(struct mcdump *dump, char *ptr, int sz_buf)
{
	int count;
	if (dump->net_flags & IFF_MULTICAST) {
		count = snprintf_s(ptr, sz_buf, sz_buf - 1, "%s", "MULTICAST ");
		if (count < 0) {
			pr_err("snprintf MULTICAST error, count=%d\n", count);
			return count;
		}
		sz_buf -= count;
		ptr += count;
	}

	if (dump->net_flags & IFF_PROMISC) {
		count = snprintf_s(ptr, sz_buf, sz_buf - 1, "%s", "PROMISC ");
		if (count < 0) {
			pr_err("snprintf PROMISC error, count=%d\n", count);
			return count;
		}
		sz_buf -= count;
		ptr += count;
	}

	if (dump->net_flags & IFF_ALLMULTI) {
		count = snprintf_s(ptr, sz_buf, sz_buf - 1, "%s", "ALLMULTI ");
		if (count < 0) {
			pr_err("snprintf ALLMULTI error, count=%d\n", count);
			return count;
		}
		sz_buf -= count;
		ptr += count;
	}

	count = snprintf_s(ptr, sz_buf, sz_buf - 1, "\n	mac filters:%d \n", dump->mac_nr);
	if (count < 0) {
		pr_err("snprintf mac filters error, count=%d\n", count);
		return count;
	}
	sz_buf -= count;
	ptr += count;

	return 0;
}
static int dump_mc_drop(int index, struct mcdump *dump, char *buf, int sz_buf)
{
	int ix, count;
	char *ptr = buf;
	char *pmac = NULL;

	struct regdef_s regdef[] = {
		{"	Rx packets:%u ", 0x618},
		{"dropped:%u\n", 0x64C},
	};

	count = snprintf_s(ptr, sz_buf, sz_buf - 1, "eth%d multicast:\n", index);
	if (count < 0) {
		pr_err("snprintf buf error, count=%d\n", count);
		return count;
	}
	sz_buf -= count;
	ptr += count;

	for (ix = 0; ix < ARRAY_SIZE(regdef); ix++) {
		count = snprintf_s(ptr, sz_buf, sz_buf - 1, regdef[ix].fmt, readl(dump->base + regdef[ix].offset));
		if (count < 0) {
			pr_err("snprintf buf error, ix=%d, count=%d\n", ix, count);
			return count;
		}
		sz_buf -= count;
		ptr += count;
	}

	count = snprintf_s(ptr, sz_buf, sz_buf - 1, "%s", "	state:");
	if (count < 0) {
		pr_err("snprintf state error, count=%d\n", count);
		return count;
	}
	sz_buf -= count;
	ptr += count;

	count = dump_mc_format_by_net_flags(dump, ptr, sz_buf);
	if (count)
		return count;

	for (ix = 0; ix < dump->mac_nr; ix++) {
		pmac = dump->mac + ix * ETH_ALEN;
		count = snprintf_s(ptr, sz_buf, sz_buf - 1, MAC_FMT, ix,
					pmac[0], pmac[1], pmac[2], /* 2:mac addr index */
					pmac[3], pmac[4], pmac[5]); /* 3,4,5:mac addr index */
		if (count < 0) {
			pr_err("snprintf mac error, ix=%d, count=%d\n", ix, count);
			return count;
		}
		sz_buf -= count;
		ptr += count;
	}

	return ptr - buf;
}

static int fo_dump_ethmc_open(struct inode *inode, struct file *file)
{
	struct mcdump *data;
	int count = 0, sz_buf;
	char *ptr;
	struct hleth_platdrv_data *pdata = inode->i_private;

	data = &pdata->mcdump;

	file->private_data = (void *)data;

	ptr = data->prbuf;
	sz_buf = sizeof(data->prbuf);

	count = dump_mc_drop(0, data, ptr, sz_buf);

	data->sz_prbuf = count;

	return nonseekable_open(inode, file);
}

static ssize_t fo_dump_ethmc_read(struct file *filp, char __user *ubuf,
				 size_t sz_ubuf, loff_t *ppos)
{
	struct mcdump *dump = (struct mcdump *)filp->private_data;

	return simple_read_from_buffer(ubuf, sz_ubuf, ppos, dump->prbuf,
		dump->sz_prbuf);
}

static const struct file_operations ethmc_fops = {
	.owner = THIS_MODULE,
	.open = fo_dump_ethmc_open,
	.read  = fo_dump_ethmc_read,
	.llseek = no_llseek,
};

static const struct file_operations ethstats_fops = {
	.owner = THIS_MODULE,
	.open = fo_dump_ethstats_open,
	.read  = fo_dump_ethstats_read,
	.llseek = no_llseek,
};

#ifdef HLETH_SKB_MEMORY_STATS
static const struct file_operations eth_mem_stats_fops = {
	.owner = THIS_MODULE,
	.open = fo_dump_eth_mem_stats_open,
	.read  = fo_dump_eth_mem_stats_read,
	.llseek = no_llseek,
};
#endif

int hleth_dbg_init(void __iomem *base, struct platform_device *pdev)
{
	char buf[30];
	int count;
	unsigned int mode = S_IFREG | S_IRUSR | S_IWUSR;
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);

	count = snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "%s", pdev->name);
	if (count < 0) {
		pr_err("snprintf buf error, count=%d\n", count);
		return -ENOENT;
	}
	pdata->root = debugfs_create_dir(buf, NULL);
	if (!pdata->root) {
		pr_err("Can't create '%s' dir.\n", buf);
		return -ENOENT;
	}

	pdata->mcdump.base = base;
	pdata->mcdump.dentry = debugfs_create_file("multicast", mode, pdata->root,
					    pdata, &ethmc_fops);
	if (!pdata->mcdump.dentry) {
		pr_err("Can't create 'read' file.\n");
		goto fail;
	}
	spin_lock_init(&pdata->mcdump.lock);

	pdata->ethstats.base = base;
	pdata->ethstats.macbase[0] = base;
	pdata->ethstats.macbase[1] = base + 0x2000;
	pdata->ethstats.dentry = debugfs_create_file("stats", mode, pdata->root,
					      pdata, &ethstats_fops);
	if (!pdata->ethstats.dentry) {
		pr_err("Can't create 'write' file.\n");
		goto fail;
	}

#ifdef HLETH_SKB_MEMORY_STATS
	pdata->eth_mem_stats.dentry = debugfs_create_file("mem_stats", mode,
						pdata->root, pdata,
						&eth_mem_stats_fops);
	if (!pdata->eth_mem_stats.dentry) {
		pr_err("Can't create 'write' file.\n");
		goto fail;
	}
#endif

	return 0;
fail:
	debugfs_remove_recursive(pdata->root);

	return -ENOENT;
}

int hleth_dbg_deinit(struct platform_device *pdev)
{
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);

	debugfs_remove_recursive(pdata->root);
	return 0;
}
