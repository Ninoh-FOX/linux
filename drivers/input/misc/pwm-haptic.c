/*
 * pwm-haptic.c - Driver for PWM based haptic devices
 *
 * Copyright (C) 2014 Paul Cercueil <paul@crapouillou.net>
 * Copyright (C) 2014 Maarten ter Huurne <maarten@treewalker.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/bitmap.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#define DEFAULT_PWM_PERIOD 1000000

struct pwm_haptic {
	struct device *dev;
	struct input_dev *input_dev;
	struct pwm_device *pwm;

	unsigned long pwm_period;
	bool is_open;
};

/*** Input/ForceFeedback ***/

static int pwm_haptic_play(struct input_dev *input, void *data,
		      struct ff_effect *effect)
{
	struct pwm_haptic *haptic = input_get_drvdata(input);
	unsigned int level = effect->u.rumble.strong_magnitude;
	u64 duty;

	dev_dbg(haptic->dev, "Configuring PWM for %u%%\n", (level * 100) >> 16);
	duty = ((u64) haptic->pwm_period * (USHRT_MAX - level)) >> 16;
	return pwm_config(haptic->pwm, (int) duty, haptic->pwm_period);
}

static int pwm_haptic_open(struct input_dev *input)
{
	struct pwm_haptic *haptic = input_get_drvdata(input);
	if (haptic->is_open)
		return -EBUSY;

	haptic->is_open = true;
	pwm_config(haptic->pwm, haptic->pwm_period, haptic->pwm_period);
	pwm_enable(haptic->pwm);
	return 0;
}

static void pwm_haptic_close(struct input_dev *input)
{
	struct pwm_haptic *haptic = input_get_drvdata(input);
	pwm_config(haptic->pwm, haptic->pwm_period, haptic->pwm_period);
	pwm_disable(haptic->pwm);
	haptic->is_open = false;
}

/*** Module ***/
#ifdef CONFIG_PM_SLEEP
static int pwm_haptic_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pwm_haptic *haptic = platform_get_drvdata(pdev);

	if (haptic->is_open)
		pwm_disable(haptic->pwm);
	return 0;
}

static int pwm_haptic_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pwm_haptic *haptic = platform_get_drvdata(pdev);

	if (haptic->is_open)
		pwm_enable(haptic->pwm);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pwm_haptic_pm_ops,
		pwm_haptic_suspend, pwm_haptic_resume);

static int pwm_haptic_probe(struct platform_device *pdev)
{
	struct input_dev *idev;
	struct pwm_haptic *haptic;
	int ret;

	haptic = devm_kzalloc(&pdev->dev, sizeof(*haptic), GFP_KERNEL);
	if (!haptic)
		return -ENOMEM;

	haptic->dev = &pdev->dev;

	haptic->input_dev = devm_input_allocate_device(&pdev->dev);
	if (haptic->input_dev == NULL) {
		dev_err(&pdev->dev, "couldn't allocate input device\n");
		return -ENOMEM;
	}

	haptic->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(haptic->pwm)) {
		dev_err(&pdev->dev, "Unable to request PWM\n");
		return PTR_ERR(haptic->pwm);
	}

	haptic->pwm_period = pwm_get_period(haptic->pwm);

	idev = haptic->input_dev;
	idev->open = pwm_haptic_open;
	idev->close = pwm_haptic_close;
	input_set_drvdata(idev, haptic);
	platform_set_drvdata(pdev, haptic);

	idev->name = pdev->name;
	idev->id.version = 1;
	idev->dev.parent = &pdev->dev;
	set_bit(FF_RUMBLE, idev->ffbit);

	ret = input_ff_create_memless(idev, NULL, pwm_haptic_play);
	if (ret < 0) {
		dev_dbg(&pdev->dev, "couldn't register vibrator to FF\n");
		goto err_free_input;
	}

	ret = input_register_device(idev);
	if (ret < 0) {
		dev_dbg(&pdev->dev, "couldn't register input device\n");
		goto err_free_input;
	}

	return 0;

err_free_input:
	input_ff_destroy(haptic->input_dev);
	return ret;
}

static int pwm_haptic_remove(struct platform_device *pdev)
{
	struct pwm_haptic *haptic = platform_get_drvdata(pdev);
	struct input_dev *idev = haptic->input_dev;

	input_unregister_device(idev);
	input_ff_destroy(idev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id pwm_haptic_dt_ids[] = {
	{ .compatible = "pwm-haptic", .data = NULL, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pwm_haptic_dt_ids);
#endif

static struct platform_driver pwm_haptic_driver = {
	.driver	= {
		.name	= "pwm-haptic",
		.pm	= &pwm_haptic_pm_ops,
		.of_match_table = of_match_ptr(pwm_haptic_dt_ids),
	},
	.probe		= pwm_haptic_probe,
	.remove		= pwm_haptic_remove,
};
module_platform_driver(pwm_haptic_driver);

MODULE_ALIAS("platform:pwm-haptic");
MODULE_DESCRIPTION("PWM haptic driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
