/*************************************************************************
*                                                                        *
*  Copyright (C) codelabs gmbh, Switzerland - all rights reserved        *
*                <https://www.codelabs.ch/>, <contact@codelabs.ch>       *
*                                                                        *
*  This program is free software: you can redistribute it and/or modify  *
*  it under the terms of the GNU General Public License as published by  *
*  the Free Software Foundation, either version 3 of the License, or     *
*  (at your option) any later version.                                   *
*                                                                        *
*  This program is distributed in the hope that it will be useful,       *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*  GNU General Public License for more details.                          *
*                                                                        *
*  You should have received a copy of the GNU General Public License     *
*  along with this program.  If not, see <http://www.gnu.org/licenses/>. *
*                                                                        *
*                                                                        *
*    @contributors                                                       *
*        2019, 2022  David Loosli <dave@codelabs.ch>                     *
*                                                                        *
*                                                                        *
*    @description                                                        *
*        irq-muensk contains the Muen SK irq chip implementation that    *
*        is required by all Linux subjects of a Muen SK system. This     *
*        driver acts as counterpart to the GIC virtualization provided   *
*        by the Muen SK. The driver currently only supports a static     *
*        configuration of the virtual CPU interface with only group 0    *
*        interrupts, separate priority drop and deactivation (i.e. EOI   *
*        mode equ. 1) and default priority and binary point values. As   *
*        the official ARM GICv2 implementation and its derivatives, the  *
*        Muen SK acknowledges and drops the priority in the exception    *
*        irq entry function to be able to let the Linux kernel handle    *
*        all interrupts as edge-triggerd, even though most hardware      *
*        interrupts are defined as level-sensitive by the actual ARM     *
*        and SoC specification. This also allows the driver to let the   *
*        ack, mask and unmask function implementations empty.            *
*    @project                                                            *
*        MuenOnARM                                                       *
*    @interface                                                          *
*        Subjects                                                        *
*    @target                                                             *
*        Linux mainline kernel 5.2                                       *
*    @reference                                                          *
*        Linux Documentation                                             *
*                                                                        *
*************************************************************************/

#include <linux/ioport.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>

#include "irq-muensk.h"

/*
 * Definitions
 */
static const struct irq_chip muensk_chip = {
	.name           = "Muen SK - (virtual) IRQ Chip, version 0.9",
	.irq_mask       = muensk_mask,
	.irq_unmask     = muensk_unmask,
	.irq_ack        = muensk_ack,
	.irq_eoi        = muensk_eoi,
	.flags          = IRQCHIP_SKIP_SET_WAKE,
};

static const struct irq_domain_ops muensk_irq_domain_ops = {
	.map   = muensk_irq_domain_map,
	.unmap = muensk_irq_domain_unmap,
	.xlate = muensk_irq_domain_xlate,
};

struct muensk_irq_data {
	struct irq_chip chip;
	unsigned long physical_address;
	void __iomem * raw_address;
	struct irq_domain * domain;
	bool initialized;
};

static struct muensk_irq_data muensk_data;

/**
 * muensk_irq_domain_map - Maps an interrupt based on its hardware
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
static int muensk_irq_domain_map(
	struct irq_domain *d, unsigned int irq, irq_hw_number_t hw
)
{
	printk(KERN_DEBUG "Muen SK IRQ Chip - domain map the IRQ No: %ld", hw);
	if (is_sgi_interrupt(hw) || is_ppi_interrupt(hw))
	{
		irq_domain_set_info(d, irq, hw, &(muensk_data.chip), d->host_data,
			handle_percpu_devid_irq, NULL, NULL);
		return irq_set_percpu_devid(irq);
	}
	else
	{
		irq_domain_set_info(d, irq, hw, &(muensk_data.chip), d->host_data,
			handle_fasteoi_irq, NULL, NULL);
		irq_set_noprobe(irq);
		return 0;
	}
}

/**
 * muensk_irq_domain_unmap - Unmaps an interrupt based on the
 * irq domain reset function. This function is part of the irq
 * domain ops specification.
 *
 * @d  :	the interrupt domain
 * @irq:	the software interrupt number
 */
