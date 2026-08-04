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

#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/percpu.h>

#include <muen/timer.h>

/*
 * Note that the handed over device id does not match the muen_events
 * clock event device address. Therefore, the clock event device is
 * currently extracted from the static 'muen_events' struct.
 */
static irqreturn_t muen_timer_handler(int irq, void *dev_id)
{
	return muen_handle_local_timer_interrupt();
}

int muen_arch_register_local_timer_interrupt(
	struct clock_event_device *evt, uint8_t hwirq)
{
	const unsigned int cpu = smp_processor_id();
	struct irq_domain *domain = irq_get_default_host();
	int virq = irq_find_mapping(domain, hwirq);

	WARN(!virq, "muen-clkevt: Failed to find IRQ mapping for hwirq %u on CPU#%u\n",
	    hwirq, cpu);
	if (!virq)
		return -1;

	evt->irq = virq;
	evt->cpumask = cpumask_of(cpu);

	int err = request_irq(virq, muen_timer_handler, IRQF_TIMER,
			      "muen-clkevt", evt);
	WARN(err, "muen-clkevt: Failed to register IRQ %d (%d) on CPU#%u\n", virq, err, cpu);
	if (err)
		return -1;

	pr_info("muen-clkevt: Using timer (event '%s') hwirq %u, virq %u on CPU#%u\n",
		evt->name, hwirq, virq, cpu);

	return 0;
}
