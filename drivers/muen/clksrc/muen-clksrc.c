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

#include <asm/timer.h>
#include <linux/clocksource.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <muen/sinfo.h>

static DEFINE_PER_CPU_ALIGNED(uint64_t, current_end);
static DEFINE_PER_CPU_ALIGNED(uint64_t, counter);

static struct cyc2ns_data muen_cyc2ns __ro_after_init;

static u64 muen_cs_read(struct clocksource *arg)
{
	const uint64_t next_end = muen_get_sched_end();

	if (next_end == this_cpu_read(current_end))
		this_cpu_inc(counter);
	else {
		this_cpu_write(counter, next_end);
		this_cpu_write(current_end, next_end);
	}

	return this_cpu_read(counter);
}

static int muen_cs_enable(struct clocksource *cs)
{
	vclocks_set_used(VDSO_CLOCKMODE_MVCLOCK);
	return 0;
}

static struct clocksource muen_cs = {
	.name			= "muen-clksrc",
	.rating			= 400,
	.read			= muen_cs_read,
	.mask			= CLOCKSOURCE_MASK(64),
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS,
	.enable			= muen_cs_enable,
	.vdso_clock_mode	= VDSO_CLOCKMODE_MVCLOCK,
};

inline u64 muen_clock_read(void)
{
	return muen_cs_read(&muen_cs);
}
EXPORT_SYMBOL(muen_clock_read);

static u64 notrace muen_sched_clock_read(void)
{
	u64 ns;

	ns = muen_cyc2ns.cyc2ns_offset;
	ns += mul_u64_u32_shr(muen_clock_read(), muen_cyc2ns.cyc2ns_mul,
			      muen_cyc2ns.cyc2ns_shift);
	return ns;
}

static int __init muen_cs_init(void)
{
	struct cyc2ns_data *d = &muen_cyc2ns;
	u64 tsc_now = muen_clock_read();

	clocks_calc_mult_shift(&d->cyc2ns_mul, &d->cyc2ns_shift,
			       muen_get_tsc_khz(), NSEC_PER_MSEC, 0);
	d->cyc2ns_offset = mul_u64_u32_shr(tsc_now, d->cyc2ns_mul,
					   d->cyc2ns_shift);

	pr_info("muen-clksrc: Using clock offset of %llu ns\n", d->cyc2ns_offset);

	paravirt_set_sched_clock(muen_sched_clock_read);
	clocksource_register_khz(&muen_cs, muen_get_tsc_khz());
	return 0;
}

core_initcall(muen_cs_init);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen clocksource driver");
MODULE_LICENSE("GPL");