static void muensk_irq_domain_unmap(struct irq_domain *d, unsigned int irq)
{
	printk(KERN_DEBUG "Muen SK IRQ Chip - domain unmap the IRQ No: %d", irq);
	irq_domain_reset_irq_data(irq_get_irq_data(irq));
}

/**
 * muensk_irq_domain_xlate - Translates the interrupt properties
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
static int muensk_irq_domain_xlate(
	struct irq_domain *d, struct device_node *ctrlr,
	const u32 *intspec, unsigned int intsize,
	unsigned long *out_hwirq, unsigned int *out_type
)
{
	if (WARN_ON(intsize != 3))
	{
		return -EINVAL;
	}

	printk(KERN_DEBUG "Muen SK IRQ Chip - domain xlate with IRQ specification: %d / %d / %d",
	          intspec[0], intspec[1], intspec[2]);

	if (intspec[0] == SGI_INTERRUPT_TYPE)
	{
		*out_hwirq = intspec[1];
		*out_type  = intspec[2] & IRQ_TYPE_SENSE_MASK;
	}

	if (intspec[0] == PPI_INTERRUPT_TYPE)
	{
		*out_hwirq = intspec[1] + 16;
		*out_type  = intspec[2] & IRQ_TYPE_SENSE_MASK;
	}

	if (intspec[0] == SPI_INTERRUPT_TYPE)
	{
		*out_hwirq = intspec[1] + 32;
		*out_type  = intspec[2] & IRQ_TYPE_SENSE_MASK;
	}

	WARN_ON(*out_type == IRQ_TYPE_NONE);
	return 0;
}

/**
 * muensk_mask - Masking should disable the signaling of an
 * interrupt to the core, but is neither required for this
 * approach (c.f. description) nor (yet) supported. For a
 * Muen SK system unmasking has to be provided by the
 * hypervisor and therefore would have to make use of an
 * HVC call and parameter passing via registers. This
 * function is part of the irq chip ops specification.
 *
 * @data :	the interrupt data
 */
void muensk_mask(struct irq_data *data)
{
	printk(KERN_DEBUG "Muen SK IRQ Chip - mask called with IRQ No: %ld", data->hwirq);
}

/**
 * muensk_unmask - Unmasking should enable the signaling of
 * an interrupt to the core, but is neither required for
 * this approach (c.f. description) nor (yet) supported.
 * For a Muen SK system unmasking has to be provided by
 * the hypervisor and therefore would have to make use of
 * an HVC call and parameter passing via registers. This
 * function is part of the irq chip ops specification.
 *
 * @data :	the interrupt data
 */
void muensk_unmask(struct irq_data *data)
{
	printk(KERN_DEBUG "Muen SK IRQ Chip - unmask called with IRQ No: %ld", data->hwirq);
}

/**
 * muensk_ack - Acknowledging should mark an interrupt to
 * be actively handled, but is not required for this
 * approach (c.f. description). This function is part of
 * the irq chip ops specification.
 *
 * @data :	the interrupt data
 */
void muensk_ack(struct irq_data *data) { }

/**
 * muensk_ack - Signals the end of the interrupt handling.
 * For the currently used approach, this is done by writing
 * to the deactivation register (c.f. description). This
 * function is part of the irq chip ops specification.
 *
 * @data :	the interrupt data
 */
void muensk_eoi(struct irq_data *data)
{
	writel_relaxed(data->hwirq, muensk_data.raw_address + IRQ_DEACTIVATE_INTERRUPT_OFFSET);
	isb();
}

/**
 * muensk_handle_irq - Called by the Linux kernel scheduling
 * and exception handling process for every interrupt raised
 * on the core's interface. For the currently used approach,
 * this function acknowledges and drops the priority of the
 * interrupt to "simulate" an edge triggered Linux irq (c.f.
 * description).
 *
 * @regs :	the registers stored at exception entry
 */
