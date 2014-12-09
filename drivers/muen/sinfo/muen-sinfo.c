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

/* Fill channel struct with channel information from resource given by index */
static void fill_channel_data(uint8_t idx, struct muen_channel_info *channel);

/* Log channel information */
static bool log_channel(const struct muen_channel_info * const channel,
		void *data);

bool muen_check_magic(void)
{
	return sinfo->magic == MUEN_SUBJECT_INFO_MAGIC;
}

bool muen_get_channel_info(const char * const name,
		struct muen_channel_info *channel)
{
	int i;

	if (!muen_check_magic())
		return false;

	pr_info("muen-sinfo: Getting channel info for %s\n", name);
	for (i = 0; i < sinfo->resource_count; i++) {
		if (strncmp(sinfo->resources[i].name.data, name,
					sinfo->resources[i].name.length) == 0) {
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

	if (!muen_check_magic())
		return false;

	for (i = 0; i < sinfo->resource_count; i++) {
		if (sinfo->resources[i].channel_info_idx != NO_RESOURCE) {
			fill_channel_data(i, &current_channel);
			if (!func(&current_channel, data))
				return false;
		}
	}
	return true;
}

uint64_t muen_get_tsc_khz(void)
{
	if (!muen_check_magic())
		return 0;

	return sinfo->tsc_khz;
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
	const struct resource_type resource = sinfo->resources[idx];
	const struct memregion_type memregion =
		sinfo->memregions[resource.memregion_idx - 1];
	const struct channel_info_type channel_info =
		sinfo->channels_info[resource.channel_info_idx - 1];

	memset(&channel->name, 0, MAX_NAME_LENGTH + 1);
	memcpy(&channel->name, resource.name.data, resource.name.length);

	channel->address  = memregion.address;
	channel->size     = memregion.size;
	channel->writable = memregion.flags & MEM_WRITABLE_FLAG;

	channel->has_event    = channel_info.flags & CHAN_EVENT_FLAG;
	channel->event_number = channel_info.event;
	channel->has_vector   = channel_info.flags & CHAN_VECTOR_FLAG;
	channel->vector       = channel_info.vector;
}

static int __init muen_sinfo_init(void)
{
	if (!muen_check_magic()) {
		pr_err("muen-sinfo: Subject information MAGIC mismatch\n");
		return -EINVAL;
	}

	pr_info("muen-sinfo: Subject information exports %d channel(s)\n",
			sinfo->channel_info_count);
	muen_for_each_channel(log_channel, NULL);

	return 0;
}

module_init(muen_sinfo_init);
MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_DESCRIPTION("Muen subject information driver");
MODULE_LICENSE("GPL");
