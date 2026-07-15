// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026  David Loosli <david@codelabs.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * This file contains the Muen RTC chip implementation that is required
 * by all Linux subjects accessung the time info memory provided by the
 * timer subject. It is a minimal real time clock implementation with a
 * read function based on the corresponding Ada implementation for native
 * subjects.
 */

#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/time64.h>

#include <muen/sinfo.h>

/* Time Info Registers */
#define RTC_TIME_BASE		0x00
#define RTC_TICK_RATE		0x08
#define RTC_TIME_ZONE		0x10

struct muen_rtc_dev {
	phys_addr_t			phys_base;
	struct rtc_device	*rtc;
	void __iomem		*virt_base;
};

static int muen_rtc_read(struct device *dev, struct rtc_time *tm)
{
	uint64_t schedule_ticks, time_base, tick_rate;
	int64_t time_zone;
	time64_t time_stamp;

	struct muen_rtc_dev *device;

	device = dev_get_drvdata(dev);

	/* NOTE the following Muen calculations use microseconds */
	time_base = readq(device->virt_base + RTC_TIME_BASE);
	if (time_base == 0ULL || time_base > 253402300799000000ULL)
		return -EINVAL;

	tick_rate = readq(device->virt_base + RTC_TICK_RATE);
	if (tick_rate < 1000000ULL || tick_rate > 100000000000ULL)
		return -EINVAL;

	time_zone = readq(device->virt_base + RTC_TIME_ZONE);
	if (time_zone < -43200000000LL || time_zone > 50400000000LL)
		return -EINVAL;

	schedule_ticks = muen_get_sched_start();
	time_stamp = time_base + time_zone +
		mul_u64_u64_div_u64(schedule_ticks, USEC_PER_SEC, tick_rate);

	/* NOTE lib.c requires seconds since epoch, NOT microseconds */
	rtc_time64_to_tm(time_stamp / USEC_PER_SEC, tm);

	return 0;
}

static const struct rtc_class_ops muen_rtc_ops = {
	.read_time = muen_rtc_read,
};

static int muen_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct muen_rtc_dev *device;

	device = devm_kzalloc(&pdev->dev, sizeof(*device), GFP_KERNEL);
	if (!device)
		return -ENOMEM;

	platform_set_drvdata(pdev, device);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	device->phys_base = res->start;
	device->virt_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(device->virt_base))
		return PTR_ERR(device->virt_base);

	device->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(device->rtc))
		return PTR_ERR(device->rtc);

	device->rtc->ops = &muen_rtc_ops;

	dev_info(&pdev->dev, "init virtual muen rtc device (addr: %#llx)\n",
		(unsigned long long)device->phys_base);

	return devm_rtc_register_device(device->rtc);
}

static const struct of_device_id muen_rtc_of_match[] = {
	{.compatible = "muen,rtc-chip-v0.1" },
	{ }
};
MODULE_DEVICE_TABLE(of, muen_rtc_of_match);

static struct platform_driver muen_rtc_driver = {
	.probe		= muen_rtc_probe,
	.driver		= {
		.name			= "muen-rtcchip",
		.of_match_table	= muen_rtc_of_match,
	},
};

module_platform_driver(muen_rtc_driver);

MODULE_AUTHOR("David Loosli <david@codelabs.ch>");
MODULE_DESCRIPTION("Muen RTC driver");
MODULE_LICENSE("GPL");
