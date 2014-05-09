/*
 * Copyright (C) 2014  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2014  Adrian-Ken Rueegsegger <ken@codelabs.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

bool muen_get_channel_info(const char * const name,
		struct muen_channel_info *channel)
{
	int i;

	if (!check_magic())
		return false;

	for (i = 0; i < sinfo->channel_count; i++) {

		if (strncmp(sinfo->channels[i].name.data, name,
					sinfo->channels[i].name.length) == 0) {

			memset(&channel->name, 0, MAX_CHANNEL_NAME_LEN + 1);
			memcpy(&channel->name, sinfo->channels[i].name.data,
					sinfo->channels[i].name.length);

			channel->address  = sinfo->channels[i].address;
			channel->size     = sinfo->channels[i].size;
			channel->writable = sinfo->channels[i].flags
				& WRITABLE_FLAG;

			channel->has_event    = sinfo->channels[i].flags
				& HAS_EVENT_FLAG;
			channel->event_number = sinfo->channels[i].event;
			channel->has_vector   = sinfo->channels[i].flags
				& HAS_VECTOR_FLAG;
			channel->vector       = sinfo->channels[i].vector;

			return true;
		}
	}
	return false;
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
	return 0;
}

module_init(muen_sinfo_init);
MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_DESCRIPTION("Muen subject information driver");
MODULE_LICENSE("GPL");
