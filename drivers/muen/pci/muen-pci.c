// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2015  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2015  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/kvm_para.h>
#include <linux/msi.h>
#include <linux/pci.h>

#include <linux/acpi.h>
#include <asm/msidef.h>
#include <asm/hw_irq.h>
#include <asm/pci_x86.h>

#include <muen/pci.h>
#include <muen/sinfo.h>
#include <muen/smp.h>

static void noop(struct irq_data *data) { }

/**
 * Trigger corresponding EOI/unmask IRQ event which is stored in chip_data
 * during IRQ setup.
 */
static void muen_eoi_level(struct irq_data *irq_data)
{
	const unsigned long event_nr = (unsigned long)irq_data->chip_data;

	kvm_hypercall0(event_nr);
}

static bool muen_match_virq(const struct muen_cpu_affinity *const affinity,
		void *data)
{
	const uint8_t *const virq = data;

	return affinity->res.kind == MUEN_RES_DEVICE
		&& affinity->res.data.dev.irq_start <= *virq
		&& affinity->res.data.dev.irq_start +
		   affinity->res.data.dev.ir_count > *virq;
}

static bool muen_get_virq_affinity(const uint8_t virq, uint8_t *cpu)
{
	struct muen_cpu_affinity affinity;

	if (!muen_smp_one_match_func(&affinity, muen_match_virq, (void *)&virq))
		return false;

	*cpu = affinity.cpu;
	return true;
}

static void muen_irq_enable(struct irq_data *d)
{
	struct irq_chip *chip;
	uint8_t cpu;
	const uint8_t virq = d->irq + ISA_IRQ_VECTOR(0);
	struct irq_desc *desc;

	if (!muen_get_virq_affinity(virq, &cpu)) {
		pr_err("muen-pci: Error retrieving CPU affinity for vector %u, not enabling IRQ %u\n",
		       virq, d->irq);
		return;
	}

	if (!cpu_online(cpu)) {
		pr_err("muen-pci: CPU %u for IRQ %u not online, not enabling\n",
		       cpu, d->irq);
		return;
	}

	/*
	 * Check that descriptor is (still) present on associated CPU.
	 * Re-create it if not. S3 suspend code migrates active IRQs away from
	 * APs to BSP before suspending, which does not work for us (see
	 * fixup_irqs() in arch/x86/kernel/irq.c).
	 */
	if (per_cpu(vector_irq, cpu)[virq] == VECTOR_UNUSED) {
		desc = irq_to_desc(d->irq);
		if (!desc) {
			pr_err("muen-pci: No descriptor for vector %u, not enabling IRQ %u\n",
			       virq, d->irq);
			return;
		}
		per_cpu(vector_irq, cpu)[virq] = irq_to_desc(d->irq);
	}

	chip = irq_data_get_irq_chip(d);
	chip->irq_unmask(d);
}

/**
 * IRQ chip for PCI interrupts
 */
static struct irq_chip pci_chip = {
	.name        = "Muen-PCI",
	.irq_ack     = noop,
	.irq_mask    = noop,
	.irq_unmask  = noop,
	.irq_enable  = muen_irq_enable,
	.irq_eoi     = muen_eoi_level,
	.flags       = IRQCHIP_SKIP_SET_WAKE,
};

/**
 * IRQ chip for PCI MSI/MSI-x interrupts
 */
static struct irq_chip msi_chip = {
	.name        = "Muen-MSI",
	.irq_ack     = noop,
	.irq_mask    = pci_msi_mask_irq,
	.irq_unmask  = pci_msi_unmask_irq,
	.irq_enable  = muen_irq_enable,
	.flags       = IRQCHIP_SKIP_SET_WAKE,
};

