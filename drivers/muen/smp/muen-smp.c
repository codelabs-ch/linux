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
#include <linux/stackprotector.h>
#include <linux/smp.h>

#include <asm/desc.h>
#include <asm/hw_irq.h>
#include <asm/spec-ctrl.h>
#include <asm/fpu/internal.h>

#include <muen/smp.h>

#include "muen-clkevt.h"

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

static void muen_verify_vec(const char *const name, const unsigned int ref)
{
	const unsigned int vec = muen_get_evt_vec(name, MUEN_RES_VECTOR);

	if (vec != ref) {
		pr_err("muen-smp: Unexpected vector %u for %s, should be %u\n",
		       vec, name, ref);
		BUG();
	}
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

		new_name(&n, "timer");
		muen_verify_vec(n.data, LOCAL_TIMER_VECTOR);
		new_name(&n, "smp_ipi_reschedule_%02d%02d", cpu, this_cpu);
		muen_verify_vec(n.data, RESCHEDULE_VECTOR);
		new_name(&n, "smp_ipi_call_func_%02d%02d", cpu, this_cpu);
		muen_verify_vec(n.data, CALL_FUNCTION_SINGLE_VECTOR);
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
	list_add_tail(&entry->list, &affinity_list);
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

	if (vec > ISA_IRQ_VECTOR(15) && vec < FIRST_SYSTEM_VECTOR) {
		irq = irq_alloc_desc_at(vec - ISA_IRQ_VECTOR(0), -1);
		per_cpu(vector_irq, this_cpu)[vec] = irq_to_desc(irq);
		pr_info("muen-smp: Allocating IRQ %u for event %s (CPU#%d)\n",
			irq, res->name.data, this_cpu);
		irq_set_chip_and_handler(irq, &dummy_irq_chip,
			handle_edge_irq);
	}
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

static void muen_smp_store_cpu_info(int id)
{
	struct cpuinfo_x86 *c = &cpu_data(id);

	*c = boot_cpu_data;
	c->cpu_index = id;
	c->initial_apicid = id;
	c->apicid = id;

	BUG_ON(c == &boot_cpu_data);
	BUG_ON(topology_update_package_map(c->phys_proc_id, id));
}

/*
 * Report back to the Boot Processor during boot time or to the caller processor
 * during CPU online.
 */
static void smp_callin(void)
{
	const int cpuid = smp_processor_id();

	muen_smp_store_cpu_info(cpuid);

	set_cpu_sibling_map(raw_smp_processor_id());

	calibrate_delay();
	cpu_data(cpuid).loops_per_jiffy = loops_per_jiffy;

	wmb();

	notify_cpu_starting(cpuid);

	/*
	 * Allow the master to continue.
	 */
	cpumask_set_cpu(cpuid, cpu_callin_mask);
}

/*
 * Activate a secondary processor.
 */
static void notrace start_secondary(void *unused)
{
	/*
	 * Don't put *anything* except direct CPU state initialization
	 * before cpu_init(), SMP booting is too fragile that we want to
	 * limit the things done here to the most necessary things.
	 */
	cr4_init();

	load_current_idt();
	cpu_init();
	x86_cpuinit.early_percpu_clock_init();
	preempt_disable();
	smp_callin();

	/* otherwise gcc will move up smp_processor_id before the cpu_init */
	barrier();
	/*
	 * Check TSC synchronization with the BP:
	 */
	check_tsc_sync_target();

	speculative_store_bypass_ht_init();

	/*
	 * Lock vector_lock and initialize the vectors on this cpu
	 * before setting the cpu online. We must set it online with
	 * vector_lock held to prevent a concurrent setup/teardown
	 * from seeing a half valid vector space.
	 */
	lock_vector_lock();
	lapic_online();
	set_cpu_online(smp_processor_id(), true);
	unlock_vector_lock();
	cpu_set_state_online(smp_processor_id());
	x86_platform.nmi_init();

	/* enable local interrupts */
	local_irq_enable();

	/* to prevent fake stack check failure in clock setup */
	boot_init_stack_canary();

	wmb();
	muen_setup_events();
	muen_setup_timer();
	muen_register_resources();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

static int do_boot_cpu(int cpu, struct task_struct *idle)
{
	unsigned long boot_error = 0;
	unsigned long timeout;

	idle->thread.sp = (unsigned long)task_pt_regs(idle);
	early_gdt_descr.address = (unsigned long)get_cpu_gdt_rw(cpu);
	initial_code = (unsigned long)start_secondary;
	initial_stack  = idle->thread.sp;

	cpumask_clear_cpu(cpu, cpu_initialized_mask);
	smp_mb();

	kvm_hypercall0(bsp_ap_start[cpu - 1]);

	/*
	 * Wait 10s total for first sign of life from AP
	 */
	boot_error = -1;
	timeout = jiffies + 10*HZ;
	while (time_before(jiffies, timeout)) {
		if (cpumask_test_cpu(cpu, cpu_initialized_mask)) {
			/*
			 * Tell AP to proceed with initialization
			 */
			cpumask_set_cpu(cpu, cpu_callout_mask);
			boot_error = 0;
			break;
		}
		schedule();
	}

	if (!boot_error) {
		/*
		 * Wait till AP completes initial initialization
		 */
		while (!cpumask_test_cpu(cpu, cpu_callin_mask))
			schedule();
	}

	return boot_error;
}

int muen_cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	unsigned long flags;
	int err, ret = 0;

	WARN_ON(irqs_disabled());

	if (cpumask_test_cpu(cpu, cpu_callin_mask)) {
		pr_info("muen-smp: do_boot_cpu %d Already started\n", cpu);
		return -ENOSYS;
	}

	/* x86 CPUs take themselves offline, so delayed offline is OK. */
	err = cpu_check_up_prepare(cpu);
	if (err && err != -EBUSY)
		return err;

	/* the FPU context is blank, nobody can own it */
	per_cpu(fpu_fpregs_owner_ctx, cpu) = NULL;

	common_cpu_up(cpu, tidle);

	err = do_boot_cpu(cpu, tidle);
	if (err) {
		pr_err("muen-smp: do_boot_cpu failed(%d) to wakeup CPU#%u\n",
		       err, cpu);
		ret = -EIO;
		goto out;
	}

	/*
	 * Check TSC synchronization with the AP (keep irqs disabled
	 * while doing so):
	 */
	local_irq_save(flags);
	check_tsc_sync_source(cpu);
	local_irq_restore(flags);

	while (!cpu_online(cpu))
		cpu_relax();

out:
	return ret;
}

static void __init muen_smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int cpu;
	struct muen_name_type n;

	smp_store_boot_cpu_info();
	set_cpu_sibling_map(0);

	pr_info("CPU0: ");
	print_cpu_info(&cpu_data(0));

	muen_setup_timer();
	muen_register_resources();

	/* In the non-SMP case, verify timer vector only */
	if (nr_cpu_ids == 1) {
		new_name(&n, "timer");
		muen_verify_vec(n.data, LOCAL_TIMER_VECTOR);
		return;
	}

	/* Assume possible CPUs to be present */
	for_each_possible_cpu(cpu)
		set_cpu_present(cpu, true);

	bsp_ap_start = kmalloc((nr_cpu_ids - 1) * sizeof(uint8_t),
			       GFP_KERNEL);
	BUG_ON(!bsp_ap_start);

	muen_setup_events();
}

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
	const unsigned int this_cpu = smp_processor_id();

	BUG_ON(cpu >= nr_cpu_ids);

	if (cpu == this_cpu)
		kvm_hypercall0(id);
	else
		smp_call_function_single(cpu, do_trigger_event, (void *)&id, 1);
}
EXPORT_SYMBOL(muen_smp_trigger_event);

