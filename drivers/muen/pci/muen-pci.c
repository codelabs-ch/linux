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
#include <linux/msi.h>
#include <linux/pci.h>

#include <asm/msidef.h>
#include <asm/hw_irq.h>

#include <muen/pci.h>
#include <muen/sinfo.h>
#include <muen/smp.h>

static void noop(struct irq_data *data) { }

/**
 * IRQ chip for PCI MSI/MSI-x interrupts
 */
static struct irq_chip msi_chip = {
	.name        = "Muen-MSI",
	.irq_ack     = noop,
	.irq_mask    = pci_msi_mask_irq,
	.irq_unmask  = pci_msi_unmask_irq,
	.flags       = IRQCHIP_SKIP_SET_WAKE,
};

static void muen_msi_compose_msg(struct pci_dev *pdev, unsigned int irq,
				 unsigned int dest, struct msi_msg *msg,
				 u8 hpet_id, u16 handle)
{
	int subhandle;

	/* Set subhandle to offset from base IRQ */
	subhandle = irq - pdev->irq;

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
			      const unsigned int irq,
			      const u16 handle)
{
	struct msi_msg msg;
	int ret;

	ret = irq_set_msi_desc(irq, msidesc);
	if (ret < 0)
		return ret;

	muen_msi_compose_msg(dev, irq, 0, &msg, -1, handle);
	pci_write_msi_msg(irq, &msg);

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
			"Error allocating IRQ desc for %d IRQ(s)\n", cnt);
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
 * Assumes the caller to be the CPU owning the IRQ.
 */
static void muen_irq_free_desc(const unsigned int irq, const unsigned int virq)
{
	irq_free_descs(irq, 1);
	__this_cpu_write(vector_irq[virq], VECTOR_UNUSED);
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

static void
muen_setup_cpu_vector_irqs(const struct muen_cpu_affinity *const affinity)
{
	unsigned int i;
	struct muen_cpu_affinity *entry;
	struct muen_device_type *dev;

	list_for_each_entry(entry, &affinity->list, list) {
		dev = &entry->res.data.dev;
		for (i = 0; i < dev->ir_count; i++)
			per_cpu(vector_irq, entry->cpu)[dev->irq_start + i] =
				irq_to_desc(dev->irq_start
						- ISA_IRQ_VECTOR(0) + i);
	}
}

static int muen_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_desc *msidesc;
	int ret;
	unsigned int irq, irq_count, virq, affinity_count;
	struct muen_cpu_affinity affinity;
	const uint16_t sid = PCI_DEVID(dev->bus->number, dev->devfn);
	const struct muen_device_type *dev_data = NULL;

	/* Multiple vectors only supported for MSI-X */
	if (nvec > 1 && type == PCI_CAP_ID_MSI) {
		dev_info(&dev->dev, "Multiple vectors only supported for MSI-X\n");
		return 1;
	}

	affinity_count = muen_smp_get_res_affinity(&affinity,
			&muen_match_devsid, (void *)&sid);
	if (list_empty(&affinity.list)) {
		dev_err(&dev->dev, "Error retrieving Muen device info for SID 0x%x\n",
			sid);
		return -EINVAL;
	}

	irq_count = muen_devres_data(&affinity, &dev_data);
	if (nvec > irq_count) {
		dev_err(&dev->dev, "Device requests more IRQs than allowed by policy (%d > %d)\n",
			nvec, irq_count);
		ret = -EINVAL;
		goto error_free_affinity;
	}
	if (!(dev_data->flags & DEV_MSI_FLAG)) {
		dev_err(&dev->dev, "Device not configured for MSI\n");
		ret = -EINVAL;
		goto error_free_affinity;
	}

	virq = dev_data->irq_start;
	dev->irq = virq - ISA_IRQ_VECTOR(0);
	if (dev->irq >= NR_IRQS_LEGACY) {
		ret = muen_irq_alloc_descs(dev, virq, nvec);
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

	muen_setup_cpu_vector_irqs(&affinity);
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

int __init muen_pci_init(void)
{
	pr_info("muen: Registering platform-specific MSI operations\n");

	x86_msi.setup_msi_irqs   = muen_setup_msi_irqs;
	x86_msi.teardown_msi_irq = muen_teardown_msi_irq;

	return 0;
}

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen PCI driver");
MODULE_LICENSE("GPL");
