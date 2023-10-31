// SPDX-License-Identifier: GPL-2.0

/*
 * Muen virtual network driver.
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

#include <linux/debugfs.h>

#include "internal.h"

/**
 * @file debug.c
 * @brief debugfs support
 *
 * The functions in this file provide developer information to the network
 * interfaces implemented in the module. For each interface a directory is
 * created beneath the top-level "muennet" directory. Each directory contains
 * an 'info' file which provides data about the private information stored in
 * the network interface.
 */

/**
 * @addtogroup debug
 */
/*@{*/

/**
 * @brief Toplevel directory.
 *
 * This variable stores the toplevel directory name "muennet".
 */
static struct dentry *debugfs_topdir;

/**
 * This function is called to initialize the debugfs. It creates the "muennet"
 * directory.
 */
void debug_initialize(void)
{
	debugfs_topdir = debugfs_create_dir("muennet", NULL);
}

/**
 * This function is called to remove the "muennet" directory when the module is
 * unloaded.
 */
void debug_shutdown(void)
{
	debugfs_remove(debugfs_topdir);
	debugfs_topdir = NULL;
}

/**
 * @brief Simple memory buffer.
 *
 * This is a memory buffer used for multiple buffer_append() operations.
 */
struct buffer {
	size_t offset; /**< current write offset */
	size_t length; /**< length of buffer     */
	char *buffer;  /**< the data buffer      */
};

/**
 * @brief Initialize buffer.
 *
 * This function initializes the buffer to use the given data array for
 * storage. The current #buffer.offset is initialized to 0.
 *
 * @param buffer the buffer
 * @param data   pointer to memory buffer
 * @param length the maximum number of elements that can be stored in data
 */
static void buffer_init(struct buffer *buffer, char *data, size_t length)
{
	buffer->offset = 0;
	buffer->length = length;
	buffer->buffer = data;
}

/**
 * @brief Append to buffer.
 *
 * This function uses a sprintf-like operation to append to the data buffer.
 *
 * @param buffer a previously initialized buffer
 * @param format the format string to use
 * @param ...    additional parameters
 * @return 0 if data could be appended
 * @return -1 if buffer would overflow
 */
static int buffer_append(struct buffer *buffer, const char *format, ...)
{
	va_list args;
	int len;

	if (buffer->offset < buffer->length) {
		va_start(args, format);
		len = vsnprintf(buffer->buffer + buffer->offset,
				buffer->length - buffer->offset,
				format, args);
		va_end(args);
		if (buffer->offset + len < buffer->length) {
			buffer->offset += len;
			return 0;
		}
	}
	buffer->offset = buffer->length;
	return -1;
}

/**
 * @brief Fetches output from buffer.
 *
 * This function should be used to retrieve the data of a buffer. It ensures
 * that the final 0 byte is written. The returned pointer can also be used to
 * free the buffer if the pointer to #buffer_init was dynamically allocated.
 *
 * @param buffer the data buffer
 * @return the pointer that was used in buffer_initialize
 */
static char *buffer_retrieve(struct buffer *buffer)
{
	buffer->buffer[buffer->length - 1] = 0;
	return buffer->buffer;
}

/**
 * @brief Data gatherer for the "info" file.
 *
 * This function is called for opening the info file. It allocates a page for
 * temporary storage and formats all information into this page. The buffer is
 * the store in the file's private data to be used in #debug_info_read and
 * freed in #debug_info_release.
 *
 * @param inode the inode of the debugfs entry
 * @param file  the file pointer
 * @return 0 on success
 * @return -ENOMEM if the temporary memory could not be allocated
 */
