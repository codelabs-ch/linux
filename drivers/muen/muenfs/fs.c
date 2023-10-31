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
 * @file fs.c
 * @brief Implementation of the file system and the file operations.
 *
 * This kernel module implements a file system named "muenfs". It facilitates
 * user-space access to shared memory channels provided by the Muen Separation
 * Kernel.
 *
 * For each channel memory region a file of the correct size is shown in the
 * file system. A program can use stat calls to get the permissions (rw or r/o)
 * of the files and the size of the region. For accessing the file read, write,
 * and mmap operations are supported.
 *
 * The current authentication model is that the files are created with uid and
 * gid set to 0. Depending on the type of the region the files have permissions
 * 0400 or 0600. No further capability checking is done by this module.
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <muen/smp.h>

#include "internal.h"

/**
 * @brief Magic value to identify this file system
 */
#define MUENFS_MAGIC 0xd2c82edd

/**
 * @brief Retrieve #memory_region_t from file.
 *
 * This function retrieves the memory region information previously stored with
 * set_memory_info().
 *
 * @param file file pointer
 * @return the memory_region_t stored in the private_data element.
 */
static inline struct memory_region_t *get_memory_info(struct file *file)
{
	return file->private_data;
}

/**
 * @brief Store #memory_region_t into file.
 *
 * This function uses the file private_data element to store the memory region
 * information.
 *
 * @param file file pointer
 * @param info a memory region information
 */
static inline void set_memory_info(struct file *file,
				   struct memory_region_t *info)
{
	file->private_data = info;
}

/**
 * @brief Open a file representing a channel memory region.
 *
 * This function checks the requested file permissions against the permissions
 * of the memory region. It also transfers the private data pointing to the
 * memory region from the inode to the file.
 *
 * @param inode inode information
 * @param file file information
 * @return 0 on success
 * @return -EPERM if write access was requested for read-only memory region
 */
static int muenfs_open(struct inode *inode, struct file *file)
{
	struct memory_region_t *my_region = inode->i_private;

	set_memory_info(file, my_region);

	if ((file->f_flags & O_ACCMODE) == O_WRONLY ||
	    (file->f_flags & O_ACCMODE) == O_RDWR) {
		if (!my_region->writable)
			return -EPERM;
	}
	atomic_inc(&my_region->open_cnt);
	return 0;
}

/**
 * @brief Close a file representing a channel memory region.
 *
 * @param inode inode information
 * @param file file information
 */
static int muenfs_close(struct inode *inode, struct file *file)
{
	struct memory_region_t *my_region = inode->i_private;

	if (atomic_dec_and_test(&my_region->open_cnt)) {
		if (my_region->irq >= 0) {
			free_irq(my_region->irq, my_region);
			my_region->irq = -1;
		}
	}
	return 0;
}

/**
 * @brief Calculates remaining bytes in a page.
 *
 * This functions calculates the remaining bytes in a page.
 *
 * @param pos current position in units of bytes
 * @return a value between 1 and PAGE_SIZE
 */
static inline size_t remaining_in_page(loff_t pos)
{
	unsigned long address = pos;
	size_t result = PAGE_SIZE;
	size_t remaining = -address & (PAGE_SIZE - 1);

	if (remaining)
		result = remaining;
	return result;
}

/**
 * @brief Read data from a memory region.
 *
 * This function reads the data from the memory region and writes it to the user
 * area. The reading is performed in page-units.
 *
 * @param file file pointer
 * @param buffer user-space buffer to store the data
 * @param length maximum number of bytes to read
 * @param offset pointer to current position, will be updated to reflect read
 *        progress
 * @return number of bytes read
 * @return -EFAULT if memory was unmapped during data copy
 */
static ssize_t muenfs_read(struct file *file,
			   char __user *buffer,
			   size_t length,
			   loff_t *offset)
{
	struct memory_region_t *my_region = get_memory_info(file);
	ssize_t to_read;
	ssize_t ret = 0;
	loff_t initial_offset = *offset;
	unsigned char __user *current_dest = buffer;
	unsigned long mem_pos = my_region->start_phys + initial_offset;
	loff_t region_size = my_region->size_in_pages << PAGE_SHIFT;

	if (initial_offset >= region_size)
		return 0;

	to_read = region_size - initial_offset;
	if (to_read > length)
		to_read = length;

	while (to_read > 0) {
		size_t max_size = remaining_in_page(mem_pos);
		const void *ptr;

		if (max_size > to_read)
			max_size = to_read;

		ptr = ioremap_cache(mem_pos, PAGE_SIZE);
		if (copy_to_user(current_dest, ptr, max_size)) {
			iounmap((void *)ptr);
			return -EFAULT;
		}
		iounmap((void *)ptr);
		current_dest += max_size;
		to_read -= max_size;
		mem_pos += max_size;
		ret += max_size;
	}

	*offset += ret;
	return ret;
}

