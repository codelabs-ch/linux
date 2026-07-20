// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023-2024  Tobias Brunner <tobias@codelabs.ch>
 * Copyright (C) 2019-2026  David Loosli <dave@codelabs.ch>
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
 *
 * This file contains the Muen SK IRQ chip implementation that is required by
 * all Linux subjects of a Muen SK system.
 *
 * The driver acts as counterpart to the GIC virtualization provided by the
 * Muen SK. It currently only supports a static configuration of the virtual
 * CPU interface with only group 0 interrupts, separate priority drop and
 * deactivation (i.e. EOI mode equ. 1) and default priority and binary point
 * values.
 *
 * Like the official ARM GICv2 implementation and its derivatives, the Muen SK
 * acknowledges and drops the priority in the exception irq entry function to
 * be able to let the Linux kernel handle all interrupts as edge-triggerd, even
 * though most hardware interrupts are defined as level-sensitive by the actual
 * ARM and SoC specification. This also allows the driver to let the ack, mask
 * and unmask function implementations empty.
 */

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/types.h>

#include <asm/exception.h>

/*
 * Definitions
 */
#define NUMBER_OF_INTERRUPTS       1024
#define NUMBER_OF_SGI_INTERRUPTS     16
#define NUMBER_OF_PPI_INTERRUPTS     16

#define SGI_INTERRUPT_TYPE    2
#define PPI_INTERRUPT_TYPE    1
#define SPI_INTERRUPT_TYPE    0

#define IRQ_CONTROL_OFFSET                     0x0000
#define IRQ_PRIORITY_MASK_OFFSET               0x0004
#define IRQ_BINARY_POINT_OFFSET                0x0008
#define IRQ_ACKNOWLEDGE_OFFSET                 0x000C
#define IRQ_END_OF_INTERRUPT_OFFSET            0x0010
#define IRQ_RUNNING_PRIORITY_OFFSET            0x0014
#define IRQ_HIGHEST_PRIORITY_OFFSET            0x0018
#define IRQ_DEACTIVATE_INTERRUPT_OFFSET        0x1000

#define IRQ_ACKNOWLEDGE_MASK                   0x03FF

#define IRQ_NO_PENDING_GROUP_1_VALUE  1022
#define IRQ_NO_PENDING_GROUP_0_VALUE  1023

#define IRQ_DEFAULT_CONTROL         0x00000201
#define IRQ_DEFAULT_PRIORITY        0x000000f8
#define IRQ_DEFAULT_BINARY_POINT    0x00000002

static inline bool is_sgi_interrupt(unsigned long hardware_irq)
{
	return hardware_irq >= 0 && hardware_irq <= 15;
}

static inline bool is_ppi_interrupt(unsigned long hardware_irq)
{
	return hardware_irq >= 16 && hardware_irq <= 31;
}

static inline bool is_spi_interrupt(unsigned long hardware_irq)
{
	return hardware_irq >= 32 && hardware_irq <= 1119;
}

struct muen_irqchip_data {
	struct irq_chip chip;
	unsigned long physical_address;
	void __iomem *raw_address;
	struct irq_domain *domain;
	bool initialized;
};

static struct muen_irqchip_data muen_chip_data;

/**
 * muen_irq_domain_map - Maps an interrupt based on its hardware
 * id and its type, i.e. software generated and private peripheral
 * interrupts per cpu with the respective domain info, flags and
 * irq handler function and shared peripheral interrupts with no
 * autoprobing and the fast EOI handler (c.f. description). This
 * function is part of the irq domain ops specification.
 *
 * @d  :	the interrupt domain
 * @irq:	the software interrupt number
 * @hw :	the hardware interrupt number
 *
 * Returns 0 for success or an error (c.f. kernel/irq/irqdesc.c,
 * irq_set_percpu_devid_partition) otherwise.
 */
static int muen_irq_domain_map(
	struct irq_domain *d, unsigned int irq, irq_hw_number_t hw
)
{
	pr_debug("%s: Domain map the irq %ld", muen_chip_data.chip.name, hw);

	if (is_sgi_interrupt(hw) || is_ppi_interrupt(hw)) {
		irq_domain_set_info(d, irq, hw, &(muen_chip_data.chip), d->host_data,
			handle_percpu_devid_irq, NULL, NULL);
		return irq_set_percpu_devid(irq);
	}

	irq_domain_set_info(d, irq, hw, &(muen_chip_data.chip), d->host_data,
		handle_fasteoi_irq, NULL, NULL);
	irq_set_noprobe(irq);
	return 0;
}

/**
 * muen_irq_domain_unmap - Unmaps an interrupt based on the
 * irq domain reset function. This function is part of the irq
 * domain ops specification.
 *
 * @d  :	the interrupt domain
 * @irq:	the software interrupt number
 */
static void muen_irq_domain_unmap(struct irq_domain *d, unsigned int irq)
{
	pr_debug("%s: Domain unmap the irq %d", muen_chip_data.chip.name, irq);
	irq_domain_reset_irq_data(irq_get_irq_data(irq));
}

