/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */
#include "vendor_gpio.h"

struct gpio_vendor_irq_data {
	void __iomem *base;
	struct gpio_chip *gc;
};

irqreturn_t vendor_gpio_irq_handler(int irq, void *data)
{
	unsigned long pending;
	int offset;
	struct gpio_vendor_irq_data *vendor_irq_data = data;
	struct gpio_chip *gc = vendor_irq_data->gc;

	pending = readb(vendor_irq_data->base + VENDOR_GPIOMIS);
	writeb(pending, vendor_irq_data->base + VENDOR_GPIOIC);
	if (pending) {
		for_each_set_bit(offset, &pending, VENDOR_GPIO_NR)
			generic_handle_irq(irq_find_mapping(gc->irq.domain,
							    offset));
	}

	return IRQ_HANDLED;
}

int vendor_gpio_init_clk_and_base(struct amba_device *adev, struct gpio_chip *gc,
		void __iomem *base)
{
	int ret, gpio_idx;
	struct clk *clk;
	struct device *dev = &adev->dev;
	struct gpio_irq_chip *girq = &gc->irq;

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		dev_warn(dev, "The GPIO clock automatic enable not support\n");
	} else {
		ret = clk_prepare_enable(clk);
		if (ret) {
			dev_warn(dev, "The GPIO clock request failed\n");
			return ret;
		}
	}

	if (dev->of_node) {
		gpio_idx = of_alias_get_id(dev->of_node, "gpio");
		if (gpio_idx < 0)
			return -ENOMEM;
		gc->base = gpio_idx * VENDOR_GPIO_NR;
	}

	if (gc->base < 0)
		gc->base = -1;

	writeb(0, base + VENDOR_GPIOIE); /* disable irqs */

	girq->parent_handler = (irq_flow_handler_t)vendor_gpio_irq_handler;
	devm_kfree(dev, girq->parents);
	girq->num_parents = 0;

	return 0;
}
EXPORT_SYMBOL(vendor_gpio_init_clk_and_base);

int vendor_gpio_init_irq(struct amba_device *adev, struct gpio_chip *gc,
		void __iomem *base)
{
	int ret, gpio_idx;
	struct device *dev = &adev->dev;
	struct gpio_vendor_irq_data *vendor_irq_data = NULL;

	vendor_irq_data = devm_kzalloc(dev, sizeof(struct gpio_vendor_irq_data), GFP_KERNEL);
	if (vendor_irq_data == NULL)
		return -ENOMEM;

	vendor_irq_data->base = base;
	vendor_irq_data->gc = gc;

	ret = devm_request_irq(dev, adev->irq[0], vendor_gpio_irq_handler, IRQF_SHARED,
			dev_name(dev), vendor_irq_data);
	if (ret) {
		dev_info(dev, "request irq failed: %d\n", ret);
		return ret;
	}

	for (gpio_idx = 0; gpio_idx < gc->ngpio; gpio_idx++)
		irq_set_parent(irq_find_mapping(gc->irq.domain, gpio_idx), adev->irq[0]);

	return 0;
}
EXPORT_SYMBOL(vendor_gpio_init_irq);
