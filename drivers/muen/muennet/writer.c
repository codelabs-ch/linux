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

#include <linux/inetdevice.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <muen/writer.h>
#include <muen/smp.h>

#include "internal.h"

/**
 * @file writer.c
 * @brief Functions for the writer part of the networking interface.
 *
 * The functions in this file implement the writer initialization and cleanup
 * as well as the actual transmit function for a network interface.
 */

static size_t gross_packet_size(size_t net_size, unsigned long flags)
{
	size_t element_size = net_size;

	/* raw / annotated IP transfers? */
	if (flags & MUENNET_HDR)
		element_size += sizeof(struct net_hdr);
	else if (flags & ETH_DEV)
		element_size += ETH_HLEN + sizeof(struct eth_hdr);

	return element_size;
}

void writer_down(struct dev_info *dev_info)
{
	if (dev_info->writer_element_size > 0)
		muen_channel_deactivate(dev_info->channel_out);

	dev_info->writer_element_size = 0;
}

void writer_up(struct dev_info *dev_info)
{
	u64 epoch;

	get_random_bytes(&epoch, sizeof(epoch));

	dev_info->writer_element_size = gross_packet_size(dev_info->mtu,
		dev_info->flags);
	muen_channel_init_writer
		(dev_info->channel_out,
		 dev_info->writer_protocol,
		 dev_info->writer_element_size,
		 dev_info->writer_region_size,
		 epoch);
	netdev_info(dev_info->dev,
		    "Using protocol %llu, channel/element size 0x%zx/0x%zx bytes\n",
		    dev_info->writer_protocol, dev_info->writer_region_size,
		    dev_info->writer_element_size);
}

/**
 * @brief Cleanup writer.
 *
 * This function releases all the memory allocated for the writer.
 *
 * @param dev_info private information stored in the network interface
 */
void cleanup_writer(struct dev_info *dev_info)
{
	if (dev_info->channel_out)
		writer_down(dev_info);
	iounmap(dev_info->channel_out);
	dev_info->channel_out = NULL;
}

/**
 * @brief Initialize writer.
 *
 * This function initializes a writer for the given memory region.
 *
 * The size of the elements is calculated based on the MTU configured for this
 * networking interface and whether the net_hdr transfer mode is used. The
 * number of elements is limited by the maximum region size. Both reader and
 * writer operating on the same memory region must be configured in a
 * consistent way.
 *
 * @param dev_info the private information stored in the network interface
 * @param region   memory region info structure
 *
 * @return 0 on success
 * @return -EPERM  if channel is not writable
 * @return -EFAULT if ioremap fails
 * @return -ENOMEM if memory allocation failed
 * @return errors returned by #common_check_region
 */
int initialize_writer(struct dev_info *dev_info,
		      const struct muen_resource_type *const channel,
		      const struct muen_resource_type *const pmtu_channel)
{
	struct muen_cpu_affinity evt_vec;

	/* some sanity checks */
	if (channel->data.mem.kind != MUEN_MEM_SUBJ_CHANNEL) {
		netdev_err(dev_info->dev, "Memory '%s' not a channel\n",
			   channel->name.data);
		return -EPERM;
	}
	if (!(channel->data.mem.flags & MEM_WRITABLE_FLAG)) {
		netdev_err(dev_info->dev, "Writer channel '%s' not writable\n",
			   channel->name.data);
		return -EPERM;
	}

	dev_info->writer_region_size = channel->data.mem.size;

	if (muen_smp_one_match(&evt_vec, channel->name.data, MUEN_RES_EVENT)) {
		dev_info->writer_event = evt_vec.res.data.number;
		dev_info->writer_cpu = evt_vec.cpu;
	} else {
		dev_info->writer_event = -1;
		dev_info->writer_cpu = -1;
	}

	/* writer_element_size is determined when the interface is set up */
	dev_info->writer_element_size = 0;

	/* now remember the start of the region and initialize it */
	dev_info->channel_out = ioremap_cache(channel->data.mem.address,
					      channel->data.mem.size);
	if (dev_info->channel_out == NULL) {
		netdev_err(dev_info->dev, "Unable to map writer channel\n");
		return -EFAULT;
	}

	if (pmtu_channel != NULL) {
		dev_info->pmtu_elements = pmtu_channel->data.mem.size
			/ sizeof(u32);

		dev_info->pmtu = ioremap_cache(pmtu_channel->data.mem.address,
					       pmtu_channel->data.mem.size);
		if (dev_info->channel_out == NULL) {
			netdev_err(dev_info->dev,
				   "Unable to map writer PMTU channel\n");
			return -EFAULT;
		}
	} else {
		dev_info->pmtu_elements = 0;
		dev_info->pmtu = NULL;
	}

	/* initialize the lock */
	spin_lock_init(&dev_info->writer_lock);

	return 0;
}

static int muennet_xmit_aux(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	return muennet_xmit(skb, skb->dev);
}

/**
 * @brief Transmit network packet.
 *
 * This function transmits the network packet to the memory region associated
 * with the given networking device.
 *
 * If no memory region is associated to this writer, the packet is dropped.
 * Otherwise it is written to the memory region.
 *
 * For locking between multiple writers the #dev_info::writer_lock is used.
 *
 * @param skb the network packet to transmit
 * @param dev the networking interface to use
 *
 * @return NET_XMIT_SUCCESS if packet could be transmitted or was dropped
 */
