/*
 * Copyright (C) 2013  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2013  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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

#include <muen/reader.h>

static bool has_epoch_changed(const struct muchannel *const channel,
			      const struct muchannel_reader *const reader)
{
	return reader->epoch != atomic64_read(&channel->hdr.epoch);
};

enum reader_result muchannel_synchronize(const struct muchannel *const channel,
					 struct muchannel_reader *reader)
{
	enum reader_result result;

	reader->syncd = false;

	reader->epoch = atomic64_read(&channel->hdr.epoch);

	if (reader->epoch == 0) {
		result = INACTIVE;
	} else {
		reader->protocol = atomic64_read(&channel->hdr.protocol);
		reader->size = atomic64_read(&channel->hdr.size);
		reader->elements = atomic64_read(&channel->hdr.elements);
		reader->rc = 0;

		if (atomic64_read(&channel->hdr.transport) != SHMSTREAM20) {
			result = INCOMPATIBLE_INTERFACE;
		} else {
			if (has_epoch_changed(channel, reader)) {
				result = EPOCH_CHANGED;
			} else {
				reader->syncd = true;
				result = SUCCESS;
			}
		}
	}

	return result;
};

enum reader_result muchannel_read(const struct muchannel *const channel,
				  struct muchannel_reader *reader,
				  void *element)
{
	u64 pos, rc;
	enum reader_result result;

	if (reader->rc >= atomic64_read(&channel->hdr.wc)) {
		result = NO_DATA;
	} else {
		rc = reader->rc;
		pos = do_div(rc, reader->elements) * reader->size;
		memcpy(element, channel->data + pos, reader->size);

		if (atomic64_read(&channel->hdr.wsc) >
		    reader->rc + reader->elements) {
			reader->rc = atomic64_read(&channel->hdr.wc);
			result = OVERRUN_DETECTED;
		} else {
			reader->rc = reader->rc + 1;
			result = SUCCESS;
		}
		if (has_epoch_changed(channel, reader))
			result = EPOCH_CHANGED;
	}

	return result;
};
