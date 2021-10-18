/*************************************************************************
*                                                                        *
*  Copyright (C) codelabs gmbh, Switzerland - all rights reserved        *
*                <https://www.codelabs.ch/>, <contact@codelabs.ch>       *
*                                                                        *
*  This program is free software: you can redistribute it and/or modify  *
*  it under the terms of the GNU General Public License as published by  *
*  the Free Software Foundation, either version 3 of the License, or     *
*  (at your option) any later version.                                   *
*                                                                        *
*  This program is distributed in the hope that it will be useful,       *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*  GNU General Public License for more details.                          *
*                                                                        *
*  You should have received a copy of the GNU General Public License     *
*  along with this program.  If not, see <http://www.gnu.org/licenses/>. *
*                                                                        *
*                                                                        *
*    @contributors                                                       *
*        2020, 2021  David Loosli <dave@codelabs.ch>                     *
*                                                                        *
*                                                                        *
*    @description                                                        *
*        channel-muensk Muen SK communication channel implementation as  *
*        as character device on the platform bus                         *
*    @project                                                            *
*        MuenOnARM                                                       *
*    @interface                                                          *
*        Subjects                                                        *
*    @target                                                             *
*        Linux mainline kernel 5.2                                       *
*    @reference                                                          *
*        Linux Device Drivers Development by John Madieu, 2017,          *
*        ISBN 978-1-78528-000-9                                          *
*                                                                        *
*    @config                                                             *
*        device tree only, example with 64-bit address size and          *
*        64-bit length (min. 4KB due to page size, max. according        *
*        to Muen SK config) and readonly configuration                   *
*                                                                        *
*            cchannel@21000000 {                                         *
*                compatible = "muen,communication-channel";              *
*                reg = <0x0 0x21000000 0x0 0x1000>;                      *
*                interrupts = <GIC_SPI 0x8 IRQ_TYPE_LEVEL_HIGH>;         *
*                type = <READONLY_CHANNEL>                               *
*                status = "okay";                                        *
*            };                                                          *
*                                                                        *
*************************************************************************/


#include <asm/exception.h>
#include <asm/io.h>

#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "channel-muensk.h"

/*
 * device id generator
 */
struct muensk_id {
    int id;
    struct list_head list;
};

static bool cdev_idgen_initialized = false;
static int cdev_counters[NUMBER_OF_TYPES];
static struct list_head cdev_id_lists[NUMBER_OF_TYPES];

static int muensk_generate_id(int *cdev_id, int type)
{
    struct muensk_id *current_id, *next_id;
    int index;

    /* setup device counter and device id lists */
    if (!cdev_idgen_initialized) {
        for (index = 0; index < NUMBER_OF_TYPES; index++) {
            cdev_counters[index] = 0;
            INIT_LIST_HEAD(&cdev_id_lists[index]);
        }
        cdev_idgen_initialized = true;
    }

    /* setup device id */
    current_id = kmalloc(sizeof(*current_id), GFP_KERNEL);

    if (!current_id) {
        pr_err("Muen SK Channel - failed to aquire memory for device id\n");
        return -ENOMEM;
    }

    current_id->id = 0;

    /* initialize new node */
    INIT_LIST_HEAD(&current_id->list);

    /* find next possible device id */
    list_add(&current_id->list, &cdev_id_lists[type]);

    while(!list_is_last(&current_id->list, &cdev_id_lists[type])) {
        next_id = list_next_entry(current_id, list);
        
        if(!(current_id->id < next_id->id)) {
            current_id->id++;
            list_swap(&current_id->list, &next_id->list);
        }
        else {
            break;
        }
    }

    *cdev_id = current_id->id;
    cdev_counters[type]++;

    return 0;
}

static int muensk_free_id(int cdev_id, int type)
{
    struct muensk_id *iterator;

    /* serach device id and delete entry */
    list_for_each_entry(iterator, &cdev_id_lists[type], list) {
        if (iterator->id == cdev_id) {
            list_del(&iterator->list);
            cdev_counters[type]--;
            return 0;
        }
    }

    return -EINVAL;
}

/*
 * character device definitions
 */
