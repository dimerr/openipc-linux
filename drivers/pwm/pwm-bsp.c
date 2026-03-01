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

#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
/* reg addr of the xth chn. */
#define pwm_period_cfg_addr(x)     (0x0000 + (0x100 * (x)))
#define pwm_duty0_cfg_addr(x)      (0x0004 + (0x100 * (x)))
#define pwm_duty1_cfg_addr(x)      (0x0008 + (0x100 * (x)))
#define pwm_duty2_cfg_addr(x)      (0x000C + (0x100 * (x)))
#define pwm_num_cfg_addr(x)        (0x0010 + (0x100 * (x)))
#define pwm_ctrl_addr(x)           (0x0014 + (0x100 * (x)))
#define pwm_dt_value_cfg_addr(x)   (0x0020 + (0x100 * (x)))
#define pwm_dt_ctrl_cfg_addr(x)    (0x0024 + (0x100 * (x)))
#define pwm_sync_cfg_addr(x)       (0x0030 + (0x100 * (x)))
#define pwm_sync_delay_cfg_addr(x) (0x0034 + (0x100 * (x)))
#define pwm_period_addr(x)         (0x0040 + (0x100 * (x)))
#define pwm_duty0_addr(x)          (0x0044 + (0x100 * (x)))
#define pwm_duty1_addr(x)          (0x0048 + (0x100 * (x)))
#define pwm_duty2_addr(x)          (0x004C + (0x100 * (x)))
#define pwm_num_addr(x)            (0x0050 + (0x100 * (x)))
#define pwm_ctrl_st_addr(x)        (0x0054 + (0x100 * (x)))
#define pwm_dt_value_addr(x)       (0x0060 + (0x100 * (x)))
#define pwm_dt_ctrl_addr(x)        (0x0064 + (0x100 * (x)))
#define pwm_sync_delay_addr(x)     (0x0074 + (0x100 * (x)))

#define PWM_SYNC_START_ADDR        0x0ff0

#define PWM_ALIGN_MODE_SHIFT      4
#define PWM_ALIGN_MODE_MASK       GENMASK(5, 4)

#define PWM_PRE_DIV_SEL_SHIFT     8
#define PWM_PRE_DIV_SEL_MASK      GENMASK(11, 8)

/* pwm dt value */
#define PWM_DT_A_SHIFT      0
#define PWM_DT_A_MASK       GENMASK(31, 16)

#define PWM_DT_B_SHIFT      16
#define PWM_DT_B_MASK       GENMASK(15, 0)

/* pwm dt ctrl */
#define PWM_DTS_OUT_0P_SHIFT      0
#define PWM_DTS_OUT_0P_MASK       BIT(0)

#define PWM_DTS_OUT_0N_SHIFT      1
#define PWM_DTS_OUT_0N_MASK       BIT(1)

#define PWM_DTS_OUT_1P_SHIFT      2
#define PWM_DTS_OUT_1P_MASK       BIT(2)

#define PWM_DTS_OUT_1N_SHIFT      3
#define PWM_DTS_OUT_1N_MASK       BIT(3)

#define PWM_DTS_OUT_2P_SHIFT      4
#define PWM_DTS_OUT_2P_MASK       BIT(4)

#define PWM_DTS_OUT_2N_SHIFT      5
#define PWM_DTS_OUT_2N_MASK       BIT(5)

#elif defined(CONFIG_ARCH_HI3516CV610_FAMILY)
#define pwm_period_cfg_addr(x)     (0x0000 + (0x100 * (x)))
#define pwm_duty0_cfg_addr(x)      (0x0004 + (0x100 * (x)))

#define pwm_num_cfg_addr(x)        (0x0010 + (0x100 * (x)))
#define pwm_ctrl_addr(x)           (0x0014 + (0x100 * (x)))

#define pwm_sync_cfg_addr(x)       (0x0030 + (0x100 * (x)))
#define pwm_sync_delay_cfg_addr(x) (0x0034 + (0x100 * (x)))
#define pwm_period_addr(x)         (0x0040 + (0x100 * (x)))
#define pwm_duty0_addr(x)          (0x0044 + (0x100 * (x)))
#define pwm_num_addr(x)            (0x0050 + (0x100 * (x)))
#define pwm_ctrl_st_addr(x)        (0x0054 + (0x100 * (x)))
#define pwm_sync_delay_addr(x)     (0x0074 + (0x100 * (x)))

#define PWM_SYNC_START_ADDR        0x0ff0

