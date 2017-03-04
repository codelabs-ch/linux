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

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/clockchips.h>
#include <muen/sinfo.h>

#define TIMER_EVENT 31

struct subject_timed_event_type {
	uint64_t tsc_trigger;
	unsigned int event_nr :5;
} __packed;

static struct subject_timed_event_type *timer_page;

static irqreturn_t handle_timer_interrupt(int irq, void *dev_id)
{
	global_clock_event->event_handler(global_clock_event);
	return IRQ_HANDLED;
}

static struct irqaction irq0  = {
	.handler = handle_timer_interrupt,
	.flags = IRQF_NOBALANCING | IRQF_IRQPOLL | IRQF_TIMER,
	.name = "muen-timer"
};

static int muen_timer_shutdown(struct clock_event_device *const evt)
{
	timer_page->tsc_trigger = ULLONG_MAX;
	return 0;
}

static int muen_timer_next_event(const unsigned long delta,
				 struct clock_event_device *const evt)
{
	const uint64_t tsc_now = muen_get_sched_end();

	timer_page->tsc_trigger = tsc_now + delta;
	return 0;
}

static struct clock_event_device muen_clockevent = {
	.name			= "muen-clkevt",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event		= muen_timer_next_event,
	.set_state_shutdown	= muen_timer_shutdown,
	.rating			= INT_MAX,
};

static int __init muen_ce_init(void)
{
	struct muen_memregion_info region;

	if (!muen_get_memregion_info("timed_event", &region)) {
		pr_warn("Unable to retrieve Muen timed event region\n");
		return -1;
	}
	pr_info("Using Muen timed event region at address 0x%llx\n",
		region.address);

	timer_page = (struct subject_timed_event_type *)ioremap_cache
	    (region.address, region.size);

	timer_page->event_nr = TIMER_EVENT;

	global_clock_event = &muen_clockevent;

	setup_irq(0, &irq0);

	pr_info("Registering clockevent device muen-clkevt\n");
	muen_clockevent.cpumask = cpu_online_mask;
	clockevents_config_and_register(&muen_clockevent,
					muen_get_tsc_khz() * 1000, 1, UINT_MAX);
	return 0;
}

core_initcall(muen_ce_init);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen clockevent driver");
MODULE_LICENSE("GPL");