/**
 * muen_irq_domain_xlate - Translates the interrupt properties
 * provided by the device tree according to the interrupt type.
 * This function is part of the irq domain ops specification.
 *
 * @d         :	the interrupt domain
 * @ctrlr     :	a device tree node
 * @intspec   :	the interrupt specification of the node
 * @intsize   :	the array size of the interrupt specification
 * @out_hwirq :	the hardware interrupt number (out parameter)
 * @out_type  :	the hardware interrupt type (out parameter)
 *
 * Returns 0 for success or an EINVAL error, if the number
 * of interrupt entries is not equal to 3 (i.e. software
 * generated vs. private peripheral vs. shared peripheral
 * for [0], hardware irq id for [1] and interrupt type for
 * [2] with edge, level etc.).
 */
static int muen_irq_domain_xlate(
	struct irq_domain *d, struct device_node *ctrlr,
	const u32 *intspec, unsigned int intsize,
	unsigned long *out_hwirq, unsigned int *out_type
)
{
	if (WARN_ON(intsize != 3))
		return -EINVAL;

	pr_debug("%s: Domain xlate with irq specification: %d / %d / %d",
		 muen_chip_data.chip.name, intspec[0], intspec[1], intspec[2]);

	if (intspec[0] == SGI_INTERRUPT_TYPE) {
		*out_hwirq = intspec[1];
		*out_type  = intspec[2] & IRQ_TYPE_SENSE_MASK;
	}

	if (intspec[0] == PPI_INTERRUPT_TYPE) {
		*out_hwirq = intspec[1] + 16;
		*out_type  = intspec[2] & IRQ_TYPE_SENSE_MASK;
	}

	if (intspec[0] == SPI_INTERRUPT_TYPE) {
		*out_hwirq = intspec[1] + 32;
		*out_type  = intspec[2] & IRQ_TYPE_SENSE_MASK;
	}

	WARN_ON(*out_type == IRQ_TYPE_NONE);
	return 0;
}

/**
 * muen_mask - Masking should disable the signaling of an
 * interrupt to the core, but is neither required for this
 * approach (c.f. description) nor (yet) supported. For a
 * Muen SK system unmasking has to be provided by the
 * hypervisor and therefore would have to make use of an
 * HVC call and parameter passing via registers. This
 * function is part of the irq chip ops specification.
 *
 * @data :	the interrupt data
 */
void muen_mask(struct irq_data *data)
{
	pr_debug("%s: Mask called with irq %ld", muen_chip_data.chip.name, data->hwirq);
}

/**
 * muen_unmask - Unmasking should enable the signaling of
 * an interrupt to the core, but is neither required for
 * this approach (c.f. description) nor (yet) supported.
 * For a Muen SK system unmasking has to be provided by
 * the hypervisor and therefore would have to make use of
 * an HVC call and parameter passing via registers. This
 * function is part of the irq chip ops specification.
 *
 * @data :	the interrupt data
 */
void muen_unmask(struct irq_data *data)
{
	pr_debug("%s: Unmask called with irq %ld", muen_chip_data.chip.name, data->hwirq);
}

/**
 * muen_ack - Acknowledging should mark an interrupt to
 * be actively handled, but is not required for this
 * approach (c.f. description). This function is part of
 * the irq chip ops specification.
 *
 * @data :	the interrupt data
 */
void muen_ack(struct irq_data *data) { }

/**
 * muen_eoi - Signals the end of the interrupt handling.
 * For the currently used approach, this is done by writing
 * to the deactivation register (c.f. description). This
 * function is part of the irq chip ops specification.
 *
 * @data :	the interrupt data
 */
void muen_eoi(struct irq_data *data)
{
	writel_relaxed(data->hwirq, muen_chip_data.raw_address + IRQ_DEACTIVATE_INTERRUPT_OFFSET);
	isb();
}

/**
 * muen_handle_irq - Called by the Linux kernel scheduling
 * and exception handling process for every interrupt raised
 * on the core's interface. For the currently used approach,
 * this function acknowledges and drops the priority of the
 * interrupt to "simulate" an edge triggered Linux irq (c.f.
 * description).
 *
 * @regs :	the registers stored at exception entry
 */
static void __exception_irq_entry muen_handle_irq(struct pt_regs *regs)
{
	u32 irq_status, irq_number;

	do {
		irq_status = readl_relaxed(muen_chip_data.raw_address + IRQ_ACKNOWLEDGE_OFFSET);
		irq_number = irq_status & IRQ_ACKNOWLEDGE_MASK;

		if (irq_number != IRQ_NO_PENDING_GROUP_1_VALUE &&
		    irq_number != IRQ_NO_PENDING_GROUP_0_VALUE) {
			writel_relaxed(irq_status, muen_chip_data.raw_address + IRQ_END_OF_INTERRUPT_OFFSET);
			generic_handle_domain_irq(muen_chip_data.domain, irq_number);
			continue;
		}
		break;
	} while (1);
}

