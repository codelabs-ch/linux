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

#define DRV_NAME	"muennet"
#define DRV_VERSION	"0.2"
#define DRV_DESCRIPTION	"Muen SK virtual network driver"

#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <net/genetlink.h>

#include "internal.h"
#include "netlink.h"

/**
 * @file net.c
 * @brief Networking interface
 *
 * @defgroup net Networking interface
 *
 * This module defines the module initialization and finalization code together
 * with the whole implementation of network operations.
 *
 * The data is transferred via the #muennet_xmit function and received by the
 * #muennet_reader_work work queue function.
 */
/*@{*/

/**
 * @brief List of networking drivers.
 *
 * This list is used to chain all initialized network interfaces together. The
 * list head points to the #dev_info::list element of the network interfaces
 * private data.
 */
static LIST_HEAD(dev_list);

/**
 * @brief Setup networking interface link.
 *
 * This function starts the network queue for the given interface and inserts
 * the reader work into the events work queue.
 *
 * @param dev networking device to operate on
 * @return always 0 to indicate success
 */
static int muennet_open(struct net_device *dev)
{
	struct dev_info *dev_info = netdev_priv(dev);

	if (!dev_info->parent && dev_info->channel_out)
		writer_up(dev_info);

	netif_carrier_on(dev);
	netif_start_queue(dev);

	if (dev_info->reader_irq < 0) {
		if (!dev_info->parent && dev_info->channel_in)
			schedule_delayed_work(&dev_info->reader_work, 0);
	} else {
		netdev_info(dev_info->dev, "Registered IRQ %d\n",
			    dev_info->reader_irq);
	}

	return 0;
}

/**
 * @brief Teardown networking interface link.
 *
 * This function stops the network queue for this driver.
 *
 * @param dev the networking interface
 * @return always returns 0
 */
static int muennet_close(struct net_device *dev)
{
	struct dev_info *dev_info = netdev_priv(dev);

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	if (!dev_info->parent && dev_info->channel_out)
		writer_down(dev_info);

	return 0;
}

/**
 * @brief Retrieve statistics about the networking interface.
 *
 * These statistics are shown in ifconfig or with "ip -s link". The following
 * values are used:
 * - rx_errors      : receive errors
 * - rx_over_errors : reader was overrun by writer
 * - rx_frame_error : invalid packet received from writer
 * - rx_packets     : packets successfully received
 * - rx_bytes       : sum of packet sizes successfully received
 * - tx_dropped     : packet dropped because no writing memory region associated
 * - tx_packets     : packets sent
 * - tx_bytes       : sum of packet sizes successfully received
 *
 * @param dev network device
 * @return reference to net_device_stats structure stored within private
 * information #dev_info.
 */
static struct net_device_stats *muennet_stats(struct net_device *dev)
{
	struct dev_info *dev_info = netdev_priv(dev);

	return &dev_info->stats;
}

/**
 * @brief Retrieve ethtool settings.
 *
 * This function provides some dummy content to make ethtool happy.
 *
 * @param dev network interface
 * @param cmd structure to be filled
 * @return always returns 0
 */
static int muennet_get_settings(struct net_device *dev,
				struct ethtool_link_ksettings *cmd)
{
	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);
	cmd->base.speed	      = SPEED_10;
	cmd->base.duplex      = DUPLEX_FULL;
	cmd->base.port	      = PORT_TP;
	cmd->base.phy_address = 0;
	cmd->base.transceiver = XCVR_INTERNAL;
	cmd->base.autoneg     = AUTONEG_DISABLE;

	return 0;
}

/**
 * @brief Interface around strscpy.
 *
 * This function is used to safely copy data into an array.
 *
 * @param array the (real) array to operate on
 * @param value the string data to copy into the array
 */
#define copy(array, value) strscpy(array, value, sizeof(array))

/**
 * @brief Retrieve driver information.
 *
 * This function is called to retrieve the information shown by "ethtool -i".
 *
 * @param dev  network interface
 * @param info the structure to fill with the information
 */
