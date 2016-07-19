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

#include <asm/i8259.h>
#include <asm/x86_init.h>
#include <asm/hypervisor.h>

#include <muen/sinfo.h>
#include <muen/pci.h>

static int muen_pic_probe(void) { return NR_IRQS_LEGACY; }

static void muen_set_cpu_features(struct cpuinfo_x86 *c)
{
       clear_cpu_cap(c, X86_FEATURE_TSC);
}

static void __init muen_init_IRQ(void)
{
	native_init_IRQ();
	init_ISA_irqs();
}

static void __init muen_platform_setup(void)
{
	x86_init.irqs.intr_init	= muen_init_IRQ;
#ifdef CONFIG_MUEN_PCI_MSI
	x86_init.pci.arch_init	= muen_msi_init;
#endif

	null_legacy_pic.nr_legacy_irqs = NR_IRQS_LEGACY;
	null_legacy_pic.probe          = muen_pic_probe;
	legacy_pic = &null_legacy_pic;
}

static uint32_t __init muen_platform(void)
{
	muen_sinfo_early_init();
	return muen_check_magic();
}

const __refconst struct hypervisor_x86 x86_hyper_muen = {
	.name		  = "Muen SK",
	.detect		  = muen_platform,
	.set_cpu_features = muen_set_cpu_features,
	.init_platform	  = muen_platform_setup,
};
EXPORT_SYMBOL(x86_hyper_muen);
