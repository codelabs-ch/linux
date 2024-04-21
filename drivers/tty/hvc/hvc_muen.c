// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2013-2024  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2013-2024  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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

#include <linux/err.h>
#include <linux/gfp_types.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kvm_para.h>
#include <linux/random.h>
#include <linux/io.h>
#include <muen/sinfo.h>
#include <muen/reader.h>
#include <muen/writer.h>
#include <muen/smp.h>

#include "hvc_console.h"

#define HVC_MUEN_COOKIE 0x4d75656e /* "Muen" in hex */
#define HVC_MUEN_PROTOCOL 1
#define HVC_MUEN_MAX_COUNT HVC_ALLOC_TTY_ADAPTERS

struct muencons_info {
	struct list_head list;
	struct hvc_struct *hvc;
	struct muchannel *channel_out;
	struct muchannel *channel_in;
	struct muchannel_reader reader;
	uint64_t channel_size;
	int vtermno;
	int event;
	int vector;
};

/* CPU to which Muen HVC driver is pinned to */
static int hvc_muen_cpu = -1;
/* Shared memory channel epoch */
static uint64_t hvc_muen_epoch;

/* Count of Muen HVC input channels */
static int hvc_muen_in_count;
/* Count of Muen HVC output channels */
static int hvc_muen_out_count;

/* Input memory regions  */
static char *in[HVC_MUEN_MAX_COUNT];
/* Output memory regions  */
static char *out[HVC_MUEN_MAX_COUNT];

module_param_array(in, charp, &hvc_muen_in_count, 0444);
MODULE_PARM_DESC(in, "Input channels, comma-separated (empty values allowed)");
module_param_array(out, charp, &hvc_muen_out_count, 0444);
MODULE_PARM_DESC(out, "Output channels (min. 1), comma-separated (empty values allowed)");

static LIST_HEAD(muencons);
static DEFINE_SPINLOCK(muencons_lock);

static struct muencons_info *vtermno_to_cons(int vtermno)
{
	struct muencons_info *entry, *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&muencons_lock, flags);
	if (list_empty(&muencons)) {
		spin_unlock_irqrestore(&muencons_lock, flags);
		return NULL;
	}

	list_for_each_entry(entry, &muencons, list) {
		if (entry->vtermno == vtermno) {
			ret = entry;
			break;
		}
	}
	spin_unlock_irqrestore(&muencons_lock, flags);

	return ret;
}

static int hvc_muen_put(uint32_t vtermno, const char *data, int count)
{
	int i;
	struct muencons_info *cons = vtermno_to_cons(vtermno);

	if (cons == NULL || !cons->channel_out)
		return -EINVAL;

	for (i = 0; i < count; i++)
		muen_channel_write(cons->channel_out, &data[i]);

	if (cons->event >= 0)
		kvm_hypercall0(cons->event);

	return count;
}

static int hvc_muen_get(uint32_t vtermno, char *buf, int count)
{
	struct muencons_info *cons = vtermno_to_cons(vtermno);
	enum muchannel_reader_result res;
	bool pending_data = true;
	char data;
	int index, i = 0;

	if (cons == NULL || !cons->channel_in)
		return -EINVAL;

	index = vtermno - HVC_MUEN_COOKIE;

	while (pending_data && i < count) {
		res = muen_channel_read(cons->channel_in, &cons->reader, &data);
		switch (res) {
		case MUCHANNEL_SUCCESS:
			buf[i] = data;
			i++;
			break;
		case MUCHANNEL_EPOCH_CHANGED:
			pr_debug("hvc_muen[%d]: Channel epoch changed\n",
				 index);
			/* retry */
			break;
		case MUCHANNEL_OVERRUN_DETECTED:
			pr_warn("hvc_muen[%d]: Channel overrun\n", index);
			muen_channel_drain(cons->channel_in, &cons->reader);
			/* retry */
			break;
		case MUCHANNEL_INCOMPATIBLE_INTERFACE:
			pr_err("hvc_muen[%d]: Incompatible channel interface\n",
			       index);
			fallthrough;
		case MUCHANNEL_NO_DATA:
		case MUCHANNEL_INACTIVE:
		default:
			pending_data = false;
			break;
		}
	}

	return i;
}

static const struct hv_ops hvc_muen_ops = {
	.get_chars = hvc_muen_get,
	.put_chars = hvc_muen_put,
	.notifier_add = notifier_add_irq,
	.notifier_del = notifier_del_irq,
	.notifier_hangup = notifier_hangup_irq,
};

