/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2025. All rights reserved.
 */
#include <linux/clk.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <asm/io.h>
#include <linux/sysfs.h>
#include <linux/limits.h>
#include <linux/securec.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/pm_wakeirq.h>

struct bsp_timer {
	void __iomem *reg_base;
	u64 delay_time_ms;
	enum {
		PERIODIC_TIMER = 0,
		ONE_SHOT_TIMER,
		WAKEUP_TIMER,
	} timer_mode;
	enum {
		TIMER_STOPED,
		TIMER_RUNNING,
	} timer_status;
	struct clk *apb_clk;
	struct clk *clk;
	int irq;
	struct platform_device *pdev;
};

enum {
	REG_LOAD,
	REG_VALUE,
	REG_CONTROL,
	REG_INTCLR,
	REG_RIS,
	REG_MIS,
	REG_BGLOAD,
	REG_END,
};

static u8 bsp_timer_reg_map[REG_END] = {
	[REG_LOAD] = 0x0, [REG_VALUE] = 0x4, [REG_CONTROL] = 0x8, [REG_INTCLR] = 0xc,
	[REG_RIS] = 0x10, [REG_MIS] = 0x14,  [REG_BGLOAD] = 0x18,
};

union reg_load {
	struct {
		u32 timer0_load;
	} bits;
	u32 u32;
};

union reg_value {
	struct {
		u32 timer0_value;
	} bits;
	u32 u32;
};

union reg_control {
	struct {
		enum { TIMER_PERIODIC_OR_CLOCKING = 0, TIMER_ONESHOT } oneshot : 1;
		enum { TIMER_SIZE_16BIT = 0, TIMER_SIZE_32BIT } timersize : 1;
		enum {
			DIV_1 = 0,
			DIV_16,
			DIV_256,
			DIV_256_BY_DEFAULT,
		} timerpre : 2;
		u32 reserved0 : 1;
		enum { DISABLE_TIMER_RIS_INT = 0, ENABLE_TIMER_RIS_INT } intenable : 1;
		enum {
			TIMER_CLOCKING,
			TIMER_PERIODIC
		} timermode : 1;
		enum {
			DISABLED_TIMER = 0,
			ENABLED_TIMER,
		} timeren : 1;
	} bits;
	u32 u32;
};

union reg_intclr {
	struct {
		enum { CLEAR_INT } intclr;
	} bits;
	u32 u32;
};

union reg_ris {
	struct {
		u32 ris;
	} bits;
	u32 u32;
};

union reg_mis {
	struct {
		u32 mis : 1;
	} bits;
	u32 u32;
};

union reg_bgload {
	struct {
		u32 bgload;
	} bits;
	u32 u32;
};

#define bsptimer_read_field(timer, reg, reg_type, field)                                                               \
	({                                                                                                             \
		union reg_type reg_val;                                                                                \
		reg_val.u32 = bsptimer_read((timer), (reg));                                                           \
		reg_val.bits.field;                                                                                    \
	})

#define bsptimer_write_field(timer, reg, reg_type, field, val)                                                         \
	do {                                                                                                           \
		union reg_type reg_val;                                                                                \
		reg_val.u32 = bsptimer_read((timer), (reg));                                                           \
		reg_val.bits.field = (val);                                                                            \
		bsptimer_write((timer), (reg), reg_val.u32);                                                           \
	} while (0)

static void bsptimer_write(struct bsp_timer *timer, u8 reg, u32 val)
{
	writel(val, timer->reg_base + bsp_timer_reg_map[reg]);
}

static u32 bsptimer_read(struct bsp_timer *timer, u8 reg)
{
	return readl(timer->reg_base + bsp_timer_reg_map[reg]);
}

static ssize_t timer_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bsp_timer *timer = dev_get_drvdata(dev);
	int ret = sprintf_s(buf, PAGE_SIZE, "%d\n", timer->timer_mode);
	if (ret == -1)
		return -EPERM;

	return strlen(buf);
}

static ssize_t timer_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val, ret;
	struct bsp_timer *timer = dev_get_drvdata(dev);
	ret = sscanf_s(buf, "%d\n", &val);
	if (ret == -1)
		return -EPERM;

	if (val > WAKEUP_TIMER)
		return -EPERM;

	timer->timer_mode = val;
	return count;
}
static DEVICE_ATTR_RW(timer_mode);

#define TIMER_HZ 3000000

#define TIMER_COUNT_PER_MS 3000

#define MS_PER_S 1000

#define TIMER_MS_MAX (U32_MAX / TIMER_COUNT_PER_MS)

