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


#ifndef _CHANNEL_MUENSK_H
#define _CHANNEL_MUENSK_H

#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define NUMBER_OF_TYPES			2

#define READONLY_CHANNEL		0
#define WRITEONLY_CHANNEL		1

#define READONLY_CHANNEL_NAME	"mrchan"
#define WRITEONLY_CHANNEL_NAME	"mwchan"

#define READONLY_CHANNEL_CLASS	"mrclass"
#define WRITEONLY_CHANNEL_CLASS	"mwclass"

/*
 * channel driver struct
 */
struct muensk_channel_data {
	unsigned int type;
	int irq_number;
	resource_size_t physical_base_address;
	resource_size_t address_space_size;
	void __iomem *virtual_base_address;
	struct mutex device_lock;
	int char_device_id;
	struct cdev char_device;
	const char *name;
};

/*
 * function prototypes
 */
static int muensk_channel_open(struct inode *inode, struct file *filp);
static int muensk_channel_release(struct inode *inode, struct file *filp);
static ssize_t muensk_channel_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static ssize_t muensk_channel_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);

static irqreturn_t muensk_channel_irq_handler(int irq, void *dev_id);
static int muensk_channel_probe(struct platform_device *pdev);
static int muensk_channel_remove(struct platform_device *pdev);

#endif /* _CHANNEL_MUENSK_H */

/** end of channel-muensk.h */
