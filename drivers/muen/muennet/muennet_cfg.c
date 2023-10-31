// SPDX-License-Identifier: GPL-2.0

/*
 * Muen virtual network driver config tool.
 *
 * Copyright (C) 2018  secunet Security Networks AG
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

#include <errno.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include "netlink.h"

static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg)
{
	int *ret = arg;
	*ret = err->error;
	return NL_STOP;
}

static int ack_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_STOP;
}

int main(int argc, char **argv) {
	struct nl_msg *msg;
	void *msg_buf;
	struct nlmsghdr *nl_hdr;
	struct genlmsghdr *genl_hdr;
	struct nl_sock *nl_sock;
	struct nl_cb *cb;
	int rc;

	if (argc < 2) {
		rc = -EINVAL;
		goto nl_socket_alloc_failure;
	}

	nl_sock = nl_socket_alloc();
	if (nl_sock == NULL) {
		rc = -ENOMEM;
		goto nl_socket_alloc_failure;
	}

	nl_socket_set_peer_port(nl_sock, 0);
	nl_socket_disable_seq_check(nl_sock);

	rc = nl_connect(nl_sock, NETLINK_GENERIC);
	if (rc != 0)
		goto connect_failure;

	rc = genl_ctrl_resolve(nl_sock, NLTYPE_MUENNET_NAME);
	if (rc < 0)
		goto genl_ctrl_resolve_failure;

	msg = nlmsg_alloc();
	if (msg == NULL) {
		rc = -ENOMEM;
		goto nlmsg_alloc_failure;
	}

	msg_buf = genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, 0, 0, 0, 0, 0);
	if (msg_buf == NULL) {
		rc = -ENOMEM;
		goto genlmsg_put_failure;
	}

	nl_hdr = nlmsg_hdr(msg);
	nl_hdr->nlmsg_type = rc;
	nl_hdr->nlmsg_flags |= NLM_F_ACK;

	genl_hdr = genlmsg_hdr(nl_hdr);

	if (strcmp(argv[1], "add_child") == 0 && argc == 4) {
		genl_hdr->cmd = MUENNET_C_ADD_CHILD;

		rc = nla_put_string(msg, MUENNET_A_DEV, argv[2]);
		if (rc != 0)
			goto genlmsg_put_failure;

		rc = nla_put_string(msg, MUENNET_A_CHILD_DEV, argv[3]);
		if (rc != 0)
			goto genlmsg_put_failure;
	} else if (strcmp(argv[1], "del_child") == 0 && argc == 3) {
		genl_hdr->cmd = MUENNET_C_DEL_CHILD;

		rc = nla_put_string(msg, MUENNET_A_CHILD_DEV, argv[2]);
		if (rc != 0)
			goto genlmsg_put_failure;
	} else if (strcmp(argv[1], "add_mark") == 0 && argc == 4) {
		genl_hdr->cmd = MUENNET_C_ADD_MARK;

		rc = nla_put_string(msg, MUENNET_A_CHILD_DEV, argv[2]);
		if (rc != 0)
			goto genlmsg_put_failure;

		rc = nla_put_u32(msg, MUENNET_A_MARK, atoi(argv[3]));
		if (rc != 0)
			goto genlmsg_put_failure;
	} else if (strcmp(argv[1], "del_mark") == 0 && argc == 4) {
		genl_hdr->cmd = MUENNET_C_DEL_MARK;

		rc = nla_put_string(msg, MUENNET_A_CHILD_DEV, argv[2]);
		if (rc != 0)
			goto genlmsg_put_failure;

		rc = nla_put_u32(msg, MUENNET_A_MARK, atoi(argv[3]));
		if (rc != 0)
			goto genlmsg_put_failure;
	} else {
		rc = -EINVAL;
		goto genlmsg_put_failure;
	}

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (cb == NULL) {
		rc = -ENOMEM;
		goto genlmsg_put_failure;
	}

	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &rc);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &rc);

	rc = nl_send_auto(nl_sock, msg);
	if (rc < 0)
		goto send_failure;

	rc = 1;

	while (rc > 0)
		nl_recvmsgs(nl_sock, cb);

send_failure:
	nl_cb_put(cb);

genlmsg_put_failure:
	nlmsg_free(msg);

nlmsg_alloc_failure:
genl_ctrl_resolve_failure:
connect_failure:
	nl_close(nl_sock);
	nl_socket_free(nl_sock);

nl_socket_alloc_failure:
	if (rc != 0)
		printf("Command failed\n");
	return rc;
}
