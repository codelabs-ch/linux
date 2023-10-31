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

#include <linux/cpu.h>
#include <linux/kvm_para.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/stackprotector.h>
#include <linux/smp.h>
#include <linux/sched/task_stack.h>

#include <asm/hw_irq.h>

#include <muen/smp.h>

static const char *const res_names[] = {
	"none", "memory", "event", "vector", "device",
};

/* BSP AP start event array */
static uint8_t *bsp_ap_start;

/* Per-CPU IPI event configuration */
struct muen_ipi_config {
	uint8_t *call_func;
	uint8_t *reschedule;
};
static DEFINE_PER_CPU(struct muen_ipi_config, muen_ipis);

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

static void new_name(struct muen_name_type *const n, const char *str, ...)
{
	va_list ap;

	memset(n->data, 0, sizeof(n->data));

	va_start(ap, str);
	vsnprintf(n->data, sizeof(n->data), str, ap);
	va_end(ap);
}

static void muen_setup_events(void)
{
	unsigned int cpu, vec;
	struct muen_name_type n;
	const unsigned int this_cpu = smp_processor_id();

	struct muen_ipi_config *const cfg = this_cpu_ptr(&muen_ipis);

	cfg->call_func = kcalloc(nr_cpu_ids, sizeof(uint8_t), GFP_ATOMIC);
	BUG_ON(!cfg->call_func);
	cfg->reschedule = kcalloc(nr_cpu_ids, sizeof(uint8_t), GFP_ATOMIC);
	BUG_ON(!cfg->reschedule);

	for_each_cpu(cpu, cpu_possible_mask) {
		if (this_cpu == cpu)
			continue;

		pr_info("muen-smp: Setup CPU#%u -> CPU#%u events/vectors\n",
			this_cpu, cpu);

		if (!this_cpu) {
			new_name(&n, "smp_signal_sm_%02d", cpu);
			bsp_ap_start[cpu - 1] = muen_get_evt_vec
				(n.data, MUEN_RES_EVENT);
			pr_info("muen-smp: event %s with number %u\n", n.data,
				bsp_ap_start[cpu - 1]);
		}

		new_name(&n, "smp_ipi_call_func_%02d%02d", this_cpu, cpu);
		cfg->call_func[cpu] = muen_get_evt_vec(n.data, MUEN_RES_EVENT);
		pr_info("muen-smp: event %s with number %u\n", n.data,
			cfg->call_func[cpu]);

		new_name(&n, "smp_ipi_reschedule_%02d%02d", this_cpu, cpu);
		cfg->reschedule[cpu] = muen_get_evt_vec(n.data, MUEN_RES_EVENT);
		pr_info("muen-smp: event %s with number %u\n", n.data,
			cfg->reschedule[cpu]);

		/* Verify target vector assignment */

		vec = muen_get_evt_vec(n.data, MUEN_RES_VECTOR);
	}
}

/* CPU resource affinity handling */
static DEFINE_SPINLOCK(affinity_list_lock);
static struct list_head affinity_list = LIST_HEAD_INIT(affinity_list);

/* Add new entry to CPU affinity list */
static void cpu_list_add_entry(const struct muen_resource_type *const res)
{
	const unsigned int this_cpu = smp_processor_id();
	struct muen_cpu_affinity *entry;

	entry = kzalloc(sizeof(struct muen_cpu_affinity), GFP_ATOMIC);

	BUG_ON(!entry);
	entry->res = *res;
	entry->cpu = this_cpu;

	spin_lock(&affinity_list_lock);
	list_add_tail_rcu(&entry->list, &affinity_list);
	spin_unlock(&affinity_list_lock);
}

/*
 * Allocate IRQ descriptor for given vector and register it with IRQ chip.
 */
static void allocate_vector(const struct muen_resource_type *const res)
{
	const int this_cpu = smp_processor_id();
	const unsigned int vec = res->data.number;
	int irq;

#ifdef CONFIG_X86_64
	if (vec > ISA_IRQ_VECTOR(15) && vec < FIRST_SYSTEM_VECTOR) {
		irq = irq_alloc_desc_at(vec - ISA_IRQ_VECTOR(0), -1);
		per_cpu(vector_irq, this_cpu)[vec] = irq_to_desc(irq);
		pr_info("muen-smp: Allocating IRQ %u for event %s (CPU#%d)\n",
			irq, res->name.data, this_cpu);
		irq_set_chip_and_handler(irq, &dummy_irq_chip,
			handle_edge_irq);
	}
#else
	irq = irq_create_mapping(NULL, vec);
	pr_info("muen-smp: Allocating IRQ %u for event %s (CPU#%d)\n",
			irq, res->name.data, this_cpu);
#endif
}

