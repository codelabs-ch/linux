#ifndef MUEN_CHANNEL_WRITER_H
#define MUEN_CHANNEL_WRITER_H

#include <muen/channel.h>

/**
 * Initialize channel with given parameters.
 */
void muchannel_initialize(struct muchannel *channel, const u64 protocol,
			  const u64 size, const u64 elements, const u64 epoch);

/**
 * Deactivate channel.
 */
void muchannel_deactivate(struct muchannel *channel);

/**
 * Write element to given channel.
 */
void muchannel_write(struct muchannel *channel, const void *const element);

#endif /* MUEN_CHANNEL_WRITER_H */
