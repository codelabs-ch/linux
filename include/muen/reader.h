#ifndef MUEN_CHANNEL_READER_H
#define MUEN_CHANNEL_READER_H

#include <muen/channel.h>

enum muchannel_reader_result {
	MUCHANNEL_INACTIVE,
	MUCHANNEL_INCOMPATIBLE_INTERFACE,
	MUCHANNEL_EPOCH_CHANGED,
	MUCHANNEL_NO_DATA,
	MUCHANNEL_OVERRUN_DETECTED,
	MUCHANNEL_SUCCESS
};

struct muchannel_reader {
	u64 epoch;
	u64 protocol;
	u64 size;
	u64 elements;
	u64 rc;
};

/*
 * Initialize reader with given protocol.
 */
void muen_channel_init_reader(struct muchannel_reader *reader, u64 protocol);

/*
 * Read next element from given channel.
 */
enum muchannel_reader_result muen_channel_read(
		const struct muchannel * const channel,
		struct muchannel_reader *reader,
		void *element);

/*
 * Drain all current channel elements.
 */
void muen_channel_drain(const struct muchannel * const channel,
			struct muchannel_reader *reader);

#endif /* MUEN_CHANNEL_READER_H */
