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

#define MUEN_PROTO_INPUT 0x9a0a8679dbc22dcbULL

/**
 * Muen input event types
 */
enum muen_event_type {
	MUEN_EV_RESET = 0,
	MUEN_EV_MOTION,
	MUEN_EV_WHEEL,
	MUEN_EV_PRESS,
	MUEN_EV_RELEASE,
};

/**
 * Muen input event information
 *
 * Objects of this type are read from the memory channel.
 */
struct muen_input_event {
	uint32_t event_type;
	uint32_t keycode;	/* KEY_* value, as specified in linux/input.h */
	int32_t  rel_x;		/* Relative pointer motion on X-Axis          */
	int32_t  rel_y;		/* Relative pointer motion on Y-Axis          */
	uint32_t led_state;	/* State of keyboard LEDs                     */
	uint32_t key_count;	/* Number of key repetitions                  */
} __packed;

/**
 * Muen input device
 */
struct muen_dev {
	struct platform_device *pdev;
	struct input_dev *kbd;
	struct input_dev *ptr;
	int irq;
	struct muchannel_reader reader;
	struct muchannel *channel;
};

static struct muen_dev *muen_input;

static char *input_channel_name = "virtual_input";
module_param_named(channel, input_channel_name, charp, 0444);
MODULE_PARM_DESC(channel,
"Name of memory region that provides input events (Default: virtual_input)");

static void process_input(struct muen_dev *input_dev,
			  struct muen_input_event info)
{
	struct input_dev *dev = NULL;
	bool key_press = false;

	switch (info.event_type) {
	case MUEN_EV_RESET:
		/* XXX: ignored */
		break;
	case MUEN_EV_MOTION:
		if (info.rel_x != 0)
			input_report_rel(input_dev->ptr, REL_X, info.rel_x);
		if (info.rel_y != 0)
			input_report_rel(input_dev->ptr, REL_Y, info.rel_y);
		input_sync(input_dev->ptr);
		break;
	case MUEN_EV_WHEEL:
		if (info.rel_x != 0)
			input_report_rel(input_dev->ptr, REL_HWHEEL,
					 info.rel_x);
		if (info.rel_y != 0)
			input_report_rel(input_dev->ptr, REL_WHEEL,
					 info.rel_y);
		input_sync(input_dev->ptr);
		break;
	case MUEN_EV_PRESS:
		key_press = true;
	case MUEN_EV_RELEASE:
		if (info.keycode < BTN_LEFT)
			dev = input_dev->kbd;
		if (info.keycode >= BTN_LEFT)
			dev = input_dev->ptr;
		if (dev) {
			input_report_key(dev, info.keycode, key_press);
			input_sync(dev);
		} else
			pr_warn("muen-input: Unhandled keycode 0x%x\n",
				info.keycode);
		break;
	default:
		pr_warn("muen-input: Unknown event type %d\n", info.event_type);
	}
}

static irqreturn_t handle_muen_input_int(int rq, void *dev_id)
{
	struct muen_dev *input_dev = dev_id;
	struct muen_input_event info;
	enum muchannel_reader_result res;
	bool pending_data = true;

	while (pending_data) {
		res = muen_channel_read(input_dev->channel, &input_dev->reader,
					&info);
		switch (res) {
		case MUCHANNEL_SUCCESS:
			process_input(input_dev, info);
			break;
		case MUCHANNEL_EPOCH_CHANGED:
			pr_debug("muen-input: Channel epoch changed\n");
			break;
		case MUCHANNEL_OVERRUN_DETECTED:
			pr_warn("muen-input: Channel overrun\n");
			muen_channel_drain(muen_input->channel,
					   &muen_input->reader);
			break;
		case MUCHANNEL_INCOMPATIBLE_INTERFACE:
			pr_err("muen-input: Incompatible channel interface\n");
			/* fall through */
		case MUCHANNEL_NO_DATA:
		case MUCHANNEL_INACTIVE:
		default:
			pending_data = false;
			break;
		}
	}

	return IRQ_HANDLED;
}