/**
 * muen_set_affinity - Set CPU affinity. Currently a no-op as we don't support
 * more than one CPU.
 */
static int muen_set_affinity(struct irq_data *d,
			       const struct cpumask *mask_val, bool force)
{
	pr_err("%s: Unable to set CPU affinity, no SMP support", muen_chip_data.chip.name);
	return IRQ_SET_MASK_OK_DONE;
}

/**
 * muen_ipi_send_mask - Send an IPI to CPUs in mask. Currently a no-op.
 */
static void muen_ipi_send_mask(struct irq_data *d, const struct cpumask *mask)
{
	if (likely(nr_cpu_ids == 1))
		return;

	pr_err("%s: Unable to send IPI, no SMP support", muen_chip_data.chip.name);
}

/**
 * Configuration objects
 */
static const struct irq_chip muen_irq_chip = {
	.name             = "muen-irqchip",
	.irq_mask         = muen_mask,
	.irq_unmask       = muen_unmask,
	.irq_ack          = muen_ack,
	.irq_eoi          = muen_eoi,
	.irq_set_affinity = muen_set_affinity,
	.ipi_send_mask    = muen_ipi_send_mask,
	.flags            = IRQCHIP_SKIP_SET_WAKE,
};

static const struct irq_domain_ops muen_irq_domain_ops = {
	.map   = muen_irq_domain_map,
	.unmap = muen_irq_domain_unmap,
	.xlate = muen_irq_domain_xlate,
};

/**
 * muen_smp_init - Initializes SMP/IPI subsystem as these IRQs are e.g.
 * enumerated by /proc/interrupts and would cause NULL-pointer dereferences
 * otherwise.
 */
static __init void muen_smp_init(void)
{
	int i, virq, base_sgi;

	for (i = 0; i < NUMBER_OF_SGI_INTERRUPTS; i++) {
		virq = irq_create_mapping(muen_chip_data.domain, i);
		if (i == 0)
			base_sgi = virq;
	}

	if (WARN_ON(base_sgi <= 0))
		return;

	set_smp_ipi_range(base_sgi, NUMBER_OF_SGI_INTERRUPTS);
}

/**
 * muen_component_address - Reads the start address of the
 * irq controller address from the device tree.
 *
 * @node           :	the device tree node of the irq controller
 * @resource_index :	the node index of the base address
 *
 * Returns the start address for success or an error otherwise.
 */
unsigned long muen_component_address(struct device_node *node, int resource_index)
{
	struct resource address_res;

	if (of_address_to_resource(node, resource_index, &address_res) != 0) {
		pr_err("%s: Could not read physical address", muen_chip_data.chip.name);
		return -1;
	}

	return address_res.start;
}

/**
 * muen_chip_init - Called by the Linux kernel init process. The
 * Muen SK irq chip driver currently only supports a static
 * configuration with group 0 enabled, group 1 disabled,
 * default priority and binary point (c.f. description).
 *
 * @node   :	the device tree node of the irq controller
 * @parent :	the parent device tree node of the irq controller
 *
 * Returns 0 for success or an error otherwise.
 */
static int __init muen_chip_init(struct device_node *node, struct device_node *parent)
{
	muen_chip_data.chip = muen_irq_chip;
	muen_chip_data.initialized = false;

	if (WARN_ON(!node))
		return -ENODEV;

	muen_chip_data.physical_address = muen_component_address(node, 0);
	muen_chip_data.raw_address      = of_iomap(node, 0);
	if (!muen_chip_data.raw_address) {
		pr_err("%s: Could not map irq controller registers\n",
		       muen_chip_data.chip.name);
		return -ENOMEM;
	}
	muen_chip_data.domain           = irq_domain_create_linear(
		&node->fwnode, NUMBER_OF_INTERRUPTS, &muen_irq_domain_ops, &muen_chip_data
	);

	/* Update nr_irqs according to our config as the default is only 64 and any
	 * IRQs higher would first get mapped to a value below that.
	 */
	nr_irqs = NUMBER_OF_INTERRUPTS;

	pr_info("%s: Init irq chip device (%s, addr: %#lx, nr_irqs: %u)",
		muen_chip_data.chip.name, node->full_name,
		muen_chip_data.physical_address, nr_irqs);

	muen_smp_init();

	set_handle_irq(muen_handle_irq);

	irq_set_default_host(muen_chip_data.domain);

	writel_relaxed(IRQ_DEFAULT_CONTROL, muen_chip_data.raw_address + IRQ_CONTROL_OFFSET);
	writel_relaxed(IRQ_DEFAULT_PRIORITY, muen_chip_data.raw_address + IRQ_PRIORITY_MASK_OFFSET);
	writel_relaxed(IRQ_DEFAULT_BINARY_POINT, muen_chip_data.raw_address + IRQ_BINARY_POINT_OFFSET);

	muen_chip_data.initialized = true;

	return muen_chip_data.initialized ? 0 : -1;
}
IRQCHIP_DECLARE(muen_irqchip, "muen,irqchip-v1.0", muen_chip_init);

/** end of irq-muen.c */
