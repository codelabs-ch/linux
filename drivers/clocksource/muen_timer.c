/*
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <asm/time.h>
#include <asm/i8259.h>

#include <linux/io.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>

#ifdef CONFIG_CLKSRC_MUEN_TIMER

static void *base;

static int __init clocksource_muen_timer_init(void)
{
	base = ioremap_cache(0x00002000, 4);
	if (base) {
		printk(KERN_INFO "Registering clocksource muen-timer\n");
		return clocksource_mmio_init(base, "muen-timer",
			1000, 366, 32, clocksource_mmio_readl_up);
	} else {
		printk(KERN_WARNING "Failed to map muen-timer\n");
		return -1;
	}
}

module_init(clocksource_muen_timer_init)

#endif

#ifdef CONFIG_CLKEVT_MUEN_NOOP

static void muen_timer_set_mode(const enum clock_event_mode mode,
				struct clock_event_device *const evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		printk(KERN_DEBUG "Muen timer event mode: CLOCK_EVT_MODE_PERIODIC\n");
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
		printk(KERN_DEBUG "Muen timer event mode: CLOCK_EVT_MODE_SHUTDOWN\n");
		break;

	case CLOCK_EVT_MODE_UNUSED:
		printk(KERN_DEBUG "Muen timer event mode: CLOCK_EVT_MODE_UNUSED\n");
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		printk(KERN_DEBUG "Muen timer event mode: CLOCK_EVT_MODE_ONESHOT\n");
		break;

	case CLOCK_EVT_MODE_RESUME:
		printk(KERN_DEBUG "Muen timer event mode: CLOCK_EVT_MODE_RESUME\n");
		break;

	default:
		printk(KERN_DEBUG "Muen timer event mode: <unknown> %d\n", mode);
		break;
	}
}

static int muen_timer_next_event(const unsigned long delta,
				 struct clock_event_device *const evt)
{
	return 0;
}

struct clock_event_device muen_timer_clockevent = {
	.name		= "muen-timer",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_MODE_ONESHOT,
	.set_mode	= muen_timer_set_mode,
	.set_next_event = muen_timer_next_event,
	.rating		= 25,
};

static int __init clockevent_muen_timer_init(void)
{
	printk(KERN_INFO "Registering clockevent device muen-timer\n");
	muen_timer_clockevent.cpumask = cpu_online_mask;
	clockevents_config_and_register(&muen_timer_clockevent, 1000, 1, 9999);
	global_clock_event = &muen_timer_clockevent;
	return 0;
}

module_init(clockevent_muen_timer_init)

#endif

static void noop_irq_data(struct irq_data *const data) {}

static int __init noop_pic_init(void)
{
	printk(KERN_INFO "Registering no-op PIC functions\n");
	i8259A_chip.irq_mask		= noop_irq_data;
	i8259A_chip.irq_disable		= noop_irq_data;
	i8259A_chip.irq_unmask		= noop_irq_data;
	i8259A_chip.irq_mask_ack	= noop_irq_data;
	return 0;
}

module_init(noop_pic_init)
