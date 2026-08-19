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

#include <muen/sinfo.h>
#include <muen/smp.h>
#include <muen/timer.h>

static const char *const res_names[] = {
	"none", "memory", "event", "vector", "device",
};

/* BSP AP start event array */
uint8_t *bsp_ap_start; // x86 only

DEFINE_PER_CPU(struct muen_ipi_config, muen_ipis);

unsigned int muen_smp_get_evt_vec(
	const char *const name,
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

void muen_smp_verify_vec(const char *const name, const unsigned int ref)
{
	const unsigned int vec = muen_smp_get_evt_vec(name, MUEN_RES_VECTOR);

	if (vec != ref) {
		pr_err("muen-smp: Unexpected vector %u for %s, should be %u\n",
		       vec, name, ref);
		BUG();
	}
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
	//^ does get_cpu() internally. do we neede the preempt dance?

	preempt_enable();
}
EXPORT_SYMBOL(muen_smp_trigger_event);

#if 0
static bool affinity_match_irq(

	const struct muen_cpu_affinity *const affinity, void *data)
{
	uint8_t irq = (uint8_t)data;

	if (affinity->res.kind == MUEN_RES_DEVICE) {
		struct muen_device_type *dev = &affinity->res.data.dev;
		return
			irq >= dev->irq_start &&
			irq <  dev->irq_start + dev->ir_count;
	} else if (affinity->res.kind == MUEN_RES_VECTOR) {
		return res->data.number == irq;
	} else {
		pr_warn("muen-smp: Unhandled affinity_match_irq resource kind %d", affinity->res.kind);
	}

	return false;
}

static int muen_affinity_get_irq_cpu(unsigned int irq)
{
	struct muen_cpu_affinity affinity;

	if (irq > UINT8_MAX)
		return false;

	if (!muen_smp_one_match_func(&affinity, muen_match_virq, (void *)irq))
		return false;

	*cpu = affinity.cpu;
	return true;
}
#endif

void muen_smp_setup_events(void)
{
	unsigned int cpu;
	struct muen_name_type n;
	const unsigned int this_cpu = smp_processor_id();
	struct muen_ipi_config *const ipis = this_cpu_ptr(&muen_ipis);

	if (ipis->call_func)
		pr_err("muen-smp: WARN: Events already setup!\n");

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
			muen_new_name(&n, "smp_signal_sm_%02d", cpu); // No SM on ARM64.
			bsp_ap_start[cpu - 1] = muen_smp_get_evt_vec
				(n.data, MUEN_RES_EVENT);
			pr_info("muen-smp: event %s with number %u\n", n.data,
				bsp_ap_start[cpu - 1]);
		}
#endif

		muen_new_name(&n, "smp_ipi_call_func_%02d%02d", this_cpu, cpu);
		ipis->call_func[cpu] = muen_smp_get_evt_vec(n.data, MUEN_RES_EVENT);
		pr_info("muen-smp: event %s with number %u\n", n.data,
			ipis->call_func[cpu]);

		muen_new_name(&n, "smp_ipi_reschedule_%02d%02d", this_cpu, cpu);
		ipis->reschedule[cpu] = muen_smp_get_evt_vec(n.data, MUEN_RES_EVENT);
		pr_info("muen-smp: event %s with number %u\n", n.data,
			ipis->reschedule[cpu]);

		/* Verify target vector assignment */
		muen_arch_verify_smp_events(this_cpu, cpu);
	}
}


/* CPU resource affinity handling */
static DEFINE_SPINLOCK(affinity_list_lock);
static struct list_head affinity_list = LIST_HEAD_INIT(affinity_list);

/* Add new entry to CPU affinity list */
static void affinity_list_add_entry(const struct muen_resource_type *const res)
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
			affinity_list_add_entry(res);
		break;
	case MUEN_RES_EVENT:
		affinity_list_add_entry(res);
		break;
	case MUEN_RES_VECTOR:
		affinity_list_add_entry(res);

		muen_arch_allocate_vector(res);
		break;
	default:
		break;
	}

	return true;
}

void muen_register_resources(void)
{
	muen_for_each_resource(register_resource, NULL);
}

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