static void muennet_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	struct dev_info *dev_info = parent_dev(netdev_priv(dev));

	copy(info->driver, DRV_NAME);
	copy(info->version, DRV_VERSION);
	copy(info->fw_version, "N/A");
	copy(info->bus_info, dev_info->bus_info);
}

/**
 * @brief Retrieve link information.
 *
 * This function tells the caller if the network interface has a link. A link
 * is there if either a reader or a writer memory region is associated with
 * this networking interface.
 *
 * @param dev network interface
 * @return true if link is there
 */
static u32 muennet_get_link(struct net_device *dev)
{
	struct dev_info *dev_info = parent_dev(netdev_priv(dev));

	return (dev_info->writer_element_size != 0 ||
		dev_info->reader_element_size != 0);
}

static void muennet_mclist(struct net_device *dev)
{
	/*
	 * This callback is supposed to deal with mc filter in
	 * _rx_ path and has nothing to do with the _tx_ path.
	 * In rx path we always accept everything userspace gives us.
	 */
}

/**
 * @brief ethtool operations
 *
 * This structure defines the ethtool operations available on this networking
 * interface.
 */
static const struct ethtool_ops muennet_ethtool_ops = {
	.get_link_ksettings = muennet_get_settings,
	.get_drvinfo	    = muennet_get_drvinfo,
	.get_link	    = muennet_get_link,
};

/**
 * @brief Networking interface destructor.
 *
 * This function is used to shutdown the reader and writer part and to free the
 * allocated memory. It is called during unregister_netdev.
 *
 * @param dev the network interface
 */

static void muennet_free(struct net_device *dev)
{
	struct dev_info *dev_info = netdev_priv(dev);

	if (!dev_info->parent) {
		if (dev_info->channel_in)
			cleanup_reader(dev_info);
		if (dev_info->channel_out)
			cleanup_writer(dev_info);
		kfree(dev_info->bus_info);
	}

	kfree(dev_info->children);
}

static const struct net_device_ops muennet_device_ops = {
	.ndo_open       = muennet_open,
	.ndo_stop       = muennet_close,
	.ndo_start_xmit = muennet_xmit,
	.ndo_get_stats  = muennet_stats,
};

static const struct net_device_ops muennet_dev_eth_ops = {
	.ndo_open       = muennet_open,
	.ndo_stop       = muennet_close,
	.ndo_start_xmit = muennet_xmit,
	.ndo_get_stats  = muennet_stats,
	.ndo_set_rx_mode    = muennet_mclist,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_features_check = passthru_features_check,
};

/**
 * @brief Setup the network interface.
 *
 * This function is called during alloc_netdev to initialize the network
 * operations for this interface.
 *
 * @param dev the network interface
 */
static void muennet_setup(struct net_device *dev)
{
	const struct dev_info *dev_info = parent_dev(netdev_priv(dev));

	if (dev_info->flags & ETH_DEV)
		dev->netdev_ops = &muennet_dev_eth_ops;
	else
		dev->netdev_ops = &muennet_device_ops;

	dev->ethtool_ops = &muennet_ethtool_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = muennet_free;
}

/**
 * @brief Add a new networking interface.
 *
 * This function creates a new networking interface in the kernel based on the
 * provided information.
 *
 * @param device_name name of the networking interface (%d is supported)
 * @param input       name of the memory region to read from (use NULL or empty
 *                    string to indicate no reading)
 * @param output      name of the memory region to write from (use NULL or
 *                    empty string to indicate no writing)
 * @param mtu         the maximum transmission unit of this interface
 * @param flags       flags that control the operation of the networking
 *                    interface (see #muennet_flags for possible values)
 * @param poll        poll interval to use for this network interface
 *
 * @return 0 on success
 * @return -ENOMEM on memory allocation failure
 * @return -ENXIO  on channel lookup failure
 * @return errors returned by #initialize_reader or #initialize_writer
 */