static struct class *muensk_channel_class[NUMBER_OF_TYPES] = {NULL, NULL};

static const struct file_operations muensk_channel_fops = {
    .owner   = THIS_MODULE,
    .open    = muensk_channel_open,
    .release = muensk_channel_release,
    .read    = muensk_channel_read,
    .write   = muensk_channel_write,
};

/*
 * character device functions
 */
static int muensk_cdevice_create(struct platform_device *pdev)
{
    struct muensk_channel_data *internal_data;
    dev_t current_dev;
    int current_type, err_value;
    char *current_name, *current_class;

    /* setup platform internal data */
    internal_data = platform_get_drvdata(pdev);

    if (!internal_data) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to get internal data\n");
        return -ENODEV;
    }

    /* setup read or write channel configuration */
    switch (internal_data->type) {
        case READONLY_CHANNEL:
            current_type  = READONLY_CHANNEL;
            current_name  = READONLY_CHANNEL_NAME;
            current_class = READONLY_CHANNEL_CLASS;
            break;
        case WRITEONLY_CHANNEL:
            current_type  = WRITEONLY_CHANNEL;
            current_name  = WRITEONLY_CHANNEL_NAME;
            current_class = WRITEONLY_CHANNEL_CLASS;
            break;
        default:
            dev_err(&pdev->dev, "Muen SK Channel - illegal type argument\n");
            return -EINVAL;
    }

    /* create class in /sys if not exists */
    if (!muensk_channel_class[current_type]) {
        muensk_channel_class[current_type] = class_create(THIS_MODULE, current_class);

        if (!muensk_channel_class[current_type]) {
            dev_err(&pdev->dev, "Muen SK Channel - failed to create channel class\n");
            return -ENODEV;
        }
    }

    /* request character device region */
    err_value = alloc_chrdev_region(&current_dev, 0, 1, current_name);

    if(err_value) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to allocate character device region");
        return err_value;
    }

    /* initialize and add character device */
    cdev_init(&internal_data->char_device, &muensk_channel_fops);
    internal_data->char_device.owner = THIS_MODULE;
    err_value = cdev_add(&internal_data->char_device, current_dev, 1);

    if(err_value) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to add character device");
        goto err_cdev_init;
    }

    /* create device node in /dev */
    err_value = muensk_generate_id(&internal_data->char_device_id, internal_data->type);

    if (err_value || !device_create(muensk_channel_class[current_type],
                                    NULL, current_dev, NULL, 
                                    "%s%d", current_name, internal_data->char_device_id)) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to create device entry in /dev\n");
        err_value = -ENODEV;
        goto err_cdev_create;
    }

    pr_info("Muen SK Channel - character device created:\n");
    pr_info("    Parent Device Name    : %s\n", internal_data->name);
    pr_info("    Device Class /sys     : %s\n", current_class);
    pr_info("    Device Name /dev      : %s%d\n", current_name, internal_data->char_device_id);
    pr_info("    Type                  : %s\n", (current_type == READONLY_CHANNEL? "readonly" : "writeonly"));

    return 0;

err_cdev_create:
    cdev_del(&internal_data->char_device);

err_cdev_init:
    unregister_chrdev_region(internal_data->char_device.dev, 1);
    return err_value;
}