static void hvc_muen_set_cpu(int cpu)
{
	int rc;
	/*
	 * Pin to event CPU. Required because hvc_muen_put() runs with IRQs
	 * disabled, so it is not possible to use smp_call_function_single() or
	 * smp_call_on_cpu() to trigger an event on a remote CPU.
	 */
	rc = set_cpus_allowed_ptr(current, cpumask_of(cpu));
	BUG_ON(rc || smp_processor_id() != cpu);
	hvc_muen_cpu = cpu;
}

static struct muencons_info *__init muencons_init(int vtermno, int event,
						  int vector, uint64_t size,
						  struct muchannel *out,
						  struct muchannel *in)
{
	struct muencons_info *info = vtermno_to_cons(vtermno);

	if (!info) {
		info = kzalloc(sizeof(struct muencons_info), GFP_KERNEL);
		if (!info)
			return ERR_PTR(-ENOMEM);
	}
	info->channel_size = size;
	info->channel_out = out;
	info->channel_in = in;
	info->vtermno = vtermno;
	info->event = event;
	info->vector = vector;

	return info;
}

/*
 * Initialize HVC Muen console with given index and epoch. On success, the
 * struct muencons is placed into the global muencons list.
 */
static int __init hvc_muen_init_console(int index, uint64_t epoch)
{
	int rc, evtno = -1, vecno = 0;
	unsigned long flags;
	struct muencons_info *info;
	struct muchannel *output;
	struct muchannel *input = NULL;
	struct muen_cpu_affinity evt, vec;
	const struct muen_resource_type *outres, *inres = NULL;

	if (index >= hvc_muen_out_count || !out[index])
		return -EINVAL;

	if (vtermno_to_cons(HVC_MUEN_COOKIE + index) != NULL) {
		pr_debug("hvc_muen[%d]: Console already initialized\n", index);
		return 0;
	}

	outres = muen_get_resource(out[index], MUEN_RES_MEMORY);
	if (!outres) {
		pr_err("hvc_muen[%d]: No output channel %s\n", index,
		       out[index]);
		return -EINVAL;
	}

	if (muen_smp_one_match(&evt, out[index], MUEN_RES_EVENT)) {
		if (evt.cpu != hvc_muen_cpu) {
			if (hvc_muen_cpu != -1) {
				pr_err("hvc_muen[%d]: Output event affinity mismatch %d != %d\n",
				       index, evt.cpu, hvc_muen_cpu);
				return -EINVAL;
			}
			hvc_muen_set_cpu(evt.cpu);
		}
		evtno = evt.res.data.number;
	} else
		pr_debug("hvc_muen[%d]: No event for output channel %s\n",
			 index, out[index]);

	output = (struct muchannel *)ioremap_cache(outres->data.mem.address,
						   outres->data.mem.size);

	pr_info("hvc_muen[%d]: Out channel %s @ 0x%llx, size 0x%llx, event %d\n",
		index, out[index], outres->data.mem.address,
		outres->data.mem.size, evtno);

	if (index >= hvc_muen_in_count || !in[index]) {
		pr_info("hvc_muen[%d]: No input channel\n", index);
	} else {
		inres = muen_get_resource(in[index], MUEN_RES_MEMORY);
		if (inres) {
			if (muen_smp_one_match(&vec, in[index],
					       MUEN_RES_VECTOR)) {
				if (vec.cpu != hvc_muen_cpu) {
					if (hvc_muen_cpu != -1)
						pr_info("hvc_muen[%d]: Input vector affinity mismatch %d != %d\n",
							index, evt.cpu,
							hvc_muen_cpu);
					else
						hvc_muen_set_cpu(vec.cpu);
				} else if (vec.res.data.number >=
					   ISA_IRQ_VECTOR(0))
					vecno = vec.res.data.number -
						ISA_IRQ_VECTOR(0);
				else
					pr_warn("hvc_muen[%d]: Input vector %d invalid\n",
						index, vec.res.data.number);
			} else
				pr_debug(
					"hvc_muen[%d]: No vector data for input channel %s\n",
					index, in[index]);
		} else
			pr_info("hvc_muen[%d]: No input channel %s\n", index,
				in[index]);

		if (inres) {
			input = (struct muchannel *)ioremap_cache(
				inres->data.mem.address, inres->data.mem.size);
			pr_info("hvc_muen[%d]: In channel %s @ 0x%llx, size 0x%llx, vector %d\n",
				index, in[index], inres->data.mem.address,
				inres->data.mem.size, vecno);
		}
	}

	info = muencons_init(HVC_MUEN_COOKIE + index, evtno, vecno,
			     outres->data.mem.size, output, input);
	if (IS_ERR(info)) {
		rc = PTR_ERR(info);
		pr_err("hvc_muen[%d]: Error initializing console %d\n", index,
		       rc);
		goto error;
	}

	muen_channel_init_writer(info->channel_out, HVC_MUEN_PROTOCOL, 1,
				 info->channel_size, epoch);

	if (info->channel_in)
		muen_channel_init_reader(&info->reader, HVC_MUEN_PROTOCOL);

	spin_lock_irqsave(&muencons_lock, flags);
	list_add_tail(&info->list, &muencons);
	spin_unlock_irqrestore(&muencons_lock, flags);

	return 0;

error:
	if (input)
		iounmap(input);
	iounmap(output);
	return rc;
}