static int add_device(const char *device_name,
		      struct net *net,
		      struct dev_info *parent,
		      const char *input,
		      const char *output,
		      int mtu,
		      const char *pmtu,
		      u64 writer_protocol,
		      u64 reader_protocol,
		      unsigned long flags,
		      unsigned int poll)
{
	int ret = -ENOMEM;
	struct net_device *dev;
	struct dev_info *dev_info;
	size_t bus_info_len = 2; /* place for separator and finishing \0 */

	if (input)
		bus_info_len += strlen(input);
	if (output)
		bus_info_len += strlen(output);

	dev = alloc_netdev(sizeof(struct dev_info), device_name,
			NET_NAME_UNKNOWN, muennet_setup);
	if (!dev)
		goto err;

	/* do further initialization of device */

	/* Keep info required for fragmentation (include/linux/netdevice.h). */
	dev->priv_flags &= ~IFF_XMIT_DST_RELEASE;

	if (flags & ETH_DEV) {
		ether_setup(dev);
		dev->priv_flags &= ~IFF_TX_SKB_SHARING;
		dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
		eth_hw_addr_random(dev);

		/* Additional information is appended to skb */
		dev->needed_tailroom = sizeof(struct eth_hdr);
	} else {
		dev->type = ARPHRD_NONE;
		dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
		dev->addr_len = 0;
		dev->hard_header_len = 0;
		dev->mtu = mtu;
	}

	if (flags & MUENNET_HDR)
		dev->hard_header_len += sizeof(struct net_hdr);

	dev_net_set(dev, net);

	dev_info = netdev_priv(dev);
	dev_info->dev = dev;

	dev_info->parent = parent;

	if (!parent) {
		dev_info->bus_info = kmalloc(bus_info_len, GFP_KERNEL);
		if (!dev_info->bus_info)
			goto err_free_netdev;
		dev_info->bus_info[0] = 0;

		if (input)
			strlcat(dev_info->bus_info, input, bus_info_len);
		strlcat(dev_info->bus_info, ":", bus_info_len);
		if (output)
			strlcat(dev_info->bus_info, output, bus_info_len);
	}

	dev_info->poll_interval = poll;
	dev_info->mtu = mtu;
	dev_info->flags = flags;
	dev_info->reader_irq = -1;
	dev_info->writer_protocol = writer_protocol;
	dev_info->reader_protocol = reader_protocol;

	/* first check all the names */
	if (input && strlen(input) > 0) {
		const struct muen_resource_type *const
			reader_channel = muen_get_resource(input,
							   MUEN_RES_MEMORY);
		if (!reader_channel) {
			netdev_err(dev_info->dev,
				   "Input channel '%s' not found\n", input);
			ret = -ENXIO;
			goto err_free_businfo;
		}
		ret = initialize_reader(dev_info, reader_channel);
		if (ret < 0) {
			netdev_err(dev_info->dev,
				   "Unable to init reader (status: %d)\n", ret);
			goto err_free_businfo;
		}
	}

	if (output && strlen(output) > 0) {
		const struct muen_resource_type *const
			writer_channel = muen_get_resource(output,
							   MUEN_RES_MEMORY);
		if (!writer_channel) {
			netdev_err(dev_info->dev,
				   "Output channel '%s' not found\n", output);
			ret = -ENXIO;
			goto err_cleanup_reader;
		}

		if (pmtu && strlen(pmtu) > 0) {
			const struct muen_resource_type *const
				pmtu_channel = muen_get_resource(
						pmtu, MUEN_RES_MEMORY);
			if (!pmtu_channel) {
				netdev_err(dev_info->dev,
					   "PMTU channel '%s' not found\n",
					   pmtu);
				ret = -ENXIO;
				goto err_cleanup_reader;
			}
			ret = initialize_writer(dev_info, writer_channel,
						pmtu_channel);
		} else
			ret = initialize_writer(dev_info, writer_channel,
						NULL);

		if (ret < 0) {
			netdev_err(dev_info->dev,
				   "Unable to init writer (status: %d)\n", ret);
			goto err_cleanup_reader;
		}
	}

	if ((flags & MUENNET_HDR) && !parent) {
		size_t i;

		/*
		 * The PMTU and children arrays represent mappings from marks (or
		 * connections) to PMTU values and pointers to dev_info structs,
		 * respectively. The maximum number of marks is deduced from the size
		 * of the PMTU memory area, and for reasons of consistency, we use the
		 * same number for both PMTUs and children. In the case where no PMTU
		 * memory area is defined, we just assume that the maximum number of
		 * child devices is 100.
		 */
		dev_info->child_elements = max_t(size_t, 100,
						 dev_info->pmtu_elements);
		dev_info->children = kmalloc_array(dev_info->child_elements,
						   sizeof(struct dev_info *),
						   GFP_KERNEL);
		if (!dev_info->children)
			goto err_cleanup_writer;

		for (i = 0; i < dev_info->child_elements; i++)
			dev_info->children[i] = dev_info;

		spin_lock_init(&dev_info->children_lock);
	}

	netif_carrier_off(dev_info->dev);
	ret = register_netdev(dev_info->dev);
	if (ret < 0) {
		netdev_err(dev_info->dev,
			   "register_netdev failed with status %d\n", ret);
		goto err_cleanup_children;
	}

	list_add_tail(&dev_info->list, &dev_list);
	debug_create_device(dev_info);
	netdev_info(dev_info->dev, "Interface added\n");

	return 0;

err_cleanup_children:
	kfree(dev_info->children);
err_cleanup_writer:
	if (output && strlen(output) > 0)
		cleanup_writer(dev_info);
err_cleanup_reader:
	if (input && strlen(input) > 0)
		cleanup_reader(dev_info);
err_free_businfo:
	kfree(dev_info->bus_info);
err_free_netdev:
	free_netdev(dev);
err:
	return ret;
}

