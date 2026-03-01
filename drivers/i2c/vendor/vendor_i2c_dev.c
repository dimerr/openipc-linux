/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */

#include "vendor_i2c_dev.h"

int i2c_config_flags(struct i2c_client *client, unsigned long arg)
{
	if (arg & I2C_M_16BIT_REG)
		client->flags |= I2C_M_16BIT_REG;
	else
		client->flags &= ~I2C_M_16BIT_REG;

	if (arg & I2C_M_16BIT_DATA)
		client->flags |= I2C_M_16BIT_DATA;
	else
		client->flags &= ~I2C_M_16BIT_DATA;

	if (arg & I2C_M_DMA)
		client->flags |= I2C_M_DMA;
	else
		client->flags &= ~I2C_M_DMA;

	return 0;
}

int i2c_config_mul_reg(struct i2c_client *client, unsigned long arg)
{
	int ret;
	struct i2c_msg msg;
	unsigned int reg_width;
	unsigned int data_width;
	unsigned int reg_data_width;

	if (copy_from_user(&msg,
			   (struct i2c_msg __user *)arg,
			   sizeof(msg)))
		return -EFAULT;

	/* i2c slave dev reg width */
	if (client->flags & I2C_M_16BIT_REG)
		reg_width = 2;
	else
		reg_width = 1;

	/* i2c send data width */
	if (client->flags & I2C_M_16BIT_DATA)
		data_width = 2;
	else
		data_width = 1;

	reg_data_width = reg_width + data_width;

	msg.buf = memdup_user(msg.buf, msg.len);

	if (IS_ERR(msg.buf)) {
		printk(KERN_ERR "dump user fail!!!\n");
		return PTR_ERR(msg.buf);
	}

	if (msg.len == 0 || reg_data_width > msg.len || msg.len % reg_data_width != 0) {
		printk(KERN_ERR "msg.len err!!!\n");
		kfree(msg.buf);
		return -EINVAL;
	}

	ret = bsp_i2c_master_send_mul_reg(client, msg.buf, msg.len, reg_data_width);

	kfree(msg.buf);

	return ret;
}
