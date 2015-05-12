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

#include <muen/writer.h>

void muen_channel_init_writer(struct muchannel *channel, const u64 protocol,
			      const u64 element_size, const u64 channel_size,
			      const u64 epoch)
{
	u64 data_size;

	muen_channel_deactivate(channel);
	memset(channel, 0, sizeof(struct muchannel));

	data_size = channel_size - sizeof(struct muchannel_header);

	atomic64_set(&channel->hdr.transport, SHMSTREAM20);
	atomic64_set(&channel->hdr.protocol, protocol);
	atomic64_set(&channel->hdr.size, element_size);
	atomic64_set(&channel->hdr.elements, data_size / element_size);
	atomic64_set(&channel->hdr.wsc, 0);
	atomic64_set(&channel->hdr.wc, 0);

	atomic64_set(&channel->hdr.epoch, epoch);
}
EXPORT_SYMBOL(muen_channel_init_writer);

void muen_channel_deactivate(struct muchannel *channel)
{
	atomic64_set(&channel->hdr.epoch, MUCHANNEL_NULL_EPOCH);
}
EXPORT_SYMBOL(muen_channel_deactivate);

void muen_channel_write(struct muchannel *channel, const void *const element)
{
	u64 wc, pos, size, tmp;

	size = atomic64_read(&channel->hdr.size);
	wc = atomic64_read(&channel->hdr.wc);
	tmp = wc;
	pos = do_div(tmp, atomic64_read(&channel->hdr.elements));

	wc = wc + 1;

	atomic64_set(&channel->hdr.wsc, wc);
	memcpy(channel->data + pos * size, element, size);
	atomic64_set(&channel->hdr.wc, wc);
}
EXPORT_SYMBOL(muen_channel_write);