static void muen_msi_compose_msg(struct pci_dev *pdev, struct msi_msg *msg,
				 unsigned int handle, unsigned int subhandle)
{
	msg->address_hi = MSI_ADDR_BASE_HI;
	msg->address_lo =
		MSI_ADDR_BASE_LO | MSI_ADDR_IR_EXT_INT |
		MSI_ADDR_IR_SHV |
		MSI_ADDR_IR_INDEX1(handle) |
		MSI_ADDR_IR_INDEX2(handle);

	msg->data = subhandle;

	dev_info(&pdev->dev, "Programming MSI address 0x%x with IRTE handle %d/%d\n",
		 msg->address_lo, handle, subhandle);
}

static int muen_setup_msi_irq(struct pci_dev *dev, struct msi_desc *msidesc,
			      const unsigned int irq, const u16 handle)
{
	unsigned int subhandle;
	struct msi_msg msg;
	int ret;

	ret = irq_set_msi_desc(irq, msidesc);
	if (ret < 0)
		return ret;

	/* Subhandle is offset from base IRQ */
	subhandle = irq - dev->irq;
	muen_msi_compose_msg(dev, &msg, handle, subhandle);
	pci_write_msi_msg(irq, &msg);

	irq_set_status_flags(irq, IRQ_NO_BALANCING);
	irq_set_chip_and_handler_name(irq, &msi_chip, handle_edge_irq, "edge");

	return 0;
}

static int muen_irq_alloc_descs(struct pci_dev *dev, const unsigned int virq,
				const unsigned int cnt)
{
	int alloc;

	alloc = irq_alloc_descs(dev->irq, dev->irq, cnt,
				dev_to_node(&dev->dev));

	if (alloc < 0) {
		dev_err(&dev->dev,
			"Error allocating %d IRQ desc(s) for IRQ %d\n", cnt,
			dev->irq);
		return -ENOSPC;
	} else if (alloc != dev->irq) {
		dev_err(&dev->dev, "Error allocating IRQ desc: %d != %d\n",
			alloc, dev->irq);
		irq_free_descs(dev->irq, cnt);
		return -EINVAL;
	}
	return 0;
}

/*
 * Free IRQ descriptor and unset per-core vector.
 */
static void muen_irq_free_desc(const unsigned int irq, const unsigned int virq)
{
	uint8_t cpu;
	bool res;

	irq_free_descs(irq, 1);

	res = muen_get_virq_affinity(virq, &cpu);
	WARN_ON(!res);
	if (res)
		per_cpu(vector_irq, cpu)[virq] = VECTOR_UNUSED;
}

static bool
muen_match_devsid(const struct muen_cpu_affinity *const affinity, void *data)
{
	const uint16_t *const sid = data;

	return affinity->res.kind == MUEN_RES_DEVICE
		&& affinity->res.data.dev.sid == *sid;
}

/*
 * Return overall IRQ count of device affinity entries in given list and set
 * data pointer to device resource with smallest IRQ value.
 */
unsigned int muen_devres_data(const struct muen_cpu_affinity *const affinity,
		const struct muen_device_type **data)
{
	struct muen_cpu_affinity *entry;
	unsigned int irq_start = 255 + 1, irq_count = 0;

	list_for_each_entry(entry, &affinity->list, list) {
		irq_count += entry->res.data.dev.ir_count;
		if (entry->res.data.dev.irq_start < irq_start) {
			irq_start = entry->res.data.dev.irq_start;
			*data = &entry->res.data.dev;
		}
	}
	return irq_count;
}

/**
 * Get device data and Muen CPU affinity data for device with given source ID.
 * On success, the caller must free the CPU affinity list.
 */
static int get_device_data(const struct pci_dev *const dev,
		unsigned int requested_irq_count,
		const struct muen_device_type **data,
		struct muen_cpu_affinity *const affinity)
{
	int ret;
	unsigned int affinity_count, irq_count;
	const uint16_t sid = PCI_DEVID(dev->bus->number, dev->devfn);

	affinity_count = muen_smp_get_res_affinity(affinity,
			&muen_match_devsid, (void *)&sid);
	if (list_empty(&affinity->list)) {
		dev_err(&dev->dev, "Error retrieving Muen device info for SID 0x%x\n",
			sid);
		return -EINVAL;
	}

