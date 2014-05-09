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

#include "musinfo.h"

#define SINFO_BASE 0x00014000

static const struct subject_info_type *
const sinfo = (struct subject_info_type *)__va(SINFO_BASE);

static int __init muen_sinfo_init(void)
{
	if (sinfo->magic != MUEN_SUBJECT_INFO_MAGIC) {
		pr_err("muen-sinfo: Subject information MAGIC mismatch, driver inactive\n");
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