static ssize_t time_ms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bsp_timer *timer = dev_get_drvdata(dev);
	int ret = sprintf_s(buf, PAGE_SIZE, "%llu\n", timer->delay_time_ms);
	if (ret == -1)
		return -EPERM;

	return strlen(buf);
}

static ssize_t time_ms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u64 val;
	int ret;
	struct bsp_timer *timer = dev_get_drvdata(dev);
	ret = sscanf_s(buf, "%llu\n", &val);
	if (ret == -1)
		return -EPERM;

	if (val > TIMER_MS_MAX)
		return -EPERM;

	timer->delay_time_ms = val;
	return count;
}
static DEVICE_ATTR_RW(time_ms);

static ssize_t timer_start_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bsp_timer *timer = dev_get_drvdata(dev);
	int ret = sprintf_s(buf, PAGE_SIZE, "%d\n", timer->timer_status);
	if (ret == -1)
		return -EPERM;

	return strlen(buf);
}

static void bsp_timer_shutdown(struct bsp_timer *timer);

static void timer_init_routine(struct bsp_timer *timer);

static int bsp_timer_cfg(struct bsp_timer *timer);

static int bsp_timer_start(struct bsp_timer *timer);

enum { TIMER_DISABLED, TIMER_ENABLED };

static ssize_t timer_start_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val, ret;
	struct bsp_timer *timer = dev_get_drvdata(dev);
	ret = sscanf_s(buf, "%d\n", &val);
	if (ret == -1)
		return -EPERM;

	switch (val) {
	case TIMER_DISABLED:
		if (timer->timer_status == TIMER_STOPED)
			return count;
		bsp_timer_shutdown(timer);
		clk_disable_unprepare(timer->clk);
		clk_disable_unprepare(timer->apb_clk);
		timer->timer_status = TIMER_STOPED;
		break;

	case TIMER_ENABLED:
		if (timer->timer_status == TIMER_RUNNING)
			return count;

		clk_prepare_enable(timer->apb_clk);
		clk_prepare_enable(timer->clk);

		if (timer->timer_mode == PERIODIC_TIMER || timer->timer_mode == ONE_SHOT_TIMER) {
			ret = bsp_timer_start(timer);
			if (ret)
				return -EPERM;
		}

		timer->timer_status = TIMER_RUNNING;
		break;
	default:
		return -EPERM;
	}

	return count;
}
static DEVICE_ATTR_RW(timer_start);

static struct attribute *bsp_timer_attrs[] = {
	&dev_attr_timer_mode.attr,
	&dev_attr_time_ms.attr,
	&dev_attr_timer_start.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bsp_timer);

#define DEFAULT_VAL 0

static void bsp_timer_shutdown(struct bsp_timer *timer)
{
	bsptimer_write_field(timer, REG_CONTROL, reg_control, intenable, DISABLE_TIMER_RIS_INT);
	bsptimer_write_field(timer, REG_CONTROL, reg_control, timeren, DISABLED_TIMER);
	bsptimer_write_field(timer, REG_LOAD, reg_load, timer0_load, DEFAULT_VAL);
	bsptimer_write_field(timer, REG_INTCLR, reg_intclr, intclr, CLEAR_INT);
	return;
}

static void timer_init_routine(struct bsp_timer *timer)
{
	bsp_timer_shutdown(timer);
	bsptimer_write_field(timer, REG_CONTROL, reg_control, timerpre, DIV_1);
	bsptimer_write_field(timer, REG_CONTROL, reg_control, timersize, TIMER_SIZE_32BIT);
	return;
}

static int bsp_timer_cfg(struct bsp_timer *timer)
{
	u32 val;

	if (timer->delay_time_ms > TIMER_MS_MAX)
		return -1;

	val = timer->delay_time_ms * TIMER_COUNT_PER_MS;

	timer_init_routine(timer);

	bsptimer_write_field(timer, REG_LOAD, reg_load, timer0_load, val);

	switch (timer->timer_mode) {
	case PERIODIC_TIMER:
		bsptimer_write_field(timer, REG_BGLOAD, reg_bgload, bgload, val);
		bsptimer_write_field(timer, REG_CONTROL, reg_control, oneshot, TIMER_PERIODIC_OR_CLOCKING);
		bsptimer_write_field(timer, REG_CONTROL, reg_control, timermode, TIMER_PERIODIC);
		break;
	case ONE_SHOT_TIMER:
	case WAKEUP_TIMER:
		bsptimer_write_field(timer, REG_CONTROL, reg_control, oneshot, TIMER_ONESHOT);
		break;
	default:
		return -1;
	}

	return 0;
}

