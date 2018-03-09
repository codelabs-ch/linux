#ifndef MUEN_SMP_H
#define MUEN_SMP_H

/**
 * Initialize SMP on Muen SK.
 */
void __init muen_smp_init(void);

/*
 * Return IRQ affinity information for PCI device with given SID.
 *
 * The function returns null if no IRQ affinity information for the specified
 * device exists. If IRQ affinity information is found, the cpu argument
 * specifies the CPU which is responsible for the IRQ(s) of the device.
 */
const struct muen_device_type *const
muen_smp_get_irq_affinity(const uint16_t sid, unsigned int *cpu);

#endif /* MUEN_SMP_H */
