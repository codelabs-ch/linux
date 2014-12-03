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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
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
#include <muen/sinfo.h>

#ifdef CONFIG_CLKSRC_MUEN_TIMER

static void __iomem *base;

static int __init clocksource_muen_timer_init(void)
{
	struct muen_channel_info channel;

	if (!muen_get_channel_info("virtual_time", &channel)) {
		pr_warn("Unable to retrieve Muen time channel\n");
		return -1;
	}
	pr_info("Using Muen time channel at address 0x%llx\n", channel.address);

	base = ioremap_cache(channel.address, 4);
	if (base) {
		return clocksource_mmio_init(base, "muen-timer",
			1000, 366, 32, clocksource_mmio_readl_up);
	} else {
		pr_warn("Failed to remap muen-timer memory\n");
		return -1;
	}
}

module_init(clocksource_muen_timer_init)

#endif

#ifdef CONFIG_CLKEVT_MUEN_NOOP

struct subject_timer_type {
	uint64_t value;
	uint8_t vector;
} __packed;

static struct subject_timer_type * timer_page;

static void muen_timer_set_mode(const enum clock_event_mode mode,
				struct clock_event_device *const evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* unsupported */
		WARN_ON(1);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		/* Cancel timer by setting timer to maximum value */
		timer_page->value = ULLONG_MAX;
		break;

	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int muen_timer_next_event(const unsigned long delta,
				 struct clock_event_device *const evt)
{
	timer_page->value  = 0;
	timer_page->vector = IRQ0_VECTOR;
	return 0;
}

static struct clock_event_device muen_timer_clockevent = {
	.name		= "muen-timer",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= muen_timer_set_mode,
	.set_next_event = muen_timer_next_event,
	.rating		= 25,
};

static int __init clockevent_muen_timer_init(void)
{
	struct muen_memregion_info region;

	if (!muen_get_memregion_info("timer", &region)) {
		pr_warn("Unable to retrieve Muen time memory region\n");
		return -1;
	}
	pr_info("Using Muen time memory region at address 0x%llx\n", region.address);

	timer_page = (struct subject_timer_type *) ioremap_cache(region.address,
			region.size);

	printk(KERN_INFO "Registering clockevent device muen-timer\n");
	muen_timer_clockevent.cpumask = cpu_online_mask;
	clockevents_config_and_register(&muen_timer_clockevent, 1000, 1, 9999);
	global_clock_event = &muen_timer_clockevent;
	return 0;
}

arch_initcall(clockevent_muen_timer_init);

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
