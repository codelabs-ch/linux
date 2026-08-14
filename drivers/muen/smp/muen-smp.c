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

// TODO: Trim includes

#include <linux/cpu.h>
#include <linux/kvm_para.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/stackprotector.h>
#include <linux/smp.h>
#include <linux/sched/task_stack.h>
#include <linux/memblock.h>

#include <muen/smp.h>
#include <muen/timer.h>

static const char *const res_names[] = {
	"none", "memory", "event", "vector", "device",
};

/* BSP AP start event array */
uint8_t *bsp_ap_start; // x86 only

uint8_t muen_irq_work_evt;

DEFINE_PER_CPU(struct muen_ipi_config, muen_ipis);

static void new_name(struct muen_name_type *const n, const char *str, ...)
{
	va_list ap;

	memset(n->data, 0, sizeof(n->data));

	va_start(ap, str);
	vsnprintf(n->data, sizeof(n->data), str, ap);
	va_end(ap);
}

static unsigned int muen_get_evt_vec(const char *const name,
				     const enum muen_resource_kind kind)
{
	const struct muen_resource_type *const
	   res = muen_get_resource(name, kind);

	if (!res) {
		pr_err("muen-smp: Required %s with name %s not present\n",
		       res_names[kind], name);
		BUG();
	}

	return res->data.number;
}

void muen_smp_setup_events(void)
{
	unsigned int cpu;
	struct muen_name_type n;
	const unsigned int this_cpu = smp_processor_id();

	struct muen_ipi_config *const ipis = this_cpu_ptr(&muen_ipis);

	ipis->call_func = kcalloc(nr_cpu_ids, sizeof(uint8_t), GFP_ATOMIC);
	BUG_ON(!ipis->call_func);
	ipis->reschedule = kcalloc(nr_cpu_ids, sizeof(uint8_t), GFP_ATOMIC);
	BUG_ON(!ipis->reschedule);

	for_each_possible_cpu(cpu) {
		if (this_cpu == cpu)
			continue;

		pr_info("muen-smp: Setup CPU#%u -> CPU#%u events/vectors\n",
			this_cpu, cpu);

#ifdef CONFIG_X86
		if (!this_cpu) {
			new_name(&n, "smp_signal_sm_%02d", cpu); // No SM on ARM64.
			bsp_ap_start[cpu - 1] = muen_get_evt_vec
				(n.data, MUEN_RES_EVENT);
			pr_info("muen-smp: event %s with number %u\n", n.data,
				bsp_ap_start[cpu - 1]);
		}
#endif

		new_name(&n, "smp_ipi_call_func_%02d%02d", this_cpu, cpu);
		ipis->call_func[cpu] = muen_get_evt_vec(n.data, MUEN_RES_EVENT);
		pr_info("muen-smp: event %s with number %u\n", n.data,
			ipis->call_func[cpu]);

		new_name(&n, "smp_ipi_reschedule_%02d%02d", this_cpu, cpu);
		ipis->reschedule[cpu] = muen_get_evt_vec(n.data, MUEN_RES_EVENT);
		pr_info("muen-smp: event %s with number %u\n", n.data,
			ipis->reschedule[cpu]);

		/* Verify target vector assignment */

		muen_arch_verify_smp_events(this_cpu, cpu);
	}
}