#define PWM_ALIGN_MODE_SHIFT      4
#define PWM_ALIGN_MODE_MASK       GENMASK(5, 4)

#else

#define pwm_period_cfg_addr(x)    (((x) * 0x20) + 0x0)
#define pwm_duty0_cfg_addr(x)    (((x) * 0x20) + 0x4)
#define pwm_cfg2_addr(x)    (((x) * 0x20) + 0x8)
#define pwm_ctrl_addr(x)    (((x) * 0x20) + 0xC)

#endif

/* pwm ctrl */
#define PWM_ENABLE_SHIFT    0
#define PWM_ENABLE_MASK     BIT(0)

#define PWM_POLARITY_SHIFT  1
#define PWM_POLARITY_MASK   BIT(1)

#define PWM_KEEP_SHIFT      2
#define PWM_KEEP_MASK       BIT(2)

#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
/* pwm period */
#define PWM_PERIOD_MASK     GENMASK(31, 0)

/* pwm duty */
#define PWM_DUTY_MASK       GENMASK(31, 0)
#endif

#ifdef CONFIG_ARCH_HI3516CV610_FAMILY
/* pwm period */
#define PWM_PERIOD_MASK     GENMASK(19, 0)

/* pwm duty */
#define PWM_DUTY_MASK       GENMASK(19, 0)
#endif

#define PWM_MHZ             1000000
#define PWM_PERIOD_HZ       1000
#define PWM_RESET_WAIT_MS   30

#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
#define MAX_COUNT_VALUE  0x100000000
#endif

#ifdef CONFIG_ARCH_HI3516CV610_FAMILY
#define MAX_COUNT_VALUE  0x100000
#endif

enum pwm_pre_div {
	PWM_PRE_DIV_1 = 0,
	PWM_PRE_DIV_2,
	PWM_PRE_DIV_4,
	PWM_PRE_DIV_8,
	PWM_PRE_DIV_16,
	PWM_PRE_DIV_32,
	PWM_PRE_DIV_64,
	PWM_PRE_DIV_128,
	PWM_PRE_DIV_256,
};

enum pwm_align {
	PWM_ALIGN_RIGHT = 0,
	PWM_ALIGN_LEFT,
	PWM_ALIGN_MIDDLE,
};

typedef enum {
	PWM_CHN_0 = 0,
	PWM_CHN_1,
	PWM_CHN_2,
	PWM_CHN_3,
	PWM_CHN_4,
	PWM_CHN_5,
} pwm_chn_index;

struct bsp_pwm_chip {
#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
	unsigned int triple_output_map;
#endif
	struct pwm_chip	chip;
	struct clk *clk;
	void __iomem *base;
	struct reset_control *rstc;
};

struct bsp_pwm_soc {
	u32 num_pwms;
	const char *pwm_name;
};

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

#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
static bool bsp_pwm_is_triple_output_chn(unsigned int triple_output_map, pwm_chn_index chn_index)
{
	return ((triple_output_map >> chn_index) & 1);
}
#endif

static void bsp_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);

	bsp_pwm_set_bits(bsp_pwm_chip->base, pwm_ctrl_addr(pwm->hwpwm),
			PWM_ENABLE_MASK, 0x1);

#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
	if (bsp_pwm_is_triple_output_chn(bsp_pwm_chip->triple_output_map, pwm->hwpwm) == 1) {
		if ((pwm->state.duty_cycle == 0) || (pwm->state.duty_cycle1 == 0) || (pwm->state.duty_cycle2 == 0)) {
			printk("WRN: When duty_cycle/1/2 is set to 0, the output will be a duty cycle of 1 cycle.\n"
				"If you need a zero duty cycle output, you can change pinout from PWM to GPIO.\n"
				"Please refer to the PWM section in the Peripheral Device Driver Operation Guide.\n");
		}
	} else {
#endif
		if (pwm->state.duty_cycle == 0) {
			printk("WRN: When duty_cycle is set to 0, the output will be a duty cycle of 1 cycle.\n"
				"If you need a zero duty cycle output, you can change pinout from PWM to GPIO.\n"
				"Please refer to the PWM section in the Peripheral Device Driver Operation Guide.\n");
		}
#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
	}
#endif
}

static void bsp_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);

	bsp_pwm_set_bits(bsp_pwm_chip->base, pwm_ctrl_addr(pwm->hwpwm),
			PWM_ENABLE_MASK, 0x0);
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

static void bsp_pwm_config(struct pwm_chip *chip,
					struct pwm_device *pwm,
					const struct pwm_state *state)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);
	u64 freq, period, duty, max_value, min_value;
