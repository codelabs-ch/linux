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

#include <muen/pci.h>
#include <muen/sinfo.h>

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

static int muen_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_desc *msidesc;
	int node, ret, irq = dev->irq;
	uint16_t sid;
	struct muen_dev_info dev_info;

	/* Multiple vectors only supported for MSI-X */
	if (nvec > 1 && type == PCI_CAP_ID_MSI) {
		dev_info(&dev->dev, "Multiple vectors only supported for MSI-X\n");
		return 1;
	}

	sid = dev->bus->number << 8 | dev->devfn;
	if (!muen_get_dev_info(sid, &dev_info)) {
		dev_err(&dev->dev, "Error retrieving Muen device info\n");
		return -EINVAL;
	}
	if (nvec > dev_info.ir_count) {
		dev_err(&dev->dev, "Device requests more IRQs than allowed by policy (%d > %d)\n",
			nvec, dev_info.ir_count);
		return -EINVAL;
	}
	if (!dev_info.msi_capable) {
		dev_err(&dev->dev, "Device not configured for MSI\n");
		return -EINVAL;
	}

	node = dev_to_node(&dev->dev);
	if (irq >= NR_IRQS_LEGACY)
		irq = irq_alloc_descs(irq, irq, nvec, node);

	if (irq < 0) {
		dev_err(&dev->dev,
			"Error allocating IRQ desc: No space for %d IRQ(s)\n",
			nvec);
		ret = -ENOSPC;
		goto error;
	} else if (irq != dev->irq) {
		dev_err(&dev->dev, "Error allocating IRQ desc: %d != %d\n",
			dev->irq, irq);
		ret = -EINVAL;
		goto error;
	}

	list_for_each_entry(msidesc, &dev->msi_list, list) {
		ret = muen_setup_msi_irq(dev, msidesc, irq,
					 dev_info.irte_start);
		if (ret < 0)
			goto error;

		dev_info(&dev->dev, "IRQ %d for MSI%s\n", irq,
			 type == PCI_CAP_ID_MSIX ? "-X" : "");
		irq++;
	}
	return 0;

error:
	irq_free_descs(dev->irq, nvec);
	return ret;
}

static void muen_teardown_msi_irq(unsigned int irq)
{
	/* Do not destroy legacy IRQs */
	if (irq >= NR_IRQS_LEGACY && irq_to_desc(irq))
		irq_free_desc(irq);
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
