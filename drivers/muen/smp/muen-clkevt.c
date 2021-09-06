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

#include <asm/time.h>
#include <asm/setup.h>
#include <asm/irq_regs.h>

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/clockchips.h>
#include <linux/percpu.h>
#include <muen/sinfo.h>

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

static struct clock_event_device muen_clockevent = {
	.name			= "muen-clkevt",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event		= muen_timer_next_event,
	.set_state_shutdown	= muen_timer_shutdown,
	.rating			= INT_MAX,
	.irq			= -1,
};

static DEFINE_PER_CPU(struct clock_event_device, muen_events);

void muen_setup_timer(void)
{
	struct subject_timed_event_type *timer_page;
	struct clock_event_device *evt = this_cpu_ptr(&muen_events);
	const int cpu = smp_processor_id();
	char mem_name[MAX_NAME_LENGTH + 1] = "timed_event";
	const struct muen_resource_type *region;
	const struct muen_resource_type *const
		timer_evt = muen_get_resource("timer", MUEN_RES_EVENT);

	if (nr_cpu_ids > 1)
		snprintf(mem_name, sizeof(mem_name), "timed_event%d", cpu);

	region = muen_get_resource(mem_name, MUEN_RES_MEMORY);

	if (!region) {
		pr_warn("muen-smp: Unable to retrieve Muen timed event region\n");
		return;
	}
	if (!timer_evt) {
		pr_warn("muen-smp: Unable to retrieve Muen timer event\n");
		return;
	}

	pr_info("muen-smp: Using timed event region at address 0x%llx with event %u\n",
		region->data.mem.address, timer_evt->data.number);
	timer_page = (struct subject_timed_event_type *)ioremap_cache
		(region->data.mem.address, region->data.mem.size);
	timer_page->event_nr = timer_evt->data.number;
	this_cpu_write(timer, timer_page);

	pr_info("muen-smp: Setup timer for CPU#%u: %p\n", cpu, evt);

	memcpy(evt, &muen_clockevent, sizeof(*evt));

	evt->cpumask = cpumask_of(smp_processor_id());

	clockevents_config_and_register(evt,
			muen_get_tsc_khz() * 1000, 1, UINT_MAX);
}

static void local_timer_interrupt(void)
{
	struct clock_event_device *evt = this_cpu_ptr(&muen_events);

	if (!evt->event_handler) {
		pr_warn("muen-smp: Spurious timer interrupt on cpu %d\n",
			smp_processor_id());
		return;
	}
	inc_irq_stat(apic_timer_irqs);

	evt->event_handler(evt);
}

__visible void __irq_entry smp_apic_timer_interrupt_muen(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	entering_ack_irq();
	local_timer_interrupt();
	exiting_irq();

	set_irq_regs(old_regs);
}
