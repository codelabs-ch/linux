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
				const unsigned int cnt, const unsigned int cpu)
{
	int i, alloc;

	alloc = irq_alloc_descs(dev->irq, dev->irq, cnt,
				dev_to_node(&dev->dev));

	if (alloc < 0) {
		dev_err(&dev->dev,
			"Error allocating IRQ desc: No space for %d IRQ(s)\n",
			cnt);
		return -ENOSPC;
	} else if (alloc != dev->irq) {
		dev_err(&dev->dev, "Error allocating IRQ desc: %d != %d\n",
			alloc, dev->irq);
		irq_free_descs(dev->irq, cnt);
		return -EINVAL;
	}

	for (i = 0; i < cnt; i++)
		per_cpu(vector_irq, cpu)[virq + i] = irq_to_desc(dev->irq + i);

	return 0;
}

static void muen_irq_free_descs(const unsigned int irq, const unsigned int virq,
				const unsigned int cnt)
{
	int i;

	irq_free_descs(irq, cnt);
	for (i = 0; i < cnt; i++)
		__this_cpu_write(vector_irq[virq + i], VECTOR_UNUSED);
}

static int muen_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_desc *msidesc;
	int ret;
	unsigned int irq, virq, cpu;
	const uint16_t sid = PCI_DEVID(dev->bus->number, dev->devfn);

	const struct muen_device_type *const
		dev_info = muen_smp_get_irq_affinity(sid, &cpu);

	/* Multiple vectors only supported for MSI-X */
	if (nvec > 1 && type == PCI_CAP_ID_MSI) {
		dev_info(&dev->dev, "Multiple vectors only supported for MSI-X\n");
		return 1;
	}

	if (!dev_info) {
		dev_err(&dev->dev, "Error retrieving Muen device info for SID 0x%x\n",
			sid);
		return -EINVAL;
	}
	if (nvec > dev_info->ir_count) {
		dev_err(&dev->dev, "Device requests more IRQs than allowed by policy (%d > %d)\n",
			nvec, dev_info->ir_count);
		return -EINVAL;
	}
	if (!(dev_info->flags & DEV_MSI_FLAG)) {
		dev_err(&dev->dev, "Device not configured for MSI\n");
		return -EINVAL;
	}

	virq = dev_info->irq_start;
	dev->irq = virq - ISA_IRQ_VECTOR(0);
	if (dev->irq >= NR_IRQS_LEGACY) {
		ret = muen_irq_alloc_descs(dev, virq, nvec, cpu);
		if (ret < 0)
			return ret;
	}

	irq = dev->irq;
	list_for_each_entry(msidesc, dev_to_msi_list(&dev->dev), list) {
		ret = muen_setup_msi_irq(dev, msidesc, irq,
					 dev_info->irte_start);
		if (ret < 0)
			goto error;

		dev_info(&dev->dev, "IRQ %d for MSI%s\n", irq,
			 type == PCI_CAP_ID_MSIX ? "-X" : "");
		irq++;
	}
	return 0;

error:
	muen_irq_free_descs(dev->irq, virq, nvec);
	return ret;
}

static void muen_teardown_msi_irq(unsigned int irq)
{
	/* Do not destroy legacy IRQs */
	if (irq >= NR_IRQS_LEGACY && irq_to_desc(irq))
		muen_irq_free_descs(irq, irq + ISA_IRQ_VECTOR(0), 1);
}

int __init muen_msi_init(void)
{
	pr_info("muen: Registering platform-specific MSI operations\n");

	x86_msi.setup_msi_irqs   = muen_setup_msi_irqs;
	x86_msi.teardown_msi_irq = muen_teardown_msi_irq;

	return 0;
}

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen PCI MSI driver");
MODULE_LICENSE("GPL");
