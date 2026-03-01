/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */
#ifndef __VENDOR_LINUX_I2C_H
#define __VENDOR_LINUX_I2C_H

struct i2c_msg;
struct i2c_adapter;
struct i2c_client;

#define I2C_M_16BIT_REG		0x0002	/* indicate reg bit-width is 16bit */
#define I2C_M_16BIT_DATA	0x0008	/* indicate data bit-width is 16bit */
#define I2C_M_DMA		0x0004	/* indicate use dma mode */

extern int bsp_i2c_master_send(const struct i2c_client *client, const char *buf,
		__u16 count);

extern int bsp_i2c_master_send_mul_reg(const struct i2c_client *client, const char *buf,
		__u16 count, unsigned int reg_data_width);

extern int bsp_i2c_master_recv(const struct i2c_client *client, const char *buf,
		int count);

extern int bsp_i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
		int num);

#endif /* __VENDOR_LINUX_I2C_H */