struct dev_info *parent_dev(struct dev_info *dev_info)
{
	if (dev_info->parent)
		return dev_info->parent;
	else
		return dev_info;
}

static const struct nla_policy muennet_genl_policy[MUENNET_A_MAX + 1] = {
	[MUENNET_A_DEV] = { .type = NLA_NUL_STRING,
			    .len = IFNAMSIZ - 1 },
	[MUENNET_A_CHILD_DEV] = { .type = NLA_NUL_STRING,
				  .len = IFNAMSIZ - 1 },
	[MUENNET_A_MARK] = { .type = NLA_U32 }
};

static bool is_muennet_dev(struct net_device *dev)
{
	struct dev_info *dev_info;
	struct dev_info *next;
	bool rc = false;

	list_for_each_entry_safe(dev_info, next, &dev_list, list) {
		rc = rc || (dev_info->dev == dev);
	}

	return rc;
}

static int add_child(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *parent;
	struct dev_info *dev_info;
	int rc;

	if (!info->attrs[MUENNET_A_DEV] ||
	    !info->attrs[MUENNET_A_CHILD_DEV])
		return -EINVAL;

	parent = dev_get_by_name(genl_info_net(info),
				 (char *)nla_data(info->attrs[MUENNET_A_DEV]));

	if (!parent)
		return -EINVAL;

	if (!is_muennet_dev(parent)) {
		dev_put(parent);
		return -EINVAL;
	}

	dev_info = netdev_priv(parent);

	if (dev_info->parent) {
		dev_put(parent);
		return -EINVAL;
	}

	rc = add_device((char *)nla_data(info->attrs[MUENNET_A_CHILD_DEV]),
			genl_info_net(info), dev_info,
			NULL, NULL, dev_info->mtu, NULL, 0, 0, 0, 0);

	dev_put(parent);
	return rc;
}

