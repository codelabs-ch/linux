/*
 * Copyright (C) 2013, 2015  Reto Buerki <reet@codelabs.ch>
 * Copyright (C) 2013, 2015  Adrian-Ken Rueegsegger <ken@codelabs.ch>
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

#include <muen/channel.h>

bool muen_channel_is_active(const struct muchannel * const channel)
{
	return atomic64_read(&channel->hdr.epoch) != MUCHANNEL_NULL_EPOCH;
}
EXPORT_SYMBOL(muen_channel_is_active);