	irq_count = muen_devres_data(affinity, data);
	if (requested_irq_count > irq_count) {
		dev_err(&dev->dev, "Device requests more IRQs than allowed by policy (%d > %d)\n",
			requested_irq_count, irq_count);
		ret = -EINVAL;
		goto error_free_affinity;
	}

	return 0;

error_free_affinity:
	muen_smp_free_res_affinity(affinity);
	return ret;
}

static int muen_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_desc *msidesc;
	int ret;
	unsigned int irq, sinfo_irq;
	struct muen_cpu_affinity affinity;
	const struct muen_device_type *dev_data = NULL;

	/* Multiple vectors only supported for MSI-X */
	if (nvec > 1 && type == PCI_CAP_ID_MSI) {
		dev_info(&dev->dev, "Multiple vectors only supported for MSI-X\n");
		return 1;
	}

	ret = get_device_data(dev, nvec, &dev_data, &affinity);
	if (ret < 0)
		return ret;

	if (!(dev_data->flags & DEV_MSI_FLAG)) {
		dev_err(&dev->dev, "Device not configured for MSI\n");
		ret = -EINVAL;
		goto error_free_affinity;
	}

	sinfo_irq = dev_data->irq_start - ISA_IRQ_VECTOR(0);
	if (sinfo_irq != dev->irq) {
		dev_err(&dev->dev, "Device has invalid IRQ %u != %u\n", dev->irq, sinfo_irq);
		ret = -EINVAL;
		goto error_free_affinity;
	}

	if (dev->irq >= NR_IRQS_LEGACY) {
		ret = muen_irq_alloc_descs(dev, sinfo_irq, nvec);
		if (ret < 0)
			goto error_free_affinity;
	}

	irq = dev->irq;
	list_for_each_entry(msidesc, dev_to_msi_list(&dev->dev), list) {
		ret = muen_setup_msi_irq(dev, msidesc, irq,
					 dev_data->irte_start);
		if (ret < 0)
			goto error_free_descs;

		dev_info(&dev->dev, "IRQ %d for MSI%s\n", irq,
			 type == PCI_CAP_ID_MSIX ? "-X" : "");
		irq++;
	}

	muen_smp_free_res_affinity(&affinity);
	return 0;

error_free_descs:
	irq_free_descs(irq, nvec);
error_free_affinity:
	muen_smp_free_res_affinity(&affinity);
	return ret;
}

static void muen_teardown_msi_irq(unsigned int irq)
{
	/* Do not destroy legacy IRQs */
	if (irq >= NR_IRQS_LEGACY && irq_to_desc(irq))
		muen_irq_free_desc(irq, irq + ISA_IRQ_VECTOR(0));
}

/**
 * Returns the number of the EOI/Unmask IRQ event corresponding to the given
 * vector. -EINVAL is returned, if none is present in Sinfo.
 */
static int muen_get_eoi_event(unsigned int vector)
{
	char name[MAX_NAME_LENGTH + 1];
	const struct muen_resource_type *res;

	memset(name, 0, sizeof(name));
	snprintf(name, MAX_NAME_LENGTH, "unmask_irq_%u", vector);

	res = muen_get_resource(name, MUEN_RES_EVENT);
	if (!res)
		return -EINVAL;

	return res->data.number;
}