static int muensk_cdevice_remove(struct platform_device *pdev)
{
    struct muensk_channel_data *internal_data;
    int current_type;
    char *current_name, *current_class;

    /* read internal platform device data */
    internal_data = platform_get_drvdata(pdev);

    if (!internal_data) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to get internal data\n");
        return -ENODEV;
    }

    /* setup read or write channel configuration */
    switch (internal_data->type) {
        case READONLY_CHANNEL:
            current_type  = READONLY_CHANNEL;
            current_name  = READONLY_CHANNEL_NAME;
            current_class = READONLY_CHANNEL_CLASS;
            break;
        case WRITEONLY_CHANNEL:
            current_type  = WRITEONLY_CHANNEL;
            current_name  = WRITEONLY_CHANNEL_NAME;
            current_class = WRITEONLY_CHANNEL_CLASS;
            break;
        default:
            dev_err(&pdev->dev, "Muen SK Channel - illegal type argument\n");
            return -EINVAL;
    }

    /* remove device node in /dev */
    if (!muensk_channel_class[current_type]) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to remove device node, no such class\n");
        return -ENODEV;
    }

    device_destroy(muensk_channel_class[current_type], internal_data->char_device.dev);
    muensk_free_id(internal_data->char_device_id, current_type);

    /* remove character device */
    cdev_del(&internal_data->char_device);

    /* unregister character device region */
    unregister_chrdev_region(internal_data->char_device.dev, 1);

    /* check device counter and remove class if necessary */
    if (cdev_counters[current_type] == 0) {
        class_destroy(muensk_channel_class[current_type]);
    }

    pr_info("Muen SK Channel - character device removed:\n");
    pr_info("    Parent Device Name    : %s\n", internal_data->name);
    pr_info("    Device Class /sys     : %s\n", current_class);
    pr_info("    Device Name /dev      : %s%d\n", current_name, internal_data->char_device_id);
    pr_info("    Type                  : %s\n", (current_type == READONLY_CHANNEL? "readonly" : "writeonly"));

    return 0;
}

static int muensk_channel_open(struct inode *inode, struct file *filp)
{
    struct muensk_channel_data *internal_data;

    /* read internal platform device data */
    internal_data = container_of(inode->i_cdev, struct muensk_channel_data, char_device);

    if (!internal_data) {
        pr_err("Muen SK Channel - failed to get internal data from inode\n");
        return -ENODEV;
    }

    /* check file permissions */
    switch (internal_data->type) {
        case READONLY_CHANNEL:
            if (!((filp->f_flags & O_ACCMODE) == O_RDONLY)) {
                pr_err("Muen SK Channel - illegal write access to read-only channel\n");
                return -EACCES;
            }
            break;
        case WRITEONLY_CHANNEL:
            if (!((filp->f_flags & O_ACCMODE) == O_WRONLY)) {
                pr_err("Muen SK Channel - illegal read access to write-only channel\n");
                return -EACCES;
            }
            break;
        default:
            pr_err("Muen SK Channel - illegal type argument\n");
            return -EINVAL;
    }

    /* ensure mutual exclusive process access */
    if (!mutex_trylock(&internal_data->device_lock)) {
        pr_warn("Muen SK Channel - character device is already in use\n");
        return -EBUSY;
    }

    pr_debug("Muen SK Channel - character device file opened:\n");
    pr_debug("    Parent Device Name    : %s\n", internal_data->name);
    pr_debug("    Device Name /dev      : %s%d\n", (internal_data->type == READONLY_CHANNEL? READONLY_CHANNEL_NAME : WRITEONLY_CHANNEL_NAME), internal_data->char_device_id);

    return 0;
}

static int muensk_channel_release(struct inode *inode, struct file *filp)
{
    struct muensk_channel_data *internal_data;

    /* read internal platform device data */
    internal_data = container_of(inode->i_cdev, struct muensk_channel_data, char_device);

    if (!internal_data) {
        pr_err("Muen SK Channel - failed to get internal data from inode\n");
        return -ENODEV;
    }

    /* release mutual exclusive process access */
    mutex_unlock(&internal_data->device_lock);

    pr_debug("Muen SK Channel - character device file released:\n");
    pr_debug("    Parent Device Name    : %s\n", internal_data->name);
    pr_debug("    Device Name /dev      : %s%d\n", (internal_data->type == READONLY_CHANNEL? READONLY_CHANNEL_NAME : WRITEONLY_CHANNEL_NAME), internal_data->char_device_id);

    return 0;
}

