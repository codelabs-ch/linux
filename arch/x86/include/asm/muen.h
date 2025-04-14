/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MUEN_H
#define _ASM_X86_MUEN_H

extern int irq_work_evt;

static inline bool muen_has_work_event(void)
{
	return irq_work_evt > -1;
}

#endif /* _ASM_X86_MUEN_H */