static int __init hvc_muen_alloc_console(int index)
{
	int rc;
	struct muencons_info *info;

	if (index >= hvc_muen_out_count || !out[index])
		return -EINVAL;

	info = vtermno_to_cons(HVC_MUEN_COOKIE + index);
	if (!info)
		return -ENODEV;

	info->hvc =
		hvc_alloc(HVC_MUEN_COOKIE + index, info->vector, &hvc_muen_ops,
			  info->channel_size - sizeof(struct muchannel_header));
	if (IS_ERR(info->hvc)) {
		rc = PTR_ERR(info->hvc);
		pr_err("hvc_muen[%d]: Error allocating HVC %d\n", index, rc);
		return -EINVAL;
	}

	return 0;
}

static int __init hvc_muen_init(void)
{
	int i, rc;
	int count = min(hvc_muen_out_count, HVC_MUEN_MAX_COUNT);

	rc = -EINVAL;
	for (i = 0; i < count; i++) {
		rc = hvc_muen_init_console(i, hvc_muen_epoch);
		if (rc) {
			pr_err("hvc_muen[%d]: Initializing HVC terminal failed (%d)\n",
			       i, rc);
			return rc;
		}
		rc = hvc_muen_alloc_console(i);
		if (rc) {
			pr_err("hvc_muen[%d]: Allocating HVC terminal failed (%d)\n",
			       i, rc);
			return rc;
		}
	}
	pr_debug("hvc_muen: Allocated %d HVC terminal device(s)\n", count);

	return rc;
}
device_initcall(hvc_muen_init);

static void hvc_muen_destroy(void)
{
	unsigned long flags;
	struct muencons_info *entry, *next;

	spin_lock_irqsave(&muencons_lock, flags);
	if (list_empty(&muencons)) {
		spin_unlock_irqrestore(&muencons_lock, flags);
		return;
	}

	list_for_each_entry_safe(entry, next, &muencons, list) {
		list_del(&entry->list);
		if (entry->hvc != NULL)
			hvc_remove(entry->hvc);
		entry->hvc = NULL;
		if (entry->channel_out) {
			muen_channel_deactivate(entry->channel_out);
			iounmap(entry->channel_out);
		}
		if (entry->channel_in) {
			muen_channel_deactivate(entry->channel_out);
			iounmap(entry->channel_in);
		}
		kfree(entry);
	}
	hvc_muen_cpu = -1;
	hvc_muen_epoch = 0;
	spin_unlock_irqrestore(&muencons_lock, flags);
}

static int __init hvc_muen_console_init(void)
{
	int rc;
	struct muen_cpu_affinity evt;
	const struct muen_resource_type *region;

	if (hvc_muen_out_count < 1 || !out[0])
		return -EINVAL;

	hvc_muen_epoch = muen_get_sched_start();

	region = muen_get_resource(out[0], MUEN_RES_MEMORY);
	if (!region) {
		pr_err("hvc_muen[0]: No output channel for initial console %s\n",
		       out[0]);
		return -EINVAL;
	}

	if (!muen_smp_one_match(&evt, out[0], MUEN_RES_EVENT))
		pr_debug("hvc_muen[0]: No event for initial console %s\n", out[0]);
	else
		hvc_muen_set_cpu(evt.cpu);

	/* NOTE: Instantiation *must* precede allocation */
	rc = hvc_instantiate(HVC_MUEN_COOKIE, 0, &hvc_muen_ops);
	if (rc) {
		pr_err("hvc_muen[0]: Registering as console failed (%d)\n", rc);
		return rc;
	}

	rc = hvc_muen_init_console(0, hvc_muen_epoch);
	if (rc)
		hvc_muen_destroy();

	return rc;
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