/**
 * @brief Write data to memory region.
 *
 * This function reads the data provided by the user and writes it to the given
 * offset into the memory region.
 *
 * @param file file pointer
 * @param buffer user-space buffer to read from
 * @param length maximum number of bytes to write
 * @param offset pointer to current position, will be updated for successful
 *        operations
 * @return number of bytes written on success
 * @return -EFAULT if inaccessible memory was encountered
 * @return -ENOSPC if the end of the memory region was reached and length is
 * larger than 0
 */
static ssize_t muenfs_write(struct file *file,
			    const char __user *buffer,
			    size_t length,
			    loff_t *offset)
{
	struct memory_region_t *my_region = get_memory_info(file);
	ssize_t to_write;
	ssize_t ret = 0;
	loff_t initial_offset = *offset;
	const unsigned char __user *current_source = buffer;
	unsigned long mem_pos = my_region->start_phys + initial_offset;
	loff_t region_size = my_region->size_in_pages << PAGE_SHIFT;

	if (initial_offset >= region_size) {
		if (length > 0)
			return -ENOSPC;
		else
			return 0;
	}

	to_write = region_size - initial_offset;
	if (to_write > length)
		to_write = length;

	while (to_write > 0) {
		size_t max_size = remaining_in_page(mem_pos);
		void *ptr;

		if (max_size > to_write)
			max_size = to_write;

		ptr = ioremap_cache(mem_pos, PAGE_SIZE);
		if (copy_from_user(ptr, current_source, max_size)) {
			iounmap(ptr);
			return -EFAULT;
		}
		iounmap(ptr);
		current_source += max_size;
		to_write -= max_size;
		mem_pos += max_size;
		ret += max_size;
	}

	*offset += ret;

	return ret;
}

/**
 * @brief Virtual memory operations for mmap.
 *
 * As we do fault-in mappings no operation is necessary.
 */
static const struct vm_operations_struct device_vm_ops = {
};

/**
 * @brief Implementation of the mmap syscall.
 *
 * This function provides the mmap syscall for this file system. It checks if
 * the size of the mapping request is within the boundaries of the memory
 * region and performs the mapping.
 *
 * @param file file pointer to the file where the mapping is performed
 * @param vma virtual memory area where the mapping is done
 * @return 0 on success
 * @return -ENOMEM if mapping is larger than the memory region size
 * @return -EAGAIN if remap_pfn_range fails
 */
