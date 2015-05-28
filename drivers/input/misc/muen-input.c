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
	struct input_dev *kbd;
	int irq;
};

static struct muen_dev *muen_input;

static struct muchannel *channel_in;
static struct muchannel_reader reader;

static irqreturn_t handle_muen_input_int(int rq, void *dev_id)
{
	struct muen_dev *input_dev = dev_id;
	struct muen_key_info info;

	while (muen_channel_read(channel_in, &reader, &info)
			== MUCHANNEL_SUCCESS) {
		input_report_key(input_dev->kbd, info.keycode, info.pressed);
		input_sync(input_dev->kbd);
	}

	return IRQ_HANDLED;
}

static struct resource muen_input_res = {
	.flags = IORESOURCE_IRQ,
};

static int __init muen_input_init(void)
{
	struct muen_channel_info channel;
	uint8_t irq_number;
	int i, error;

	if (!muen_get_channel_info("virtual_keyboard", &channel)) {
		pr_err("muen-input: Unable to retrieve input channel\n");
		return -EINVAL;
	}

	if (!channel.has_vector) {
		pr_err("muen-input: Unable to retrieve vector for input channel\n");
		return -EINVAL;
	}

	irq_number = channel.vector - IRQ0_VECTOR;
	pr_info("muen-input: Using input channel at address 0x%llx, IRQ %d\n",
		channel.address, irq_number);

	channel_in = (struct muchannel *)ioremap_cache(channel.address,
			channel.size);
	muen_input_res.start = irq_number;
	muen_input_res.end   = irq_number;

	muen_input = kzalloc(sizeof(struct muen_dev), GFP_KERNEL);
	if (!muen_input)
		return -ENOMEM;

	muen_input->pdev = platform_device_register_simple("muen-input", -1,
							 &muen_input_res, 1);
	if (IS_ERR(muen_input->pdev)) {
		pr_err("muen-input: Unable to allocate platform device\n");
		error = -ENODEV;
		goto error_register_pdev;
	}

	muen_input->irq = irq_number;
	muen_input->kbd = input_allocate_device();
	if (!muen_input->kbd) {
		pr_err("muen-input: Unable to allocate keyboard input device\n");
		error = -ENOMEM;
		goto error_alloc_kbd;
	}

	muen_input->kbd->name = "Muen Virtual Keyboard";
	muen_input->kbd->phys = "muen-input/input0";
	muen_input->kbd->id.bustype = BUS_HOST;
	muen_input->kbd->id.vendor = 0x0001;
	muen_input->kbd->id.product = 0x0001;
	muen_input->kbd->id.version = 0x0001;

	muen_input->kbd->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);

	for (i = KEY_ESC; i < KEY_UNKNOWN; i++)
		__set_bit(i, muen_input->kbd->keybit);
	for (i = KEY_OK; i < KEY_MAX; i++)
		__set_bit(i, muen_input->kbd->keybit);

	error = request_irq(muen_input->irq, handle_muen_input_int, 0,
			    "muen-input", muen_input);
	if (error) {
		pr_err("muen-input: Unable to request IRQ %d\n",
		       muen_input->irq);
		goto error_request_irq;
	}

	error = input_register_device(muen_input->kbd);
	if (error) {
		pr_err("muen-input: Unable to register keyboard as input device\n");
		goto error_register_kbd;
	}

	muen_channel_init_reader(&reader, 2);

	return 0;

error_register_kbd:
	free_irq(muen_input->irq, muen_input);
error_request_irq:
	input_free_device(muen_input->kbd);
error_alloc_kbd:
	platform_device_unregister(muen_input->pdev);
error_register_pdev:
	kfree(muen_input);
	iounmap(channel_in);
	return error;
}

static void __exit muen_input_cleanup(void)
{
	free_irq(muen_input->irq, muen_input);
	input_unregister_device(muen_input->kbd);
	platform_device_unregister(muen_input->pdev);
	kfree(muen_input);
	iounmap(channel_in);
}

module_init(muen_input_init);
module_exit(muen_input_cleanup);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen virtual input device");
MODULE_LICENSE("GPL");
