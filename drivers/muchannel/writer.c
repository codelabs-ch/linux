/*
 * Copyright (C) 2013, 2014  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2013, 2014  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <muen/writer.h>

void muchannel_initialize(struct muchannel *channel, const u64 protocol,
			  const u64 size, const u64 elements, const u64 epoch)
{
	muchannel_deactivate(channel);

	memset(channel, 0, sizeof(struct muchannel));

	atomic64_set(&channel->hdr.transport, SHMSTREAM20);
	atomic64_set(&channel->hdr.protocol, protocol);
	atomic64_set(&channel->hdr.size, size);
	atomic64_set(&channel->hdr.elements, elements);
	atomic64_set(&channel->hdr.wsc, 0);
	atomic64_set(&channel->hdr.wc, 0);

	atomic64_set(&channel->hdr.epoch, epoch);
}

void muchannel_deactivate(struct muchannel *channel)
{
	atomic64_set(&channel->hdr.epoch, NULL_EPOCH);
}

void muchannel_write(struct muchannel *channel, const void *const element)
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