static int muenfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct memory_region_t *my_region = get_memory_info(file);
	unsigned long requested_size = vma->vm_end - vma->vm_start;
	unsigned long region_size = my_region->size_in_pages << PAGE_SHIFT;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long physaddr = offset + my_region->start_phys;

	if (offset >= region_size || requested_size > region_size - offset)
		return -ENOMEM;

	vma->vm_ops = &device_vm_ops;

	/* Remap-pfn-range will mark the range VM_IO and VM_RESERVED */
	if (remap_pfn_range(vma,
		vma->vm_start,
		physaddr >> PAGE_SHIFT,
		requested_size,
		vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static irqreturn_t muenfs_irq_handler(int rq, void *c)
{
	struct memory_region_t *my_region = (struct memory_region_t *)c;

	my_region->events++;
	wake_up(&my_region->p_queue);
	return IRQ_HANDLED;
}

static __poll_t muenfs_poll(struct file *file, struct poll_table_struct *wait)
{
	struct muen_cpu_affinity evt_vec;
	int err;
	struct memory_region_t *my_region = get_memory_info(file);

	if (my_region->irq < 0) {
		if (muen_smp_one_match(&evt_vec, my_region->name,
			    MUEN_RES_VECTOR)) {
			my_region->irq = evt_vec.res.data.number
			    /*- ISA_IRQ_VECTOR(0)*/;
			if (!irq_has_action(my_region->irq)) {
				init_waitqueue_head(&my_region->p_queue);
				err = request_irq(my_region->irq,
					muenfs_irq_handler,
					IRQF_SHARED,
					my_region->name,
					my_region);
				if (err) {
					pr_info("muenfs: unable to register interrupt handler for %s: %d",
						my_region->name, err);
					goto out_err;
				}
			} else {
				pr_info("muenfs: (%s) irq handler already registered on event: %d\n",
					my_region->name,
					evt_vec.res.data.number);
				goto out_err;
			}
		}
	}
	if (my_region->irq >= 0) {
		poll_wait(file, &my_region->p_queue, wait);
		if (my_region->events != my_region->polls) {
			my_region->polls = my_region->events;
			return POLLIN | POLLRDNORM;
		}
		return 0;
	}

out_err:
	my_region->irq = -1;
	return POLLERR;
}

/**
 * @brief File operations for this file system.
 */
static const struct file_operations muenfs_file_fops = {
	.open		= muenfs_open,
	.release	= muenfs_close,
	.read		= muenfs_read,
	.llseek		= generic_file_llseek,
	.write		= muenfs_write,
	.poll		= muenfs_poll,
	.mmap		= muenfs_mmap,
};

/**
 * @brief Create a new inode.
 *
 * @param sb super block of our file system
 * @param mode the file mode to use (includes specification of the file type)
 * @return a new inode on success
 * @return NULL if inode could not be allocated
 */
static struct inode *muenfs_make_inode(struct super_block *sb, int mode)
{
	struct inode *ret = new_inode(sb);

	if (ret) {
		ret->i_mode = mode;
		ret->i_uid.val = ret->i_gid.val = 0;
		ret->i_blocks = 0;
		ret->i_atime = ret->i_mtime = ret->i_ctime = current_time(ret);
	}
	return ret;
}

/**
 * @brief Updates the attributes of an inode.
 *
 * This function is called to update the attributes of an inode after changing
 * them. Changing the size is prohibited because this would require a size
 * change of the underlying memory region, which is not supported.
 * Setting executable/writable-attributes is only allowed for memory regions
 * marked as executable/writable in the Muen system policy.
 *
 * @param ns namespace, passed to setattr_prepare
 * @param dentry directory entry to update
 * @param attr new attributes
 * @return 0 on success
 * @return errors returned by setattr_prepare()
 * @return EPERM on size changes or mode change requests violating the Muen
 * system policy
 */
static int muenfs_set_attr(struct user_namespace *ns, struct dentry *dentry,
			   struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct memory_region_t *region = inode->i_private;
	int error;

	error = setattr_prepare(ns, dentry, attr);
	if (error)
		return error;

	if (attr->ia_valid & ATTR_SIZE && attr->ia_size != inode->i_size)
		return -EPERM;

	if (attr->ia_valid & ATTR_MODE) {
		/* check mode 0111 = (S_IXUSR | S_IXGRP | S_IXOTH) */
		if (attr->ia_mode & 0111 && !region->executable)
			return -EPERM;

		/* check mode 0222 = (S_IWUSR | S_IWGRP | S_IWOTH) */
		if (attr->ia_mode & 0222 && !region->writable)
			return -EPERM;
	}

	setattr_copy(ns, inode, attr);
	mark_inode_dirty(inode);

	return error;
}

/**
 * @brief Inode operations for files describing the memregions.
 *
 * This structure is used to override the setattr routine with
 * muenfs_set_attr().
 */
static const struct inode_operations muenfs_file_inode_ops = {
	.setattr = muenfs_set_attr,
};

/**
 * @brief Callback parameter type.
 */
struct cb_arg {
	struct super_block *sb;	/**< the super block of the file system    */
	struct dentry *dir;	/**< the parent directory to create a file */
};

/**
 * @brief Create a file.
 *
 * This function creates a new file in our file system. The region given as
 * parameter is stored in the private data of the inode for later retrieval.
 * Depending on the permissions of the memory region the file mode is set to
 * 0600 for read-write access or 0400 for read-only access. The file size is
 * set to the channel memory region size in bytes.
 *
 * @param region the memory region associated with the file
 * @param data refers to the super block of the file system and the base
 * directory
 * @param region the memory region associated with the file
 * @return true on success
 * @return false if directory entry could not be allocated
 */
static bool muenfs_create_file(const struct muen_resource_type *const info,
			       void *const data)
{
	struct cb_arg *arg = data;
	struct dentry *dentry;
	struct inode *inode;
	struct qstr qname;
	int file_mode = 0400;
	struct memory_region_t *region;

	/* only export channel and subject device memory */
	if (info->kind != MUEN_RES_MEMORY ||
		(info->data.mem.kind != MUEN_MEM_SUBJ_CHANNEL
		 && info->data.mem.kind != MUEN_MEM_SUBJ_DEVICE))
		return true;

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		goto out;

	strncpy(region->name, info->name.data, info->name.length);
	region->start_phys = info->data.mem.address;
	region->writable = info->data.mem.flags & MEM_WRITABLE_FLAG;
	region->executable = info->data.mem.flags & MEM_EXECUTABLE_FLAG;
	region->size_in_pages = info->data.mem.size >> PAGE_SHIFT;
	region->irq = -1;

	/* create hashed name */
	qname.name = region->name;
	qname.len = strlen(region->name);
	qname.hash = full_name_hash(arg->dir, region->name, qname.len);

	/* create inode and dentry */
	if (region->writable)
		file_mode = 0600;

	inode = muenfs_make_inode(arg->sb, S_IFREG | file_mode);
	if (!inode)
		goto out_free;

	inode->i_ino = get_next_ino();
	inode->i_size = region->size_in_pages << PAGE_SHIFT;
	inode->i_fop = &muenfs_file_fops;
	inode->i_op = &muenfs_file_inode_ops;
	inode->i_private = region;

	dentry = d_alloc(arg->dir, &qname);
	if (!dentry)
		goto out_iput;

	/* put into cache and return */
	d_add(dentry, inode);
	pr_info("muenfs: registered file %s - start 0x%016llx, size 0x%08llx, access %s\n",
	       region->name, region->start_phys, inode->i_size,
	       region->writable ? "rw" : "ro");
	return true;

out_iput:
	iput(inode);
out_free:
	kfree(region);
out:
	return false;
}

/**
 * @brief Create a file for each Muen channel
 *
 * This function creates a new file in the root directory for every Muen
 * channel that is present.
 *
 * @param sb super block of the file system
 * @return 0 on success
 * @return errors returned by muenfs_create_file()
 */
static int muenfs_create_files(struct super_block *sb)
{
	struct cb_arg args = { .sb = sb, .dir = sb->s_root };

	return muen_for_each_resource(muenfs_create_file, &args) ? 0 : -ENOMEM;
}

/**
 * @brief Fills the super block of the file system.
 *
 * This functions initializes the super block, creates the root directory and
 * the files using the muenfs_create_files() function.
 *
 * @param sb the super block of the file system
 * @param data file system options, currently ignored
 * @param silent don't display any printk message if true (ignored)
 * @return 0 on success
 * @see simple_fill_super() and muenfs_create_files() for possible error
 * conditions
 */
static int muenfs_fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr empty_descr = {""};
	int err;

	err = simple_fill_super(sb, MUENFS_MAGIC, &empty_descr);
	if (err)
		return err;

	return muenfs_create_files(sb);
	/* TODO: Register dentry_operations to free
	 *       allocated memory-region structures.
	 */
}

/**
 * @brief Mount super block.
 *
 * This function uses mount_single() to provide a single instance of the file
 * system. The function muenfs_fill_super() is specified to fill the super
 * block of the instance.
 *
 * @param fst file system type specification
 * @param flags parameters specified in the user-space for this mount operation
 * @param devname device to mount, ignored
 * @param data file system options specified in user-space
 * @return pointer or error condition returned by mount_single()
 */
static struct dentry *muenfs_mount(struct file_system_type *fst,
				  int flags, const char *devname, void *data)
{
	return mount_single(fst, flags, data, muenfs_fill_super);
}

/**
 * This description contains the owner, the name, the operation to get the
 * super block, and the operation to destroy the super block. Here
 * kill_litter_super() is required as we are holding references to the
 * directory entries in the file system.
 */
struct file_system_type muenfs_type = {
	.owner		= THIS_MODULE,
	.name		= "muenfs",
	.mount		= muenfs_mount,
	.kill_sb	= kill_litter_super,
};
