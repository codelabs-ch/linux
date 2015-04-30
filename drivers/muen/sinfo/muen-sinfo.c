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

#include <asm/io.h>

#include <linux/init.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <muen/sinfo.h>

#include "musinfo.h"

static unsigned long long sinfo_addr;
static int __init setup_sinfo_addr(char *arg)
{
	if (kstrtoull(arg, 16, &sinfo_addr))
		return -EINVAL;

	return 0;
}
early_param("muen_sinfo", setup_sinfo_addr);

static const struct subject_info_type * sinfo;

/* Fill channel struct with channel information from resource given by index */
static void fill_channel_data(uint8_t idx, struct muen_channel_info *channel);

/* Fill memregion struct with memory region info from resource given by index */
static void fill_memregion_data(uint8_t idx,
		struct muen_memregion_info *region);

/* Log channel information */
static bool log_channel(const struct muen_channel_info * const channel,
		void *data);

static bool is_memregion(const struct resource_type * const resource)
{
	return resource->memregion_idx != NO_RESOURCE;
}

static bool is_channel(const struct resource_type * const resource)
{
	return is_memregion(resource) && resource->channel_info_idx != NO_RESOURCE;
}

bool muen_check_magic(void)
{
	return sinfo->magic == MUEN_SUBJECT_INFO_MAGIC;
}
EXPORT_SYMBOL(muen_check_magic);

bool muen_get_channel_info(const char * const name,
		struct muen_channel_info *channel)
{
	int i;

	if (!muen_check_magic())
		return false;

	for (i = 0; i < sinfo->resource_count; i++) {
		if (is_channel(&sinfo->resources[i]) &&
			strncmp(sinfo->resources[i].name.data, name,
					sinfo->resources[i].name.length) == 0) {
			fill_channel_data(i, channel);
			return true;
		}
	}
	return false;
}
EXPORT_SYMBOL(muen_get_channel_info);

bool muen_get_memregion_info(const char * const name,
		struct muen_memregion_info *memregion)
{
	int i;

	if (!muen_check_magic())
		return false;

	for (i = 0; i < sinfo->resource_count; i++) {
		if (is_memregion(&sinfo->resources[i]) &&
			strncmp(sinfo->resources[i].name.data, name,
					sinfo->resources[i].name.length) == 0) {
			fill_memregion_data(i, memregion);
			return true;
		}
	}
	return false;
}
EXPORT_SYMBOL(muen_get_memregion_info);

bool muen_for_each_channel(channel_cb func, void *data)
{
	int i;
	struct muen_channel_info current_channel;

	if (!muen_check_magic())
		return false;

	for (i = 0; i < sinfo->resource_count; i++) {
		if (is_channel(&sinfo->resources[i])) {
			fill_channel_data(i, &current_channel);
			if (!func(&current_channel, data))
				return false;
		}
	}
	return true;
}
EXPORT_SYMBOL(muen_for_each_channel);

bool muen_for_each_memregion(memregion_cb func, void *data)
{
	int i;
	struct muen_memregion_info current_region;

	if (!muen_check_magic())
		return false;

	for (i = 0; i < sinfo->resource_count; i++) {
		if (is_memregion(&sinfo->resources[i])) {
			fill_memregion_data(i, &current_region);
			if (!func(&current_region, data))
				return false;
		}
	}
	return true;
}
EXPORT_SYMBOL(muen_for_each_memregion);

uint64_t muen_get_tsc_khz(void)
{
	if (!muen_check_magic())
		return 0;

	return sinfo->tsc_khz;
}
EXPORT_SYMBOL(muen_get_tsc_khz);

static bool log_channel(const struct muen_channel_info * const channel,
		void *data)
{
	if (channel->has_event || channel->has_vector) {
		pr_info("muen-sinfo: [%s with %s %03d] %s\n",
				channel->writable ? "writer" : "reader",
				channel->has_event ? "event " : "vector",
				channel->has_event ? channel->event_number : channel->vector,
				channel->name);
	} else {
		pr_info("muen-sinfo: [%s with no %s ] %s\n",
			channel->writable ? "writer" : "reader",
			channel->writable ? "event " : "vector",
			channel->name);
	}

	return true;
}

static bool log_memregion(const struct muen_memregion_info * const region,
		void *data)
{
	pr_info("muen-sinfo: [addr 0x%016llx size 0x%016llx %s%s] %s\n",
			region->address, region->size,
			region->writable ? "rw" : "ro",
			region->executable ? "x" : "-", region->name);

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

static void fill_memregion_data(uint8_t idx, struct muen_memregion_info *region)
{
	const struct resource_type resource = sinfo->resources[idx];
	const struct memregion_type memregion =
		sinfo->memregions[resource.memregion_idx - 1];

	memset(&region->name, 0, MAX_NAME_LENGTH + 1);
	memcpy(&region->name, resource.name.data, resource.name.length);

	region->address    = memregion.address;
	region->size       = memregion.size;
	region->writable   = memregion.flags & MEM_WRITABLE_FLAG;
	region->executable = memregion.flags & MEM_EXECUTABLE_FLAG;
}

void __init muen_sinfo_early_init(void)
{
	/* This call site is too early to create mapping using ioremap */
	sinfo = (struct subject_info_type *)__va(sinfo_addr);
}

static int __init muen_sinfo_init(void)
{
	sinfo = (struct subject_info_type *)ioremap_cache(sinfo_addr,
			sizeof(struct subject_info_type));
	if (!muen_check_magic()) {
		pr_err("muen-sinfo: Subject information MAGIC mismatch\n");
		return -EINVAL;
	}

	pr_info("muen-sinfo: Subject information @ 0x%016llx\n", sinfo_addr);
	pr_info("muen-sinfo: Subject information exports %d memory region(s)\n",
			sinfo->memregion_count);
	muen_for_each_memregion(log_memregion, NULL);

	pr_info("muen-sinfo: Subject information exports %d channel(s)\n",
			sinfo->channel_info_count);
	muen_for_each_channel(log_channel, NULL);

	return 0;
}

console_initcall(muen_sinfo_init);
MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen subject information driver");
MODULE_LICENSE("GPL");
