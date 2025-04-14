/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IRQ_WORK_H
#define _ASM_IRQ_WORK_H

#include <asm/cpufeature.h>
#include <asm/muen.h>

#ifdef CONFIG_X86_LOCAL_APIC
static inline bool arch_irq_work_has_interrupt(void)
{
#ifndef CONFIG_MUEN_GUEST
	return boot_cpu_has(X86_FEATURE_APIC);
#else
	return muen_has_work_event();
#endif
}
#else
static inline bool arch_irq_work_has_interrupt(void)
{
	return false;
}
#endif

#endif /* _ASM_IRQ_WORK_H */
