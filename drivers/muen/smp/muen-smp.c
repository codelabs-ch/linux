#include <linux/cpu.h>
#include <linux/kvm_para.h>
#include <linux/delay.h>
#include <linux/sched/task_stack.h>
#include <linux/stackprotector.h>

#include <asm/smp.h>
#include <asm/desc.h>
#include <asm/hw_irq.h>
#include <asm/misc.h>
#include <asm/fpu/internal.h>

#include <muen/sinfo.h>

#include "muen-clkevt.h"

static const char *const res_names[] = {
	"none", "memory", "event", "vector", "device",
};

/* BSP AP start event array */
static uint8_t *bsp_ap_start = NULL;

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
	unsigned int this_cpu = smp_processor_id();
	struct muen_name_type n;

	struct muen_ipi_config *const cfg = this_cpu_ptr(&muen_ipis);

	/* In the non-SMP case, verify timer vector only */
	if (nr_cpu_ids == 1) {
		new_name(&n, "timer");
		muen_verify_vec(n.data, LOCAL_TIMER_VECTOR);
		return;
	}

	cfg->call_func = kzalloc(nr_cpu_ids * sizeof(uint8_t), GFP_ATOMIC);
	BUG_ON(!cfg->call_func);
	cfg->reschedule = kzalloc(nr_cpu_ids * sizeof(uint8_t), GFP_ATOMIC);
	BUG_ON(!cfg->reschedule);

	for_each_cpu(cpu, cpu_possible_mask) {
		if (this_cpu == cpu)
			continue;

		pr_info("muen-smp: Setup CPU#%u -> CPU#%u events/vectors\n",
			this_cpu, cpu);

		if (!this_cpu) {
			new_name(&n, "smp_signal_sm_%02d", cpu);
			bsp_ap_start[cpu - 1] = muen_get_evt_vec(n.data, MUEN_RES_EVENT);
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
	if (boot_cpu_has(X86_FEATURE_PCID))
		__write_cr4(__read_cr4() | X86_CR4_PCIDE);

	load_current_idt();
	cpu_init();
	preempt_disable();
	smp_callin();

	/* otherwise gcc will move up smp_processor_id before the cpu_init */
	barrier();
	/*
	 * Check TSC synchronization with the BP:
	 */
	check_tsc_sync_target();

	/*
	 * Lock vector_lock and initialize the vectors on this cpu
	 * before setting the cpu online. We must set it online with
	 * vector_lock held to prevent a concurrent setup/teardown
	 * from seeing a half valid vector space.
	 */
	lock_vector_lock();
	setup_vector_irq(smp_processor_id());
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
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

static int do_boot_cpu(int cpu, struct task_struct *idle)
{
	volatile u32 *trampoline_status =
		(volatile u32 *) __va(real_mode_header->trampoline_status);

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

	/* mark "stuck" area as not stuck */
	*trampoline_status = 0;

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

	smp_store_boot_cpu_info();
	set_cpu_sibling_map(0);

	pr_info("CPU0: ");
	print_cpu_info(&cpu_data(0));
	pr_info("muen-smp: Trampoline address is 0x%x\n",
		real_mode_header->trampoline_start);

	/* Assume possible CPUs to be present */
	for_each_possible_cpu(cpu)
		set_cpu_present(cpu, true);

	bsp_ap_start = kmalloc((nr_cpu_ids - 1) * sizeof(uint8_t),
			       GFP_KERNEL);
	BUG_ON(!bsp_ap_start);

	muen_setup_events();
	muen_setup_timer();
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

void __init muen_smp_init(void)
{
	smp_ops.smp_prepare_cpus = muen_smp_prepare_cpus;
	smp_ops.cpu_up = muen_cpu_up;
	smp_ops.send_call_func_ipi = muen_smp_send_call_function_ipi;
	smp_ops.send_call_func_single_ipi = muen_smp_send_call_function_single_ipi;
	smp_ops.smp_send_reschedule = muen_smp_send_reschedule;
}