static int del_child(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *child;
	struct dev_info *dev_info;
	spinlock_t *lock;
	size_t i;
	unsigned long flags;

	if (!info->attrs[MUENNET_A_CHILD_DEV])
		return -EINVAL;

	child = dev_get_by_name(genl_info_net(info), (char *)
				nla_data(info->attrs[MUENNET_A_CHILD_DEV]));

	if (!child)
		return -EINVAL;

	if (!is_muennet_dev(child)) {
		dev_put(child);
		return -EINVAL;
	}

	dev_info = netdev_priv(child);

	if (!dev_info->parent) {
		dev_put(child);
		return -EINVAL;
	}

	lock = &dev_info->parent->children_lock;
	spin_lock_irqsave(lock, flags);

	for (i = 0; i < dev_info->parent->child_elements; i++) {
		if (dev_info->parent->children[i] == dev_info)
			dev_info->parent->children[i] = dev_info->parent;
	}

	list_del(&dev_info->list);
	spin_unlock_irqrestore(lock, flags);
	debug_remove_device(dev_info);
	dev_put(child);
	unregister_netdev(dev_info->dev);

	return 0;
}

static int modify_mark(bool add, struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *child;
	struct dev_info *dev_info;
	u32 mark;
	unsigned long flags;

	if (!info->attrs[MUENNET_A_CHILD_DEV] ||
	    !info->attrs[MUENNET_A_MARK])
		return -EINVAL;

	child = dev_get_by_name(genl_info_net(info), (char *)
				nla_data(info->attrs[MUENNET_A_CHILD_DEV]));

	if (!child)
		return -EINVAL;

	if (!is_muennet_dev(child)) {
		dev_put(child);
		return -EINVAL;
	}

	dev_info = netdev_priv(child);

	if (!dev_info->parent) {
		dev_put(child);
		return -EINVAL;
	}

	mark = nla_get_u32(info->attrs[MUENNET_A_MARK]);

	if (mark >= 1 && mark <= dev_info->parent->child_elements) {
		spin_lock_irqsave(&dev_info->parent->children_lock, flags);

		if (add &&
		    dev_info->parent->children[mark - 1] == dev_info->parent)
			dev_info->parent->children[mark - 1] = dev_info;
		else if (!add &&
			 dev_info->parent->children[mark - 1] == dev_info)
			dev_info->parent->children[mark - 1] = dev_info->parent;
		else {
			spin_unlock_irqrestore(&dev_info->parent->children_lock,
					       flags);
			dev_put(child);
			return -EINVAL;
		}

		spin_unlock_irqrestore(&dev_info->parent->children_lock, flags);
	} else {
		dev_put(child);
		return -EINVAL;
	}

	dev_put(child);
	return 0;
}

static int add_mark(struct sk_buff *skb, struct genl_info *info)
{
	return modify_mark(true, skb, info);
}

static int del_mark(struct sk_buff *skb, struct genl_info *info)
{
	return modify_mark(false, skb, info);
}

static const struct genl_ops muennet_genl_ops[] = {
	{
	.cmd	= MUENNET_C_ADD_CHILD,
	.flags	= 0,
	.doit	= add_child,
	.dumpit	= NULL,
	},
	{
	.cmd	= MUENNET_C_DEL_CHILD,
	.flags	= 0,
	.doit	= del_child,
	.dumpit	= NULL,
	},
	{
	.cmd	= MUENNET_C_ADD_MARK,
	.flags	= 0,
	.doit	= add_mark,
	.dumpit	= NULL,
	},
	{
	.cmd	= MUENNET_C_DEL_MARK,
	.flags	= 0,
	.doit	= del_mark,
	.dumpit	= NULL,
	},
};

static struct genl_family muennet_gnl_family __ro_after_init = {
	.hdrsize	= 0,
	.name		= NLTYPE_MUENNET_NAME,
	.version	= 1,
	.netnsok	= true,
	.policy		= muennet_genl_policy,
	.maxattr	= MUENNET_A_MAX,
	.module		= THIS_MODULE,
	.ops		= muennet_genl_ops,
	.n_ops		= ARRAY_SIZE(muennet_genl_ops),
};

/**
 * @brief Maximum number of interfaces
 *
 * This is the maximum number of interfaces supported by the module parameters.
 */
#define MAX_INTERFACES 32

/**
 * @brief Poll interval (in µs)
 *
 * This is the default poll interval, use "poll" module parameter to override.
 */
static unsigned int poll = 1;

