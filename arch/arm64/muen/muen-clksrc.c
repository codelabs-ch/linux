// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2016  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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
 */

#include <linux/clocksource.h>
#include <linux/module.h>
#include <linux/sched_clock.h>
#include <muen/sinfo.h>

static DEFINE_PER_CPU_ALIGNED(uint64_t, current_end);
static DEFINE_PER_CPU_ALIGNED(uint64_t, counter);

static atomic64_t global_last_end = ATOMIC64_INIT(0);

/*
 * Timekeeping is a global singleton and has a hard requirement on
 * clocksources being monotonic across all CPUs (unlike sched_clock).
 *
 * Since sched_info is per-cpu and frame end time can vary depending on
 * scheduling plan we have to fix this here the hard way.
 *
 * Alternative: validate scheduling plans to prevent Linux sibling cores
 * from running in different timeslices.
 *
 * Alternative2: Peek into BSP sinfo directly and use it's sense of time.
 */
static u64 muen_cs_read(struct clocksource *arg)
{
	u64 now, global;

	now = muen_get_sched_end();
	global = atomic64_read(&global_last_end);

	if (now > global) // needs update
		while (global < now && !atomic64_try_cmpxchg(&global_last_end, &global, now));

	return atomic64_read(&global_last_end);
}

#ifdef CONFIG_DEBUG_TIMEKEEPING
#define MEUN_CS_FLAGS_DEBUG \
	(CLOCK_SOURCE_MUST_VERIFY | CLOCK_SOURCE_VERIFY_PERCPU)
#else
#define MEUN_CS_FLAGS_DEBUG 0
#endif

/*
 * Note that the clocksource for arm64 currently does not support vDSO
 * mode (i.e. direct access by the user space to clock counter, so no
 * is syscall required), because the VDSO_CLOCKMODE_MVCLOCK flag does
 * not seem to be implemented (see VDSO_CLOCKMODE_ARCHTIMER). Further,
 * the rating has to be higher than the 400 of the ARM Generic Timer.
 */
static struct clocksource muen_cs = {
	.name			= "muen-clksrc",
	.rating			= 600,
	.read			= muen_cs_read,
	.mask			= CLOCKSOURCE_MASK(64),
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS
				| CLOCK_SOURCE_VALID_FOR_HRES
				| MEUN_CS_FLAGS_DEBUG,
	.uncertainty_margin = 5 * NSEC_PER_MSEC, // depends on scheduling plan.
	.vdso_clock_mode	= VDSO_CLOCKMODE_NONE
};

/* Note: Sched clock is allowed to skew per-cpu, unlike global clocksource. */
inline u64 muen_clock_read(void) // _sched_clock_read
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
EXPORT_SYMBOL(muen_clock_read);

/*
 * Note that 'paravirt_set_sched_clock' is not available for arm64, so
 * a normal scheduling clock device is registered. Using this approach,
 * Linux takes over the counter cycle to ns conversion.
 */
static int __init muen_cs_init(void)
{
	pr_info("muen-clksrc: Initialize clock with %llu khz\n", muen_get_tsc_khz());
	sched_clock_register(muen_clock_read, 64, muen_get_tsc_khz() * 1000);
	clocksource_register_khz(&muen_cs, muen_get_tsc_khz());
	return 0;
}

core_initcall(muen_cs_init);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_AUTHOR("David Loosli <david@codelabs.ch>");
MODULE_DESCRIPTION("Muen clocksource driver");
MODULE_LICENSE("GPL");