int muennet_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dev_info *child_dev_info = netdev_priv(dev);
	struct dev_info *dev_info = parent_dev(child_dev_info);
	unsigned long flags, max_data_size;
	int skb_data_len;

	/* check if writing is possible */
	if (!dev_info->writer_element_size) {
		child_dev_info->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return NET_XMIT_SUCCESS;
	}

	/* check for supported network protocols */
	if (dev_info->flags & MUENNET_HDR) {
		if (skb->protocol != htons(ETH_P_IP) &&
		    skb->protocol != htons(ETH_P_IPV6)) {
			child_dev_info->stats.tx_dropped++;
			dev_kfree_skb(skb);
			return NET_XMIT_SUCCESS;
		}
	}

	spin_lock_irqsave(&dev_info->writer_lock, flags);

	if (dev_info->pmtu != NULL &&
	    skb->mark >= 1 && skb->mark <= dev_info->pmtu_elements) {
		u32 pmtu = dev_info->pmtu[skb->mark - 1];

		if (skb->len > pmtu) {
			struct iphdr *iph;
			struct flowi4 fl4;
			struct rtable *rt = NULL;

			switch (skb->protocol) {
			case htons(ETH_P_IP):
				spin_unlock_irqrestore(&dev_info->writer_lock,
						       flags);
				iph = ip_hdr(skb);

				if ((iph->frag_off & htons(IP_DF)) == 0) {
					IPCB(skb)->frag_max_size = pmtu;
					ip_do_fragment(dev_net(skb->dev), NULL,
						       skb, muennet_xmit_aux);
				} else {
					memset(&fl4, 0, sizeof(fl4));
					fl4.flowi4_oif = dev->ifindex;
					fl4.flowi4_tos = RT_TOS(iph->tos);
					fl4.daddr = iph->daddr;
					fl4.saddr = inet_select_addr(dev, iph->saddr,
								     RT_SCOPE_UNIVERSE);

					rt = ip_route_output_key(dev_net(dev),
								 &fl4);
					if (!IS_ERR(rt)) {
						skb_dst_set(skb, &rt->dst);
						icmp_send(skb,
							  ICMP_DEST_UNREACH,
							  ICMP_FRAG_NEEDED,
							  htonl(pmtu));
					} else
						netdev_err(child_dev_info->dev,
							   "Route lookup for ICMP failed (dst: %pI4, src: %pI4)\n",
							   &fl4.daddr, &fl4.saddr);

					dev_kfree_skb(skb);
				}
				return NET_XMIT_SUCCESS;
			case htons(ETH_P_IPV6):
				spin_unlock_irqrestore(&dev_info->writer_lock,
						       flags);
				icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, pmtu);
				dev_kfree_skb(skb);
				return NET_XMIT_SUCCESS;
			}
		}
	}

	max_data_size = dev_info->writer_element_size;
	skb_data_len = skb->len;

	if (dev_info->flags & MUENNET_HDR) {
		struct net_hdr *hdr;

		hdr = (struct net_hdr *)skb_push(skb, sizeof(struct net_hdr));

		hdr->mark = skb->mark;
		hdr->length = skb_data_len;

		switch (skb->protocol) {
		case htons(ETH_P_IP):
			hdr->protocol = IPPROTO_IPIP;
			hdr->qos = ip_hdr(skb)->tos >> 2;
			break;
		case htons(ETH_P_IPV6):
			hdr->protocol = IPPROTO_IPV6;
			hdr->qos = ip6_tclass(ip6_flowinfo(ipv6_hdr(skb))) >> 2;
			break;
		default:
			hdr->protocol = 0;
			hdr->qos = 0;
			break;
		}
	} else if (dev_info->flags & ETH_DEV) {
		max_data_size -= sizeof(struct eth_hdr);
		if (unlikely(skb_tailroom(skb) < sizeof(struct eth_hdr))) {
			struct sk_buff *nskb;

			nskb = skb_copy_expand(skb, 0,
					       sizeof(struct eth_hdr),
					       GFP_ATOMIC);
			if (likely(nskb)) {
				dev_kfree_skb(skb);
				skb = nskb;
			} else {
				netdev_warn(child_dev_info->dev,
					    "Oversized packet dropped (size = %u, tail = %u, MTU = %u)\n",
					    skb_data_len, skb_tailroom(skb),
					    dev_info->mtu);
				child_dev_info->stats.tx_dropped++;
				spin_unlock_irqrestore(&dev_info->writer_lock,
						       flags);
				dev_kfree_skb(skb);
				return NET_XMIT_SUCCESS;
			}
		}
	}

	if (skb->len > max_data_size) {
		netdev_warn(child_dev_info->dev,
			    "Oversized packet dropped (size = %u, max = %lu, MTU = %u)\n",
			    skb->len, max_data_size, dev_info->mtu);
		child_dev_info->stats.tx_dropped++;
		spin_unlock_irqrestore(&dev_info->writer_lock, flags);
		dev_kfree_skb(skb);
		return NET_XMIT_SUCCESS;
	}

	skb_padto(skb, dev_info->writer_element_size);

	if (dev_info->flags & ETH_DEV) {
		struct eth_hdr *hdr;

		hdr = (struct eth_hdr *)(skb->data - sizeof(struct eth_hdr) +
					 dev_info->writer_element_size);
		hdr->length = skb_data_len;
	}

	muen_channel_write(dev_info->channel_out, skb->data);
	if (dev_info->writer_event >= 0)
		muen_smp_trigger_event(dev_info->writer_event, dev_info->writer_cpu);

	/* update stats */
	child_dev_info->stats.tx_packets++;
	child_dev_info->stats.tx_bytes += skb->len;

	/* end of protected section */
	spin_unlock_irqrestore(&dev_info->writer_lock, flags);

	dev_kfree_skb(skb);
	return NET_XMIT_SUCCESS;
}
