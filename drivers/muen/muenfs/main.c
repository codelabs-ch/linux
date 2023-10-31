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

/**
 * @file main.c
 * @brief Muenfs register/unregister functionality.
 *
 * These routines are dealing with file system initialization and cleanup.
 */

#include <linux/module.h>
#include <linux/fs.h>

#include "internal.h"

/**
 * @brief Module initialization.
 *
 * This function registers the Muen file system.
 *
 * @return 0 on success, negative values on error
 */
static int __init muenfs_init(void)
{
	return register_filesystem(&muenfs_type);
}

/**
 * @brief Module finalization.
 *
 * This function unregisters the Muen file system.
 */
static void __exit muenfs_exit(void)
{
	unregister_filesystem(&muenfs_type);
}

module_init(muenfs_init);
module_exit(muenfs_exit);

MODULE_DESCRIPTION("Muen SK shared memory channel file system");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Torsten Hilbrich <torsten.hilbrich@secunet.com>");
