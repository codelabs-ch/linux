// SPDX-License-Identifier: GPL-2.0+
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

static DEFINE_PER_CPU_ALIGNED(uint64_t, current_start) = 0;
static DEFINE_PER_CPU_ALIGNED(uint64_t, counter) = 0;

static u64 muen_cs_read(struct clocksource *arg)
{
	const uint64_t next_start = muen_get_sched_start();

	if (next_start == this_cpu_read(current_start))
		this_cpu_inc(counter);
	else {
		this_cpu_write(counter, next_start);
		this_cpu_write(current_start, next_start);
	}

	return this_cpu_read(counter);
}

static struct clocksource muen_cs = {
	.name	= "muen-clksrc",
	.rating	= 400,
	.read	= muen_cs_read,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

inline u64 muen_sched_clock_read(void)
{
	return muen_cs_read(&muen_cs);
}
EXPORT_SYMBOL(muen_sched_clock_read);

static int __init muen_cs_init(void)
{
	clocksource_register_khz(&muen_cs, muen_get_tsc_khz());
	pv_ops.time.sched_clock = muen_sched_clock_read;
	return 0;
}

core_initcall(muen_cs_init);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen clocksource driver");
MODULE_LICENSE("GPL");