static ssize_t muensk_channel_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct muensk_channel_data *internal_data;
    char *device_buffer;
    void __iomem *start_address;

    /* read internal platform device data */
    internal_data = container_of(filp->f_inode->i_cdev, struct muensk_channel_data, char_device);

    if (!internal_data) {
        pr_err("Muen SK Channel - failed to get internal data from file\n");
        return -ENODEV;
    }

    /* check in channel range */
    if (!(*f_pos < internal_data->address_space_size)) {
        pr_debug("Muen SK Channel - offset outside channel size\n");
        return 0;
    }

    /* adjust byte count if needed */
    if (internal_data->address_space_size < (*f_pos + count)) {
        count = internal_data->address_space_size - *f_pos;
    }

    /* read from device */
    device_buffer = kmalloc(count, GFP_KERNEL);
    start_address = internal_data->virtual_base_address + *f_pos;

    memcpy_fromio(device_buffer, start_address, count);

    /* copy to user space */
    if (copy_to_user(buf, device_buffer, count)) {
        pr_err("Muen SK Channel - failed to copy buffer to user space\n");
        return -EIO;
    }

    /* advance file position */
    *f_pos += count;

    pr_debug("Muen SK Channel - character device file read:\n");
    pr_debug("    Parent Device Name    : %s\n", internal_data->name);
    pr_debug("    Device Name /dev      : %s%d\n", (internal_data->type == READONLY_CHANNEL? READONLY_CHANNEL_NAME : WRITEONLY_CHANNEL_NAME), internal_data->char_device_id);
    pr_debug("    Number of Bytes       : %ld\n", count);

    /* free buffer and return number of bytes read */
    kfree(device_buffer);
    return count;
}

static ssize_t muensk_channel_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct muensk_channel_data *internal_data;
    char *device_buffer;
    void __iomem *start_address;

    /* read internal platform device data */
    internal_data = container_of(filp->f_inode->i_cdev, struct muensk_channel_data, char_device);

    if (!internal_data) {
        pr_err("Muen SK Channel - failed to get internal data from file\n");
        return -ENODEV;
    }

    /* check in channel range */
    if (!(*f_pos < internal_data->address_space_size)) {
        pr_err("Muen SK Channel - offset outside channel size\n");
        return -EINVAL;
    }

    /* adjust byte count if needed */
    if (internal_data->address_space_size < (*f_pos + count)) {
        count = internal_data->address_space_size - *f_pos;
    }

    /* copy from user space */
    device_buffer = kmalloc(count, GFP_KERNEL);
    if (copy_from_user(device_buffer, buf, count)) {
        pr_err("Muen SK Channel - failed to copy buffer to user space\n");
        return -EFAULT;
    }

    /* write to device */
    start_address = internal_data->virtual_base_address + *f_pos;
    memcpy_toio(start_address, device_buffer, count);

    /* advance file position */
    *f_pos += count;

    pr_debug("Muen SK Channel - character device file written:\n");
    pr_debug("    Parent Device Name    : %s\n", internal_data->name);
    pr_debug("    Device Name /dev      : %s%d\n", (internal_data->type == READONLY_CHANNEL? READONLY_CHANNEL_NAME : WRITEONLY_CHANNEL_NAME), internal_data->char_device_id);
    pr_debug("    Number of Bytes       : %ld\n", count);

    /* free buffer and return number of bytes read */
    kfree(device_buffer);
    return count;
}

/*
 * platform device definitions
 */
static struct of_device_id muensk_channel_of_match[] = {
    {.compatible    = "muen,communication-channel",},
    {/* end of list */},
};
 
static struct platform_driver muensk_channel_driver = {
    .probe          = muensk_channel_probe,
    .remove         = muensk_channel_remove,
    .driver         = {
        .name               = "muen,communication-channel",
	    .owner              = THIS_MODULE,
	    .of_match_table     = muensk_channel_of_match,
    },
};
module_platform_driver(muensk_channel_driver);

/*
 * platform device functions
 */
static irqreturn_t muensk_channel_irq_handler(int irq, void *dev_id)
{
    pr_info("Muen SK Channel - IRQ not yet supported\n");
    return IRQ_HANDLED;
}

