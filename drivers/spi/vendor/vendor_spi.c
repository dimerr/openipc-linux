/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */
#include "vendor_spi.h"

void vendor_internal_cs_control(struct ssp_vendor_data *vendor_data,
		int cur_cs, u32 command)
{
	u32 tmp;

	if (vendor_data->num_cs > 1) {
		tmp = readl(vendor_data->cs_data.virt_addr);
		tmp &= ~(vendor_data->cs_data.cs_mask_bit);
		tmp |= ((u32)cur_cs) << vendor_data->cs_data.cs_sb;
		writel(tmp, vendor_data->cs_data.virt_addr);
	}
	if (command == SSP_CHIP_SELECT)
		/* Enable SSP */
		writew((readw(VENDOR_SSP_CR1(vendor_data->virtbase)) |
					VENDOR_SSP_CR1_MASK_SSE),
				VENDOR_SSP_CR1(vendor_data->virtbase));
	else
		/* disable SSP */
		writew((readw(VENDOR_SSP_CR1(vendor_data->virtbase)) &
					(~VENDOR_SSP_CR1_MASK_SSE)),
				VENDOR_SSP_CR1(vendor_data->virtbase));
}
EXPORT_SYMBOL(vendor_internal_cs_control);

void vendor_ssp_setup(struct ssp_vendor_data *vendor_data,
		struct pl022_config_chip *chip_info, struct spi_device *spi, u16 *cr1)
{
	u32 tmp;

	if (spi->master->slave)
		chip_info->hierarchy = SSP_SLAVE;
	else
		chip_info->hierarchy = SSP_MASTER;

	if (vendor_data->slave_tx_disable)
		chip_info->slave_tx_disable = VENDOR_SSP_DO_NOT_DRIVE_TX;
	else
		chip_info->slave_tx_disable = VENDOR_SSP_DRIVE_TX;

	if (spi->mode & SPI_LSB_FIRST)
		tmp = !!SPI_LSB_FIRST;
	else
		tmp = !SPI_LSB_FIRST;
	VENDOR_SSP_WRITE_BITS(*cr1, tmp, VENDOR_SSP_CR1_MASK_BITEND,
			VENDOR_SSP_BITEND_SHIFT_BIT);

	if (spi->mode & SPI_CPHA)
		VENDOR_SSP_WRITE_BITS(*cr1, 0x1, VENDOR_SSP_CR1_MASK_ALTASENS,
				VENDOR_SSP_ALTASENS_SHIFT_BIT);
	else
		VENDOR_SSP_WRITE_BITS(*cr1, 0x0, VENDOR_SSP_CR1_MASK_ALTASENS,
				VENDOR_SSP_ALTASENS_SHIFT_BIT);
}
EXPORT_SYMBOL(vendor_ssp_setup);

static void try_deassert_spi_reset(struct amba_device *adev)
{
	struct reset_control *spi_rst = NULL;
	spi_rst = devm_reset_control_get(&adev->dev, "bsp_spi_rst");
	if (IS_ERR_OR_NULL(spi_rst))
		return;
	/* deassert reset if "resets" property is set */
	dev_info(&adev->dev, "deassert reset\n");
	reset_control_deassert(spi_rst);
}

/* Before using the SPI, you need to read and write a piece
 of data to clear the abnormal status of the RAT memory */
