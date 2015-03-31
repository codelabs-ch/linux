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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/msi.h>
#include <linux/pci.h>

#include <asm/msidef.h>
#include <asm/x86_init.h>

/**
 * Returns the MSI handle of the given PCI device. The value is encoded
 * in the _PRT entry which has a PIN value greater than 3.
 */
static int acpi_get_prt_msi_handle(struct pci_dev *dev)
{
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_pci_routing_table *entry;
	acpi_handle handle = NULL;
	const int device_addr = PCI_SLOT(dev->devfn);
	int msi_handle = -1;

	if (dev->bus->bridge)
		handle = ACPI_HANDLE(dev->bus->bridge);

	if (!handle)
		return -ENODEV;

	/* 'handle' is the _PRT's parent (root bridge or PCI-PCI bridge) */
	status = acpi_get_irq_routing_table(handle, &buffer);
	if (ACPI_FAILURE(status)) {
		kfree(buffer.pointer);
		return -ENODEV;
	}

	entry = buffer.pointer;
	while (entry && (entry->length > 0)) {
		if (((entry->address >> 16) & 0xffff) == device_addr &&
			entry->pin > 3) {
			/* Handle is encoded in the _PRT entry's pin field */
			msi_handle = entry->pin;
			break;
		}
		entry = (struct acpi_pci_routing_table *)
		    ((unsigned long)entry + entry->length);
	}

	kfree(buffer.pointer);
	return msi_handle;
}

static void muen_msi_compose_msg(struct pci_dev *pdev, unsigned int irq,
		unsigned int dest, struct msi_msg *msg, u8 hpet_id)
{
	int handle;
	handle = acpi_get_prt_msi_handle(pdev);

	BUG_ON(handle < 0);

	msg->address_hi = MSI_ADDR_BASE_HI;
	msg->address_lo =
		MSI_ADDR_BASE_LO | MSI_ADDR_IR_EXT_INT |
		MSI_ADDR_IR_INDEX1(handle) |
		MSI_ADDR_IR_INDEX2(handle);

	/* No subhandle */
	msg->data = 0;

	dev_info(&pdev->dev, "programming MSI address 0x%x with IRTE handle %d\n",
			msg->address_lo, handle);
}

static int muen_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	pr_err("muen: muen_setup_msi_irqs not implemented\n");
	return 1;
}

static void muen_teardown_msi_irq(unsigned int irq)
{
	pr_err("muen: muen_teardown_msi_irq not implemented\n");
}

int __init muen_msi_init(void)
{
	pr_info("muen: Registering platform-specific MSI operations\n");

	x86_msi.setup_msi_irqs   = muen_setup_msi_irqs;
	x86_msi.teardown_msi_irq = muen_teardown_msi_irq;
	x86_msi.compose_msi_msg  = muen_msi_compose_msg;

	return 0;
}

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen PCI MSI driver");
MODULE_LICENSE("GPL");
