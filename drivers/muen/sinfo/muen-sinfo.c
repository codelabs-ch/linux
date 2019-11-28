// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
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

#include "muschedinfo.h"

static char subject_name[MAX_NAME_LENGTH + 1];
static bool subject_name_unset = true;

static unsigned long long sinfo_addr;
static int __init setup_sinfo_addr(char *arg)
{
	if (kstrtoull(arg, 16, &sinfo_addr))
		return -EINVAL;

	return 0;
}

early_param("muen_sinfo", setup_sinfo_addr);

static const struct subject_info_type *sinfo;
static const struct scheduling_info_type *sched_info;

uint8_t no_hash[HASH_LENGTH] = {0};

static const char *const content_names[] = {
	"uninitialized", "fill", "file",
};

static bool hash_available(const uint8_t *const first)
{
	return memcmp(first, no_hash, HASH_LENGTH) != 0;
}

struct iterator {
	const struct muen_resource_type *res;
	unsigned int idx;
};

/*
 * Iterate over all resources beginning at given start resource.  If the res
 * member of the iterator is NULL, the function (re)starts the iteration at the
 * first available resource.
 */
static bool iterate_resources(struct iterator *const iter)
{
	if (!muen_check_magic())
		return false;

	if (!iter->res) {
		iter->res = &sinfo->resources[0];
		iter->idx = 0;
	} else {
		iter->res++;
		iter->idx++;
	}
	return iter->idx < sinfo->resource_count
		&& iter->res->kind != MUEN_RES_NONE;
}

static bool log_resource(const struct muen_resource_type *const res, void *data)
{
	char str[65];

	switch (res->kind) {
	case MUEN_RES_MEMORY:
		pr_info("muen-sinfo: memory [%s, addr 0x%016llx size 0x%016llx %s%s%s] %s\n",
			content_names[res->data.mem.content],
			res->data.mem.address, res->data.mem.size,
			res->data.mem.flags & MEM_WRITABLE_FLAG ? "rw" : "ro",
			res->data.mem.flags & MEM_EXECUTABLE_FLAG ? "x" : "-",
			res->data.mem.flags & MEM_CHANNEL_FLAG ? "c" : "-",
			res->name.data);

		if (res->data.mem.content == MUEN_CONTENT_FILL)
			pr_info("muen-sinfo:  [pattern 0x%.2x]\n",
				res->data.mem.pattern);

		if (hash_available(res->data.mem.hash)) {
			bin2hex(&str[0], (void *)res->data.mem.hash,
				HASH_LENGTH);
			str[64] = '\0';
			pr_info("muen-sinfo:  [hash 0x%s]\n", str);
		}
		break;
	case MUEN_RES_DEVICE:
		pr_info("muen-sinfo: device [sid 0x%x IRTE/IRQ start %u/%u IR count %u flags %u] %s\n",
			res->data.dev.sid, res->data.dev.irte_start,
			res->data.dev.irq_start, res->data.dev.ir_count,
			res->data.dev.flags, res->name.data);
		break;
	case MUEN_RES_EVENT:
		pr_info("muen-sinfo: event [number %u] %s\n",
			res->data.number, res->name.data);
		break;
	case MUEN_RES_VECTOR:
		pr_info("muen-sinfo: vector [number %u] %s\n",
			res->data.number, res->name.data);
		break;
	case MUEN_RES_NONE:
		break;
	default:
		pr_info("muen-sinfo: UNKNOWN resource at address %p\n",
			res);
		break;
	}

	return true;
}

bool muen_names_equal(const struct muen_name_type *const n1,
		      const char *const n2)
{
	return n1->length == strlen(n2)
		&& strncmp(n1->data, n2, n1->length) == 0;
}
EXPORT_SYMBOL(muen_names_equal);

bool muen_check_magic(void)
{
	return sinfo->magic == MUEN_SUBJECT_INFO_MAGIC;
}
EXPORT_SYMBOL(muen_check_magic);

const char *const muen_get_subject_name(void)
{
	if (!muen_check_magic())
		return NULL;

	if (subject_name_unset)	{
		memset(subject_name, 0, MAX_NAME_LENGTH + 1);
		memcpy(subject_name, &sinfo->name.data, sinfo->name.length);
		subject_name_unset = false;
	}

	return subject_name;
}
EXPORT_SYMBOL(muen_get_subject_name);

const struct muen_resource_type *const
muen_get_resource(const char *const name, enum muen_resource_kind kind)
{
	struct iterator i = { NULL, 0 };

	while (iterate_resources(&i))
		if (i.res->kind == kind && muen_names_equal(&i.res->name, name))
			return i.res;

	return NULL;
}
EXPORT_SYMBOL(muen_get_resource);

const struct muen_device_type *const muen_get_device(const uint16_t sid)
{
	struct iterator i = { NULL, 0 };

	while (iterate_resources(&i))
		if (i.res->kind == MUEN_RES_DEVICE &&
				i.res->data.dev.sid == sid)
			return &i.res->data.dev;

	return NULL;
}
EXPORT_SYMBOL(muen_get_device);

bool muen_for_each_resource(resource_cb func, void *data)
{
	struct iterator i = { NULL, 0 };

	while (iterate_resources(&i))
		if (!func(i.res, data))
			return false;

	return true;
}
EXPORT_SYMBOL(muen_for_each_resource);

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

	return atomic64_read(&sched_info->tsc_schedule_start);
}
EXPORT_SYMBOL(muen_get_sched_start);

inline uint64_t muen_get_sched_end(void)
{
	if (!muen_check_magic())
		return 0;

	return atomic64_read(&sched_info->tsc_schedule_end);
}
EXPORT_SYMBOL(muen_get_sched_end);

void __init muen_sinfo_early_init(void)
{
	const unsigned long sinfo_page_size = roundup
		(sizeof(struct subject_info_type),
		 PAGE_SIZE);

	sinfo = (struct subject_info_type *)early_ioremap(sinfo_addr,
			sizeof(struct subject_info_type));
	sched_info = (struct scheduling_info_type *)early_ioremap(sinfo_addr +
		sinfo_page_size, sizeof(struct scheduling_info_type));
}

static int __init muen_sinfo_init(void)
{
	void __iomem *early_sinfo = (void *)sinfo;
	void __iomem *early_sched_info = (void *)sched_info;
	const unsigned long sinfo_page_size = roundup
		(sizeof(struct subject_info_type),
		 PAGE_SIZE);
	sinfo = (struct subject_info_type *)ioremap_cache(sinfo_addr,
			sizeof(struct subject_info_type));
	if (!muen_check_magic()) {
		pr_err("muen-sinfo: Subject information MAGIC mismatch\n");
		return -EINVAL;
	}
	sched_info = (struct scheduling_info_type *)ioremap_cache(sinfo_addr +
		sinfo_page_size, sizeof(struct scheduling_info_type));

	early_iounmap(early_sinfo, sizeof(struct subject_info_type));
	early_iounmap(early_sched_info, sizeof(struct scheduling_info_type));

	pr_info("muen-sinfo: Subject information    @ 0x%016llx\n", sinfo_addr);
	pr_info("muen-sinfo: Scheduling information @ 0x%016llx\n",
		sinfo_addr + sinfo_page_size);
	pr_info("muen-sinfo: Subject name is '%s'\n", muen_get_subject_name());
	pr_info("muen-sinfo: Subject exports %u resources\n",
		sinfo->resource_count);
	muen_for_each_resource(log_resource, NULL);

	return 0;
}

console_initcall(muen_sinfo_init);
MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen subject information driver");
MODULE_LICENSE("Dual BSD/GPL");
