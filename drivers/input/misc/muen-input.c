/*
 * Copyright (C) 2013-2015  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2013-2015  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <muen/reader.h>
#include <muen/sinfo.h>

struct muen_key_info {
	uint8_t keycode;	/* KEY_* value, as specified in linux/input.h */
	uint8_t pressed;	/* 1 if key way pressed, 0 otherwise */
} __packed;

struct muen_dev {
	struct platform_device *pdev;
	struct input_dev *dev;
	int irq;
};

static struct muen_dev *muen_kbd;

static struct muchannel *channel_in;
static struct muchannel_reader reader;

static irqreturn_t handle_muen_kbd_int(int rq, void *dev_id)
{
	struct muen_dev *kbd = dev_id;
	struct muen_key_info info;

	while (muen_channel_read(channel_in, &reader, &info)
			== MUCHANNEL_SUCCESS) {
		input_report_key(kbd->dev, info.keycode, info.pressed);
		input_sync(kbd->dev);
	}

	return IRQ_HANDLED;
}

static struct resource muen_kbd_res = {
	.flags = IORESOURCE_IRQ,
};

static int __init muen_kbd_init(void)
{
	struct muen_channel_info channel;
	uint8_t irq_number;
	int i, error;

	if (!muen_get_channel_info("virtual_keyboard", &channel)) {
		pr_err("muen-kbd: Unable to retrieve keyboard channel\n");
		return -EINVAL;
	}

	if (!channel.has_vector) {
		pr_err("muen-kbd: Unable to retrieve vector for keyboard channel\n");
		return -EINVAL;
	}

	irq_number = channel.vector - IRQ0_VECTOR;
	pr_info("muen-kbd: Using keyboard channel at address 0x%llx, IRQ %d\n",
			channel.address, irq_number);

	channel_in = (struct muchannel *)ioremap_cache(channel.address,
			channel.size);
	muen_kbd_res.start = irq_number;
	muen_kbd_res.end   = irq_number;

	muen_kbd = kzalloc(sizeof(struct muen_dev), GFP_KERNEL);
	if (!muen_kbd)
		return -ENOMEM;

	muen_kbd->pdev = platform_device_register_simple("muen-kbd", -1,
							 &muen_kbd_res, 1);
	if (IS_ERR(muen_kbd->pdev)) {
		pr_err("muen-kbd: Unable to allocate platform device");
		return -ENODEV;
	}

	muen_kbd->irq = irq_number;
	muen_kbd->dev = input_allocate_device();
	if (!muen_kbd->dev)
		return -ENOMEM;

	muen_kbd->dev->name = "Muen Virtual Keyboard";
	muen_kbd->dev->phys = "muen-kbd/input0";
	muen_kbd->dev->id.bustype = BUS_HOST;
	muen_kbd->dev->id.vendor = 0x0001;
	muen_kbd->dev->id.product = 0x0001;
	muen_kbd->dev->id.version = 0x0001;

	muen_kbd->dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);

	for (i = KEY_ESC; i < KEY_UNKNOWN; i++)
		__set_bit(i, muen_kbd->dev->keybit);
	for (i = KEY_OK; i < KEY_MAX; i++)
		__set_bit(i, muen_kbd->dev->keybit);

	error = request_irq(muen_kbd->irq, handle_muen_kbd_int, 0, "muen-kbd",
			    muen_kbd);
	if (error)
		return error;

	muen_channel_init_reader(&reader, 2);

	error = input_register_device(muen_kbd->dev);
	if (error) {
		pr_err("muen-kbd: Unable to register input device");
		input_free_device(muen_kbd->dev);
		platform_device_unregister(muen_kbd->pdev);
		kfree(muen_kbd);
		return error;
	}

	return 0;
}

static void __exit muen_kbd_cleanup(void)
{
	input_unregister_device(muen_kbd->dev);
	platform_device_unregister(muen_kbd->pdev);
	kfree(muen_kbd);
	iounmap(channel_in);
}

module_init(muen_kbd_init);
module_exit(muen_kbd_cleanup);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen virtual keyboard device");
MODULE_LICENSE("GPL");