int muen_smp_get_res_affinity(struct muen_cpu_affinity *const result,
		match_func func, void *match_data)
{
	unsigned int count = 0;
	struct muen_cpu_affinity *entry, *copy;

	INIT_LIST_HEAD(&result->list);

	list_for_each_entry(entry, &affinity_list, list) {
		if (!func || func(entry, match_data)) {
			copy = kmemdup(entry, sizeof(*entry), GFP_KERNEL);
			if (!copy)
				goto free_and_exit;
			list_add_tail(&copy->list, &result->list);
			count++;
		}
	}
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

bool muen_smp_one_match(struct muen_cpu_affinity *const result,
		const char *const name, const enum muen_resource_kind kind)
{
	unsigned int affinity_count;
	struct muen_cpu_affinity affinity, *first;
	const struct match_data match = {
		.name = name,
		.kind = kind,
	};

	affinity_count = muen_smp_get_res_affinity(&affinity,
			muen_match_name_kind, (void *)&match);
	if (affinity_count == 1) {
		first = list_first_entry(&affinity.list,
				struct muen_cpu_affinity, list);
		*result = *first;
	}

	muen_smp_free_res_affinity(&affinity);
	return affinity_count == 1;
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

void __init muen_smp_init(void)
{
	smp_ops.smp_prepare_cpus = muen_smp_prepare_cpus;
	smp_ops.cpu_up = muen_cpu_up;
	smp_ops.send_call_func_ipi = muen_smp_send_call_function_ipi;
	smp_ops.send_call_func_single_ipi = muen_smp_send_call_function_single_ipi;
	smp_ops.smp_send_reschedule = muen_smp_send_reschedule;
}