static void __exception_irq_entry muensk_handle_irq(struct pt_regs *regs)
{
	u32 irq_status, irq_number;

	do
	{
		irq_status = readl_relaxed(muensk_data.raw_address + IRQ_ACKNOWLEDGE_OFFSET);
		irq_number = irq_status & IRQ_ACKNOWLEDGE_MASK;

		if (irq_number != IRQ_NO_PENDING_GROUP_1_VALUE &&
			irq_number != IRQ_NO_PENDING_GROUP_0_VALUE)
		{
			writel_relaxed(irq_status, muensk_data.raw_address + IRQ_END_OF_INTERRUPT_OFFSET);
			generic_handle_domain_irq(muensk_data.domain, irq_number);
			continue;
		}
		break;
	} while (1);
}

/**
 * muensk_smp_init - Initializes SMP/IPI subsystem as these IRQs are e.g.
 * enumerated by /proc/interrupts and would cause NULL-pointer dereferences
 * otherwise.
 */
static __init void muensk_smp_init(void)
{
	int i, virq, base_sgi;

	for (i = 0; i < NUMBER_OF_SGI_INTERRUPTS; i++) {
		virq = irq_create_mapping(muensk_data.domain, i);
		if (i == 0) {
			base_sgi = virq;
		}
	}

	if (WARN_ON(base_sgi <= 0))
		return;

	set_smp_ipi_range(base_sgi, NUMBER_OF_SGI_INTERRUPTS);
}

/**
 * muensk_component_address - Reads the start address of the
 * irq controller address from the device tree.
 *
 * @node           :	the device tree node of the irq controller
 * @resource_index :	the node index of the base address
 *
 * Returns the start address for success or an error otherwise.
 */
unsigned long muensk_component_address(struct device_node *node, int resource_index)
{
	struct resource address_res;

	if (of_address_to_resource(node, resource_index, &address_res) != 0) {
		printk(KERN_ERR "ERROR %s: could not read physical address", muensk_data.chip.name);
		return -1;
	}

	return address_res.start;
}

/**
 * muensk_init - Called by the Linux kernel init process. The
 * Muen SK irq chip driver currently only supports a static
 * configuration with group 0 enabled, group 1 disabled,
 * default priority and binary point (c.f. description).
 *
 * @node   :	the device tree node of the irq controller
 * @parent :	the parent device tree node of the irq controller
 *
 * Returns 0 for success or an error otherwise.
 */
static int __init muensk_init(struct device_node *node, struct device_node *parent)
{
	muensk_data.chip = muensk_chip;
	muensk_data.initialized = false;

	printk(KERN_INFO "%s", muensk_data.chip.name);

	if (WARN_ON(!node)) {
		return -ENODEV;
	}

	muensk_data.physical_address = muensk_component_address(node, 0);
	muensk_data.raw_address      = of_iomap(node, 0);

	set_handle_irq(muensk_handle_irq);

	muensk_data.domain = irq_domain_create_linear(
		&node->fwnode, NUMBER_OF_INTERRUPTS, &muensk_irq_domain_ops, &muensk_data
	);

	muensk_smp_init();

	irq_set_default_host(muensk_data.domain);

	writel_relaxed(IRQ_DEFAULT_CONTROL, muensk_data.raw_address + IRQ_CONTROL_OFFSET);
	writel_relaxed(IRQ_DEFAULT_PRIORITY, muensk_data.raw_address + IRQ_PRIORITY_MASK_OFFSET);
	writel_relaxed(IRQ_DEFAULT_BINARY_POINT, muensk_data.raw_address + IRQ_BINARY_POINT_OFFSET);

	muensk_data.initialized = true;

	printk(KERN_DEBUG "    DTS Node Name: %s", node->full_name);
	printk(KERN_DEBUG "    Physical Address: %#lx", muensk_data.physical_address);
	printk(KERN_DEBUG "    IRQ Status: %s", (muensk_data.initialized ?
		"initialization successful" : "initialization failed"));

	return muensk_data.initialized ? 0 : -1;
}
IRQCHIP_DECLARE(muensk_v0, "muensk,irq-v0", muensk_init);

/** end of irq-muensk.c */
