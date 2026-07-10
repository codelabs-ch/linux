// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2014  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2014  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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

#include "asm/bug.h"

#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include "linux/irqdomain.h"
#include <linux/percpu.h>

#include <muen/sinfo.h>

#include "muen-clkevt.h"

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

/*
 * Note that the handed over device id does not match the muen_events
 * clock event device address. Therefore, the clock event device is
 * currently extracted from the static 'muen_events' struct.
 */
static irqreturn_t muen_timer_handler(int irq, void *dev_id)
{
	struct clock_event_device *evt = this_cpu_ptr(&muen_events);

	if (likely(evt->event_handler)) {
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	pr_warn("muen-clkevt: Spurious timer interrupt or unattached event handler on cpu %d\n", smp_processor_id());
	return IRQ_NONE;
}

void muen_register_clockevent_dev(void)
{
	const unsigned int cpu = smp_processor_id();

	struct irq_domain *domain;
	const struct muen_resource_type *const
		timer_evt = muen_get_resource("timer", MUEN_RES_VECTOR);
	BUG_ON(!timer_evt);

	domain = irq_get_default_host();
	int virq = irq_find_mapping(domain, timer_evt->data.number);

	if (!virq) {
		pr_err("muen-clkevt: Could not find interrupt mapping for hwirq %u on cpu %u\n", timer_evt->data.number, cpu);
		BUG();
	}

	struct clock_event_device *evt = this_cpu_ptr(&muen_events);

	memcpy(evt, &muen_clockevent, sizeof(*evt));
	evt->irq = virq;
	evt->cpumask = cpumask_of(cpu);

	int err = request_irq(virq, muen_timer_handler, IRQF_TIMER,
		"muen-clkevt", evt);
	if (err) {
		pr_err("muen-clkevt: Could not register clock event (timer) interrupt %d (%d) for cpu %u\n", virq, err, cpu);
		BUG();
	}

	pr_info("muen-clkevt: Registering clock event device (timer) with hwirq %u, virq %u and name '%s' for cpu %u\n",
		timer_evt->data.number, virq, evt->name, cpu);
	clockevents_config_and_register(evt,
		muen_get_tsc_khz() * 1000, 1, UINT_MAX);
}
