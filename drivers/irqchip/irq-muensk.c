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
*        2019, 2021  David Loosli <dave@codelabs.ch>                     *
*                                                                        *
*                                                                        *
*    @description                                                        *
*        irq-muensk Linux subject Muen SK interrupt chip implementation  *
*        as the counterpart of the MuenSK GIC implementation (needed for *
*        all Linux subjects running on the Muen SK)                      *
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
 * definitions
 */
static const struct irq_chip muensk_chip = {
	.name					= "Muen SK - (virtual) IRQ Chip, version 0",
	.irq_mask				= muensk_mask,
	.irq_unmask				= muensk_unmask,
	.irq_ack				= muensk_ack,
	.irq_eoi				= muensk_eoi,
	.flags					= IRQCHIP_SKIP_SET_WAKE,
};

static const struct irq_domain_ops muensk_irq_domain_ops = {
	.map   = muensk_irq_domain_map,
	.unmap = muensk_irq_domain_unmap,
	.xlate = muensk_irq_domain_xlate,
};

struct muensk_irq_data {
	struct irq_chip chip;
	unsigned long distributor_physical_address;
	void __iomem * distributor_raw_address;
	unsigned long cpu_interface_physical_address;
	void __iomem * cpu_interface_raw_address;
	struct irq_domain * domain;
	bool initialized;
};

static struct muensk_irq_data muensk_data;

/*
 * ToDo
 */
static int muensk_irq_domain_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	printk(KERN_INFO "MuenSK IRQ Chip - domain map the IRQ No: %ld", hw);

	if (is_sgi_interrupt(hw) || is_ppi_interrupt(hw)) {
		irq_set_percpu_devid(irq);
		irq_domain_set_info(d, irq, hw, &(muensk_data.chip), d->host_data,
			handle_percpu_devid_irq, NULL, NULL);
		irq_set_status_flags(irq, IRQ_NOAUTOEN);
	} else {
		irq_domain_set_info(d, irq, hw, &(muensk_data.chip), d->host_data,
			handle_fasteoi_irq, NULL, NULL);
		irq_clear_status_flags(irq, IRQ_NOPROBE);
	}
	return 0;
}

static void muensk_irq_domain_unmap(struct irq_domain *d, unsigned int irq)
{
	/*
	 * currently not implmeneted (no ARMv8 GICv2 distributor
	 * virtualization, Muen SK core does this and has full
	 * control over the interrupt assignment to subjects)
	 */
    printk(KERN_INFO "MuenSK IRQ Chip - domain unmap the IRQ No: %d", irq);
}

static int muensk_irq_domain_xlate(struct irq_domain *d, struct device_node *ctrlr,
	const u32 *intspec, unsigned int intsize,
	unsigned long *out_hwirq, unsigned int *out_type)
{
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

	/*
	 * Device Tree Specification Error Check
	 */
	WARN_ON(*out_type == IRQ_TYPE_NONE);

	return 0;
}

/*
 * ToDo
 */
void muensk_mask(struct irq_data *data)
{
	/*
	 * currently not implmeneted (no ARMv8 GICv2 distributor
	 * virtualization, Muen SK core does this and has full
	 * control over the interrupt assignment to subjects)
	 */
    printk(KERN_INFO "MuenSK IRQ Chip - mask called with IRQ No: %ld", data->hwirq);
}

void muensk_unmask(struct irq_data *data)
{
	/*
	 * currently not implmeneted (no ARMv8 GICv2 distributor
	 * virtualization, Muen SK core does this and has full
	 * control over the interrupt assignment to subjects)
	 */
    printk(KERN_INFO "MuenSK IRQ Chip - unmask called with IRQ No: %ld", data->hwirq);
}

void muensk_ack(struct irq_data *data)
{
	/* do nothing due to calling sequence linux vs. ARM GICv2 */
}

void muensk_eoi(struct irq_data *data)
{
	writel_relaxed(data->hwirq, muensk_data.cpu_interface_raw_address + IRQ_ALIASED_END_OF_INTERRUPT_OFFSET);
	isb();
}

/*
 * ToDo
 */
static void __exception_irq_entry muensk_handle_irq(struct pt_regs *regs)
{
	u32 irq_status, irq_number;

	do {
		irq_status = readl_relaxed(muensk_data.cpu_interface_raw_address + IRQ_ALIASED_ACKNOWLEDGE_OFFSET);
		irq_number = irq_status & IRQ_ACKNOWLEDGE_MASK;

		if (irq_number != IRQ_NO_PENDING_GROUP_1_VALUE && irq_number != IRQ_NO_PENDING_GROUP_0_VALUE) {
			handle_domain_irq(muensk_data.domain, irq_number, regs);
			continue;
		}
		break;
	} while (1);
}

/*
 * ToDo
 */
unsigned long muensk_component_address(struct device_node *node, int resource_index)
{
	struct resource address_res;

	if (of_address_to_resource(node, resource_index, &address_res) != 0) {
		printk(KERN_INFO "ERROR %s: could not read physical address", muensk_data.chip.name);
		return -1;
	}

	return address_res.start;
}

bool muensk_write_read_assert(u32 register_value, volatile void __iomem *address)
{
	u32 register_temp;

	writel_relaxed(register_value, address);
	isb();
	register_temp = readl_relaxed(address);

	return register_temp & register_value;
}

static int __init muensk_init(struct device_node *node, struct device_node *parent)
{
	muensk_data.chip = muensk_chip;
	muensk_data.initialized = false;

	printk(KERN_INFO "%s", muensk_data.chip.name);

	if (WARN_ON(!node)) {
		return -ENODEV;
	}

	muensk_data.distributor_physical_address = muensk_component_address(node, 0);
	muensk_data.cpu_interface_physical_address = muensk_component_address(node, 1);

	muensk_data.distributor_raw_address = of_iomap(node, 0);
	muensk_data.cpu_interface_raw_address = of_iomap(node, 1);

	set_handle_irq(muensk_handle_irq);

	muensk_data.domain = irq_domain_create_linear(&node->fwnode, NUMBER_OF_INTERRUPTS, &muensk_irq_domain_ops, &muensk_data);

	muensk_data.initialized = true;

	printk(KERN_INFO "    DTS Node Name: %s", node->full_name);
	printk(KERN_INFO "    Distributor Physical Address: %#lx", muensk_data.distributor_physical_address);
	printk(KERN_INFO "    CPU Interface Physical Address: %#lx", muensk_data.cpu_interface_physical_address);
	printk(KERN_INFO "    IRQ Status: %s", (muensk_data.initialized ? "initialization successful" : "initialization failed"));

	return muensk_data.initialized ? 0 : -1;
}
IRQCHIP_DECLARE(muensk_v0, "muensk,irq-v0", muensk_init);

/** end of irq-muensk.c */
