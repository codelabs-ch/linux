/*
 * Copyright (C) 2015  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2015  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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
#include <muen/sinfo.h>

uint64_t current_start = 0, counter = 0;

static inline unsigned long long muen_read_tsc(void)
{
	const uint64_t next_start = muen_get_sched_start();

	if (next_start == current_start)
		counter++;
	else
		counter = current_start = next_start;

	return counter;
}

static int __init muen_tsc_init(void)
{
	pv_cpu_ops.read_tsc = muen_read_tsc;
	pr_info("muen-tsc: Driver active\n");
	return 0;
}

console_initcall(muen_tsc_init);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen paravirt TSC driver");
MODULE_LICENSE("GPL");
