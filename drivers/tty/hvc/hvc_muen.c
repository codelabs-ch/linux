// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2013, 2015  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2013, 2015  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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

#include <linux/module.h>
#include <linux/kvm_para.h>
#include <linux/random.h>
#include <linux/io.h>
#include <muen/writer.h>
#include <muen/smp.h>

#include "hvc_console.h"

#define HVC_MUEN_COOKIE	0x4d75656e	/* "Muen" in hex */
#define CHANNEL_NAME	"virtual_console"

static struct hvc_struct *hvc_muen_dev;

static uint8_t event_number;
static uint64_t channel_size;
static struct muchannel *channel_out;

static int hvc_muen_put(uint32_t vtermno, const char *buf, int count)
{
	int i;

	for (i = 0; i < count; i++)
		muen_channel_write(channel_out, &buf[i]);

	kvm_hypercall0(event_number);

	return count;
}

static int hvc_muen_get(uint32_t vtermno, char *buf, int count)
{
	return 0;
}

static const struct hv_ops hvc_muen_ops = {
	.get_chars = hvc_muen_get,
	.put_chars = hvc_muen_put,
};

static int __init hvc_muen_init(void)
{
	struct hvc_struct *hp;

	BUG_ON(hvc_muen_dev);

	hp = hvc_alloc(HVC_MUEN_COOKIE, 0, &hvc_muen_ops, channel_size);
	if (IS_ERR(hp))
		return PTR_ERR(hp);

	hvc_muen_dev = hp;

	return 0;
}

module_init(hvc_muen_init);

static void __exit hvc_muen_exit(void)
{
	if (hvc_muen_dev)
		hvc_remove(hvc_muen_dev);

	muen_channel_deactivate(channel_out);
	iounmap(channel_out);
}

module_exit(hvc_muen_exit);

static int __init hvc_muen_console_init(void)
{
	int rc;
	struct muen_cpu_affinity evt;
	const u64 epoch = muen_get_sched_start();
	const struct muen_resource_type *const
		region = muen_get_resource(CHANNEL_NAME, MUEN_RES_MEMORY);

	if (!region) {
		pr_err("hvc_muen: Unable to retrieve console channel %s\n",
		       CHANNEL_NAME);
		return -EINVAL;
	}

	if (!muen_smp_one_match(&evt, CHANNEL_NAME, MUEN_RES_EVENT)) {
		pr_err("hvc_muen: Unable to retrieve event data for console channel %s\n",
		       CHANNEL_NAME);
		return -EINVAL;
	}

	event_number = evt.res.data.number;
	channel_size = region->data.mem.size;
	pr_info("hvc_muen: Channel @ 0x%llx, size 0x%llx, event %u, cpu %u, epoch 0x%llx\n",
		region->data.mem.address, region->data.mem.size,
		evt.res.data.number, evt.cpu, epoch);

	/*
	 * Pin to event CPU. Required because hvc_muen_put() runs with IRQs
	 * disabled, so it is not possible to use smp_call_function_single() or
	 * smp_call_on_cpu() to trigger an event on a remote CPU.
	 */
	rc = set_cpus_allowed_ptr(current, cpumask_of(evt.cpu));
	BUG_ON(rc || smp_processor_id() != evt.cpu);

	channel_out = (struct muchannel *)
		ioremap_cache(region->data.mem.address,
			      region->data.mem.size);

	muen_channel_init_writer(channel_out, 1, 1, channel_size,
				 epoch);
	hvc_instantiate(HVC_MUEN_COOKIE, 0, &hvc_muen_ops);

	return 0;
}

/*
 * Use early_initcall instead of console_initcall here to ensure hvc_muen is
 * initialized only after the SMP affinity db is filled.
 */
early_initcall(hvc_muen_console_init);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen hypervisor console driver");
MODULE_LICENSE("GPL");
