// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2014  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2014  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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

#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/io.h>
#include <muen/sinfo.h>
#include <muen/timer.h>

struct subject_timed_event_type {
	uint64_t tsc_trigger;
	unsigned int event_nr :6;
} __packed;

static DEFINE_PER_CPU(struct subject_timed_event_type *, timer);

static int muen_timer_shutdown(struct clock_event_device *const evt)
{
	struct subject_timed_event_type *timer_page = this_cpu_read(timer);

	timer_page->tsc_trigger = ULLONG_MAX;
	return 0;
}

static int muen_timer_next_event(const unsigned long delta,
				 struct clock_event_device *const evt)
{
	const uint64_t tsc_now = muen_get_sched_end();
	struct subject_timed_event_type *timer_page = this_cpu_read(timer);

	timer_page->tsc_trigger = tsc_now + delta;
	return 0;
}

/*
 * Note that currently on arm64 platforms an ARM_ARCH_TIMER is still
 * required for the early boot phase of the Linux kernel. Therefore,
 * the rating has to be higher than the 400 of the ARM Generic Timer.
 */
static struct clock_event_device muen_clockevent = {
	.name			= "muen-clkevt",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event		= muen_timer_next_event,
	.set_state_shutdown	= muen_timer_shutdown,
	.rating			= INT_MAX,
};

static DEFINE_PER_CPU(struct clock_event_device, muen_events);

void muen_setup_timer_page(unsigned int cpu)
{
	struct subject_timed_event_type *timer_page;
	char mem_name[MAX_NAME_LENGTH + 1] = "timed_event";
	const struct muen_resource_type *region;
	uint64_t addr;

	if (nr_cpu_ids > 1)
		snprintf(mem_name, sizeof(mem_name), "timed_event%d", 0);

	region = muen_get_resource(mem_name, MUEN_RES_MEMORY);
	BUG_ON(!region);
	BUG_ON(region->data.mem.size != PAGE_SIZE);

	addr = region->data.mem.address + (cpu * PAGE_SIZE);

	pr_info("muen-clkevt: Using timed event region at address 0x%llx for CPU#%u\n",
		addr, cpu);
	timer_page = (struct subject_timed_event_type *)memremap
		(addr, region->data.mem.size, MEMREMAP_WB);
	BUG_ON(!timer_page);
	per_cpu(timer, cpu) = timer_page;
}

void muen_setup_timer_event(void)
{
	struct subject_timed_event_type *timer_page = this_cpu_read(timer);
	const struct muen_resource_type *const
		timer_evt = muen_get_resource("timer", MUEN_RES_EVENT);
	BUG_ON(!timer_evt);

	pr_info("muen-clkevt: Using timed event %u for CPU#%u\n",
		timer_evt->data.number, smp_processor_id());
	timer_page->event_nr = timer_evt->data.number;
}

void muen_register_clockevent_dev(void)
{
	int err;
	const struct muen_resource_type *const
		timer_evt = muen_get_resource("timer", MUEN_RES_VECTOR);
	struct clock_event_device *evt = this_cpu_ptr(&muen_events);

	WARN_ON(!timer_evt);
	if (!timer_evt)
		return;

	memcpy(evt, &muen_clockevent, sizeof(*evt));

	err = muen_arch_register_local_timer_interrupt(evt, timer_evt->data.number);
	if (err)
		return;

	clockevents_config_and_register(evt,
		muen_get_tsc_khz() * 1000, 1, UINT_MAX);

	pr_info("muen-clkevt: Registered clockevent for CPU#%u\n", smp_processor_id());
}

irqreturn_t muen_handle_local_timer_interrupt(void)
{
	struct clock_event_device *evt = this_cpu_ptr(&muen_events);

	if (likely(evt->event_handler)) {
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	pr_warn("muen-clkevt: Spurious timer interrupt or unattached event handler on cpu %d\n",
		smp_processor_id());

	return IRQ_NONE;
}
