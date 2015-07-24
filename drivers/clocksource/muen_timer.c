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

#include <linux/module.h>
#include <linux/clockchips.h>
#include <muen/sinfo.h>

struct subject_timer_type {
	uint64_t value;
	uint8_t vector;
} __packed;

static struct subject_timer_type *timer_page;

static void muen_timer_set_mode(const enum clock_event_mode mode,
				struct clock_event_device *const evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* unsupported */
		WARN_ON(1);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		/* Cancel timer by setting timer to maximum value */
		timer_page->value = ULLONG_MAX;
		break;

	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int muen_timer_next_event(const unsigned long delta,
				 struct clock_event_device *const evt)
{
	uint64_t tsc_now;

	rdtscll(tsc_now);
	timer_page->value = tsc_now + delta;
	return 0;
}

static struct clock_event_device muen_timer_clockevent = {
	.name		= "muen-timer",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= muen_timer_set_mode,
	.set_next_event = muen_timer_next_event,
	.rating		= INT_MAX,
};

static int __init clockevent_muen_timer_init(void)
{
	struct muen_memregion_info region;

	if (!muen_get_memregion_info("timer", &region)) {
		pr_warn("Unable to retrieve Muen time memory region\n");
		return -1;
	}
	pr_info("Using Muen time memory region at address 0x%llx\n",
		region.address);

	timer_page = (struct subject_timer_type *)ioremap_cache(region.address,
								region.size);

	timer_page->vector = IRQ0_VECTOR;
	setup_default_timer_irq();

	pr_info("Registering clockevent device muen-timer\n");
	muen_timer_clockevent.cpumask = cpu_online_mask;
	clockevents_config_and_register(&muen_timer_clockevent,
					muen_get_tsc_khz() * 1000, 1, UINT_MAX);
	global_clock_event = &muen_timer_clockevent;
	return 0;
}

arch_initcall(clockevent_muen_timer_init);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen clock event driver");
MODULE_LICENSE("GPL");
