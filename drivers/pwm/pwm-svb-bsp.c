/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/reset.h>

#define SVB_PWM0_OUT1_OFFSET	0x4
#define SVB_PWM0_REG_LEN	0x4
#define svb_pwm0_outx_cfg_addr(x)     (SVB_PWM0_OUT1_OFFSET + (SVB_PWM0_REG_LEN * (x)))
/* svb pwm0 period */
#define SVB_PWM0_PERIOD_SHIFT    4
#define SVB_PWM0_PERIOD_MASK     GENMASK(13, 4)
/* svb pwm0 duty */
#define SVB_PWM0_DUTY_SHIFT      16
#define SVB_PWM0_DUTY_MASK       GENMASK(25, 16)
/* svb pwm0 load parm(period and duty) */
#define SVB_PWM0_LOAD_SHIFT      2
#define SVB_PWM0_LOAD_MASK       BIT(2)
#define SVB_PWM0_INV_SHIFT       1
#define SVB_PWM0_INV_MASK        BIT(1)
#define SVB_PWM0_ENABLE_SHIFT    0
#define SVB_PWM0_ENABLE_MASK     BIT(0)

#define PWM_PERIOD_HZ       1000

struct bsp_pwm_chip {
	struct pwm_chip	chip;
	struct clk *clk;
	void __iomem *base;
	struct reset_control *rstc;
};

struct bsp_pwm_soc {
	u32 num_pwms;
	const char *pwm_name;
};

#define MAX_COUNT_VALUE  0x400
#define _1MHZ	1000000
#define _50MHZ  50000000
#define PWM_CELLS_NUM	3

static inline struct bsp_pwm_chip *to_bsp_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct bsp_pwm_chip, chip);
}

static void bsp_pwm_set_bits(void __iomem *base, u32 offset,
					u32 mask, u32 data)
{
	void __iomem *address = base + offset;
	u32 value;

	value = readl(address);
	value &= ~mask;
	value |= (data & mask);
	writel(value, address);
}

static void bsp_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);

	bsp_pwm_set_bits(bsp_pwm_chip->base, svb_pwm0_outx_cfg_addr(pwm->hwpwm),
			SVB_PWM0_ENABLE_MASK, 0x1);

	if (pwm->state.duty_cycle == 0) {
		printk("WRN: When duty_cycle is set to 0, the output will be a duty cycle of 1 cycle.\n"
			"If you need a zero duty cycle output, you can change pinout from PWM to GPIO.\n"
			"Please refer to the PWM section in the Peripheral Device Driver Operation Guide.\n");
	}
}

static void bsp_pwm_load_parm(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);

	bsp_pwm_set_bits(bsp_pwm_chip->base, svb_pwm0_outx_cfg_addr(pwm->hwpwm),
			SVB_PWM0_LOAD_MASK, (0x1 << SVB_PWM0_LOAD_SHIFT));
}

static void bsp_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);

	bsp_pwm_set_bits(bsp_pwm_chip->base, svb_pwm0_outx_cfg_addr(pwm->hwpwm),
			SVB_PWM0_ENABLE_MASK, 0x0);
}

static inline u64 div_u64_round(u64 dividend, u64 divisor)
{
	u64 result;
	result = div_u64(dividend + div_u64(divisor, 2), (u32)divisor); // Division and rounding: (a + b / 2) / b
	return result;
}

static inline u64 div_u64_roundup(u64 dividend, u64 divisor)
{
	u64 result;
	result = div_u64(dividend + divisor - 1, (u32)divisor); // Division and rounding up: (a + b - 1) / b
	return result;
}

static void bsp_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
					const struct pwm_state *state)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);
	u64 freq, period, duty, max_value, min_value;

	freq = div_u64(_50MHZ, _1MHZ);
	max_value = div_u64_round(MAX_COUNT_VALUE * PWM_PERIOD_HZ, freq);
	min_value = div_u64_roundup(div_u64(PWM_PERIOD_HZ, 2), freq); // Half cycle number

	if (state->period > max_value) {
		printk("ERR: register NOT applied, period should not more than max config value:%llu ns.\n", max_value);
		return;
	}
	period = div_u64_round(freq * state->period, PWM_PERIOD_HZ);
	if (period == 0) {
		printk("ERR: register NOT applied, period is less than min config value:%llu ns.\n", min_value);
		return;
	}

	if (state->duty_cycle > state->period) {
		printk("ERR: register NOT applied, duty_cycle should not more than period value.\n");
		return;
	}
	duty = div_u64_round(freq * state->duty_cycle, PWM_PERIOD_HZ);

	bsp_pwm_set_bits(bsp_pwm_chip->base, svb_pwm0_outx_cfg_addr(pwm->hwpwm),
			SVB_PWM0_PERIOD_MASK, (u32)((period - 1) << SVB_PWM0_PERIOD_SHIFT));

	bsp_pwm_set_bits(bsp_pwm_chip->base, svb_pwm0_outx_cfg_addr(pwm->hwpwm),
			SVB_PWM0_DUTY_MASK, (u32)(((duty == 0) ? 0 : (duty - 1)) << SVB_PWM0_DUTY_SHIFT));
}

