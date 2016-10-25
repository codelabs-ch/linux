/*
 * Copyright (C) 2014-2015  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2014-2015  Adrian-Ken Rueegsegger <ken@codelabs.ch>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/module.h>
#include <muen/sinfo.h>

#include "musinfo.h"

static char subject_name[MAX_NAME_LENGTH + 1];
static bool subject_name_set = false;

static unsigned long long sinfo_addr;
static int __init setup_sinfo_addr(char *arg)
{
	if (kstrtoull(arg, 16, &sinfo_addr))
		return -EINVAL;

	return 0;
}

early_param("muen_sinfo", setup_sinfo_addr);

static const struct subject_info_type *sinfo;

static bool log_channel(const struct muen_channel_info * const channel,
			void *data)
{
	if (channel->has_event || channel->has_vector) {
		pr_info("muen-sinfo: [%s with %s %03d] %s\n",
			channel->writable ? "writer" : "reader",
			channel->has_event ? "event " : "vector",
			channel->has_event ?
				channel->event_number : channel->vector,
			channel->name);
	} else {
		pr_info("muen-sinfo: [%s with no %s ] %s\n",
			channel->writable ? "writer" : "reader",
			channel->writable ? "event " : "vector",
			channel->name);
	}

	return true;
}

uint8_t no_hash[HASH_LENGTH] = {0};

static const char * const content_names[] = {
	"uninitialized", "fill", "file",
};

static bool hash_available(const uint8_t * const first)
{
	return memcmp(first, no_hash, HASH_LENGTH) != 0;
}

static bool log_memregion(const struct muen_memregion_info * const region,
			  void *data)
{
	char str[65];

	pr_info("muen-sinfo: [%s, addr 0x%016llx size 0x%016llx %s%s] %s\n",
		content_names[region->content], region->address, region->size,
		region->writable ? "rw" : "ro", region->executable ? "x" : "-",
		region->name);

	if (region->content == MUEN_CONTENT_FILL)
		pr_info("muen-sinfo:  [pattern 0x%.2x]\n", region->pattern);

	if (hash_available(region->hash)) {
		bin2hex(&str[0], (void *)region->hash, HASH_LENGTH);
		str[64] = '\0';
		pr_info("muen-sinfo:  [hash 0x%s]\n", str);
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

static void fill_memregion_data(uint8_t idx, struct muen_memregion_info *region)
{
	const struct resource_type resource = sinfo->resources[idx];
	const struct memregion_type memregion =
		sinfo->memregions[resource.memregion_idx - 1];

	memset(&region->name, 0, MAX_NAME_LENGTH + 1);
	memcpy(&region->name, resource.name.data, resource.name.length);

	memcpy(&region->hash, memregion.hash, HASH_LENGTH);

	region->content    = memregion.content;
	region->address    = memregion.address;
	region->size       = memregion.size;
	region->pattern    = memregion.pattern;
	region->writable   = memregion.flags & MEM_WRITABLE_FLAG;
	region->executable = memregion.flags & MEM_EXECUTABLE_FLAG;
}

static bool is_memregion(const struct resource_type * const resource)
{
	return resource->memregion_idx != NO_RESOURCE;
}

static bool is_channel(const struct resource_type * const resource)
{
	return is_memregion(resource) &&
	       resource->channel_info_idx != NO_RESOURCE;
}

static void fill_dev_data(uint8_t idx, struct muen_dev_info *dev)
{
	const struct dev_info_type dev_info = sinfo->dev_info[idx];

	dev->sid         = dev_info.sid;
	dev->irte_start  = dev_info.irte_start;
	dev->irq_start   = dev_info.irq_start;
	dev->ir_count    = dev_info.ir_count;
	dev->msi_capable = dev_info.flags & DEV_MSI_FLAG;
}

bool muen_check_magic(void)
{
	return sinfo->magic == MUEN_SUBJECT_INFO_MAGIC;
}
EXPORT_SYMBOL(muen_check_magic);

const char * const muen_get_subject_name(void)
{
	if (!muen_check_magic())
		return NULL;

	if (!subject_name_set)
	{
		memset(subject_name, 0, MAX_NAME_LENGTH + 1);
		memcpy(subject_name, &sinfo->name.data, sinfo->name.length);
		subject_name_set = true;
	}

	return subject_name;
}
EXPORT_SYMBOL(muen_get_subject_name);

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

bool muen_get_dev_info(const uint16_t sid, struct muen_dev_info *dev)
{
	int i;

	if (!muen_check_magic())
		return false;

	for (i = 0; i < sinfo->dev_info_count; i++) {
		if (sinfo->dev_info[i].sid == sid) {
			fill_dev_data(i, dev);
			return true;
		}
	}
	return false;
}
EXPORT_SYMBOL(muen_get_dev_info);

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

inline uint64_t muen_get_sched_start(void)
{
	if (!muen_check_magic())
		return 0;

	return sinfo->tsc_schedule_start;
}
EXPORT_SYMBOL(muen_get_sched_start);

inline uint64_t muen_get_sched_end(void)
{
	if (!muen_check_magic())
		return 0;

	return sinfo->tsc_schedule_end;
}
EXPORT_SYMBOL(muen_get_sched_end);

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
	pr_info("muen-sinfo: Subject name is '%s'\n", muen_get_subject_name());
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
MODULE_LICENSE("Dual BSD/GPL");
