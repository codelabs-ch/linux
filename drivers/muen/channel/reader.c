// SPDX-License-Identifier: GPL-2.0+
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

#include <linux/export.h>
#include <linux/math64.h>
#include <linux/string.h>

#include <muen/reader.h>

static bool has_epoch_changed(const struct muchannel *const channel,
			      const struct muchannel_reader *const reader)
{
	return reader->epoch != atomic64_read(&channel->hdr.epoch);
};

static enum muchannel_reader_result synchronize(
		const struct muchannel *const channel,
		struct muchannel_reader *reader)
{
	u64 epoch, epoch_after, proto, transport, size, elements, data_size;

	epoch = atomic64_read(&channel->hdr.epoch);
	/* Read header fields only after epoch */
	smp_rmb();
	proto = atomic64_read(&channel->hdr.protocol);
	transport = atomic64_read(&channel->hdr.transport);
	size = atomic64_read(&channel->hdr.size);
	elements = atomic64_read(&channel->hdr.elements);
	/* Re-read epoch after header fields to detect reads during epoch change */
	smp_rmb();
	epoch_after = atomic64_read(&channel->hdr.epoch);

	if (epoch == MUCHANNEL_NULL_EPOCH || epoch != epoch_after)
		return MUCHANNEL_INCOMPATIBLE_INTERFACE;

	if (reader->channel_size <= sizeof(struct muchannel_header))
		return MUCHANNEL_INCOMPATIBLE_INTERFACE;
	data_size = reader->channel_size - sizeof(struct muchannel_header);

	if (reader->protocol != proto || transport != SHMSTREAM20 ||
	    size == 0 || size != reader->size ||
	    elements == 0 || elements > div64_u64(data_size, size))
		return MUCHANNEL_INCOMPATIBLE_INTERFACE;

	reader->epoch = epoch;
	reader->elements = elements;
	reader->rc = 0;

	return MUCHANNEL_EPOCH_CHANGED;
};

void muen_channel_init_reader(struct muchannel_reader *reader, u64 protocol,
			      u64 element_size, u64 channel_size)
{
	reader->epoch = MUCHANNEL_NULL_EPOCH;
	reader->protocol = protocol;
	reader->size = element_size;
	reader->elements = 0;
	reader->rc = 0;
	reader->channel_size = channel_size;
};
EXPORT_SYMBOL(muen_channel_init_reader);

enum muchannel_reader_result muen_channel_read(
		const struct muchannel *const channel,
		struct muchannel_reader *reader,
		void *element)
{
	u64 pos, rc;
	enum muchannel_reader_result result;

	if (muen_channel_is_active(channel)) {
		if (reader->epoch == MUCHANNEL_NULL_EPOCH ||
				has_epoch_changed(channel, reader))
			return synchronize(channel, reader);

		if (reader->rc >= atomic64_read(&channel->hdr.wc))
			result = MUCHANNEL_NO_DATA;
		else {
			div64_u64_rem(reader->rc, reader->elements, &rc);
			pos = rc * reader->size;
			memcpy(element, channel->data + pos, reader->size);

			if (atomic64_read(&channel->hdr.wsc) >
			    reader->rc + reader->elements) {
				result = MUCHANNEL_OVERRUN_DETECTED;
				reader->rc = atomic64_read(&channel->hdr.wc);
			} else {
				result = MUCHANNEL_SUCCESS;
				reader->rc = reader->rc + 1;
			}
			if (has_epoch_changed(channel, reader))
				result = MUCHANNEL_EPOCH_CHANGED;
		}
	} else {
		reader->epoch = MUCHANNEL_NULL_EPOCH;
		result = MUCHANNEL_INACTIVE;
	}

	return result;
};
EXPORT_SYMBOL(muen_channel_read);

void muen_channel_drain(const struct muchannel *const channel,
			struct muchannel_reader *reader)
{
	reader->rc = atomic64_read(&channel->hdr.wc);
};
EXPORT_SYMBOL(muen_channel_drain);
