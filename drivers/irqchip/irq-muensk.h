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

#ifndef _IRQ_MUENSK_H
#define _IRQ_MUENSK_H

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/types.h>

#include <asm/exception.h>

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
#define IRQ_DEACTIVATE_INTERRUPT_OFFSET		     0x1000

#define IRQ_ACKNOWLEDGE_MASK                   0x03FF

#define IRQ_NO_PENDING_GROUP_1_VALUE		1022
#define IRQ_NO_PENDING_GROUP_0_VALUE		1023

#define IRQ_DEFAULT_CONTROL         0x00000201
#define IRQ_DEFAULT_PRIORITY        0x000000f8
#define IRQ_DEFAULT_BINARY_POINT    0x00000002

/*
 * inline functions
 */
static inline bool is_sgi_interrupt (unsigned long hardware_irq)
{
	return 0 <= hardware_irq && hardware_irq <= 15;
}

static inline bool is_ppi_interrupt (unsigned long hardware_irq)
{
	return 16 <= hardware_irq && hardware_irq <= 31;
}

static inline bool is_spi_interrupt (unsigned long hardware_irq)
{
	return 32 <= hardware_irq && hardware_irq <= 1119;
}

/*
 * function prototypes
 */
static int muensk_irq_domain_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw);
static void muensk_irq_domain_unmap(struct irq_domain *d, unsigned int irq);
static int muensk_irq_domain_xlate(
	struct irq_domain *d, struct device_node *ctrlr,
	const u32 *intspec, unsigned int intsize,
	unsigned long *out_hwirq, unsigned int *out_type
);
static int muensk_set_affinity(struct irq_data *d,
			       const struct cpumask *mask_val, bool force);
static void muensk_ipi_send_mask(struct irq_data *d, const struct cpumask *mask);

void muensk_mask(struct irq_data *data);
void muensk_unmask(struct irq_data *data);
void muensk_ack(struct irq_data *data);
void muensk_eoi(struct irq_data *data);

static void __exception_irq_entry muensk_handle_irq(struct pt_regs *regs);

static int __init muensk_init(struct device_node *node, struct device_node *parent);

#endif /* _IRQ_MUENSK_H */
