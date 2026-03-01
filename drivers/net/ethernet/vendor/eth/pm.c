/*
 * Copyright (c) CompanyNameMagicTag 2022. All rights reserved.
 * Description: Power management settings.
 * Author: AuthorNameMagicTag
 * Create: 2022-4-20
 */

#include <securec.h>
#include <linux/crc16.h>

#define HLETH_PM_N              (31)
#define HLETH_PM_FILTERS        (4)

struct hleth_pm_config {
	unsigned char index;		/* bit0--eth0 bit1--eth1 */
	unsigned char uc_pkts_enable;
	unsigned char magic_pkts_enable;
	unsigned char wakeup_pkts_enable;
	struct {
		unsigned int	mask_bytes : HLETH_PM_N;
		unsigned int	reserved   : 1;/* userspace ignore this bit */
		unsigned char	offset;	/* >= 12 */
		unsigned char	value[HLETH_PM_N];/* byte string */
		unsigned char	valid;	/* valid filter */
	} filter[HLETH_PM_FILTERS];
};

static unsigned char g_filter_value[HLETH_PM_FILTERS][HLETH_PM_N];

#define HLETH_PMT_CTRL		0x0500
#define HLETH_PMT_MASK0		0x0504
#define HLETH_PMT_MASK1		0x0508
#define HLETH_PMT_MASK2		0x050c
#define HLETH_PMT_MASK3		0x0510
#define HLETH_PMT_CMD		0x0514
#define HLETH_PMT_OFFSET	0x0518
#define HLETH_PMT_CRC1_0	0x051c
#define HLETH_PMT_CRC3_2	0x0520

static void hleth_initcrctable(void);
static unsigned short hleth_computecrc(char* message, int nbytes);
static unsigned short calculate_crc16(char *buf, unsigned int mask)
{
	char data[HLETH_PM_N];
	int i, len = 0;
	int ret;

	ret = memset_s(data, sizeof(data), 0, sizeof(data));
	if (ret != EOK) {
		return (unsigned short)ret;
	}

	for (i = 0; i < HLETH_PM_N; i++) {
		if (mask & 0x1)
			data[len++] = buf[i];

		mask >>= 1;
	}

	return hleth_computecrc(data, len);
}
static unsigned int config_ctrl_set(const struct hleth_pm_config *config)
{
	unsigned int v = 0;
	if (config->uc_pkts_enable)
		v |= 1 << 9;	/* 9:uc pkts wakeup */
	if (config->wakeup_pkts_enable)
		v |= 1 << 2;	/* 2:use filter framework */
	if (config->magic_pkts_enable)
		v |= 1 << 1;	/* magic pkts wakeup */

	v |= 3 << 5;		/* 3,5:clear irq status */
	return v;
}
static int hleth_pmt_config_eth(struct hleth_pm_config *config, struct hleth_netdev_priv *priv)
{
	unsigned int v = 0, cmd = 0, offset = 0;
	unsigned short crc[HLETH_PM_FILTERS] = {0};
	int reg_mask, i;

	if (priv == NULL || config == NULL)
		return -EINVAL;

	local_lock(priv);
	if (config->wakeup_pkts_enable) {
		/* disable wakeup_pkts_enable before reconfig? */
		v = hleth_readl(priv->port_base, HLETH_PMT_CTRL);
		v &= ~(1 << 2); /* 2:left shift val */
		 hleth_writel(priv->port_base, v, HLETH_PMT_CTRL);/* any side effect? */
	} else {
		goto config_ctrl;
	}

/*
 * filter.valid		mask.valid	mask_bytes	effect
 *	0		*		*		no use the filter
 *	1		0		*		all pkts can wake-up(non-exist)
 *	1		1		0		all pkts can wake-up
 *	1		1		!0		normal filter
 */
	/* setup filter */
	for (i = 0; i < HLETH_PM_FILTERS; i++) {
		if (config->filter[i].valid) {
			if (config->filter[i].offset < 12) /* 12:operated value */
				continue;
			/* offset and valid bit */
			offset |= config->filter[i].offset << (i * 8); /* 8:operated value */
			cmd    |= 1 << (i * 8); /* 8:valid bit */
			/* mask */
			reg_mask = HLETH_PMT_MASK0 + (i * 4); /* 4:mask */

			/*
			 * for logic, mask valid bit(bit31) must set to 0,
			 * 0 is enable
			 */
			v = config->filter[i].mask_bytes;
			v &= ~(1 << 31); /* 31:left shift val */
			hleth_writel(priv->port_base, v, reg_mask);

			/* crc */
			memcpy_s(g_filter_value[i], HLETH_PM_N, config->filter[i].value, HLETH_PM_N);
			crc[i] = calculate_crc16(config->filter[i].value, v);
			if (i <= 1) {/* for filter0 and filter 1 */
				v = hleth_readl(priv->port_base, HLETH_PMT_CRC1_0);
				v &= ~(0xFFFF << (16 * i)); /* 16:operated value */
				v |= crc[i] << (16 * i); /* 16:operated value */
				hleth_writel(priv->port_base, v, HLETH_PMT_CRC1_0);
			} else {/* filter2 and filter3 */
				v = hleth_readl(priv->port_base, HLETH_PMT_CRC3_2);
				v &= ~(0xFFFF << (16 * (i - 2))); /* 2,16:operated value */
				v |= crc[i] << (16 * (i - 2)); /* 2,16:operated value */
				hleth_writel(priv->port_base, v, HLETH_PMT_CRC3_2);
			}
		}
	}

	if (cmd) {
		hleth_writel(priv->port_base, offset, HLETH_PMT_OFFSET);
		hleth_writel(priv->port_base, cmd, HLETH_PMT_CMD);
	}

config_ctrl:
	v = config_ctrl_set(config);
	hleth_writel(priv->port_base, v, HLETH_PMT_CTRL);

	local_unlock(priv);

	return 0;
}

