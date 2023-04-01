// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2018  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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

/*
 * Setup timer page for given CPU
 *
 * This function uses ioremap_cache so it cannot be called from atomic
 * context, therefore it must be called by the BSP only.
 */
void muen_setup_timer_page(unsigned int cpu);

/* Setup timer event for calling CPU */
void muen_setup_timer_event(void);

/* Register clockevents for calling CPU */
void muen_register_clockevent_dev(void);
