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

#include <asm/div64.h>
#include <asm/unaligned.h>

#include <muen/reader.h>

static bool has_epoch_changed(const struct muchannel * const channel,
			      const struct muchannel_reader * const reader)
{
	return reader->epoch != atomic64_read(&channel->hdr.epoch);
};

static enum muchannel_reader_result synchronize(
		const struct muchannel * const channel,
		struct muchannel_reader *reader)
{
	enum muchannel_reader_result result;

	if (reader->protocol == atomic64_read(&channel->hdr.protocol) &&
		SHMSTREAM20 == atomic64_read(&channel->hdr.transport)) {

		reader->epoch = atomic64_read(&channel->hdr.epoch);
		reader->size = atomic64_read(&channel->hdr.size);
		reader->elements =
			atomic64_read(&channel->hdr.elements);
		reader->rc = 0;

		result = MUCHANNEL_EPOCH_CHANGED;
	} else
		result = MUCHANNEL_INCOMPATIBLE_INTERFACE;

	return result;
};

void muen_channel_init_reader(struct muchannel_reader *reader, u64 protocol)
{
	reader->epoch = MUCHANNEL_NULL_EPOCH;
	reader->protocol = protocol;
	reader->size = 0;
	reader->elements = 0;
	reader->rc = 0;
};
EXPORT_SYMBOL(muen_channel_init_reader);

enum muchannel_reader_result muen_channel_read(
		const struct muchannel * const channel,
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
			rc = reader->rc;
			pos = do_div(rc, reader->elements) * reader->size;
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

void muen_channel_drain(const struct muchannel * const channel,
			struct muchannel_reader *reader)
{
	reader->rc = atomic64_read(&channel->hdr.wc);
};
EXPORT_SYMBOL(muen_channel_drain);
