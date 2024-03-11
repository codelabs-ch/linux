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
#include <linux/irqdomain.h>

#include <muen/smp.h>

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

	irq = irq_create_mapping(NULL, vec);
	pr_info("muen-smp: Allocating IRQ %u for event %s (CPU#%d)\n",
		irq, res->name.data, this_cpu);
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

/**
 * Defined for x86, not for ARM64
 */
static inline void kvm_hypercall0(unsigned int num)
{
	asm volatile("mov   x0, %[num]\n"
		     "hvc #1"
		     : /* no outputs */
		     : [num] "r"(num));
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
	return 0;
}
console_initcall(muen_smp_init);
