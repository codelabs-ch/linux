// SPDX-License-Identifier: GPL-2.0+
/*
 * Muen platform setup code
 *
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

#include <linux/irq.h>
#include <linux/io.h>
#include <linux/kvm_para.h>
#include <linux/pm.h>

#include <asm/i8259.h>
#include <asm/x86_init.h>
#include <asm/hypervisor.h>
#include <asm/cpufeature.h>
#include <asm/reboot.h>

#include <muen/sinfo.h>
#include <muen/pci.h>
#include <muen/smp.h>

static void muen_machine_restart(char *__unused)
{
	const struct muen_resource_type *const
		event = muen_get_resource("reboot",	MUEN_RES_EVENT);

	if (!event) {
		pr_warn("muen: No reboot event, halting CPU\n");
		stop_this_cpu(NULL);
	}
	kvm_hypercall0(event->data.number);
}

static void muen_machine_emergency_restart(void)
{
	muen_machine_restart(NULL);
}

static void muen_machine_halt(void)
{
	const struct muen_resource_type *const
		event = muen_get_resource("poweroff", MUEN_RES_EVENT);

	if (!event) {
		pr_warn("muen: No poweroff event, halting CPU\n");
		stop_this_cpu(NULL);
	}
	kvm_hypercall0(event->data.number);
}

static void muen_machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
	muen_machine_halt();
}

static void muen_machine_crash_shutdown(struct pt_regs *regs)
{
	muen_machine_halt();
}

static const struct machine_ops muen_machine_ops __initconst = {
	.restart		= muen_machine_restart,
	.halt			= muen_machine_halt,
	.power_off		= muen_machine_power_off,
	.shutdown		= muen_machine_halt,
	.crash_shutdown		= muen_machine_crash_shutdown,
	.emergency_restart	= muen_machine_emergency_restart,
};

static int muen_pic_probe(void) { return NR_IRQS_LEGACY; }

static void __init muen_init_IRQ(void)
{
	native_init_IRQ();
	init_ISA_irqs();
}

static unsigned long muen_get_tsc(void)
{
	return muen_get_tsc_khz();
}

static void __init muen_platform_setup(void)
{
	setup_clear_cpu_cap(X86_FEATURE_TSC);
	tsc_khz = muen_get_tsc_khz();

	x86_init.irqs.intr_init	= muen_init_IRQ;
#ifdef CONFIG_MUEN_PCI
	x86_init.irqs.create_pci_msi_domain = muen_create_pci_msi_domain;
	x86_init.pci.arch_init	= muen_pci_init;
#endif

	x86_platform.calibrate_cpu = muen_get_tsc;
	x86_platform.calibrate_tsc = muen_get_tsc;

	/* Avoid searching for BIOS MP tables */
	x86_init.mpparse.find_smp_config = x86_init_noop;
	x86_init.mpparse.get_smp_config = x86_init_uint_noop;

#ifdef CONFIG_MUEN_SMP
	muen_smp_init();
#endif

	null_legacy_pic.nr_legacy_irqs = NR_IRQS_LEGACY;
	null_legacy_pic.probe          = muen_pic_probe;
	legacy_pic = &null_legacy_pic;

	machine_ops = muen_machine_ops;
}

static uint32_t __init muen_platform(void)
{
	muen_sinfo_early_init();
	return muen_check_magic();
}

const __initconst struct hypervisor_x86 x86_hyper_muen = {
	.name		    = "Muen SK",
	.detect		    = muen_platform,
	.type		    = X86_HYPER_MUEN,
	.init.init_platform = muen_platform_setup,
};
EXPORT_SYMBOL(x86_hyper_muen);