static bool register_resource(
		const struct muen_resource_type *const res, void *data)
{
	switch (res->kind) {
	case MUEN_RES_DEVICE:
		/*
		 * Register device IRQs in CPU affinity list. IRQs are
		 * guaranteed to be unique because they can only be assigned to
		 * one CPU.
		 */
		if (res->data.dev.ir_count)
			cpu_list_add_entry(res);
		break;
	case MUEN_RES_EVENT:
		cpu_list_add_entry(res);
		break;
	case MUEN_RES_VECTOR:
		cpu_list_add_entry(res);

		allocate_vector(res);
		break;
	default:
		break;
	}

	return true;
}

static void muen_register_resources(void)
{
	muen_for_each_resource(register_resource, NULL);
}

#ifndef CONFIG_X86_64
static inline void kvm_hypercall0(unsigned long num)
{
	switch (num)
	{
		case 7:
			asm volatile("hvc #7");
			break;
		case 8:
			asm volatile("hvc #8");
			break;
		default:
			pr_err("muen-smp: Unknown number for HVC call: %lu\n", num);
			break;
	}
}
#endif

void muen_smp_send_call_function_single_ipi(int cpu)
{
	struct muen_ipi_config *const cfg = this_cpu_ptr(&muen_ipis);

	kvm_hypercall0(cfg->call_func[cpu]);
}

void muen_smp_send_call_function_ipi(const struct cpumask *mask)
{
	unsigned int cpu;
	struct muen_ipi_config *const cfg = this_cpu_ptr(&muen_ipis);

	for_each_cpu(cpu, mask)
		kvm_hypercall0(cfg->call_func[cpu]);
}

void muen_smp_send_reschedule(int cpu)
{
	struct muen_ipi_config *const cfg = this_cpu_ptr(&muen_ipis);

	kvm_hypercall0(cfg->reschedule[cpu]);
}

static void do_trigger_event(void *data)
{
	uint8_t *id = data;

	kvm_hypercall0(*id);
}

void muen_smp_trigger_event(const uint8_t id, const uint8_t cpu)
{
	unsigned int this_cpu;

	preempt_disable();
	this_cpu = smp_processor_id();

	BUG_ON(cpu >= nr_cpu_ids);

	if (cpu == this_cpu)
		kvm_hypercall0(id);
	else
		smp_call_function_single(cpu, do_trigger_event, (void *)&id, 1);

	preempt_enable();
}
EXPORT_SYMBOL(muen_smp_trigger_event);

int muen_smp_get_res_affinity(struct muen_cpu_affinity *const result,
		match_func func, void *match_data)
{
	unsigned int count = 0;
	struct muen_cpu_affinity *entry, *copy;

	INIT_LIST_HEAD(&result->list);

	rcu_read_lock();
	list_for_each_entry_rcu(entry, &affinity_list, list) {
		if (!func || func(entry, match_data)) {
			copy = kmemdup(entry, sizeof(*entry), GFP_ATOMIC);
			if (!copy) {
				rcu_read_unlock();
				goto free_and_exit;
			}
			list_add_tail(&copy->list, &result->list);
			count++;
		}
	}
	rcu_read_unlock();
	return count;

free_and_exit:
	muen_smp_free_res_affinity(result);
	return -ENOMEM;
}
EXPORT_SYMBOL(muen_smp_get_res_affinity);

struct match_data {
	const char *const name;
	const enum muen_resource_kind kind;
};

static bool
muen_match_name_kind(const struct muen_cpu_affinity *const affinity, void *data)
{
	const struct match_data *const match = data;

	return affinity->res.kind == match->kind
		&& muen_names_equal(&affinity->res.name, match->name);
}

bool muen_smp_one_match_func(struct muen_cpu_affinity *const result,
		match_func func, void *match_data)
{
	unsigned int affinity_count;
	struct muen_cpu_affinity affinity, *first;

	affinity_count = muen_smp_get_res_affinity(&affinity, func, match_data);
	if (affinity_count == 1) {
		first = list_first_entry(&affinity.list,
				struct muen_cpu_affinity, list);
		*result = *first;
	}

	muen_smp_free_res_affinity(&affinity);
	return affinity_count == 1;
}
EXPORT_SYMBOL(muen_smp_one_match_func);

bool muen_smp_one_match(struct muen_cpu_affinity *const result,
		const char *const name, const enum muen_resource_kind kind)
{
	const struct match_data match = {
		.name = name,
		.kind = kind,
	};

	return muen_smp_one_match_func(result, muen_match_name_kind,
			(void *)&match);
}
EXPORT_SYMBOL(muen_smp_one_match);

void muen_smp_free_res_affinity(struct muen_cpu_affinity *const to_free)
{
	struct muen_cpu_affinity *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &to_free->list, list) {
		list_del(&entry->list);
		kfree(entry);
	}
}
EXPORT_SYMBOL(muen_smp_free_res_affinity);

static int __init muen_smp_init(void)
{
	muen_sinfo_log_resources();
	muen_register_resources();
	muen_setup_events();
	return 0;
}
console_initcall(muen_smp_init);
