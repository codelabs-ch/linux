/*
 * Muen shared memory channel file system.
 *
 * Copyright (C) 2015  secunet Security Networks AG
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef INTERNAL_H_
#define INTERNAL_H_

/**
 * @file internal.h
 * @brief Common information shared by the other source files.
 */

#include <linux/types.h>
#include <linux/wait.h>
#include <muen/sinfo.h>

/**
 * @brief Structure holding information about memregions.
 *
 * This structure holds information about each identified channel memory
 * region. The information is provided by the Muen SK in the subject info page.
 */
struct memory_region_t {
	char name[MAX_NAME_LENGTH + 1]; /**< the name of the memory region                      */
	uint64_t start_phys;            /**< the start of the memory region as physical address */
	bool writable;                  /**< whether the region is writable or not              */
	bool executable;                /**< whether the region is executable or not            */
	size_t size_in_pages;           /**< size of the memory region in pages                 */
	int irq;                        /**< IRQ number                                         */
	struct wait_queue_head p_queue; /**< Poll wait queue                                    */
	int polls, events;
	atomic_t open_cnt;              /**< reference counter for open / close                 */
};

/**
 * @brief Description of our file system.
 */
extern struct file_system_type muenfs_type;

#endif