/**
 * @brief Interface names
 *
 * This array is filled with the list of interfaces specified with the "name"
 * module parameter.
 */
static char *name[MAX_INTERFACES];

/**
 * @brief Input memory regions
 *
 * This array is filled with the list of input memory regions specified with
 * the "in" module parameter.
 */
static char *in[MAX_INTERFACES];

/**
 * @brief Output memory regions
 *
 * This array is filled with the list of output memory regions specified with
 * the "out" module parameter.
 */
static char *out[MAX_INTERFACES];

/**
 * @brief Maximum transfer unit
 *
 * This array is filled with the list of mtu specified with the "mtu" module
 * parameter. If no MTU is given via module parameter a default value of 1500
 * is used.
 */
static char *mtu[MAX_INTERFACES];

/**
 * @brief Memory regions for PMTU values
 *
 * This array is filled with the list of memory regions holding the PMTU values
 * for each writer.
 */
static char *pmtu[MAX_INTERFACES];

/**
 * @brief Interface flags
 *
 * This array is filled with the list of interface flags specified with the
 * "flags" module parameter. The flags are a list of names (see #flag_names for
 * valid names) where the values for each interface are separated with "+" and
 * the value list for all interfaces are separated with ",". If no flags are
 * given for a interface a default value of 0 is used.
 */
static char *flags[MAX_INTERFACES];

/**
 * @brief Writer protocol
 *
 * This array is filled with the list of writer protocols specified with the
 * writer_protocol parameter
 */
static char *writer_protocol[MAX_INTERFACES];

/**
 * @brief Reader protocol
 *
 * This array is filled with the list of reader protocols specified with the
 * reader_protocol parameter
 */
static char *reader_protocol[MAX_INTERFACES];

/**
 * @brief Count of interface names
 *
 * This should be set to the number of interface names specified in the module
 * parameters.
 */
static int name_count;

module_param_array(name, charp, &name_count, 0444);
MODULE_PARM_DESC(name, "List of interface names, separated with comma");
module_param_array(in, charp, NULL, 0444);
MODULE_PARM_DESC(in, "List of input memregions, separated with comma (empty values permitted)");
module_param_array(out, charp, NULL, 0444);
MODULE_PARM_DESC(out, "List of output memregions, separated with comma (empty values permitted)");
module_param_array(pmtu, charp, NULL, 0444);
MODULE_PARM_DESC(pmtu, "List of input memregions holding PMTU values");
module_param_array(mtu, charp, NULL, 0444);
MODULE_PARM_DESC(mtu, "List of MTUs to use, separated with comma (default is 1500)");
module_param_array(writer_protocol, charp, NULL, 0444);
MODULE_PARM_DESC(writer_protocol, "List of writer protocol IDs, separated with comma");
module_param_array(reader_protocol, charp, NULL, 0444);
MODULE_PARM_DESC(reader_protocol, "List of reader protocol IDs, separated with comma");
module_param_array(flags, charp, NULL, 0444);
MODULE_PARM_DESC(flags, "List of flags separated with comma (flags for a device separated with +)");
module_param(poll, uint, 0444);
MODULE_PARM_DESC(poll, "Wait period in reader thread (in µs)");

/**
 * @brief Parse interface flags for one interface.
 *
 * This function parses the interface flag names which are separated with "+".
 * Case is important when comparing flag names. Unknown flag names result in an
 * error.
 *
 * @param names list of flag names separated with "+" *
 * @return resulting bit value. *
 * @return -EINVAL if a flag is unknown
 */
static int parse_flags(const char *names)
{
	int result = 0;
	int last_value = 0;
	const char *next_pos;

	while (!last_value) {
		size_t i;
		int found = 0;

		next_pos = strchr(names, '+');
		if (next_pos == NULL) {
			next_pos = names + strlen(names);
			last_value = 1;
		}

		for (i = 0; flag_names[i].name != NULL; i++) {
			if (strncmp(flag_names[i].name, names,
				    next_pos - names) == 0) {
				result |= flag_names[i].value;
				found = 1;
			}
		}

		if (!found) {
			pr_err(DRV_NAME ": Invalid flag name found in '%s'\n",
			       names);
			return -EINVAL;
		}

		names = next_pos + 1;
	}
	return result;
}

