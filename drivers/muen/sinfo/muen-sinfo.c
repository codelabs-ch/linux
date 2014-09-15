/*
 * Copyright (C) 2014  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2014  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <muen/sinfo.h>

#include "musinfo.h"

#define SINFO_BASE 0x00014000

static const struct subject_info_type *
const sinfo = (struct subject_info_type *)__va(SINFO_BASE);

/* Check subject info header magic */
static bool check_magic(void);

/* Fill channel struct with information from channel given by index */
static void fill_channel_data(uint8_t idx, struct muen_channel_info *channel);

/* Log channel information */
static bool log_channel(const struct muen_channel_info * const channel,
		void *data);

bool muen_get_channel_info(const char * const name,
		struct muen_channel_info *channel)
{
	int i;

	if (!check_magic())
		return false;

	for (i = 0; i < sinfo->channel_count; i++) {

		if (strncmp(sinfo->channels[i].name.data, name,
					sinfo->channels[i].name.length) == 0) {
			fill_channel_data(i, channel);
			return true;
		}
	}
	return false;
}

bool muen_for_each_channel(channel_cb func, void *data)
{
	int i;
	struct muen_channel_info current_channel;

	if (!check_magic())
		return false;

	for (i = 0; i < sinfo->channel_count; i++) {
		fill_channel_data(i, &current_channel);
		if (!func(&current_channel, data))
			return false;
	}
	return true;
}

static bool log_channel(const struct muen_channel_info * const channel,
		void *data)
{
	pr_info("muen-sinfo: [addr 0x%016llx size 0x%016llx %s] %s\n",
			channel->address, channel->size,
			channel->writable ? "rw" : "ro", channel->name);

	if (channel->has_event) {
		pr_info("muen-sinfo:   (specifies event  %03d)\n",
				channel->event_number);
	}
	if (channel->has_vector) {
		pr_info("muen-sinfo:   (specifies vector %03d)\n",
				channel->vector);
	}

	return true;
}

static void fill_channel_data(uint8_t idx, struct muen_channel_info *channel)
{
	memset(&channel->name, 0, MAX_CHANNEL_NAME_LEN + 1);
	memcpy(&channel->name, sinfo->channels[idx].name.data,
			sinfo->channels[idx].name.length);

	channel->address  = sinfo->channels[idx].address;
	channel->size     = sinfo->channels[idx].size;
	channel->writable = sinfo->channels[idx].flags & WRITABLE_FLAG;

	channel->has_event    = sinfo->channels[idx].flags & HAS_EVENT_FLAG;
	channel->event_number = sinfo->channels[idx].event;
	channel->has_vector   = sinfo->channels[idx].flags & HAS_VECTOR_FLAG;
	channel->vector       = sinfo->channels[idx].vector;
}

static bool check_magic(void)
{
	return sinfo->magic == MUEN_SUBJECT_INFO_MAGIC;
}

static int __init muen_sinfo_init(void)
{
	if (!check_magic()) {
		pr_err("muen-sinfo: Subject information MAGIC mismatch\n");
		return -EINVAL;
	}

	pr_info("muen-sinfo: Subject information exports %d channel(s)\n",
			sinfo->channel_count);
	muen_for_each_channel(log_channel, NULL);
	return 0;
}

module_init(muen_sinfo_init);
MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_DESCRIPTION("Muen subject information driver");
MODULE_LICENSE("GPL");
