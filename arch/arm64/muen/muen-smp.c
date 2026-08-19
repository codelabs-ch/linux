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
#include <linux/cpuhotplug.h>

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

static int muen_smp_prepare_cpu(unsigned int cpu)
{
	pr_info("muen-smp: Prepare CPU#%u", cpu);

	// Need to setup sinfo in PREPARE due to sleepy memremap calls
	muen_sinfo_setup(cpu);

	if (IS_ENABLED(CONFIG_MUEN_CLKSRC)) {
		muen_setup_timer_page(cpu);
	}

	return 0;
}

static int muen_smp_starting_cpu(unsigned int cpu)
{
	pr_info("muen-smp: Starting CPU#%u", cpu);

	BUG_ON(!muen_check_magic());
	muen_sinfo_log_resources();

	if (IS_ENABLED(CONFIG_MUEN_CLKSRC)) {
		muen_setup_timer_event();
	}

	muen_smp_setup_events();

	return 0;
}

static int muen_smp_online_cpu(unsigned int cpu)
{
	pr_info("muen-smp: Online CPU#%u", cpu);

	muen_register_resources();
	//^ TODO: Move earlier. Not valid in STARTING due to sleeping mutex
	// in irq_create_mapping.
	/* Note: before muen_register_clkevent_dev due to request_irq dependency */

	if (IS_ENABLED(CONFIG_MUEN_CLKSRC)) {
		/* Very, very late, but works for now. We should not do
		 * this in STARTUP as that's in atomic context and
		 * request_irq can sleep */
		muen_register_clockevent_dev();
	}

	return 0;
}

static int __init muen_pre_smp_init(void)
{
	int ret;

	muen_sinfo_log_resources();
	muen_register_resources();

	if (IS_ENABLED(CONFIG_MUEN_CLKSRC)) {
		muen_setup_timer_page(0);
		muen_setup_timer_event();
		muen_register_clockevent_dev();
	}

	muen_smp_setup_events();

	ret = cpuhp_setup_state_nocalls(CPUHP_BP_PREPARE_DYN,
					"smp/muen:prepare",
					muen_smp_prepare_cpu,
					NULL);
	BUG_ON(ret < 0);

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_KVM_STARTING, // TODO: stolen
					/* after ARM_ARCH_TIMER, for muen-clkevt may want before */
				"smp/muen:starting",
				muen_smp_starting_cpu, NULL);
	BUG_ON(ret < 0);

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
				"smp/muen:online",
				muen_smp_online_cpu, NULL);
	BUG_ON(ret < 0);

	return 0;
}
early_initcall(muen_pre_smp_init);
/* just before smp_init(),
   but after (arch_)smp_prepare_cpus
*/