/**
 * @brief Module cleanup routine.
 *
 * This function is called during module unloading. It removes all debugfs
 * entries and networking interfaces.
 */
static void muennet_cleanup(void)
{
	struct dev_info *dev_info;
	struct dev_info *next;

	genl_unregister_family(&muennet_gnl_family);

	list_for_each_entry_safe(dev_info, next, &dev_list, list) {
		list_del(&dev_info->list);
		debug_remove_device(dev_info);
		unregister_netdev(dev_info->dev);
	}
	debug_shutdown();
}

/**
 * @brief Module initialization routine.
 *
 * This function parses the module parameters. For each interface specified by
 * the "name" parameter a networking interface is created (with the memory
 * region given by "in" and "out" parameters). If the setup of a network
 * interface failed, all previously created network interfaces will be cleaned
 * up and the error is returned to user space.
 *
 * @return 0 for successful module loading
 * @return errors returned by #add_device
 */
static int __init muennet_init(void)
{
	int i;
	int ret;

	debug_initialize();

	for (i = 0; i < name_count; i++) {
		unsigned int device_mtu = 1500;
		unsigned long flag_value = 0;
		u64 device_writer_protocol = 0;
		u64 device_reader_protocol = 0;

		if (!name[i] || strlen(name[i]) == 0)
			continue;

		if (strlen(name[i]) >= IFNAMSIZ) {
			pr_err(DRV_NAME ": interface name too long '%s'\n",
			       name[i]);
			ret = -EINVAL;
			goto error;
		}

		if (mtu[i] != NULL && strlen(mtu[i]) > 0)
			if (kstrtouint(mtu[i], 10, &device_mtu) != 0) {
				pr_err(DRV_NAME ": MTU invalid\n");
				ret = -EINVAL;
				goto error;
			};

		if (!out[i] && !in[i]) {
			pr_err(DRV_NAME ": no channel specified for '%s'\n",
			       name[i]);
			ret = -EINVAL;
			goto error;
		}

		if (out[i]) {
			if (writer_protocol[i] != NULL &&
				strlen(writer_protocol[i]) > 0)
				if (kstrtoull(writer_protocol[i], 16,
					&device_writer_protocol) != 0) {
					pr_err(DRV_NAME ": writer_protocol invalid\n");
					ret = -EINVAL;
					goto error;
			}
			if (!device_writer_protocol) {
				pr_err(DRV_NAME ": writer_protocol missing\n");
				ret = -EINVAL;
				goto error;
			}
		}

		if (in[i]) {
			if (reader_protocol[i] != NULL &&
				strlen(reader_protocol[i]) > 0)
				if (kstrtoull(reader_protocol[i], 16,
					&device_reader_protocol) != 0) {
					pr_err(DRV_NAME ": reader_protocol invalid\n");
					ret = -EINVAL;
					goto error;
			}

			if (!device_reader_protocol) {
				pr_err(DRV_NAME ": reader_protocol missing\n");
				ret = -EINVAL;
				goto error;
			}
		}

		if (flags[i] != NULL && strlen(flags[i]) > 0) {
			ret = parse_flags(flags[i]);
			if (ret < 0)
				goto error;

			flag_value = ret;
		}

		ret = add_device(name[i], &init_net, NULL, in[i], out[i],
				 device_mtu, pmtu[i], device_writer_protocol,
				 device_reader_protocol, flag_value, poll);
		if (ret < 0)
			goto error;
	}

	ret = genl_register_family(&muennet_gnl_family);
	if (ret != 0)
		goto error;

	return 0;
error:
	/* try to cleanup already created interfaces */
	muennet_cleanup();
	return ret;
}

module_init(muennet_init);
module_exit(muennet_cleanup);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Torsten Hilbrich <torsten.hilbrich@secunet.com>");

/*@}*/
