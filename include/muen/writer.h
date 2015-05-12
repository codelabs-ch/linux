#ifndef MUEN_CHANNEL_WRITER_H
#define MUEN_CHANNEL_WRITER_H

#include <muen/channel.h>

/**
 * Initialize channel with given parameters.
 */
void muen_channel_init_writer(struct muchannel *channel, const u64 protocol,
			      const u64 element_size, const u64 channel_size,
			      const u64 epoch);

/**
 * Deactivate channel.
 */
void muen_channel_deactivate(struct muchannel *channel);

/**
 * Write element to given channel.
 */
void muen_channel_write(struct muchannel *channel, const void * const element);

#endif /* MUEN_CHANNEL_WRITER_H */
