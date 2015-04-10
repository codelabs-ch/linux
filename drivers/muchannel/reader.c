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

#include <muen/reader.h>

static bool has_epoch_changed(const struct muchannel *const channel,
			      const struct muchannel_reader *const reader)
{
	return reader->epoch != atomic64_read(&channel->hdr.epoch);
};

static enum reader_result synchronize(const struct muchannel *const channel,
				      struct muchannel_reader *reader)
{
	enum reader_result result;

	if (reader->protocol == atomic64_read(&channel->hdr.protocol) &&
		SHMSTREAM20 == atomic64_read(&channel->hdr.transport)) {

		reader->epoch = atomic64_read(&channel->hdr.epoch);
		reader->size = atomic64_read(&channel->hdr.size);
		reader->elements =
			atomic64_read(&channel->hdr.elements);
		reader->rc = 0;

		result = EPOCH_CHANGED;
	} else
		result = INCOMPATIBLE_INTERFACE;

	return result;
};

void muchannel_init(struct muchannel_reader *reader, u64 protocol)
{
	reader->epoch = NULL_EPOCH;
	reader->protocol = protocol;
	reader->size = 0;
	reader->elements = 0;
	reader->rc = 0;
};
EXPORT_SYMBOL(muchannel_init);

enum reader_result muchannel_read(const struct muchannel *const channel,
				  struct muchannel_reader *reader,
				  void *element)
{
	u64 pos, rc;
	enum reader_result result;

	if (is_active(channel)) {
		if (has_epoch_changed(channel, reader))
			return synchronize(channel, reader);

		if (reader->rc >= atomic64_read(&channel->hdr.wc))
			result = NO_DATA;
		else {
			rc = reader->rc;
			pos = do_div(rc, reader->elements) * reader->size;
			memcpy(element, channel->data + pos, reader->size);

			if (atomic64_read(&channel->hdr.wsc) >
			    reader->rc + reader->elements) {
				result = OVERRUN_DETECTED;
				reader->rc = atomic64_read(&channel->hdr.wc);
			} else {
				result = SUCCESS;
				reader->rc = reader->rc + 1;
			}
			if (has_epoch_changed(channel, reader))
				result = EPOCH_CHANGED;
		}
	} else
		result = INACTIVE;

	return result;
};
EXPORT_SYMBOL(muchannel_read);

void muchannel_drain(const struct muchannel *const channel,
		     struct muchannel_reader *reader)
{
	reader->rc = atomic64_read(&channel->hdr.wc);
};
EXPORT_SYMBOL(muchannel_drain);