/* pmt_config will overwrite pre-config */
int hleth_pmt_config(const struct net_device *ndev, struct hleth_pm_config *config)
{
	static int init;
	struct hleth_netdev_priv *priv = netdev_priv(ndev);

	if (init == 0) {
		hleth_initcrctable();
		init = 1;
	}

	if (hleth_pmt_config_eth(config, priv))
		return -1;

	priv->pm_state_set = true;
	device_set_wakeup_enable(priv->dev, 1);
	priv->mac_wol_enabled = true;
	return 0;
}

int hleth_pmt_get_config(struct net_device *ndev, struct hleth_pm_config *config)
{
	unsigned int val, cmd, offset;
	struct hleth_netdev_priv *ld = netdev_priv(ndev);
	unsigned int i;
	int reg_mask;

	config->index = ld->port;
	val = hleth_readl(ld->port_base, HLETH_PMT_CTRL);
	if (val & (1 << 9)) /* 9: bit */
		config->uc_pkts_enable = 1;
	if (val & (1 << 2)) /* 2: bit */
		config->wakeup_pkts_enable = 1;
	if (val & (1 << 1))
		config->magic_pkts_enable = 1;

	if (config->wakeup_pkts_enable) {
		cmd = hleth_readl(ld->port_base, HLETH_PMT_CMD);
		offset = hleth_readl(ld->port_base, HLETH_PMT_OFFSET);

		for (i = 0; i < HLETH_PM_FILTERS; i++) {
			if ((cmd & (unsigned int)(1 << (i * 8))) == 0) /* 8: bit */
				continue;
			config->filter[i].valid = 1;
			config->filter[i].offset = offset >> (i * 8); /* 8: bit */
			reg_mask = HLETH_PMT_MASK0 + (i * 4); /* 4: mask */
			config->filter[i].mask_bytes = readl(ld->glb_base + reg_mask);
			memcpy_s(config->filter[i].value, HLETH_PM_N, g_filter_value[i], HLETH_PM_N);
		}
	}

	return 0;
}

bool hleth_pmt_enter(struct platform_device *pdev)
{
	int i, pm = false;
	unsigned int v;
	struct hleth_netdev_priv *priv = NULL;
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);

	for (i = 0; i < HLETH_MAX_PORT; i++) {
		if (!pdata->hleth_devs_save[i])
			continue;

		priv = netdev_priv(pdata->hleth_devs_save[i]);

		local_lock(priv);
		if (priv->pm_state_set) {
			v = hleth_readl(priv->port_base, HLETH_PMT_CTRL);
			v |= 1 << 0;	/* enter power down */
			v |= 1 << 3;	/* 3:enable wakeup irq */
			v |= 3 << 5;	/* 3,5:clear irq status */
			hleth_writel(priv->port_base, v, HLETH_PMT_CTRL);

			priv->pm_state_set = false;
			pm = true;
		}
		local_unlock(priv);
	}
	return pm;
}

