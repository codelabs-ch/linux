/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2018  Adrian-Ken Rueegsegger <ken@codelabs.ch>
 * Copyright (C) 2026  David Loosli <david@codelabs.ch>
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
 */

#ifndef MUEN_TIMER_H
#define MUEN_TIMER_H

#include <linux/clockchips.h>
#include <linux/irqreturn.h>

/*
 * Setup timer page for given CPU
 *
 * This function uses memremap so it cannot be called from atomic
 * context, therefore it must be called by the BSP only.
 */
void muen_setup_timer_page(unsigned int cpu);

/* Setup timer event for calling CPU */
void muen_setup_timer_event(void);

/* Register clockevents for calling CPU */
void muen_register_clockevent_dev(void);

int muen_arch_register_local_timer_interrupt(
	struct clock_event_device *evt, uint8_t evt_nr);
irqreturn_t muen_handle_local_timer_interrupt(void);

#endif