static int debug_info_open(struct inode *inode, struct file *file)
{
	struct dev_info *dev_info;
	struct buffer buffer;
	size_t i;
	char *page = (char *)__get_free_page(GFP_KERNEL);

	if (!page)
		return -ENOMEM;

	buffer_init(&buffer, page, PAGE_SIZE);

	dev_info = inode->i_private;
	buffer_append(&buffer, "in/out: %s\n", dev_info->bus_info);
	buffer_append(&buffer, "mtu: %d\n", dev_info->mtu);
	buffer_append(&buffer, "flags: ");
	if (dev_info->flags == 0) {
		buffer_append(&buffer, "(none)\n");
	} else {
		for (i = 0; flag_names[i].name != NULL; i++) {
			if (dev_info->flags & flag_names[i].value) {
				buffer_append(&buffer, "%s ",
					      flag_names[i].name);
			}
		}
		buffer_append(&buffer, "\n");
	}

	buffer_append(&buffer, "poll: every %u µs\n", dev_info->poll_interval);

	if (dev_info->writer_element_size) {
		buffer_append(&buffer, "writer is enabled\n");
		buffer_append(&buffer, "writer.element_size: %zu\n",
			      dev_info->writer_element_size);
	} else {
		buffer_append(&buffer, "writer is disabled\n");
	}

	if (dev_info->reader_element_size) {
		buffer_append(&buffer, "reader is enabled\n");
		buffer_append(&buffer, "reader.element_size: %zu\n",
			      dev_info->reader_element_size);
	} else {
		buffer_append(&buffer, "reader is disabled\n");
	}

	buffer_append(&buffer, "stats.rx_packets: %lu\n",
		      dev_info->stats.rx_packets);
	buffer_append(&buffer, "stats.rx_bytes: %lu\n",
		      dev_info->stats.rx_bytes);
	buffer_append(&buffer, "stats.rx_errors: %lu\n",
		      dev_info->stats.rx_errors);
	buffer_append(&buffer, "stats.rx_over_errors: %lu\n",
		      dev_info->stats.rx_over_errors);
	buffer_append(&buffer, "stats.rx_frame_errors: %lu\n",
		      dev_info->stats.rx_frame_errors);
	buffer_append(&buffer, "stats.tx_packets: %lu\n",
		      dev_info->stats.tx_packets);
	buffer_append(&buffer, "stats.tx_bytes: %lu\n",
		      dev_info->stats.tx_bytes);

	file->private_data = buffer_retrieve(&buffer);
	return 0;
}

/**
 * @brief Read function for the "info" file.
 *
 * This function simply retrieves the information prepared by #debug_info_open
 * and sends it to the user.
 *
 * @param file   the file pointer
 * @param buf    the user buffer where to place the information
 * @param nbytes the number of bytes to be stored
 * @param ppos   pointer to the user offset pointer.
 * @return the number of bytes written
 */
static ssize_t debug_info_read(struct file *file, char __user *buf,
			       size_t nbytes, loff_t *ppos)
{
	char *page = file->private_data;

	return simple_read_from_buffer(buf, nbytes, ppos, page, strlen(page));
}

/**
 * @brief Called for close operation on "info" file.
 *
 * This function retrieves the allocated memory stored in the file's private
 * data and releases it.
 *
 * @param inode the inode of the debugfs
 * @param file  the file pointer
 * @return 0 to indicate success
 */

static int debug_info_release(struct inode *inode, struct file *file)
{
	char *page = file->private_data;

	file->private_data = NULL;
	free_page((unsigned long)page);
	return 0;
}

/**
 * @brief The "info" file operations.
 *
 * This provides the operation for file opening, reading and closing. It also
 * provides a llseek function to avoid the big kernel lock in the default
 * implementation.
 */
static const struct file_operations debug_info_fops = {
	.owner   = THIS_MODULE,
	.open    = debug_info_open,
	.read    = debug_info_read,
	.release = debug_info_release,
	.llseek  = generic_file_llseek,
};

/**
 * This function registers the "info" file for the network interface specified
 * by dev_info.
 *
 * @param dev_info private information of the networking interface
 * @return 0 on success
 * @return -ENOMEM if memory allocation failed
 */
int debug_create_device(struct dev_info *dev_info)
{
	int ret = 0;

	if (!debugfs_topdir)
		goto err;

	dev_info->debugfs_dir = debugfs_create_dir(dev_info->dev->name,
						   debugfs_topdir);
	if (!dev_info->debugfs_dir) {
		ret = -ENOMEM;
		goto err;
	}
	dev_info->debugfs_info = debugfs_create_file
		("info", 0400, dev_info->debugfs_dir, dev_info,
		 &debug_info_fops);
	if (!dev_info->debugfs_info) {
		ret = -ENOMEM;
		goto err_create_file;
	}

	return ret;

err_create_file:
	debugfs_remove(dev_info->debugfs_dir);
err:
	return ret;
}

/**
 * This function removes the previously created "info" files of a network
 * interface.
 *
 * @param dev_info private information of the networking interface.
 */

void debug_remove_device(struct dev_info *dev_info)
{
	debugfs_remove(dev_info->debugfs_info);
	dev_info->debugfs_info = NULL;
	debugfs_remove(dev_info->debugfs_dir);
	dev_info->debugfs_dir = NULL;
}

/*@}*/
