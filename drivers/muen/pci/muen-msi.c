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

#include <asm/x86_init.h>

static void muen_msi_compose_msg(struct pci_dev *pdev, unsigned int irq,
		unsigned int dest, struct msi_msg *msg, u8 hpet_id)
{
	pr_err("muen: muen_msi_compose_msg not implemented\n");
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
