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

#include <asm/irq_regs.h>
#include <asm/idtentry.h>
#include <asm/apic.h>

#include <linux/clockchips.h>
#include <linux/percpu.h>

#include <muen/timer.h>

void muen_arch_register_local_timer_interrupt(
	struct clock_event_device *evt,
	uint8_t __always_unused evt_nr)
{
	const unsigned int cpu = smp_processor_id();
	evt->cpumask = cpumask_of(cpu);
	evt->irq = -1;

	/* No need to register irq on x86, already routed to sysvec below
	 * from policy. */
}

DEFINE_IDTENTRY_SYSVEC(sysvec_muen_timer_interrupt)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	ack_APIC_irq();
	if (muen_arch_local_timer_interrupt() == IRQ_HANDLED)
		inc_irq_stat(apic_timer_irqs);

	set_irq_regs(old_regs);
}