static void bsp_timer_start_now(struct bsp_timer *timer)
{
	bsptimer_write_field(timer, REG_CONTROL, reg_control, intenable, ENABLE_TIMER_RIS_INT);
	bsptimer_write_field(timer, REG_CONTROL, reg_control, timeren, ENABLED_TIMER);
}

static int bsp_timer_start(struct bsp_timer *timer)
{
	if (bsp_timer_cfg(timer))
		return -1;

	bsp_timer_start_now(timer);
	return 0;
}

static irqreturn_t dummy_handler(int irq, void *data)
{
	struct device *dev = (struct device *)data;
	struct bsp_timer *timer = dev_get_drvdata(dev);

	bsptimer_write_field(timer, REG_INTCLR, reg_intclr, intclr, CLEAR_INT);

	switch (timer->timer_mode) {
	case PERIODIC_TIMER:
		break;
	case ONE_SHOT_TIMER:
		timer->timer_status = TIMER_STOPED;
		break;
	case WAKEUP_TIMER:
		break;
	default:
		dev_printk(KERN_ERR, dev, "irq: Error timer mode\n");
	}

	return IRQ_HANDLED;
}

static int bsp_timer_probe(struct platform_device *pdev)
{
	struct bsp_timer *bsp_timer;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int irq, ret;

	bsp_timer = devm_kzalloc(&pdev->dev, sizeof(*bsp_timer), GFP_KERNEL);
	if (!bsp_timer)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		return -EBUSY;
	}

	bsp_timer->irq = irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	bsp_timer->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR_OR_NULL(bsp_timer->reg_base)) {
		dev_printk(KERN_ERR, dev, "probe: bsp timer request ioresource failed\n");
		return -ENOMEM;
	}

	bsp_timer->apb_clk = devm_clk_get(&pdev->dev, "apb");
	if (IS_ERR_OR_NULL(bsp_timer->apb_clk)) {
		dev_printk(KERN_ERR, dev, "probe: bsp timer request clk failed\n");
		return -ENOMEM;
	}

	bsp_timer->clk = devm_clk_get(&pdev->dev, "timer");
	if (IS_ERR_OR_NULL(bsp_timer->clk)) {
		dev_printk(KERN_ERR, dev, "probe: bsp timer request clk failed\n");
		return -ENOMEM;
	}

	ret = devm_request_threaded_irq(dev, irq, NULL, dummy_handler, IRQF_ONESHOT, "bsp-timer", dev);
	if (ret)
		return ret;

	bsp_timer->pdev = pdev;

	dev_set_drvdata(dev, bsp_timer);

#ifdef CONFIG_PM_SLEEP
	device_init_wakeup(&pdev->dev, true);
#endif

	return 0;
}

static int bsp_timer_remove(struct platform_device *pdev)
{
	struct bsp_timer *timer = dev_get_drvdata(&pdev->dev);
	timer->timer_status = TIMER_STOPED;
	bsp_timer_shutdown(timer);
	return 0;
}

static const struct of_device_id bsp_timer_of_match[] = {
	{
		.compatible = "vendor,bsp-timer",
	},
	{},
};
MODULE_DEVICE_TABLE(of, bsp_timer_of_match);

#ifdef CONFIG_PM_SLEEP
static int bsp_timer_suspend(struct device *dev)
{
	struct bsp_timer *timer = dev_get_drvdata(dev);

	if (device_may_wakeup(dev) && timer->timer_status == TIMER_RUNNING)
		return enable_irq_wake(timer->irq);

	return 0;
}

static int bsp_timer_resume(struct device *dev)
{
	struct bsp_timer *timer = dev_get_drvdata(dev);

	if (device_may_wakeup(dev) && timer->timer_status == TIMER_RUNNING)
		return disable_irq_wake(timer->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(bsp_timer_pm_ops, bsp_timer_suspend,
			 bsp_timer_resume);
#endif

static struct platform_driver bsp_timer_driver = {
	.probe	= bsp_timer_probe,
	.remove = bsp_timer_remove,
	.driver	= {
		.name = "bsp-timer",
		.of_match_table = of_match_ptr(bsp_timer_of_match),
		.dev_groups = bsp_timer_groups,
#ifdef CONFIG_PM
		.pm = &bsp_timer_pm_ops,
#endif
	},
};
module_platform_driver(bsp_timer_driver);

MODULE_LICENSE("GPL");
