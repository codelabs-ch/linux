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
#include <linux/sched/task_stack.h>
#include <linux/memblock.h>

#include <asm/apic.h>
#include <asm/desc.h>
#include <asm/hw_irq.h>
#include <asm/realmode.h>
#include <asm/spec-ctrl.h>
#include <asm/fpu/api.h>

#include <muen/smp.h>
#include <muen/timer.h>


static const char *const res_names[] = { // DUP
	"none", "memory", "event", "vector", "device",
};

static unsigned int muen_get_evt_vec(const char *const name, // DUP
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

static void new_name(struct muen_name_type *const n, const char *str, ...) // DUP
{
	va_list ap;

	memset(n->data, 0, sizeof(n->data));

	va_start(ap, str);
	vsnprintf(n->data, sizeof(n->data), str, ap);
	va_end(ap);
}

void muen_arch_verify_smp_events(unsigned int this_cpu, unsigned int cpu)
{
	struct muen_name_type n;

	new_name(&n, "timer");
	muen_verify_vec(n.data, LOCAL_TIMER_VECTOR);
	new_name(&n, "smp_ipi_reschedule_%02d%02d", cpu, this_cpu);
	muen_verify_vec(n.data, RESCHEDULE_VECTOR);
	new_name(&n, "smp_ipi_call_func_%02d%02d", cpu, this_cpu);
	muen_verify_vec(n.data, CALL_FUNCTION_SINGLE_VECTOR);
}

/*
 * Allocate IRQ descriptor for given vector and register it with IRQ chip.
 */
void muen_arch_allocate_vector(const struct muen_resource_type *const res)
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

	cpu_init_secondary();
	rcu_cpu_starting(raw_smp_processor_id());
	x86_cpuinit.early_percpu_clock_init();
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
	set_cpu_online(smp_processor_id(), true);
	lapic_online();
	unlock_vector_lock();
	cpu_set_state_online(smp_processor_id());
	x86_platform.nmi_init();

	/* enable local interrupts */
	local_irq_enable();

	x86_cpuinit.setup_percpu_clockev();

	wmb();
	muen_smp_setup_events();
	muen_setup_timer_event();
	muen_register_clockevent_dev();
	muen_register_resources();
	muen_sinfo_log_resources();
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

	muen_sinfo_setup(cpu);
	muen_setup_timer_page(cpu);
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

	muen_sinfo_log_resources();
	muen_setup_timer_page(0);
	muen_setup_timer_event();
	muen_register_clockevent_dev();
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

	muen_smp_setup_events();
}

static void __init muen_smp_reserve_real_mode(void)
{
	const phys_addr_t addr = 0x20000;
	phys_addr_t mem;
	size_t size = real_mode_size_needed();

	if (!size)
		return;

	WARN_ON(slab_is_available());

	/* Allocate our expected AP trampoline address. */
	mem = memblock_phys_alloc_range(size, PAGE_SIZE, addr, addr + size);
	if (!mem)
		pr_warn("muen-smp: Unable to allocate AP trampoline @ 0x%llx, size 0x%lx\n",
			addr, size);
	else {
		set_real_mode_mem(mem);
		pr_info("muen-smp: Allocated AP trampoline @ 0x%llx, size 0x%lx\n",
			addr, size);
	}

	/*
	 * Unconditionally reserve the entire first 1M, see comment in
	 * setup_arch().
	 */
	memblock_reserve(0, SZ_1M);
}

static void muen_smp_send_call_function_single_ipi(int cpu)
{
	struct muen_ipi_config *const cfg = this_cpu_ptr(&muen_ipis);

	kvm_hypercall0(cfg->call_func[cpu]);
}

static void muen_smp_send_call_function_ipi(const struct cpumask *mask)
{
	unsigned int cpu;
	struct muen_ipi_config *const cfg = this_cpu_ptr(&muen_ipis);

	for_each_cpu(cpu, mask)
		kvm_hypercall0(cfg->call_func[cpu]);
}

static void muen_smp_send_reschedule(int cpu)
{
	struct muen_ipi_config *const cfg = this_cpu_ptr(&muen_ipis);

	kvm_hypercall0(cfg->reschedule[cpu]);
}

void __init muen_smp_init(void)
{
	smp_ops.smp_prepare_cpus = muen_smp_prepare_cpus;
	smp_ops.cpu_up = muen_cpu_up;
	smp_ops.send_call_func_ipi = muen_smp_send_call_function_ipi;
	smp_ops.send_call_func_single_ipi = muen_smp_send_call_function_single_ipi;
	smp_ops.smp_send_reschedule = muen_smp_send_reschedule;

	x86_platform.realmode_reserve = muen_smp_reserve_real_mode;
}
