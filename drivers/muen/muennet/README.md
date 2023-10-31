# Muennet

## Introduction

The *muennet* Linux kernel module implements a virtual network interface driver
which sends and receives data via shared memory channels provided by the [Muen
Separation Kernel][1].

From a user-space perspective, a network interface created using the *muennet*
kernel module behaves just like an ordinary network interface.

## Protocols

Currently, the following protocols are supported:

- Raw mode (`SOCK_RAW`) for custom data
- IPv4/IPv6 traffic

## Usage

The following command inserts the *muennet* module into the kernel. The module
parameters configure the module to create a virtual *net0* network interface
using *channel_in* for data input and *channel_out* for data output. The reader
and writer protocols are arbitrary values which must match between
communicating endpoints.

	$ modprobe muennet    \
		name=net0         \
		reader_protocol=2 \
		writer_protocol=2 \
		in=channel_in     \
		out=channel_out   \
		flags=net_hdr

The *net_hdr* flag is required to send IP traffic over the network interface.
It can be omitted if the communicating endpoints apply a custom protocol over
raw data.

The *eth_dev* flag specifies that the network interface implements an ethernet
device. This allows users to operate on layer 2 ethernet frames and gives them
full control over the ethernet header.

Configure the newly created network interface as usual:

    $ ifconfig net0 192.168.1.1
    $ ip route add 192.168.1.0/24 dev net0

The module parameters accept a list of values in order to create multiple
network interfaces with associated settings:

	$ modprobe muennet                  \
		name=net0,net1                  \
		reader_protocol=12,2            \
		writer_protocol=8,2             \
		in=testchannel_1,testchannel_3  \
		out=testchannel_2,testchannel_4 \
		flags=net_hdr,net_hdr

Use the `modinfo` command to see all supported module parameters with their
explanation.

## Child Device Support

The `muennet_cfg` tool can be used to create child devices for muennet network
interfaces. Packets can be routed to specific interfaces by setting Netfilter
marks for given child devices. The usage of the tool is described in the
following paragraphs.

Add new child interface called 'child' (string) to interface 'parent' (string).
Child interfaces can only be added to the main, parent muennet interface:

	$ muennet_cfg add_child <parent> <child>

Delete child interface 'child' (string):

	$ muennet_cfg del_child <child>

Add mark (int) to given child interface 'child' (string). This causes all
incoming packets carrying this mark to be routed to the specified child
interface:

	$ muennet_cfg add_mark <child> <mark>

---
**NOTE**

The mark set on a child interface is only used for *incoming* packets. Outgoing
traffic is *not* automatically marked. This can be achieved by adding an
appropriate netfilter rule.

	$ iptables -t mangle -A OUTPUT -o <child> -j MARK --set-mark <mark>

---

Delete mark (int) from given child interface 'child' (string):

	$ muennet_cfg del_mark <child name> <mark>

[1]: https://muen.sk
