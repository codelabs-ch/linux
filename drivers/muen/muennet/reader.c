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

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/workqueue.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <muen/smp.h>

#include "internal.h"

/**
 * @file reader.c
 * @brief Implementation of reader part.
 *
 * This file implements the functions to setup and cleanup the reader part of
 * the network interface as well as the work queue function responsible for
 * polling the channel for new data and injecting them into the network stack.
 */

/**
 * @brief Cleanup reader.
 *
 * This function frees all previously allocated memory and removes delayed
 * reader work from the work queue.
 *
 * @param dev_info private information stored in the network interface
 */
void cleanup_reader(struct dev_info *dev_info)
{
	dev_info->reader_element_size = 0;

	if (dev_info->reader_irq >= 0)
		free_irq(dev_info->reader_irq, dev_info);
	dev_info->reader_irq = -1;

	cancel_delayed_work_sync(&dev_info->reader_work);

	iounmap(dev_info->channel_in);
	dev_info->channel_in = NULL;
}

/**
 * @brief Allocates an skb.
 *
 * This little utility function allocates a new skb.
 *
 * @param skb      address of skb to allocate
 * @param dev_info contains reader_element_size and flags from which the size
 *                 of the skb is derived
 *
 * @return 0 on success
 * @return -ENOMEM if memory allocation failed
 */
static int get_skb(struct sk_buff **skb, struct dev_info *dev_info)
{
	size_t size = dev_info->reader_element_size;
	struct sk_buff *new_skb;

	if (dev_info->flags & MUENNET_HDR)
		size += sizeof(struct net_hdr);
	else if (dev_info->flags & ETH_DEV)
		size += sizeof(struct eth_hdr);

	new_skb = alloc_skb(size, GFP_KERNEL);
	if (new_skb == NULL)
		return -ENOMEM;

	*skb = new_skb;
	return 0;
}

/**
 * @brief Reader work queue function
 *
 * This work queue function will query the memory region for new data.
 *
 * As long as data is available, it will be read and injected into the
 * networking code. Between successive reads, re-schedule to give the kernel
 * the possibility to run other tasks.
 *
 * If no data is available, the reader work will be re-scheduled with a timeout
 * as specified by the #dev_info::poll_interval value.
 *
 * @param work work struct used to retrieve #dev_info for this interface
 */
static void muennet_reader_work(struct work_struct *work)
{
	struct dev_info *dev_info =
		container_of(work, struct dev_info, reader_work.work);
	struct dev_info *child_dev_info = dev_info;
	struct sk_buff *skb = NULL;

	enum muchannel_reader_result result = MUCHANNEL_SUCCESS;

	while (result == MUCHANNEL_SUCCESS ||
			result == MUCHANNEL_OVERRUN_DETECTED) {
		uint32_t len;
		__be16 protocol = 0;
		struct iphdr *ipv4_hdr;
		struct ipv6hdr *ipv6_hdr;
		unsigned long flags;

		if (get_skb(&skb, dev_info)) {
			netdev_warn(dev_info->dev,
				    "Failed to allocate skb\n");
			break;
		}

		result = muen_channel_read(dev_info->channel_in,
					   &dev_info->reader,
					   skb->data);

		switch (result) {
		case MUCHANNEL_EPOCH_CHANGED:
			/* TODO: Check protocol */
			dev_info->reader_element_size = dev_info->reader.size;

			if (dev_info->reader_element_size > 0x100000) {
				netdev_err(dev_info->dev,
					   "Element size to big %zu\n",
					   dev_info->reader_element_size);
				dev_info->reader_element_size = 0;
				goto schedule;
			} else {
				consume_skb(skb);
				skb = NULL;
			}
			break;

		case MUCHANNEL_OVERRUN_DETECTED:
			dev_info->stats.rx_errors++;
			dev_info->stats.rx_over_errors++;
			dev_kfree_skb(skb);
			break;

		case MUCHANNEL_SUCCESS:
			/* check if net_hdr flag is given */
			if (dev_info->flags & MUENNET_HDR) {
				struct net_hdr *hdr =
					(struct net_hdr *)skb->data;

				skb->mark = hdr->mark;

				spin_lock_irqsave(&dev_info->children_lock,
						  flags);

				if (skb->mark >= 1 &&
				    skb->mark <= dev_info->child_elements)
					child_dev_info = dev_info->children[skb->mark - 1];

				skb_reserve(skb, sizeof(struct net_hdr));
				switch (hdr->protocol) {
				case IPPROTO_IPIP:
					ipv4_hdr = (struct iphdr *)skb->data;
					protocol = htons(ETH_P_IP);
					len = be16_to_cpu(ipv4_hdr->tot_len);
					break;
				case IPPROTO_IPV6:
					ipv6_hdr = (struct ipv6hdr *)skb->data;
					protocol = htons(ETH_P_IPV6);
					len = be16_to_cpu(ipv6_hdr->payload_len)
						+ 40;
					break;
				}
			} else if (dev_info->flags & ETH_DEV) {
				struct eth_hdr *hdr = (struct eth_hdr *)
					(skb->data +
					 dev_info->reader_element_size -
					 sizeof(struct eth_hdr));
				skb_put(skb, hdr->length);
				protocol = eth_type_trans(skb, dev_info->dev);
				len = 0;
			} else
				len = dev_info->reader_element_size;

			if (len > dev_info->reader_element_size ||
					len > skb_tailroom(skb)) {
				netdev_warn(child_dev_info->dev,
					    "Invalid length: %u\n",
					    (unsigned int)len);
				child_dev_info->stats.rx_errors++;
				child_dev_info->stats.rx_frame_errors++;
				if (dev_info->flags & MUENNET_HDR)
					spin_unlock_irqrestore(&dev_info->children_lock,
							       flags);
				goto schedule;
			}

			/*
			 * now the skb is ready to be processed, but correct
			 * some data first
			 */
			skb->dev = child_dev_info->dev;
			if (len > 0)
				skb_put(skb, len);
			skb->protocol = protocol;

			/* process and update stats */
			child_dev_info->stats.rx_packets++;
			child_dev_info->stats.rx_bytes += skb->len;
			if (dev_info->flags & MUENNET_HDR)
				spin_unlock_irqrestore(&dev_info->children_lock,
						       flags);

			netif_rx(skb);

			/* now mark the skb as processed */
			skb = NULL;

			/* allow the kernel to run other tasks */
			schedule();
			break;

		default:
			goto schedule;
		}
	}

schedule:
	dev_kfree_skb(skb);

	if (dev_info->reader_irq < 0)
		schedule_delayed_work
			(&dev_info->reader_work,
			 usecs_to_jiffies(dev_info->poll_interval * 1000));
}

