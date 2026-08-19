// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2018  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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

#include "linux/init.h"
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>

#include <muen/smp.h>
#include <muen/timer.h>

/*
 * Allocate IRQ descriptor for given vector and register it with IRQ chip.
 */
void muen_arch_allocate_vector(const struct muen_resource_type *const res)
{
	const unsigned int hwirq = res->data.number;
	int virq = irq_create_mapping(NULL/*=default_domain*/, hwirq);

	pr_info("muen-smp: Allocate irq with hwirq %u, virq %u for event %s (CPU#%d)\n",
				hwirq, virq, res->name.data, smp_processor_id());
}

inline void kvm_hypercall0(unsigned int num)
{
	asm volatile("mov   x0, %[num]\n"
		     "hvc #1"
		     : /* no outputs */
		     : [num] "r"(num));
}
EXPORT_SYMBOL(kvm_hypercall0);

void muen_arch_verify_smp_events(unsigned int this_cpu, unsigned int cpu)
{
}

static int __init muen_pre_smp_init(void)
{
	muen_sinfo_log_resources();
	muen_register_resources();

	if (IS_ENABLED(CONFIG_MUEN_CLKSRC)) {
		muen_setup_timer_page(0);
		muen_setup_timer_event();
		muen_register_clockevent_dev();
	}

	return 0;
}
early_initcall(muen_pre_smp_init);
/* just before smp_init(),
   but after (arch_)smp_prepare_cpus
*/
