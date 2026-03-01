/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */
#ifndef __VENDOR_I2C_DEV_H
#define __VENDOR_I2C_DEV_H

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define I2C_CONFIG_MUL_REG	0x070c
#define I2C_CONFIG_FLAGS	0x070d

int i2c_config_flags(struct i2c_client *client, unsigned long arg);

int i2c_config_mul_reg(struct i2c_client *client, unsigned long arg);

#endif /* __VENDOR_I2C_DEV_H */
