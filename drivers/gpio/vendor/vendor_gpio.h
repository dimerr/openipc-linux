/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */
#ifndef __VENDOR_LINUX_GPIO_H
#define __VENDOR_LINUX_GPIO_H

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/gpio/driver.h>
#include <linux/device.h>
#include <linux/amba/bus.h>

#define VENDOR_GPIO_NR	8
#define VENDOR_GPIOIE  0x410
#define VENDOR_GPIOMIS 0x418
#define VENDOR_GPIOIC  0x41C

int vendor_gpio_init_clk_and_base(struct amba_device *adev, struct gpio_chip *gc,
		void __iomem *base);
int vendor_gpio_init_irq(struct amba_device *adev, struct gpio_chip *gc,
		void __iomem *base);

#endif /* __VENDOR_LINUX_GPIO_H */