#ifdef CONFIG_ARCH_HI3516CV610_FAMILY
static void vendor_ssp_clr_ratmem_abnormal(struct ssp_vendor_data *vendor_data)
{
	int polling_count = 0;

	/* Disable SSP */
	writew((readw(VENDOR_SSP_CR1(vendor_data->virtbase)) & (~VENDOR_SSP_CR1_MASK_SSE)),
	       VENDOR_SSP_CR1(vendor_data->virtbase));

	writew(VENDOR_SSP_DISABLE_IRQ, VENDOR_SSP_IMSC(vendor_data->virtbase));

	writew(VENDOR_SSP_TRAINING_DATA, VENDOR_SSP_DR(vendor_data->virtbase));

	writew(VENDOR_SSP_TRAINING_START, VENDOR_SSP_ITCR(vendor_data->virtbase));

	while (VENDOR_SSP_POLLING_TIMEOUT > polling_count) {
		if (readw(VENDOR_SSP_SR(vendor_data->virtbase)) == VENDOR_SSP_TX_STATUS)
			break;
		udelay(VENDOR_SSP_WAIT_TIME);
		polling_count++;
	}

	readw(VENDOR_SSP_TDR(vendor_data->virtbase));

	polling_count = 0;
	while (VENDOR_SSP_POLLING_TIMEOUT > polling_count) {
		if (readw(VENDOR_SSP_SR(vendor_data->virtbase)) == VENDOR_SSP_DEFAULT_STATUS)
			break;
		udelay(VENDOR_SSP_WAIT_TIME);
		polling_count++;
	}

	writew(VENDOR_SSP_TRAINING_DATA, VENDOR_SSP_TDR(vendor_data->virtbase));

	polling_count = 0;
	while (VENDOR_SSP_POLLING_TIMEOUT > polling_count) {
		if (readw(VENDOR_SSP_SR(vendor_data->virtbase)) == VENDOR_SSP_RX_STATUS)
			break;
		udelay(VENDOR_SSP_WAIT_TIME);
		polling_count++;
	}

	readw(VENDOR_SSP_DR(vendor_data->virtbase));

	polling_count = 0;
	while (VENDOR_SSP_POLLING_TIMEOUT > polling_count) {
		if (readw(VENDOR_SSP_SR(vendor_data->virtbase)) == VENDOR_SSP_DEFAULT_STATUS)
			break;
		udelay(VENDOR_SSP_WAIT_TIME);
		polling_count++;
	}

	writew(VENDOR_SSP_TRAINING_END, VENDOR_SSP_ITCR(vendor_data->virtbase));
}
#endif

static int vendor_ssp_get_slave_mode_data(struct ssp_vendor_data *vendor_data,
		struct spi_master *master, struct amba_device *adev)
{
	unsigned int slave_mode;
	struct device_node *np = adev->dev.of_node;

	if (of_property_read_u32(np, "vendor,slave_mode",
				&slave_mode) == 0) {
		if (slave_mode == 1) {
			master->slave = true;
		} else if (slave_mode == 0) {
			master->slave = false;
		} else {
			dev_err(&adev->dev, "cannot get slave mode!!!\n");
			return -EINVAL;
		}
	}

	if (of_property_read_u32(np, "vendor,slave_tx_disable",
				&vendor_data->slave_tx_disable)) {
		dev_err(&adev->dev, "cannot get slave_tx_disable!!!\n");
		return -EPROBE_DEFER;
	}

	return 0;
}

static int vendor_ssp_get_cs_data(struct ssp_vendor_data *vendor_data,
		struct amba_device *adev, bool internal_cs_ctrl)
{
	struct device_node *np = adev->dev.of_node;

	if (vendor_data->num_cs > 1 && internal_cs_ctrl) {
		if (of_address_to_resource(np, 1,
					&vendor_data->cs_data.res)) {
			return -EPROBE_DEFER;
		}
		if (of_property_read_u32(np, "spi_cs_sb",
					&vendor_data->cs_data.cs_sb)) {
			return -EPROBE_DEFER;
		}
		if (of_property_read_u32(np, "spi_cs_mask_bit",
					&vendor_data->cs_data.cs_mask_bit)) {
			return -EPROBE_DEFER;
		}
		vendor_data->cs_data.virt_addr = devm_ioremap(&adev->dev,
				vendor_data->cs_data.res.start,
				resource_size(&vendor_data->cs_data.res));
		if (vendor_data->cs_data.virt_addr == NULL) {
			dev_err(&adev->dev, "cs_data.virt_addr nomem!!!\n");
			return -ENOMEM;
		}
	}

	return 0;
}

int vendor_ssp_init(struct ssp_vendor_data *vendor_data, void __iomem *virtbase,
		struct spi_master *master, struct amba_device *adev, bool internal_cs_ctrl)
{
	int ret;

	master->mode_bits |= SPI_LSB_FIRST;
	vendor_data->virtbase = virtbase;
	vendor_data->num_cs = master->num_chipselect;

	ret = vendor_ssp_get_slave_mode_data(vendor_data, master, adev);
	if (ret != 0)
		return ret;

	ret = vendor_ssp_get_cs_data(vendor_data, adev, internal_cs_ctrl);
	if (ret != 0)
		return ret;


	try_deassert_spi_reset(adev);
#ifdef CONFIG_ARCH_HI3516CV610_FAMILY
	vendor_ssp_clr_ratmem_abnormal(vendor_data);
#endif
	writew(0x0, VENDOR_SSP_TX_FIFO_CR(vendor_data->virtbase));
	writew(0x0, VENDOR_SSP_RX_FIFO_CR(vendor_data->virtbase));

	return 0;
}
EXPORT_SYMBOL(vendor_ssp_init);
