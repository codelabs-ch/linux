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

#ifndef INTERNAL_H_
#define INTERNAL_H_

#include <linux/netdevice.h>
#include <muen/sinfo.h>
#include <muen/reader.h>

/**
 * @file internal.h
 * @brief Interface between the modules
 * @defgroup common Commonly used functions
 *
 * This module contains mainly the data structure that is shared between the
 * other components. The transfer mechanism (muchannel) uses a fixed packet
 * size. This size is directly used for raw mode transfers. To support IP
 * traffic, the net_hdr protocol must be used to output a specific header
 * before the actual data. In this mode the networking interface determines the
 * length from the transferred IP headers.
 */
/*@{*/

/**
 * @brief Networking device private information.
 *
 * This is the private information associated with each created networking
 * device.
 */
struct dev_info {
	struct list_head list;           /**< list head for chaining into #dev_list                */
	struct net_device *dev;          /**< reference to networking interface                    */
	struct dev_info *parent;         /**< reference to parent device                           */
	struct net_device_stats stats;   /**< contains receive and transmit information            */
	char *bus_info;                  /**< text representation for input and output association */
	int mtu;                         /**< MTU for this interface                               */
	u32 *pmtu;                       /**< PMTUs for this interface                             */
	size_t pmtu_elements;            /**< maximum number of PMTUs supported                    */
	struct dev_info **children;      /**< table mapping marks to child devices                 */
	size_t child_elements;           /**< maximum number of entries in children table          */
	spinlock_t children_lock;        /**< lock for accessing child device table                */
	unsigned long flags;             /**< flags given on the command line                      */
	spinlock_t writer_lock;          /**< lock for accessing the writer part                   */
	struct muchannel *channel_out;   /**< output channel for write operations                  */
	size_t writer_element_size;      /**< size of elements for write operations                */
	size_t writer_region_size;       /**< total size of writer buffer                          */
	int writer_event;                /**< event to trigger to inform peer about new data       */
	int writer_cpu;                  /**< CPU affinity of writer event                         */
	u64 writer_protocol;             /**< protocol id for writer                               */
	u64 reader_protocol;             /**< protocol id for reader                               */
	unsigned int poll_interval;      /**< sleep period for reader work                         */
	struct muchannel_reader reader;  /**< channel reader                                       */
	size_t reader_element_size;      /**< size of elements for read operations                 */
	int reader_irq;                  /**< IRQ number for the channel reader                    */
	struct muchannel *channel_in;    /**< input channel for read operations                    */
	struct delayed_work reader_work; /**< delayed reader work queue struct                     */
	struct dentry *debugfs_dir;      /**< directory entry for debugfs directory                */
	struct dentry *debugfs_info;     /**< directory entry for information file                 */
};

struct dev_info *parent_dev(struct dev_info *dev_info);

/**
 * @brief Flag bit values
 *
 * These are the values for the bits that can be stored in #dev_info.flags.
 */
enum muennet_flags {
	MUENNET_HDR = 1, /**< add network information needed for IPv4/IPv6 */
	ETH_DEV     = 2, /**< treat interface as ethernet device */
};

/**
 * @brief Name-value mappings for the flags.
 *
 * This data type is used to map symbolic names of the flags to the bit values.
 */
struct flag_name {
	const char *name;
	unsigned long value;
};

/**
 * @brief Mapping of the currently implemented flags.
 *
 * This variable stores the currently known flags with their symbolic names
 * that can be given when loading the module. These symbolic names are also
 * used when writing information in the debugfs.
 */
static const struct flag_name flag_names[] = {
	{ .name = "net_hdr",
	  .value = MUENNET_HDR },
	{ .name = "eth_dev",
	  .value = ETH_DEV },
	{},
};

/**
 * @brief Header format for network header flag.
 *
 * This header encodes additional information in the data transferred via
 * the shared channel to avoid having to guess some of the packet properties.
 */
struct net_hdr {
	u32 mark;    /**< netfilter mark                          */
	u16 length;  /**< length of the payload                   */
	u8 protocol; /**< the IP protocol embedded in the payload */
	u8 qos;      /**< the QoS value embedded in the payload   */
} __packed;
/*@}*/

/**
 * @brief Header format for ethernet device flag.
 *
 * This header encodes additional information in the data transferred via
 * the shared channel to avoid having to guess some of the packet properties.
 */
struct eth_hdr {
	u16 length;  /**< length of the ethernet packet (header + payload) */
} __packed;
/*@}*/

/**
 * @defgroup debug Debugging and tracing tools
 *
 * This module can be split into two parts. The functions are used to setup the
 * debugfs entries for the module and for each individual networking device.
 */
/*@{*/

/**
 * @brief Initialize debugfs for driver
 */
void debug_initialize(void);

/**
 * @brief Removes debugfs for driver
 */
void debug_shutdown(void);

/**
 * @brief Initialize directory for single device
 */
int debug_create_device(struct dev_info *dev_info);

/**
 * @brief Remove directory for single device
 */
void debug_remove_device(struct dev_info *dev_info);
/*@}*/

/**
 * @defgroup reader Network reader
 */
/*@{*/

/**
 * @brief Initializes the network device reader part
 */
int initialize_reader(struct dev_info *dev_info,
		      const struct muen_resource_type *const region);

/**
 * @brief Shuts down the network device reader part
 */
void cleanup_reader(struct dev_info *dev_info);
/*@}*/

/**
 * @defgroup writer Network writer
 */
/*@{*/

/**
 * @brief Activate specified network writer
 */
void writer_up(struct dev_info *dev_info);

/**
 * @brief Deactivate specified network writer
 */
void writer_down(struct dev_info *dev_info);

/**
 * @brief Initializes the network device writer part
 */
int initialize_writer(struct dev_info *dev_info,
		      const struct muen_resource_type *const region,
		      const struct muen_resource_type *const pmtu_region);

/**
 * @brief Shuts down the network device writer part
 */
void cleanup_writer(struct dev_info *dev_info);

/**
 * @brief Transmit given sbk using the specified network device
 */
int muennet_xmit(struct sk_buff *skb, struct net_device *dev);
/*@}*/

#endif
