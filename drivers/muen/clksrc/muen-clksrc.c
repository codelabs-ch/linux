/*
 * Copyright (C) 2016  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2016  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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
 */

#include <linux/clocksource.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <muen/sinfo.h>

uint64_t current_start = 0, counter = 0;

static cycle_t muen_cs_read(struct clocksource *arg)
{
	const uint64_t next_start = muen_get_sched_start();

	if (next_start == current_start)
		counter++;
	else
		counter = current_start = next_start;

	return counter;
}

unsigned long calibrate_delay_is_known(void)
{
	return muen_get_tsc_khz() * 1000 / HZ;
}

static struct clocksource muen_cs = {
	.name	= "muen-clksrc",
	.rating	= 400,
	.read	= muen_cs_read,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

inline cycle_t muen_sched_clock_read(void)
{
	return muen_cs_read(&muen_cs);
}
EXPORT_SYMBOL(muen_sched_clock_read);

static int __init muen_cs_init(void)
{
	clocksource_register_khz(&muen_cs, muen_get_tsc_khz());
	pv_time_ops.sched_clock = muen_sched_clock_read;
	set_sched_clock_stable();
	return 0;
}

console_initcall(muen_cs_init);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen clocksource driver");
MODULE_LICENSE("GPL");