static int muen_enable_irq(struct pci_dev *dev)
{
	int ret;
	unsigned long event_nr;
	unsigned int virq;
	struct muen_cpu_affinity affinity;
	const struct muen_device_type *dev_data = NULL;

	ret = get_device_data(dev, 1, &dev_data, &affinity);
	if (ret < 0)
		return ret;

	virq = dev_data->irq_start;
	dev->irq = virq - ISA_IRQ_VECTOR(0);
	if (dev_data->flags & DEV_MSI_FLAG) {
		dev_dbg(&dev->dev, "Skipping PCI IRQ allocation in favor of MSI\n");
		muen_smp_free_res_affinity(&affinity);
		return 0;
	}

	if (dev->irq >= NR_IRQS_LEGACY) {
		ret = muen_irq_alloc_descs(dev, virq, 1);
		if (ret < 0)
			goto error_free_affinity;
	}
	ret = muen_get_eoi_event(virq);
	if (ret < 0) {
		dev_err(&dev->dev, "EOI event for IRQ %d not present\n",
				dev->irq);
		ret = -EINVAL;
		goto error_free_desc;
	}

	event_nr = ret;
	irq_set_chip_data(dev->irq, (void *)event_nr);
	irq_set_chip_and_handler_name(dev->irq, &pci_chip,
			handle_fasteoi_irq, "fasteoi");
	dev_info(&dev->dev, "PCI IRQ %d (EOI event: %lu)\n", dev->irq,
				 event_nr);

	muen_smp_free_res_affinity(&affinity);
	return 0;

error_free_desc:
	irq_free_descs(dev->irq, 1);
error_free_affinity:
	muen_smp_free_res_affinity(&affinity);
	return ret;
}

static void muen_disable_irq(struct pci_dev *dev)
{
	muen_teardown_msi_irq(dev->irq);
}

/*
 * Required to set x86_init.pci.init in order to signal successful PCI
 * initialization, see pci_subsys_init().
 * By default, it is set to pci_acpi_init which will return an error due to
 * acpi_noirq.
 */
int __init pci_init_noop(void)
{
	return 0;
}

int __init muen_pci_init(void)
{
	pr_info("muen: Registering platform-specific PCI/MSI operations\n");

	pcibios_enable_irq = muen_enable_irq;
	pcibios_disable_irq = muen_disable_irq;

	x86_init.pci.init = pci_init_noop;
	x86_init.pci.init_irq = x86_init_noop;

	/* Avoid ACPI interference */
	acpi_noirq_set();

	return 0;
}

static int muen_msi_domain_alloc_irqs(struct irq_domain *domain, struct device *dev,
				      int nvec)
{
	int type;

	if (WARN_ON_ONCE(!dev_is_pci(dev)))
		return -EINVAL;

	if (first_msi_entry(dev)->msi_attrib.is_msix)
		type = PCI_CAP_ID_MSIX;
	else
		type = PCI_CAP_ID_MSI;

	return muen_setup_msi_irqs(to_pci_dev(dev), nvec, type);
}

static void muen_msi_domain_free_irqs(struct irq_domain *domain, struct device *dev)
{
	int i;
	struct msi_desc *entry;
	struct pci_dev *pdev;

	if (WARN_ON_ONCE(!dev_is_pci(dev)))
		return;

	pdev = to_pci_dev(dev);

	for_each_pci_msi_entry(entry, pdev) {
		if (entry->irq)
			for (i = 0; i < entry->nvec_used; i++)
				muen_teardown_msi_irq(entry->irq + i);
	}
}

static struct msi_domain_ops muen_pci_msi_domain_ops = {
	.domain_alloc_irqs	= muen_msi_domain_alloc_irqs,
	.domain_free_irqs	= muen_msi_domain_free_irqs,
};

static struct msi_domain_info muen_pci_msi_domain_info = {
	.flags		= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
			  MSI_FLAG_PCI_MSIX,
	.ops		= &muen_pci_msi_domain_ops,
	.chip		= &msi_chip,
	.handler	= handle_edge_irq,
	.handler_name	= "edge",
};

struct irq_domain * __init muen_create_pci_msi_domain(void)
{
	struct irq_domain *d = NULL;
	struct fwnode_handle *fn;

	fn = irq_domain_alloc_named_fwnode("Muen-PCI-MSI");
	if (fn)
		d = pci_msi_create_irq_domain(fn, &muen_pci_msi_domain_info, NULL);

	BUG_ON(!d);

	return d;
}

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen PCI driver");
MODULE_LICENSE("GPL");
