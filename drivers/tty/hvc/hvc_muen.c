/*
 * Copyright (C) 2013  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2013  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kvm_para.h>

#include <muen/writer.h>

#include "hvc_console.h"

#define HVC_MUEN_COOKIE	0x4d75656e	/* "Muen" in hex */
#define CHANNEL_SIZE	4032
#define PENDING_DATA	1

struct hvc_struct *hvc_muen_dev;

static struct muchannel *channel_out = (struct muchannel *)__va(0x3000);

static int hvc_muen_put(uint32_t vtermno, const char *buf, int count)
{
	int i;

	for (i = 0; i < count; i++)
		muchannel_write(channel_out, &buf[i]);

	kvm_hypercall0(PENDING_DATA);

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

	hp = hvc_alloc(HVC_MUEN_COOKIE, 0, &hvc_muen_ops, CHANNEL_SIZE);
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

	muchannel_deactivate(channel_out);
}

module_exit(hvc_muen_exit);

static int __init hvc_muen_console_init(void)
{
	pr_devel("Initializing Muen console\n");
	muchannel_initialize(channel_out, 1, 1, CHANNEL_SIZE, 1);

	hvc_instantiate(HVC_MUEN_COOKIE, 0, &hvc_muen_ops);

	return 0;
}

console_initcall(hvc_muen_console_init);