void hleth_pmt_exit(struct platform_device *pdev)
{
	int i;
	unsigned int v;
	struct hleth_netdev_priv *priv = NULL;
	struct hleth_platdrv_data *pdata = platform_get_drvdata(pdev);

	for (i = 0; i < HLETH_MAX_PORT; i++) {
		if (!pdata->hleth_devs_save[i])
			continue;

		priv = netdev_priv(pdata->hleth_devs_save[i]);

		/* logic auto exit power down mode */
		local_lock(priv);

		v = hleth_readl(priv->port_base, HLETH_PMT_CTRL);
		v &= ~(1 << 0);	/* enter power down */
		v &= ~(1 << 3);	/* 3:enable wakeup irq */

		v |= 3 << 5;	/* 3,5:clear irq status */
		hleth_writel(priv->port_base, v, HLETH_PMT_CTRL);

		local_unlock(priv);

		priv->mac_wol_enabled = false;
	}
}

#define CRC16			/* Change it to CRC16 for CRC16 Computation */

#define FALSE	0
#define TRUE	!FALSE

#if defined(CRC16)
#define CRC_NAME		"CRC-16"
#define POLYNOMIAL		0x8005
#define INITIAL_REMAINDER	0xFFFF
#define FINAL_XOR_VALUE		0x0000
#define REVERSE_DATA		TRUE
#define REVERSE_REMAINDER	FALSE
#endif

#define WIDTH    (8 * sizeof(unsigned short))
#define TOPBIT   (1 << (WIDTH - 1))

#if (REVERSE_DATA == TRUE)
#undef  REVERSE_DATA
#define reverse_data(x)		((unsigned char) reverse((x), 8))
#else
#undef  REVERSE_DATA
#define reverse_data(x)		(x)
#endif

#if (REVERSE_REMAINDER == TRUE)
#undef  REVERSE_REMAINDER
#define reverse_remainder(x)	((unsigned short) reverse((x), WIDTH))
#else
#undef  REVERSE_REMAINDER
#define reverse_remainder(x)	(x)
#endif

static unsigned short crctable[256];

/* Reverse the data
 *
 * Input1: Data to be reversed
 * Input2: number of bits in the data
 * Output: The reversed data
 */
static unsigned long reverse(unsigned long data, unsigned char nbits)
{
	unsigned long  reversed = 0x00000000;
	unsigned char  bit;

	/* Reverse the data about the center bit. */
	for (bit = 0; bit < nbits; ++bit) {
		/* If the LSB bit is set, set the reflection of it. */
		if (data & 0x01)
			reversed |= (1 << ((nbits - 1) - bit));

		data = (data >> 1);
	}
	return reversed;
}

/* This Initializes the partial CRC look up table */
static void hleth_initcrctable(void)
{
	unsigned short remainder;
	unsigned short dividend;
	unsigned char  bit;

	/* Compute the remainder of each possible dividend. */
	for (dividend = 0; dividend < 256; ++dividend) { /* 256:operated value */
		/* Start with the dividend followed by zeros. */
		remainder = dividend << (WIDTH - 8); /* 8:operated value */

		/* Perform modulo-2 division, a bit at a time. */
		for (bit = 8; bit > 0; --bit) { /* 8:init val */
			/* Try to divide the current data bit. */
			if (remainder & TOPBIT)
				remainder = (remainder << 1) ^ POLYNOMIAL;
			else
				remainder = (remainder << 1);
		}

		/* Store the result into the table. */
		crctable[dividend] = remainder;
	}
}

static unsigned short hleth_computecrc(char *message, int nbytes)
{
	unsigned short	remainder = INITIAL_REMAINDER;
	int	byte;
	unsigned char  data;

	/* Divide the message by the polynomial, a byte at a time. */
	for (byte = 0; byte < nbytes; ++byte) {
		data = reverse_data(message[byte]) ^ (remainder >> (WIDTH - 8)); /* 8:operated value */
		remainder = crctable[data] ^ (remainder << 8); /* 8:left shift val */
	}

	/* The final remainder is the CRC. */
	return (reverse_remainder(remainder) ^ FINAL_XOR_VALUE);
}