static int muensk_channel_probe(struct platform_device *pdev)
{
    struct resource *channel_resource;
    struct muensk_channel_data *internal_data;
    int err_value;

    /* setup internal platform device data */
    internal_data = kmalloc(sizeof(*internal_data), GFP_KERNEL);

    if (!internal_data) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to aquire memory for internal data\n");
        return -ENOMEM;
    }

    platform_set_drvdata(pdev, internal_data);
    internal_data->name = pdev->dev.of_node->full_name;
    mutex_init(&internal_data->device_lock);

    /* process memory register from device tree */
    channel_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    if (!channel_resource) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to read register resource (c.f. device tree)\n");
        err_value = -ENOMEM;
        goto err_platform_resource;
    }

    internal_data->physical_base_address = channel_resource->start;
    internal_data->address_space_size    = resource_size(channel_resource);
    internal_data->virtual_base_address  = devm_ioremap_resource(&pdev->dev, channel_resource);

    if(IS_ERR(internal_data->virtual_base_address)) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to I/O remap channel address space\n");
        err_value = -ENODEV;
        goto err_ioremap_resource;
    }

    /* process irq configuration from device tree */
    internal_data->irq_number = platform_get_irq(pdev, 0);

    if (internal_data->irq_number < 0) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to read irq configuration (c.f. device tree)\n");
        err_value = -ENOMEM;
        goto err_platform_irq;
    }

    err_value = devm_request_irq(&pdev->dev, internal_data->irq_number, &muensk_channel_irq_handler, 0, "MuenSK Channel xy", NULL);

    if(err_value) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to register IRQ number %#x\n", internal_data->irq_number);
        err_value = -ENODEV;
        goto err_request_irq;
    }

    /* process channel type configuration from device tree */
    err_value = of_property_read_u32(pdev->dev.of_node, "type", &internal_data->type);

    if (err_value ||
        !(internal_data->type == READONLY_CHANNEL || 
          internal_data->type == WRITEONLY_CHANNEL)) {
        dev_err(&pdev->dev, "Muen SK Channel - illegal channel type %#x (c.f. device tree)\n", internal_data->type);
        err_value = -ENODEV;
        goto err_platform_type;
    }

    pr_info("Muen SK Channel - device probe:\n");
    pr_info("    DTS Node Name /proc   : %s\n", internal_data->name);
    pr_info("    Physical Address Base : %#llx\n", internal_data->physical_base_address);
    pr_info("    Address Space Size    : %#llx\n", internal_data->address_space_size);
    pr_info("    IRQ number            : %d\n", internal_data->irq_number);
    pr_info("    Type                  : %s\n", (internal_data->type == READONLY_CHANNEL? "readonly" : "writeonly"));

    /* create character device */
    err_value = muensk_cdevice_create(pdev);

    if(err_value) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to create character device\n");
        goto err_cdev_create;
    }

    return 0;

err_cdev_create:

err_platform_type:
    devm_free_irq(&pdev->dev, internal_data->irq_number, pdev);

err_request_irq:

err_platform_irq:
    devm_iounmap(&pdev->dev, internal_data->virtual_base_address);

err_ioremap_resource:

err_platform_resource:
    kfree(internal_data);
    return err_value;
}

static int muensk_channel_remove(struct platform_device *pdev)
{
    struct muensk_channel_data *internal_data;

    /* remove character device */
    muensk_cdevice_remove(pdev);

    /* read internal platform device data */
    internal_data = platform_get_drvdata(pdev);

    if (!internal_data) {
        dev_err(&pdev->dev, "Muen SK Channel - failed to get internal data\n");
        return -ENODEV;
    }

    /* 
     * NOTE: due to devm calls in channel probe function and according to
     * https://www.kernel.org/doc/Documentation/driver-model/devres.txt
     * the unmap and free function calls can now be ommited (managed)
     */

    pr_info("Muen SK Channel - device remove:\n");
    pr_info("    DTS Node Name         : %s\n", internal_data->name);
    pr_info("    Physical Address Base : %#llx\n", internal_data->physical_base_address);
    pr_info("    Address Space Size    : %#llx\n", internal_data->address_space_size);
    pr_info("    IRQ number            : %d\n", internal_data->irq_number);
    pr_info("    Type                  : %s\n", (internal_data->type == READONLY_CHANNEL? "readonly" : "writeonly"));

    /* free internal device data */
    kfree(internal_data);

    return 0;
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("David Loosli <david.loosli@daves-treehouse.ch>");
MODULE_DESCRIPTION("Muen SK - communication channel driver");
/** end of channel-muensk.c */
