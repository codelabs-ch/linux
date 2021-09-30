#ifndef MUEN_PCI_H
#define MUEN_PCI_H

#include <linux/irqdomain.h>

/**
 * Initialize Muen PCI driver.
 */
int __init muen_pci_init(void);

/**
 * Create Muen PCI-MSI IRQ Domain.
 */
struct irq_domain * __init muen_create_pci_msi_domain(void);

#endif /* MUEN_PCI_H */
