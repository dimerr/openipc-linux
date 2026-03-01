/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */
#ifndef __VENDOR_LINUX_SPI_H
#define __VENDOR_LINUX_SPI_H

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/reset.h>

#define VENDOR_SSP_WRITE_BITS(reg, val, mask, sb) \
	((reg) = (((reg) & ~(mask)) | (((val)<<(sb)) & (mask))))
/*
 * The Vendor version of this block adds some bits
 * in SSP_CR1
 */
#define VENDOR_SSP_CR1_MASK_SSE 	(0x1UL << 1)
#define VENDOR_SSP_CR1_MASK_BITEND 	(0x1UL << 4)
#define VENDOR_SSP_CR1_MASK_ALTASENS 	(0x1UL << 6)

#define VENDOR_SSP_CR0(r)		((r) + 0x000)
#define VENDOR_SSP_CR1(r)		((r) + 0x004)
#define VENDOR_SSP_DR(r)		((r) + 0x008)
#define VENDOR_SSP_SR(r)		((r) + 0x00C)
#define VENDOR_SSP_CPSR(r)		((r) + 0x010)
#define VENDOR_SSP_IMSC(r)		((r) + 0x014)
#define VENDOR_SSP_RIS(r)		((r) + 0x018)
#define VENDOR_SSP_MIS(r)		((r) + 0x01C)
#define VENDOR_SSP_ICR(r)		((r) + 0x020)
#define VENDOR_SSP_DMACR(r)		((r) + 0x024)
#define VENDOR_SSP_CSR(r)		((r) + 0x030)
#define VENDOR_SSP_ITCR(r)		((r) + 0x080)
#define VENDOR_SSP_ITIP(r)		((r) + 0x084)
#define VENDOR_SSP_ITOP(r)		((r) + 0x088)
#define VENDOR_SSP_TDR(r)		((r) + 0x08C)
#define VENDOR_SSP_TX_FIFO_CR(r)	((r) + 0x028)
#define VENDOR_SSP_RX_FIFO_CR(r)	((r) + 0x02C)

#define VENDOR_SSP_POLLING_TIMEOUT 	1000
#define VENDOR_SSP_DRIVE_TX		0
#define VENDOR_SSP_DO_NOT_DRIVE_TX	1

#define VENDOR_SSP_DISABLE_IRQ		0x0
#define VENDOR_SSP_TRAINING_DATA	0x5aa5
#define VENDOR_SSP_TRAINING_START	0x2
#define VENDOR_SSP_TRAINING_END		0x0
#define VENDOR_SSP_WAIT_TIME		100
#define VENDOR_SSP_DEFAULT_STATUS	0x3
#define VENDOR_SSP_RX_STATUS		0x7
#define VENDOR_SSP_TX_STATUS		0x2

#define VENDOR_SSP_BITEND_SHIFT_BIT	4
#define VENDOR_SSP_ALTASENS_SHIFT_BIT	6

struct cs_data {
	struct resource		res;
	void __iomem		*virt_addr;
	unsigned int		cs_sb;
	unsigned int		cs_mask_bit;
};

struct ssp_vendor_data {
	unsigned int slave_tx_disable;
	unsigned int num_cs;
	struct cs_data cs_data;
	void __iomem *virtbase;
};

int vendor_ssp_init(struct ssp_vendor_data *vendor_data, void __iomem *virtbase,
		struct spi_master *master, struct amba_device *adev, bool internal_cs_ctrl);

void vendor_ssp_setup(struct ssp_vendor_data *vendor_data,
		struct pl022_config_chip *chip_info, struct spi_device *spi, u16 *cr1);

void vendor_internal_cs_control(struct ssp_vendor_data *vendor_data,
		int cur_cs, u32 command);

#endif /* __VENDOR_LINUX_SPI_H */