#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
	u64 duty1, duty2;
#endif
	freq = div_u64((u64)clk_get_rate(bsp_pwm_chip->clk), PWM_MHZ);
	max_value = div_u64_round(MAX_COUNT_VALUE * PWM_PERIOD_HZ, freq);
	min_value = div_u64_roundup(div_u64(PWM_PERIOD_HZ, 2), freq); // 2 means half cycle number

	if (state->period > max_value) {
		printk("ERR: register NOT applied, period should not more than max config value:%llu ns.\n", max_value);
		return;
	}
	period = div_u64_round(freq * state->period, PWM_PERIOD_HZ);
	if (period == 0) {
		printk("ERR: register NOT applied, period is less than min config value:%llu ns.\n", min_value);
		return;
	}

	duty = div_u64_round(freq * state->duty_cycle, PWM_PERIOD_HZ);
#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
	if ((state->duty_cycle1 > state->period) || (state->duty_cycle2 > state->period)) {
		printk("ERR: register NOT applied, duty_cycle1/2 should not more than period value.\n");
		return;
	}
	duty1 = div_u64_round(freq * state->duty_cycle1, PWM_PERIOD_HZ);
	duty2 = div_u64_round(freq * state->duty_cycle2, PWM_PERIOD_HZ);
	bsp_pwm_set_bits(bsp_pwm_chip->base, pwm_ctrl_addr(pwm->hwpwm),
		PWM_PRE_DIV_SEL_MASK, (PWM_PRE_DIV_1 << PWM_PRE_DIV_SEL_SHIFT));
#endif
	bsp_pwm_set_bits(bsp_pwm_chip->base, pwm_period_cfg_addr(pwm->hwpwm),
			PWM_PERIOD_MASK, (u32)(period - 1));

	bsp_pwm_set_bits(bsp_pwm_chip->base, pwm_duty0_cfg_addr(pwm->hwpwm),
			PWM_DUTY_MASK, (u32)((duty == 0) ? 0 : (duty - 1)));
#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
	if (bsp_pwm_is_triple_output_chn(bsp_pwm_chip->triple_output_map, pwm->hwpwm) == 1) {
		bsp_pwm_set_bits(bsp_pwm_chip->base, pwm_duty1_cfg_addr(pwm->hwpwm),
				PWM_DUTY_MASK, (u32)((duty1 == 0) ? 0 : (duty1 - 1)));

		bsp_pwm_set_bits(bsp_pwm_chip->base, pwm_duty2_cfg_addr(pwm->hwpwm),
				PWM_DUTY_MASK, (u32)((duty2 == 0) ? 0 : (duty2 - 1)));
	}
#endif
}

static void bsp_pwm_set_polarity(struct pwm_chip *chip,
					struct pwm_device *pwm,
					enum pwm_polarity polarity)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);

	if (polarity == PWM_POLARITY_INVERSED)
		bsp_pwm_set_bits(bsp_pwm_chip->base, pwm_ctrl_addr(pwm->hwpwm),
				PWM_POLARITY_MASK, (0x1 << PWM_POLARITY_SHIFT));
	else
		bsp_pwm_set_bits(bsp_pwm_chip->base, pwm_ctrl_addr(pwm->hwpwm),
				PWM_POLARITY_MASK, (0x0 << PWM_POLARITY_SHIFT));
}

static void bsp_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				struct pwm_state *state)
{
	struct bsp_pwm_chip *bsp_pwm_chip = to_bsp_pwm_chip(chip);
	void __iomem *base;
	u32 freq, value;

	freq = div_u64((u64)clk_get_rate(bsp_pwm_chip->clk), PWM_MHZ);
	base = bsp_pwm_chip->base;

	value = readl(base + pwm_period_cfg_addr(pwm->hwpwm));
	state->period = div_u64(value * PWM_PERIOD_HZ, freq);

	value = readl(base + pwm_duty0_cfg_addr(pwm->hwpwm));
	state->duty_cycle = div_u64(value * PWM_PERIOD_HZ, freq);
#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
	if (bsp_pwm_is_triple_output_chn(bsp_pwm_chip->triple_output_map, pwm->hwpwm) == 1) {
		value = readl(base + pwm_duty1_cfg_addr(pwm->hwpwm));
		state->duty_cycle1 = div_u64(value * PWM_PERIOD_HZ, freq);
		value = readl(base + pwm_duty2_cfg_addr(pwm->hwpwm));
		state->duty_cycle2 = div_u64(value * PWM_PERIOD_HZ, freq);
	}
#endif
	value = readl(base + pwm_ctrl_addr(pwm->hwpwm));
	state->enabled = (PWM_ENABLE_MASK & value);
}