/**
 * @brief Reader interrupt handler.
 *
 * Receive path interrupt handler. Returns immediately after scheduling reader
 * work bottom halve.
 *
 * @param irq  irq to handle (unused)
 * @param data dev_info struct of the associated interface
 * @return always returns IRQ_HANDLED
 */
static irqreturn_t muennet_intr_rx(int __always_unused irq, void *data)
{
	struct dev_info *dev_info = data;

	if (!delayed_work_pending(&dev_info->reader_work))
		schedule_delayed_work(&dev_info->reader_work, 0);

	return IRQ_HANDLED;
}

/**
 * @brief Initialize reader.
 *
 * This function initializes the reader part of the networking interface. It
 * uses the channel info struct to setup the shared memory channel and
 * initializes the reader work function.
 *
 * The size of the elements is calculated based on the MTU configured for this
 * networking interface. The number of elements is limited by the maximum
 * region size. Both reader and writer using the same memory region must be
 * configured in a consistent way.
 *
 * @param dev_info the private information of the networking interface
 * @param region   the memory region to use for reading
 * @return 0 on success
 * @return -EPERM  if channel is writable
 * @return -EFAULT if ioremap fails
 * @return -EIO    if requesting an IRQ fails
 */
int initialize_reader(struct dev_info *dev_info,
		      const struct muen_resource_type *const channel)
{
	struct muen_cpu_affinity evt_vec;

	if (channel->data.mem.kind != MUEN_MEM_SUBJ_CHANNEL) {
		netdev_err(dev_info->dev, "Memory '%s' not a channel\n",
			   channel->name.data);
		return -EPERM;
	}
	if (channel->data.mem.flags & MEM_WRITABLE_FLAG) {
		netdev_err(dev_info->dev, "Reader channel '%s' writable\n",
			   channel->name.data);
		return -EPERM;
	}

	dev_info->reader_element_size = 0;

	dev_info->channel_in = ioremap_cache(channel->data.mem.address,
					     channel->data.mem.size);
	if (dev_info->channel_in == NULL) {
		netdev_err(dev_info->dev, "Unable to map reader channel\n");
		return -EFAULT;
	}

	muen_channel_init_reader(&dev_info->reader, dev_info->reader_protocol);

	INIT_DELAYED_WORK(&dev_info->reader_work, muennet_reader_work);

	dev_info->reader_irq = -1;

	if (muen_smp_one_match(&evt_vec, channel->name.data, MUEN_RES_VECTOR)) {
		dev_info->reader_irq =
			evt_vec.res.data.number /* - ISA_IRQ_VECTOR(0)*/;

		if (irq_has_action(dev_info->reader_irq)) {
			netdev_err(dev_info->dev, "IRQ %d already in use\n",
				   dev_info->reader_irq);
			dev_info->reader_irq = -1;
			return -EIO;
		}

		if (request_irq(dev_info->reader_irq, muennet_intr_rx,
				IRQF_SHARED, dev_info->dev->name, dev_info)) {
			netdev_err(dev_info->dev, "Unable to request IRQ %d\n",
				   dev_info->reader_irq);
			dev_info->reader_irq = -1;
			return -EIO;
		}
	}

	return 0;
}