static void bsp_pwm_set_polarity(struct pwm_chip *chip,
					struct pwm_device *pwm,
					enum pwm_polarity polarity)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);

	if (polarity == PWM_POLARITY_INVERSED)
		bsp_pwm_set_bits(bsp_pwm_chip->base, svb_pwm0_outx_cfg_addr(pwm->hwpwm),
				SVB_PWM0_INV_MASK, (0x1 << SVB_PWM0_INV_SHIFT));
	else
		bsp_pwm_set_bits(bsp_pwm_chip->base, svb_pwm0_outx_cfg_addr(pwm->hwpwm),
				SVB_PWM0_INV_MASK, (0x0 << SVB_PWM0_INV_SHIFT));
}

static void bsp_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				struct pwm_state *state)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);
	void __iomem *base;
	u32 freq, value;

	freq = div_u64(_50MHZ, _1MHZ);
	base = bsp_pwm_chip->base;

	value = ((readl(base + svb_pwm0_outx_cfg_addr(pwm->hwpwm)) && SVB_PWM0_PERIOD_MASK) >> SVB_PWM0_PERIOD_SHIFT);
	state->period = div_u64(value * PWM_PERIOD_HZ, freq);

	value = ((readl(base + svb_pwm0_outx_cfg_addr(pwm->hwpwm)) && SVB_PWM0_DUTY_MASK) >> SVB_PWM0_DUTY_SHIFT);
	state->duty_cycle = div_u64(value * PWM_PERIOD_HZ, freq);

	value = readl(base + svb_pwm0_outx_cfg_addr(pwm->hwpwm));
	state->enabled = (SVB_PWM0_ENABLE_MASK & value);
}

static int bsp_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
				const struct pwm_state *state)
{
	if (state->polarity != pwm->state.polarity)
		bsp_pwm_set_polarity(chip, pwm, state->polarity);

	if (state->period != pwm->state.period ||
		state->duty_cycle != pwm->state.duty_cycle) {
		bsp_pwm_config(chip, pwm, state);
		bsp_pwm_load_parm(chip, pwm);
	}

	if (state->enabled != pwm->state.enabled) {
		if (state->enabled) {
			bsp_pwm_load_parm(chip, pwm);
			bsp_pwm_enable(chip, pwm);
		} else {
			bsp_pwm_disable(chip, pwm);
		}
	}

	return 0;
}

static const struct pwm_ops bsp_pwm_ops = {
	.get_state = bsp_pwm_get_state,
	.apply = bsp_pwm_apply,

	.owner = THIS_MODULE,
};

static int bsp_pwm_probe(struct platform_device *pdev)
{
	struct bsp_pwm_chip *pwm_chip;
	struct resource *res;
	int ret;

	pwm_chip = devm_kzalloc(&pdev->dev, sizeof(*pwm_chip), GFP_KERNEL);
	if (pwm_chip == NULL)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pwm_chip->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pwm_chip->base))
		return PTR_ERR(pwm_chip->base);

	ret = device_property_read_u32(&pdev->dev, "chn-num", &pwm_chip->chip.npwm);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to get property \"chn-num\"\n");
		return -EINVAL;
	}

	pwm_chip->chip.ops = &bsp_pwm_ops;
	pwm_chip->chip.dev = &pdev->dev;
	pwm_chip->chip.base = -1;
	pwm_chip->chip.of_xlate = of_pwm_xlate_with_flags;
	pwm_chip->chip.of_pwm_n_cells = PWM_CELLS_NUM;

	ret = pwmchip_add(&pwm_chip->chip);
	if (ret < 0) {
		return ret;
	}

	platform_set_drvdata(pdev, pwm_chip);

	return 0;
}

static int bsp_pwm_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct bsp_pwm_chip *pwm_chip;

	pwm_chip = platform_get_drvdata(pdev);

	ret = pwmchip_remove(&pwm_chip->chip);

	return ret;
}

static const struct of_device_id bsp_pwm_of_match[] = {
	{ .compatible = "vendor,svb-pwm", },
	{  }
};
MODULE_DEVICE_TABLE(of, bsp_pwm_of_match);

static struct platform_driver bsp_pwm_driver = {
	.driver = {
		.name = "bsp-svb-pwm",
		.of_match_table = bsp_pwm_of_match,
	},
	.probe = bsp_pwm_probe,
	.remove	= bsp_pwm_remove,
};
module_platform_driver(bsp_pwm_driver);

MODULE_LICENSE("GPL");