static int bsp_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
				const struct pwm_state *state)
{
	if (state->polarity != pwm->state.polarity)
		bsp_pwm_set_polarity(chip, pwm, state->polarity);

	if (state->period != pwm->state.period ||
#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
		state->duty_cycle1 != pwm->state.duty_cycle1 ||
		state->duty_cycle2 != pwm->state.duty_cycle2 ||
#endif
		state->duty_cycle != pwm->state.duty_cycle)
		bsp_pwm_config(chip, pwm, state);

	if (state->enabled != pwm->state.enabled) {
		if (state->enabled)
			bsp_pwm_enable(chip, pwm);
		else
			bsp_pwm_disable(chip, pwm);
	}

	return 0;
}

static const struct pwm_ops bsp_pwm_ops = {
	.get_state = bsp_pwm_get_state,
	.apply = bsp_pwm_apply,

	.owner = THIS_MODULE,
};

static void bsp_pwm_probe_set_chip_ops(struct platform_device *pdev, struct bsp_pwm_chip *pwm_chip)
{
	pwm_chip->chip.ops = &bsp_pwm_ops;
	pwm_chip->chip.dev = &pdev->dev;
	pwm_chip->chip.base = -1;
	pwm_chip->chip.of_xlate = of_pwm_xlate_with_flags;
	pwm_chip->chip.of_pwm_n_cells = 3;
}

static int bsp_pwm_probe(struct platform_device *pdev)
{
	struct bsp_pwm_chip *pwm_chip;
	struct resource *res;
	int i, ret;

	pwm_chip = devm_kzalloc(&pdev->dev, sizeof(*pwm_chip), GFP_KERNEL);
	if (pwm_chip == NULL)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;
	pwm_chip->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pwm_chip->base))
		return PTR_ERR(pwm_chip->base);

	pwm_chip->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pwm_chip->clk)) {
		dev_err(&pdev->dev, "getting clock failed with %ld\n", PTR_ERR(pwm_chip->clk));
		return PTR_ERR(pwm_chip->clk);
	}
#ifdef CONFIG_ARCH_HI3519DV500_FAMILY
	device_property_read_u32(&pdev->dev, "triple-output-map", &pwm_chip->triple_output_map);
#endif
	ret = device_property_read_u32(&pdev->dev, "chn-num", &pwm_chip->chip.npwm);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to get property \"chn-num\"\n");
		return -EINVAL;
	}

	bsp_pwm_probe_set_chip_ops(pdev, pwm_chip);

	ret = clk_prepare_enable(pwm_chip->clk);
	if (ret < 0)
		return ret;

	pwm_chip->rstc = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(pwm_chip->rstc)) {
		clk_disable_unprepare(pwm_chip->clk);
		return PTR_ERR(pwm_chip->rstc);
	}

	reset_control_assert(pwm_chip->rstc);
	msleep(PWM_RESET_WAIT_MS);
	reset_control_deassert(pwm_chip->rstc);

	ret = pwmchip_add(&pwm_chip->chip);
	if (ret < 0) {
		reset_control_assert(pwm_chip->rstc);
		clk_disable_unprepare(pwm_chip->clk);
		return ret;
	}

	for (i = 0; i < pwm_chip->chip.npwm; i++)
		bsp_pwm_set_bits(pwm_chip->base, pwm_ctrl_addr(i), PWM_KEEP_MASK, (0x1 << PWM_KEEP_SHIFT));

	platform_set_drvdata(pdev, pwm_chip);

	return 0;
}

static int bsp_pwm_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct bsp_pwm_chip *pwm_chip;

	pwm_chip = platform_get_drvdata(pdev);

	ret = pwmchip_remove(&pwm_chip->chip);

	reset_control_assert(pwm_chip->rstc);
	clk_disable_unprepare(pwm_chip->clk);

	return ret;
}

static const struct of_device_id bsp_pwm_of_match[] = {
	{ .compatible = "vendor,pwm", },
	{ }
};
MODULE_DEVICE_TABLE(of, bsp_pwm_of_match);

static struct platform_driver bsp_pwm_driver = {
	.driver = {
		.name = "bsp-pwm",
		.of_match_table = bsp_pwm_of_match,
	},
	.probe = bsp_pwm_probe,
	.remove	= bsp_pwm_remove,
};
module_platform_driver(bsp_pwm_driver);

MODULE_LICENSE("GPL");