static struct resource muen_input_res = {
	.flags = IORESOURCE_IRQ,
};

static int __init muen_input_init(void)
{
	struct muen_channel_info channel;
	struct muen_input_event ev;
	uint8_t irq_number;
	int i, error;

	if (!muen_get_channel_info(input_channel_name, &channel)) {
		pr_err("muen-input: Unable to retrieve input channel '%s'\n",
		       input_channel_name);
		return -EINVAL;
	}

	if (!channel.has_vector) {
		pr_err("muen-input: Unable to retrieve vector for input channel '%s'\n",
		       input_channel_name);
		return -EINVAL;
	}

	muen_input = kzalloc(sizeof(struct muen_dev), GFP_KERNEL);
	if (!muen_input)
		return -ENOMEM;

	irq_number = channel.vector - ISA_IRQ_VECTOR(0);
	pr_info("muen-input: Using input channel '%s' at address 0x%llx, IRQ %d\n",
		input_channel_name, channel.address, irq_number);

	muen_input_res.start = irq_number;
	muen_input_res.end   = irq_number;

	muen_input->channel = (struct muchannel *)ioremap_cache(channel.address,
								channel.size);

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

	/* pointing/mouse device */
	muen_input->ptr = input_allocate_device();
	if (!muen_input->ptr) {
		pr_err("muen-input: Unable to allocate mouse input device\n");
		error = -ENOMEM;
		goto error_alloc_ptr;
	}

	muen_input->ptr->name = "Muen Virtual Pointer";
	muen_input->ptr->phys = "muen-input/input1";
	muen_input->ptr->id.bustype = BUS_HOST;
	muen_input->ptr->id.vendor = 0x0001;
	muen_input->ptr->id.product = 0x0001;
	muen_input->ptr->id.version = 0x0001;

	input_set_capability(muen_input->ptr, EV_REL, REL_X);
	input_set_capability(muen_input->ptr, EV_REL, REL_Y);
	input_set_capability(muen_input->ptr, EV_REL, REL_WHEEL);
	input_set_capability(muen_input->ptr, EV_REL, REL_HWHEEL);

	__set_bit(EV_KEY, muen_input->ptr->evbit);
	for (i = BTN_LEFT; i <= BTN_TASK; i++)
		__set_bit(i, muen_input->ptr->keybit);

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

	error = input_register_device(muen_input->ptr);
	if (error) {
		pr_err("muen-input: Unable to register mouse as input device\n");
		goto error_register_ptr;
	}

	/* Initialize reader and discard all previous data */
	muen_channel_init_reader(&muen_input->reader, MUEN_PROTO_INPUT);
	muen_channel_read(muen_input->channel, &muen_input->reader, &ev);
	muen_channel_drain(muen_input->channel, &muen_input->reader);

	return 0;

error_register_ptr:
	input_unregister_device(muen_input->kbd);
error_register_kbd:
	free_irq(muen_input->irq, muen_input);
error_request_irq:
	if (muen_input->ptr)
		input_free_device(muen_input->ptr);
error_alloc_ptr:
	if (muen_input->kbd)
		input_free_device(muen_input->kbd);
error_alloc_kbd:
	platform_device_unregister(muen_input->pdev);
error_register_pdev:
	iounmap(muen_input->channel);
	kfree(muen_input);
	return error;
}

static void __exit muen_input_cleanup(void)
{
	free_irq(muen_input->irq, muen_input);
	input_unregister_device(muen_input->ptr);
	input_unregister_device(muen_input->kbd);
	platform_device_unregister(muen_input->pdev);
	iounmap(muen_input->channel);
	kfree(muen_input);
}

module_init(muen_input_init);
module_exit(muen_input_cleanup);

MODULE_AUTHOR("Reto Buerki <reet@codelabs.ch>");
MODULE_AUTHOR("Adrian-Ken Rueegsegger <ken@codelabs.ch>");
MODULE_DESCRIPTION("Muen virtual input device");
MODULE_LICENSE("GPL");
